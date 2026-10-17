#include "cloud.h"
#include "i18n.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "settings.h"

static const char *TAG = "cloud";

/* TigerTag's public Firebase client key: served without authentication at
 * https://tigertag-cdn.web.app/__/firebase/init.json, not a secret. */
#define API_KEY "AIzaSyCkxPTs_Cv0KVLqsZj-UKWWqIY0OtfVpnw"
#define PROJECT "tigertag-connect"
#define MAX_RESPONSE (128 * 1024)

static char s_id_token[1792];
static char s_refresh[768];
static char s_uid[48];
static char s_email[96];
static char s_name[64]; /* the account's displayName, as TigerScale shows it */
static int64_t s_token_at_us;
static volatile bool s_link_ok;   /* the last exchange with Firebase worked */
static int64_t s_last_try_us;     /* last keep-alive attempt */

#define TOKEN_FRESH_US (50LL * 60 * 1000000)  /* a token is reused this long */
#define KEEPALIVE_AGE_US (45LL * 60 * 1000000) /* renewed before it can lapse */
#define KEEPALIVE_RETRY_US (30LL * 1000000)

/* One renewal at a time: the feed task keeps the session alive while the
 * application task may be reading the account. Taken around the HTTPS exchange
 * only; the caller re-checks freshness once it holds it, so two tasks that
 * both found the token stale do one request, not two. */
static SemaphoreHandle_t token_lock(void)
{
    static StaticSemaphore_t buf;
    static SemaphoreHandle_t h;
    if (!h)
    {
        h = xSemaphoreCreateMutexStatic(&buf);
    }
    return h;
}

/* ---- HTTPS ---------------------------------------------------------------- */

typedef struct
{
    char *buf;
    size_t len;
    size_t cap;
} rx_t;

static esp_err_t on_http(esp_http_client_event_t *e)
{
    rx_t *r = (rx_t *)e->user_data;
    if (e->event_id != HTTP_EVENT_ON_DATA || !r)
    {
        return ESP_OK;
    }
    size_t need = r->len + (size_t)e->data_len + 1;
    if (need > r->cap)
    {
        size_t nc = r->cap ? r->cap * 2 : 4096;
        while (nc < need)
        {
            nc *= 2;
        }
        if (nc > MAX_RESPONSE)
        {
            return ESP_FAIL;
        }
        char *nb = heap_caps_realloc(r->buf, nc, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!nb)
        {
            return ESP_FAIL;
        }
        r->buf = nb;
        r->cap = nc;
    }
    memcpy(r->buf + r->len, e->data, e->data_len);
    r->len += e->data_len;
    r->buf[r->len] = '\0';
    return ESP_OK;
}

/* Returns the HTTP status, or -1 when the request itself failed. */
static int http_do(const char *url, esp_http_client_method_t method, const char *body,
                   const char *bearer, rx_t *rx)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = method,
        .event_handler = on_http,
        .user_data = rx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c)
    {
        return -1;
    }
    if (body)
    {
        esp_http_client_set_header(c, "Content-Type", "application/json");
        esp_http_client_set_post_field(c, body, (int)strlen(body));
    }
    char auth[1900];
    if (bearer)
    {
        snprintf(auth, sizeof auth, "Bearer %s", bearer);
        esp_http_client_set_header(c, "Authorization", auth);
    }
    esp_err_t e = esp_http_client_perform(c);
    int status = (e == ESP_OK) ? esp_http_client_get_status_code(c) : -1;
    if (e != ESP_OK)
    {
        ESP_LOGW(TAG, "request failed: %s", esp_err_to_name(e));
    }
    esp_http_client_cleanup(c);
    if (bearer)
    {
        memset(auth, 0, sizeof auth);
    }
    /* Reachable and answering is what the account icon reports; a 4xx is the
     * caller's to interpret (a revoked session signs out, which says "none"). */
    if (status >= 200 && status < 300)
    {
        s_link_ok = true;
    }
    else if (status <= 0 || status >= 500)
    {
        s_link_ok = false;
    }
    return status;
}

static cloud_err_t map_error(const cJSON *root)
{
    const cJSON *err = cJSON_GetObjectItem(root, "error");
    const cJSON *msg = err ? cJSON_GetObjectItem(err, "message") : NULL;
    const char *m = cJSON_IsString(msg) ? msg->valuestring : "";
    ESP_LOGW(TAG, "server said: %s", m);
    if (strstr(m, "EMAIL_NOT_FOUND") || strstr(m, "INVALID_PASSWORD") ||
        strstr(m, "INVALID_LOGIN_CREDENTIALS") || strstr(m, "TOKEN_EXPIRED") ||
        strstr(m, "INVALID_REFRESH_TOKEN") || strstr(m, "INVALID_EMAIL"))
    {
        return CLOUD_ERR_CREDENTIALS;
    }
    if (strstr(m, "MISSING_PASSWORD"))
    {
        return CLOUD_ERR_NO_PASSWORD;
    }
    if (strstr(m, "TOO_MANY_ATTEMPTS"))
    {
        return CLOUD_ERR_RATE;
    }
    if (strstr(m, "USER_DISABLED"))
    {
        return CLOUD_ERR_DISABLED;
    }
    return CLOUD_ERR_OTHER;
}

static void copy_str(char *dst, size_t n, const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(v))
    {
        strlcpy(dst, v->valuestring, n);
    }
}

static void save_session(void)
{
    /* Another account: its name is not the last one's; read again with the country. */
    char was[48] = "";
    settings_get("acct_uid", was, sizeof was);
    if (strcmp(was, s_uid) != 0)
    {
        s_name[0] = '\0';
        settings_erase("acct_name");
    }
    settings_set("acct_refresh", s_refresh);
    settings_set("acct_uid", s_uid);
    settings_set("acct_email", s_email);
}

/* ---- Session -------------------------------------------------------------- */

const char *cloud_err_text(cloud_err_t e)
{
    switch (e)
    {
    case CLOUD_OK:
        return "";
    case CLOUD_ERR_NETWORK:
        return tr(S_ERR_ACCOUNT_SILENT);
    case CLOUD_ERR_CREDENTIALS:
        return tr(S_ERR_CREDENTIALS);
    case CLOUD_ERR_NO_PASSWORD:
        return tr(S_ERR_GOOGLE);
    case CLOUD_ERR_RATE:
        return tr(S_ERR_RATE);
    case CLOUD_ERR_DISABLED:
        return tr(S_ERR_DISABLED);
    default:
        return tr(S_ERR_OTHER);
    }
}

bool cloud_has_session(void)
{
    if (s_refresh[0])
    {
        return true;
    }
    return settings_get("acct_refresh", s_refresh, sizeof s_refresh);
}

const char *cloud_email(void)
{
    if (!s_email[0])
    {
        settings_get("acct_email", s_email, sizeof s_email);
    }
    return s_email;
}

const char *cloud_display_name(void)
{
    if (!s_name[0])
    {
        settings_get("acct_name", s_name, sizeof s_name);
    }
    return s_name;
}

void cloud_sign_out(void)
{
    memset(s_id_token, 0, sizeof s_id_token);
    memset(s_refresh, 0, sizeof s_refresh);
    s_uid[0] = s_email[0] = s_name[0] = '\0';
    s_link_ok = false;
    settings_erase("acct_name");
    settings_erase("acct_refresh");
    settings_erase("acct_uid");
    settings_erase("acct_email");
}

cloud_err_t cloud_sign_in(const char *email, const char *password)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "email", email);
    cJSON_AddStringToObject(j, "password", password);
    cJSON_AddBoolToObject(j, "returnSecureToken", true);
    char *body = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!body)
    {
        return CLOUD_ERR_OTHER;
    }

    rx_t rx = {0};
    int st = http_do("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" API_KEY,
                     HTTP_METHOD_POST, body, NULL, &rx);
    memset(body, 0, strlen(body)); /* the password does not outlive the call */
    cJSON_free(body);

    if (st <= 0 || !rx.buf)
    {
        free(rx.buf);
        return CLOUD_ERR_NETWORK;
    }
    cJSON *r = cJSON_Parse(rx.buf);
    memset(rx.buf, 0, rx.len);
    free(rx.buf);
    if (!r)
    {
        return CLOUD_ERR_OTHER;
    }
    if (st != 200)
    {
        cloud_err_t e = map_error(r);
        cJSON_Delete(r);
        return e;
    }
    copy_str(s_id_token, sizeof s_id_token, r, "idToken");
    copy_str(s_refresh, sizeof s_refresh, r, "refreshToken");
    copy_str(s_uid, sizeof s_uid, r, "localId");
    cJSON_Delete(r);
    if (!s_id_token[0] || !s_refresh[0] || !s_uid[0])
    {
        return CLOUD_ERR_OTHER;
    }
    strlcpy(s_email, email, sizeof s_email);
    s_token_at_us = esp_timer_get_time();
    save_session();
    ESP_LOGI(TAG, "signed in");
    return CLOUD_OK;
}

static cloud_err_t refresh_token(void)
{
    if (!s_refresh[0] && !cloud_has_session())
    {
        return CLOUD_ERR_CREDENTIALS;
    }
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "grant_type", "refresh_token");
    cJSON_AddStringToObject(j, "refresh_token", s_refresh);
    char *body = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!body)
    {
        return CLOUD_ERR_OTHER;
    }
    rx_t rx = {0};
    int st = http_do("https://securetoken.googleapis.com/v1/token?key=" API_KEY, HTTP_METHOD_POST, body,
                     NULL, &rx);
    memset(body, 0, strlen(body));
    cJSON_free(body);
    if (st <= 0 || !rx.buf)
    {
        free(rx.buf);
        return CLOUD_ERR_NETWORK;
    }
    cJSON *r = cJSON_Parse(rx.buf);
    free(rx.buf);
    if (!r)
    {
        return CLOUD_ERR_OTHER;
    }
    if (st != 200)
    {
        cloud_err_t e = map_error(r);
        cJSON_Delete(r);
        if (e == CLOUD_ERR_CREDENTIALS || e == CLOUD_ERR_DISABLED)
        {
            cloud_sign_out();
        }
        return e;
    }
    copy_str(s_id_token, sizeof s_id_token, r, "id_token");
    char fresh[sizeof s_refresh] = {0};
    copy_str(fresh, sizeof fresh, r, "refresh_token");
    copy_str(s_uid, sizeof s_uid, r, "user_id");
    cJSON_Delete(r);
    if (fresh[0] && strcmp(fresh, s_refresh) != 0)
    {
        strlcpy(s_refresh, fresh, sizeof s_refresh);
        settings_set("acct_refresh", s_refresh);
    }
    if (!s_uid[0])
    {
        settings_get("acct_uid", s_uid, sizeof s_uid);
    }
    s_token_at_us = esp_timer_get_time();
    return s_id_token[0] ? CLOUD_OK : CLOUD_ERR_OTHER;
}

cloud_err_t cloud_resume(void)
{
    if (!cloud_has_session())
    {
        return CLOUD_ERR_CREDENTIALS;
    }
    settings_get("acct_uid", s_uid, sizeof s_uid);
    cloud_email();
    SemaphoreHandle_t lk = token_lock();
    xSemaphoreTake(lk, portMAX_DELAY);
    cloud_err_t r = refresh_token();
    xSemaphoreGive(lk);
    return r;
}

static bool token_fresh(void)
{
    return s_id_token[0] && (esp_timer_get_time() - s_token_at_us) < TOKEN_FRESH_US;
}

static cloud_err_t ensure_token(void)
{
    if (token_fresh())
    {
        return CLOUD_OK;
    }
    SemaphoreHandle_t lk = token_lock();
    xSemaphoreTake(lk, portMAX_DELAY);
    /* Whoever held it may have renewed it while we waited. */
    cloud_err_t r = token_fresh() ? CLOUD_OK : refresh_token();
    xSemaphoreGive(lk);
    return r;
}

cloud_link_t cloud_link(void)
{
    if (!s_refresh[0])
    {
        return CLOUD_LINK_NONE;
    }
    return s_link_ok ? CLOUD_LINK_OK : CLOUD_LINK_CONNECTING;
}

void cloud_keepalive(void)
{
    if (!s_refresh[0])
    {
        return; /* no account: nothing to keep */
    }
    const int64_t now = esp_timer_get_time();
    const bool stale = !s_id_token[0] || (now - s_token_at_us) >= KEEPALIVE_AGE_US;
    if (!stale && s_link_ok)
    {
        return;
    }
    if (s_last_try_us && (now - s_last_try_us) < KEEPALIVE_RETRY_US)
    {
        return;
    }
    s_last_try_us = now;

    SemaphoreHandle_t lk = token_lock();
    xSemaphoreTake(lk, portMAX_DELAY);
    /* Another task may have just renewed it: then there is nothing to do, and
     * the exchange that did it has already said the link is up. */
    const bool just_renewed = s_id_token[0] && (esp_timer_get_time() - s_token_at_us) < KEEPALIVE_RETRY_US;
    cloud_err_t r = just_renewed ? CLOUD_OK : refresh_token();
    xSemaphoreGive(lk);
    ESP_LOGD(TAG, "keep-alive: %s", r == CLOUD_OK ? "ok" : cloud_err_text(r));
}

/* ---- Printers ------------------------------------------------------------- */

/* Tiger Studio's list (it does not enumerate the account either): the brands,
 * then its printer plugins, Klipper and OctoPrint. */
static const char *BRANDS[] = {"creality", "flashforge", "bambulab", "snapmaker", "elegoo",
                               "anycubic", "prusa",      "klipper",  "octoprint"};

/* Server-side field mask: Firestore sends only these, which keeps a Creality
 * brand from costing tens of kilobytes of discovery dumps. */
static const char *MASK =
    "?mask.fieldPaths=printerName&mask.fieldPaths=name&mask.fieldPaths=ip&mask.fieldPaths=broker&mask.fieldPaths=updatedAt"
    "&mask.fieldPaths=ipAddress&mask.fieldPaths=lanIp&mask.fieldPaths=host"
    "&mask.fieldPaths=mode&mask.fieldPaths=printerModelId&mask.fieldPaths=modelId"
    "&mask.fieldPaths=discovery.ip&mask.fieldPaths=discovery.transport"
    "&mask.fieldPaths=serial&mask.fieldPaths=serialNumber&mask.fieldPaths=sn"
    "&mask.fieldPaths=deviceId&mask.fieldPaths=discovery.deviceSn"
    "&mask.fieldPaths=password&mask.fieldPaths=dev_access_code&mask.fieldPaths=accessCode"
    "&mask.fieldPaths=access_code&mask.fieldPaths=checkCode&mask.fieldPaths=mqttPassword&mask.fieldPaths=username&mask.fieldPaths=acuModelId"
    "&mask.fieldPaths=apiKey"
    "&mask.fieldPaths=protocol&mask.fieldPaths=mainboardId"
    "&mask.fieldPaths=discovery.protocol&mask.fieldPaths=discovery.mainboardId";

/* Anycubic in cloud mode, asked of that brand only: the broker key and machine
 * type of the topics, and what the shared session signs in with - the Anycubic
 * account and the client identity Tiger Studio writes on every cloud printer
 * (as TigerSpool reads them). */
static const char *MASK_ACU_CLOUD =
    "&mask.fieldPaths=key&mask.fieldPaths=machineType&mask.fieldPaths=cloudPrinterId"
    "&mask.fieldPaths=cloudEmail&mask.fieldPaths=cloudToken&mask.fieldPaths=acuCloudCaDerB64"
    "&mask.fieldPaths=acuCloudClientCertPem&mask.fieldPaths=acuCloudClientKeyPem";

/* The Anycubic account and client identity from the last read, in PSRAM.
 * Never logged and never stored: the account read brings them at every start. */
static struct
{
    SemaphoreHandle_t lock;
    char *email, *token, *ca_der_b64, *cert_pem, *key_pem;
} s_acu;

static char *ps_dup(const char *s)
{
    if (!s || !s[0])
    {
        return NULL;
    }
    const size_t n = strlen(s) + 1;
    char *d = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (d)
    {
        memcpy(d, s, n);
    }
    return d;
}

static void acu_keep(char **slot, const char *v)
{
    if (!v || !v[0] || (*slot && strcmp(*slot, v) == 0))
    {
        return;
    }
    free(*slot);
    *slot = ps_dup(v);
}

/* Firestore REST wraps every value: {"ip": {"stringValue": "1.2.3.4"}}. */
static const char *fs_str(const cJSON *fields, const char *key)
{
    const cJSON *f = cJSON_GetObjectItem(fields, key);
    if (!f)
    {
        return NULL;
    }
    const cJSON *v = cJSON_GetObjectItem(f, "stringValue");
    if (!cJSON_IsString(v))
    {
        v = cJSON_GetObjectItem(f, "integerValue"); /* integers travel as strings */
    }
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static const cJSON *fs_map(const cJSON *fields, const char *key)
{
    const cJSON *f = cJSON_GetObjectItem(fields, key);
    const cJSON *m = f ? cJSON_GetObjectItem(f, "mapValue") : NULL;
    return m ? cJSON_GetObjectItem(m, "fields") : NULL;
}

static const char *first_of(const cJSON *fields, const char *const *keys, int n)
{
    for (int i = 0; i < n; i++)
    {
        const char *s = fs_str(fields, keys[i]);
        if (s && s[0])
        {
            return s;
        }
    }
    return NULL;
}

/* A time field as milliseconds since 1970, whichever way it was stored: an
 * integer of milliseconds, or a Firestore timestamp ("2026-09-10T21:20:06.421Z",
 * always UTC). 0 when there is nothing readable - the oldest. */
static int64_t fs_millis(const cJSON *fields, const char *key)
{
    const cJSON *f = cJSON_GetObjectItem(fields, key);
    if (!f)
    {
        return 0;
    }
    const cJSON *v = cJSON_GetObjectItem(f, "integerValue");
    if (cJSON_IsString(v))
    {
        return strtoll(v->valuestring, NULL, 10);
    }
    v = cJSON_GetObjectItem(f, "doubleValue");
    if (cJSON_IsNumber(v))
    {
        return (int64_t)v->valuedouble;
    }
    v = cJSON_GetObjectItem(f, "timestampValue");
    int Y, M, D, h, m, sec;
    if (!cJSON_IsString(v) || sscanf(v->valuestring, "%4d-%2d-%2dT%2d:%2d:%2d", &Y, &M, &D, &h, &m, &sec) != 6)
    {
        return 0;
    }
    int ms = 0, digits = 0;
    const char *dot = strchr(v->valuestring, '.');
    for (const char *c = dot ? dot + 1 : ""; *c >= '0' && *c <= '9' && digits < 3; c++, digits++)
    {
        ms = ms * 10 + (*c - '0');
    }
    while (dot && digits++ < 3)
    {
        ms *= 10;
    }
    /* Days from 1970-01-01, proleptic Gregorian (Howard Hinnant's days_from_civil). */
    Y -= M <= 2;
    const int era = (Y >= 0 ? Y : Y - 399) / 400;
    const unsigned yoe = (unsigned)(Y - era * 400);
    const unsigned doy = (153 * (unsigned)(M > 2 ? M - 3 : M + 9) + 2) / 5 + (unsigned)D - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
    return ((days * 86400 + h * 3600 + m * 60 + sec) * 1000) + ms;
}

/* The serial that identifies a machine; FlashForge writes it with or without
 * its "SN" prefix. Empty when the account holds none (a K2). */
static const char *id_serial(const char *sn, bool real)
{
    if (!real)
    {
        return "";
    }
    return strncmp(sn, "SN", 2) == 0 ? sn + 2 : sn;
}

/* Two documents describe the same machine, as TigerSpool decides it: the same
 * serial, and either the same address or the same mode - an A1 whose LAN and
 * cloud documents both carry its address, an AD5X that moved and left its old
 * document behind. A LAN and a cloud document at different addresses stay two
 * (an X1C is imported both ways on purpose). Without a serial, the address is
 * all there is. */
static bool same_printer(const account_printer_t *a, bool a_sn, const account_printer_t *b, bool b_sn)
{
    if (strcmp(a->brand, b->brand) != 0)
    {
        return false;
    }
    const char *sa = id_serial(a->sn, a_sn), *sb = id_serial(b->sn, b_sn);
    if (sa[0] || sb[0])
    {
        return strcmp(sa, sb) == 0 && (strcmp(a->ip, b->ip) == 0 || a->cloud == b->cloud);
    }
    return a->ip[0] && strcmp(a->ip, b->ip) == 0;
}

/* A brand that does not answer keeps the printers it had, in its place: one
 * slow request must not make printers vanish from the list (and switch them
 * off) until the next read. */
static void keep_brand(const account_printer_t *old, int n_old, const char *brand, account_printer_t *out, int max,
                       int *count)
{
    for (int i = 0; i < n_old && *count < max; i++)
    {
        if (strcmp(old[i].brand, brand) == 0)
        {
            out[(*count)++] = old[i];
        }
    }
}

cloud_err_t cloud_fetch_printers(account_printer_t *out, int max, int *count, int *failed_brands)
{
    const int n_old = *count > 0 && *count <= max ? *count : 0;
    *failed_brands = 0;
    cloud_err_t te = ensure_token();
    if (te != CLOUD_OK)
    {
        return te;
    }
    account_printer_t *old = NULL;
    if (n_old)
    {
        old = heap_caps_malloc(n_old * sizeof *old, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (old)
        {
            memcpy(old, out, n_old * sizeof *old);
        }
    }
    *count = 0;
    /* Per entry of this read: its updatedAt, and whether its serial is real
     * (the document id stands in when there is none). */
    int64_t upd[CLOUD_MAX_PRINTERS] = {0};
    bool real_sn[CLOUD_MAX_PRINTERS] = {0};

    /* "broker": where Tiger Studio writes a Bambu Lab printer's LAN address (as TigerSpool reads it). */
    static const char *IP_KEYS[] = {"ip", "broker", "ipAddress", "lanIp", "host"};
    static const char *MODEL_KEYS[] = {"printerModelId", "modelId"};
    static const char *SN_KEYS[] = {"serial", "serialNumber", "sn", "deviceId"};
    static const char *CC_KEYS[] = {"password", "dev_access_code", "accessCode",
                                    "access_code", "checkCode", "mqttPassword"};

    for (size_t b = 0; b < sizeof BRANDS / sizeof BRANDS[0]; b++)
    {
        char url[1536]; /* the field mask alone is ~900 bytes */
        const bool acu = strcmp(BRANDS[b], "anycubic") == 0;
        snprintf(url, sizeof url,
                 "https://firestore.googleapis.com/v1/projects/" PROJECT
                 "/databases/(default)/documents/users/%s/printers/%s/devices%s%s",
                 s_uid, BRANDS[b], MASK, acu ? MASK_ACU_CLOUD : "");
        rx_t rx = {0};
        int st = http_do(url, HTTP_METHOD_GET, NULL, s_id_token, &rx);
        ESP_LOGI(TAG, "%s: http %d, %u bytes", BRANDS[b], st, (unsigned)rx.len);
        if (st != 200 || !rx.buf)
        {
            free(rx.buf);
            (*failed_brands)++;
            keep_brand(old, old ? n_old : 0, BRANDS[b], out, max, count);
            continue;
        }
        cJSON *root = cJSON_Parse(rx.buf);
        free(rx.buf);
        if (!root)
        {
            (*failed_brands)++;
            keep_brand(old, old ? n_old : 0, BRANDS[b], out, max, count);
            continue;
        }
        const int brand_first = *count;
        const cJSON *docs = cJSON_GetObjectItem(root, "documents");
        const cJSON *doc;
        cJSON_ArrayForEach(doc, docs)
        {
            if (*count >= max)
            {
                break;
            }
            const cJSON *fields = cJSON_GetObjectItem(doc, "fields");
            if (!fields)
            {
                continue;
            }
            account_printer_t *p = &out[*count];
            memset(p, 0, sizeof *p);
            strlcpy(p->brand, BRANDS[b], sizeof p->brand);

            const cJSON *nm = cJSON_GetObjectItem(doc, "name");
            if (cJSON_IsString(nm))
            {
                const char *slash = strrchr(nm->valuestring, '/');
                strlcpy(p->id, slash ? slash + 1 : nm->valuestring, sizeof p->id);
            }
            const char *name = fs_str(fields, "printerName");
            if (!name)
            {
                name = fs_str(fields, "name");
            }
            strlcpy(p->name, name ? name : p->brand, sizeof p->name);

            const cJSON *disc = fs_map(fields, "discovery");
            const char *ip = first_of(fields, IP_KEYS, 5);
            if (!ip)
            {
                ip = disc ? fs_str(disc, "ip") : NULL;
            }
            const char *sn = first_of(fields, SN_KEYS, 4);
            if (!sn && disc)
            {
                sn = fs_str(disc, "deviceSn");
            }
            strlcpy(p->sn, sn && sn[0] ? sn : p->id, sizeof p->sn);
            const bool has_sn = sn && sn[0];
            const char *cc = first_of(fields, CC_KEYS, 6);
            if (cc)
            {
                strlcpy(p->cc, cc, sizeof p->cc);
            }
            /* An API key Studio cleared is written as "": absent, then. */
            const char *key = fs_str(fields, "apiKey");
            if (key && key[0])
            {
                strlcpy(p->key, key, sizeof p->key);
            }
            const char *un = fs_str(fields, "username");
            if (un)
            {
                strlcpy(p->user, un, sizeof p->user);
            }
            /* acuModelId, not modelId: the first is Anycubic's own number and
             * half of every topic, the second the TigerTag catalogue id. */
            const char *dv = fs_str(fields, "deviceId");
            if (dv)
            {
                strlcpy(p->dev_id, dv, sizeof p->dev_id);
            }
            const char *am = fs_str(fields, "acuModelId");
            if (am)
            {
                strlcpy(p->acu_model, am, sizeof p->acu_model);
            }
            if (ip)
            {
                strlcpy(p->ip, ip, sizeof p->ip);
            }
            /* Elegoo's Centauri Carbon 1 speaks SDCP, not the CC2's MQTT. Studio
             * writes `protocol: "sdcp"` with the printer and keeps the
             * MainboardID of its discovery, at the top or under `discovery`. */
            const char *proto = fs_str(fields, "protocol");
            if (!proto && disc)
            {
                proto = fs_str(disc, "protocol");
            }
            if (proto)
            {
                strlcpy(p->proto, proto, sizeof p->proto);
            }
            const char *board = fs_str(fields, "mainboardId");
            if (!board && disc)
            {
                board = fs_str(disc, "mainboardId");
            }
            if (board)
            {
                strlcpy(p->board_id, board, sizeof p->board_id);
            }
            const char *mid = first_of(fields, MODEL_KEYS, 2);
            if (mid)
            {
                strlcpy(p->model_id, mid, sizeof p->model_id);
            }
            const char *mode = fs_str(fields, "mode");
            p->cloud = mode && strcmp(mode, "cloud") == 0;
            if (acu && p->cloud)
            {
                /* The same two halves of the topics, under their cloud names:
                 * the broker key is the device id, the machine type the model. */
                const char *key = fs_str(fields, "key");
                strlcpy(p->dev_id, key ? key : "", sizeof p->dev_id);
                const char *mt = fs_str(fields, "machineType");
                if (mt)
                {
                    strlcpy(p->acu_model, mt, sizeof p->acu_model);
                }
                const char *cpid = fs_str(fields, "cloudPrinterId");
                if (cpid && cpid[0])
                {
                    strlcpy(p->sn, cpid, sizeof p->sn);
                }
                if (!s_acu.lock)
                {
                    s_acu.lock = xSemaphoreCreateMutex();
                }
                xSemaphoreTake(s_acu.lock, portMAX_DELAY);
                acu_keep(&s_acu.email, fs_str(fields, "cloudEmail"));
                acu_keep(&s_acu.token, fs_str(fields, "cloudToken"));
                acu_keep(&s_acu.ca_der_b64, fs_str(fields, "acuCloudCaDerB64"));
                acu_keep(&s_acu.cert_pem, fs_str(fields, "acuCloudClientCertPem"));
                acu_keep(&s_acu.key_pem, fs_str(fields, "acuCloudClientKeyPem"));
                xSemaphoreGive(s_acu.lock);
                /* Which fields are there, by name, never by value. */
                const bool has_key = p->dev_id[0], has_type = p->acu_model[0];
                const bool has_tok = fs_str(fields, "cloudToken"), has_id = fs_str(fields, "acuCloudClientCertPem");
                ESP_LOGI(TAG, "anycubic cloud printer: key %s, machineType %s, token %s, identity %s",
                         has_key ? "present" : "MISSING", has_type ? "present" : "MISSING",
                         has_tok ? "present" : "MISSING", has_id ? "present" : "MISSING");
            }
            /* The same printer twice: the document updated last describes it as
             * it is now (an older one keeps an old address or a rotated access
             * code), and takes the older one's place in the list. */
            const int64_t mine = fs_millis(fields, "updatedAt");
            int dup = -1;
            for (int j = brand_first; j < *count && dup < 0; j++)
            {
                if (same_printer(&out[j], real_sn[j], p, has_sn))
                {
                    dup = j;
                }
            }
            if (dup >= 0)
            {
                if (mine > upd[dup])
                {
                    out[dup] = *p;
                    upd[dup] = mine;
                    real_sn[dup] = has_sn;
                }
                ESP_LOGI(TAG, "%s: two documents for one printer, the newer kept", BRANDS[b]);
                continue;
            }
            upd[*count] = mine;
            real_sn[*count] = has_sn;
            (*count)++;
        }
        cJSON_Delete(root);
    }
    free(old);
    return CLOUD_OK;
}

bool cloud_anycubic_identity(cloud_acu_identity_t *out)
{
    memset(out, 0, sizeof *out);
    if (!s_acu.lock)
    {
        return false;
    }
    xSemaphoreTake(s_acu.lock, portMAX_DELAY);
    const bool ok = s_acu.email && s_acu.token && s_acu.ca_der_b64 && s_acu.cert_pem && s_acu.key_pem;
    if (ok)
    {
        out->email = ps_dup(s_acu.email);
        out->token = ps_dup(s_acu.token);
        out->ca_der_b64 = ps_dup(s_acu.ca_der_b64);
        out->cert_pem = ps_dup(s_acu.cert_pem);
        out->key_pem = ps_dup(s_acu.key_pem);
    }
    xSemaphoreGive(s_acu.lock);
    if (ok && !(out->email && out->token && out->ca_der_b64 && out->cert_pem && out->key_pem))
    {
        cloud_anycubic_identity_free(out);
        return false;
    }
    return ok;
}

void cloud_anycubic_identity_free(cloud_acu_identity_t *id)
{
    free(id->email);
    free(id->token);
    free(id->ca_der_b64);
    free(id->cert_pem);
    free(id->key_pem);
    memset(id, 0, sizeof *id);
}

cloud_err_t cloud_bambu_session(char *user, size_t user_size, char *token, size_t token_size,
                                char *region, size_t region_size)
{
    user[0] = token[0] = region[0] = '\0';
    cloud_err_t te = ensure_token();
    if (te != CLOUD_OK)
    {
        return te;
    }
    char url[256];
    snprintf(url, sizeof url,
             "https://firestore.googleapis.com/v1/projects/" PROJECT
             "/databases/(default)/documents/users/%s/printers/bambulab/secrets/cloud_session",
             s_uid);
    rx_t rx = {0};
    int st = http_do(url, HTTP_METHOD_GET, NULL, s_id_token, &rx);
    if (st == 404)
    {
        free(rx.buf);
        return CLOUD_ERR_NOT_FOUND;
    }
    if (st != 200 || !rx.buf)
    {
        free(rx.buf);
        return CLOUD_ERR_NETWORK;
    }
    cJSON *root = cJSON_Parse(rx.buf);
    memset(rx.buf, 0, rx.len); /* it holds a live token */
    free(rx.buf);
    if (!root)
    {
        return CLOUD_ERR_OTHER;
    }
    const cJSON *f = cJSON_GetObjectItem(root, "fields");
    const char *u = f ? fs_str(f, "mqttUsername") : NULL;
    const char *t = f ? fs_str(f, "accessToken") : NULL;
    const char *r = f ? fs_str(f, "region") : NULL;
    cloud_err_t out = CLOUD_ERR_NOT_FOUND;
    if (u && t && u[0] && t[0])
    {
        strlcpy(user, u, user_size);
        strlcpy(token, t, token_size);
        strlcpy(region, r && r[0] ? r : "us", region_size);
        out = CLOUD_OK;
    }
    cJSON_Delete(root);
    return out;
}

/* The countries kept in the account (users/{uid} { squadCountry, vatCountry }),
 * set from Studio. Read only, and only to choose a time zone: the device sends
 * nothing about them anywhere. CLOUD_ERR_NOT_FOUND when the account holds none. */
cloud_err_t cloud_account_country(char *squad, size_t squad_size, char *vat, size_t vat_size)
{
    squad[0] = vat[0] = '\0';
    cloud_err_t te = ensure_token();
    if (te != CLOUD_OK)
    {
        return te;
    }
    char url[256];
    snprintf(url, sizeof url,
             "https://firestore.googleapis.com/v1/projects/" PROJECT
             "/databases/(default)/documents/users/%s"
             "?mask.fieldPaths=squadCountry&mask.fieldPaths=vatCountry&mask.fieldPaths=displayName",
             s_uid);
    rx_t rx = {0};
    int st = http_do(url, HTTP_METHOD_GET, NULL, s_id_token, &rx);
    if (st == 404)
    {
        free(rx.buf);
        return CLOUD_ERR_NOT_FOUND;
    }
    if (st != 200 || !rx.buf)
    {
        free(rx.buf);
        return CLOUD_ERR_NETWORK;
    }
    cJSON *root = cJSON_Parse(rx.buf);
    free(rx.buf);
    if (!root)
    {
        return CLOUD_ERR_OTHER;
    }
    const cJSON *f = cJSON_GetObjectItem(root, "fields");
    const char *s = f ? fs_str(f, "squadCountry") : NULL;
    const char *v = f ? fs_str(f, "vatCountry") : NULL;
    /* The same document holds the name the owner gave the account (Studio,
     * the mobile app): kept so that the settings show it even offline. */
    const char *dn = f ? fs_str(f, "displayName") : NULL;
    if (dn && dn[0] && strcmp(dn, cloud_display_name()) != 0)
    {
        strlcpy(s_name, dn, sizeof s_name);
        settings_set("acct_name", s_name);
    }
    if (s)
    {
        strlcpy(squad, s, squad_size);
    }
    if (v)
    {
        strlcpy(vat, v, vat_size);
    }
    cJSON_Delete(root);
    return (squad[0] || vat[0]) ? CLOUD_OK : CLOUD_ERR_NOT_FOUND;
}

/* The language kept in the account (users/{uid}/prefs/app { lang }), shared
 * with Studio, the mobile app and the other devices. Read here; written by
 * cloud_push_lang() when a language is picked on this device. */
cloud_err_t cloud_account_lang(char *out, size_t out_size)
{
    out[0] = '\0';
    cloud_err_t te = ensure_token();
    if (te != CLOUD_OK)
    {
        return te;
    }
    char url[256];
    snprintf(url, sizeof url,
             "https://firestore.googleapis.com/v1/projects/" PROJECT
             "/databases/(default)/documents/users/%s/prefs/app?mask.fieldPaths=lang",
             s_uid);
    rx_t rx = {0};
    int st = http_do(url, HTTP_METHOD_GET, NULL, s_id_token, &rx);
    if (st == 404)
    {
        free(rx.buf);
        return CLOUD_ERR_NOT_FOUND;
    }
    if (st != 200 || !rx.buf)
    {
        free(rx.buf);
        return CLOUD_ERR_NETWORK;
    }
    cJSON *root = cJSON_Parse(rx.buf);
    free(rx.buf);
    if (!root)
    {
        return CLOUD_ERR_OTHER;
    }
    const cJSON *f = cJSON_GetObjectItem(root, "fields");
    const char *l = f ? fs_str(f, "lang") : NULL;
    cloud_err_t r = CLOUD_ERR_NOT_FOUND;
    if (l && l[0])
    {
        strlcpy(out, l, out_size);
        r = CLOUD_OK;
    }
    cJSON_Delete(root);
    return r;
}

/* ---- Pairing (Google accounts) -------------------------------------------- */

/* The device has no password to give for an account created with Google. It
 * asks the TigerTag backend for a code, shows it as a QR, and the owner
 * approves it from their phone, signed in there; the backend then hands the
 * device a custom token. The same four Cloud Functions as TigerScale V3 and
 * TigerSpool (TigerTag_Firebase_Backend functions/pairing.js). */
#define PAIR_START_URL "https://us-central1-tigertag-connect.cloudfunctions.net/pairStart"
#define PAIR_POLL_URL "https://us-central1-tigertag-connect.cloudfunctions.net/pairPoll"

cloud_err_t cloud_pair_start(const char *device, const char *fw, cloud_pair_t *out)
{
    memset(out, 0, sizeof *out);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "device", device);
    cJSON_AddStringToObject(j, "model", "Tiger Touch");
    /* What the approval page calls it: "display" reads as "your screen". */
    cJSON_AddStringToObject(j, "kind", "display");
    cJSON_AddStringToObject(j, "fw", fw);
    char *body = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!body)
    {
        return CLOUD_ERR_OTHER;
    }
    rx_t rx = {0};
    int st = http_do(PAIR_START_URL, HTTP_METHOD_POST, body, NULL, &rx);
    cJSON_free(body);
    if (st != 200 || !rx.buf)
    {
        ESP_LOGW(TAG, "pairStart HTTP %d", st);
        free(rx.buf);
        return st <= 0 ? CLOUD_ERR_NETWORK : CLOUD_ERR_OTHER;
    }
    cJSON *r = cJSON_Parse(rx.buf);
    free(rx.buf);
    if (!r)
    {
        return CLOUD_ERR_OTHER;
    }
    copy_str(out->verify_url, sizeof out->verify_url, r, "verify_url");
    copy_str(out->code, sizeof out->code, r, "code");
    copy_str(out->poll_token, sizeof out->poll_token, r, "poll_token");
    const cJSON *iv = cJSON_GetObjectItem(r, "interval");
    out->interval_s = cJSON_IsNumber(iv) && iv->valueint > 0 && iv->valueint < 60 ? iv->valueint : 5;
    cJSON_Delete(r);
    return out->verify_url[0] && out->poll_token[0] ? CLOUD_OK : CLOUD_ERR_OTHER;
}

/* The account id is not in signInWithCustomToken's answer: it is the user_id
 * claim of the ID token (a JWT), read from its base64url payload. TigerScale
 * lost the uid until the next restart for not doing this. */
static void uid_from_jwt(const char *jwt, char *out, size_t n)
{
    out[0] = '\0';
    const char *a = strchr(jwt, '.');
    const char *b = a ? strchr(a + 1, '.') : NULL;
    if (!a || !b || b - a - 1 <= 0 || b - a - 1 > 2000)
    {
        return;
    }
    size_t len = (size_t)(b - a - 1);
    char *p = heap_caps_malloc(len + 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    unsigned char *dec = heap_caps_malloc(len + 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p || !dec)
    {
        free(p);
        free(dec);
        return;
    }
    for (size_t i = 0; i < len; i++)
    {
        const char c = a[1 + i];
        p[i] = c == '-' ? '+' : (c == '_' ? '/' : c);
    }
    while (len % 4)
    {
        p[len++] = '=';
    }
    size_t got = 0;
    if (mbedtls_base64_decode(dec, len + 3, &got, (const unsigned char *)p, len) == 0)
    {
        dec[got] = '\0';
        cJSON *c = cJSON_Parse((const char *)dec);
        copy_str(out, n, c, "user_id");
        cJSON_Delete(c);
    }
    free(p);
    free(dec);
}

static cloud_err_t sign_in_custom(const char *custom_token, const char *email)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "token", custom_token);
    cJSON_AddBoolToObject(j, "returnSecureToken", true);
    char *body = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!body)
    {
        return CLOUD_ERR_OTHER;
    }
    rx_t rx = {0};
    int st = http_do("https://identitytoolkit.googleapis.com/v1/accounts:signInWithCustomToken?key=" API_KEY,
                     HTTP_METHOD_POST, body, NULL, &rx);
    memset(body, 0, strlen(body));
    cJSON_free(body);
    if (st <= 0 || !rx.buf)
    {
        free(rx.buf);
        return CLOUD_ERR_NETWORK;
    }
    cJSON *r = cJSON_Parse(rx.buf);
    memset(rx.buf, 0, rx.len);
    free(rx.buf);
    if (!r)
    {
        return CLOUD_ERR_OTHER;
    }
    if (st != 200)
    {
        cloud_err_t e = map_error(r);
        cJSON_Delete(r);
        return e;
    }
    copy_str(s_id_token, sizeof s_id_token, r, "idToken");
    copy_str(s_refresh, sizeof s_refresh, r, "refreshToken");
    cJSON_Delete(r);
    uid_from_jwt(s_id_token, s_uid, sizeof s_uid);
    if (!s_id_token[0] || !s_refresh[0] || !s_uid[0])
    {
        return CLOUD_ERR_OTHER;
    }
    strlcpy(s_email, email ? email : "", sizeof s_email);
    s_token_at_us = esp_timer_get_time();
    save_session();
    ESP_LOGI(TAG, "signed in by pairing");
    return CLOUD_OK;
}

cloud_pair_status_t cloud_pair_poll(const char *poll_token)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "poll_token", poll_token);
    char *body = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!body)
    {
        return PAIR_ERROR;
    }
    rx_t rx = {0};
    int st = http_do(PAIR_POLL_URL, HTTP_METHOD_POST, body, NULL, &rx);
    cJSON_free(body);
    if (st != 200 || !rx.buf)
    {
        ESP_LOGW(TAG, "pairPoll HTTP %d", st);
        free(rx.buf);
        return PAIR_ERROR;
    }
    cJSON *r = cJSON_Parse(rx.buf);
    memset(rx.buf, 0, rx.len);
    free(rx.buf);
    if (!r)
    {
        return PAIR_ERROR;
    }
    const cJSON *stj = cJSON_GetObjectItem(r, "status");
    const char *status = cJSON_IsString(stj) ? stj->valuestring : "";
    cloud_pair_status_t out = PAIR_ERROR;
    if (strcmp(status, "pending") == 0)
    {
        out = PAIR_PENDING;
    }
    else if (strcmp(status, "denied") == 0)
    {
        out = PAIR_DENIED;
    }
    else if (strcmp(status, "expired") == 0)
    {
        out = PAIR_EXPIRED;
    }
    else if (strcmp(status, "approved") == 0)
    {
        const cJSON *ct = cJSON_GetObjectItem(r, "custom_token");
        const cJSON *em = cJSON_GetObjectItem(r, "email");
        if (cJSON_IsString(ct))
        {
            /* The token is handed out once: if this exchange fails, the
             * pairing is spent and has to start again. */
            out = sign_in_custom(ct->valuestring, cJSON_IsString(em) ? em->valuestring : "") == CLOUD_OK
                      ? PAIR_APPROVED
                      : PAIR_ERROR;
        }
    }
    cJSON_Delete(r);
    return out;
}

/* ---- The account's language ----------------------------------------------- */

/* Writes users/{uid}/prefs/app { lang }, creating the document when needed:
 * the same merge-write as Studio's saveAccountLang() and TigerScale's
 * pushLangToCloud(), touching that one field only. */
cloud_err_t cloud_push_lang(const char *code)
{
    cloud_err_t te = ensure_token();
    if (te != CLOUD_OK)
    {
        return te;
    }
    char url[256];
    snprintf(url, sizeof url,
             "https://firestore.googleapis.com/v1/projects/" PROJECT
             "/databases/(default)/documents/users/%s/prefs/app?updateMask.fieldPaths=lang",
             s_uid);
    char body[96];
    snprintf(body, sizeof body, "{\"fields\":{\"lang\":{\"stringValue\":\"%s\"}}}", code);
    rx_t rx = {0};
    int st = http_do(url, HTTP_METHOD_PATCH, body, s_id_token, &rx);
    free(rx.buf);
    if (st == 200)
    {
        ESP_LOGI(TAG, "language written to the account");
        return CLOUD_OK;
    }
    ESP_LOGW(TAG, "language not written: HTTP %d", st);
    return st <= 0 ? CLOUD_ERR_NETWORK : CLOUD_ERR_OTHER;
}

/* ---- Presence -------------------------------------------------------------- */

static void fs_put_str(cJSON *f, cJSON *mask, const char *k, const char *v)
{
    cJSON *o = cJSON_AddObjectToObject(f, k);
    cJSON_AddStringToObject(o, "stringValue", v);
    cJSON_AddItemToArray(mask, cJSON_CreateString(k));
}

static void fs_put_int(cJSON *f, cJSON *mask, const char *k, int v)
{
    char b[16];
    snprintf(b, sizeof b, "%d", v);
    cJSON *o = cJSON_AddObjectToObject(f, k);
    cJSON_AddStringToObject(o, "integerValue", b);
    cJSON_AddItemToArray(mask, cJSON_CreateString(k));
}

/* A value the device cannot know is written as null, never left out: left out,
 * the field would keep whatever an older write put there. */
static void fs_put_null(cJSON *f, cJSON *mask, const char *k)
{
    cJSON *o = cJSON_AddObjectToObject(f, k);
    cJSON_AddStringToObject(o, "nullValue", "NULL_VALUE");
    cJSON_AddItemToArray(mask, cJSON_CreateString(k));
}

static void fs_put_ids(cJSON *f, cJSON *mask, const char *k, const cloud_presence_t *p, bool online_only)
{
    cJSON *o = cJSON_AddObjectToObject(f, k);
    cJSON *arr = cJSON_AddObjectToObject(o, "arrayValue");
    cJSON *vals = cJSON_AddArrayToObject(arr, "values"); /* an empty array is written too */
    for (int i = 0; i < p->n && i < CLOUD_PRESENCE_MAX; i++)
    {
        if (!online_only || p->online[i])
        {
            cJSON *v = cJSON_CreateObject();
            cJSON_AddStringToObject(v, "stringValue", p->ids[i]);
            cJSON_AddItemToArray(vals, v);
        }
    }
    cJSON_AddItemToArray(mask, cJSON_CreateString(k));
}

/* Whether the owner already named this device in Tiger Studio: that name is
 * kept, and only a document without one gets the default. */
static bool has_display_name(const char *doc_url)
{
    char url[340];
    snprintf(url, sizeof url, "%s?mask.fieldPaths=display_name", doc_url);
    rx_t rx = {0};
    const int st = http_do(url, HTTP_METHOD_GET, NULL, s_id_token, &rx);
    bool named = false;
    if (st == 200 && rx.buf)
    {
        cJSON *root = cJSON_Parse(rx.buf);
        const char *name = fs_str(cJSON_GetObjectItem(root, "fields"), "display_name");
        named = name && name[0];
        cJSON_Delete(root);
    }
    free(rx.buf);
    return named;
}

cloud_err_t cloud_heartbeat(const cloud_presence_t *p)
{
    cloud_err_t te = ensure_token();
    if (te != CLOUD_OK)
    {
        return te;
    }
    char path[160];
    snprintf(path, sizeof path, "projects/" PROJECT "/databases/(default)/documents/users/%s/tigertouches/%s", s_uid,
             p->mac);
    char doc_url[300];
    snprintf(doc_url, sizeof doc_url, "https://firestore.googleapis.com/v1/%s", path);
    const bool named = p->full && has_display_name(doc_url);

    cJSON *root = cJSON_CreateObject();
    cJSON *w = cJSON_CreateObject();
    cJSON_AddItemToArray(cJSON_AddArrayToObject(root, "writes"), w);
    cJSON *up = cJSON_AddObjectToObject(w, "update");
    cJSON_AddStringToObject(up, "name", path);
    cJSON *f = cJSON_AddObjectToObject(up, "fields");
    cJSON *mask = cJSON_AddArrayToObject(cJSON_AddObjectToObject(w, "updateMask"), "fieldPaths");

    if (p->full)
    {
        fs_put_str(f, mask, "mac", p->mac);
        fs_put_str(f, mask, "fw_version", p->fw);
        fs_put_str(f, mask, "mdns_hostname", p->host);
        if (!named)
        {
            char name[24];
            snprintf(name, sizeof name, "TigerTouch-%s", p->mac + 8);
            for (char *c = name + 11; *c; c++)
            {
                *c = (char)toupper((unsigned char)*c);
            }
            fs_put_str(f, mask, "display_name", name);
        }
        fs_put_null(f, mask, "hardware_revision");
    }
    if (p->rssi != 0)
    {
        fs_put_int(f, mask, "wifi_signal_dbm", p->rssi);
    }
    else
    {
        fs_put_null(f, mask, "wifi_signal_dbm");
    }
    fs_put_str(f, mask, "ip_address", p->ip);
    fs_put_str(f, mask, "power_state", p->screen_off ? "screen_off" : "active");
    /* Whether a battery is fitted is what the owner declared, as on TigerSpool.
     * Its charge, whether it charges and the power source are not known: the
     * board gives the firmware no way to read them (docs/HARDWARE.md), so null,
     * never a guess. */
    cJSON *bp = cJSON_AddObjectToObject(f, "battery_present");
    cJSON_AddBoolToObject(bp, "booleanValue", p->battery);
    cJSON_AddItemToArray(mask, cJSON_CreateString("battery_present"));
    cJSON *dm = cJSON_AddObjectToObject(f, "demo");
    cJSON_AddBoolToObject(dm, "booleanValue", p->demo);
    cJSON_AddItemToArray(mask, cJSON_CreateString("demo"));
    fs_put_null(f, mask, "power_source");
    fs_put_null(f, mask, "battery_percent");
    fs_put_null(f, mask, "is_charging");
    fs_put_int(f, mask, "printers_active", p->n);
    fs_put_ids(f, mask, "printer_ids", p, false);
    fs_put_ids(f, mask, "printers_online", p, true);

    cJSON *t = cJSON_CreateObject();
    cJSON_AddStringToObject(t, "fieldPath", "last_heartbeat_at");
    cJSON_AddStringToObject(t, "setToServerValue", "REQUEST_TIME");
    cJSON_AddItemToArray(cJSON_AddArrayToObject(w, "updateTransforms"), t);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body)
    {
        return CLOUD_ERR_OTHER;
    }
    rx_t rx = {0};
    const int st = http_do("https://firestore.googleapis.com/v1/projects/" PROJECT
                           "/databases/(default)/documents:commit",
                           HTTP_METHOD_POST, body, s_id_token, &rx);
    cJSON_free(body);
    free(rx.buf);
    if (st == 200)
    {
        if (p->full)
        {
            ESP_LOGI(TAG, "presence written to the account");
        }
        return CLOUD_OK;
    }
    ESP_LOGW(TAG, "heartbeat not written: HTTP %d", st);
    return st <= 0 ? CLOUD_ERR_NETWORK : CLOUD_ERR_OTHER;
}

/* ---- Names ----------------------------------------------------------------- */

const char *cloud_brand_label(const char *brand)
{
    static const struct
    {
        const char *id;
        const char *label;
    } MAP[] = {
        {"creality", "Creality"}, {"bambulab", "Bambu Lab"}, {"flashforge", "FlashForge"},
        {"snapmaker", "Snapmaker"}, {"elegoo", "Elegoo"},    {"anycubic", "Anycubic"},
        {"prusa", "Prusa"},     {"klipper", "Klipper"}, {"octoprint", "OctoPrint"},
    };
    for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++)
    {
        if (strcmp(MAP[i].id, brand) == 0)
        {
            return MAP[i].label;
        }
    }
    return brand;
}
