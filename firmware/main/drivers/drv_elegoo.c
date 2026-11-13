/* Elegoo Centauri Carbon 2, over its own MQTT broker: plain TCP on port 1883,
 * user "elegoo", the printer's access code as password (reference: Tiger
 * Studio's elegoo/PROTOCOL.md).
 *
 * The device registers itself under a client id, asks for a full snapshot
 * (method 1002) and the Canvas filament list (method 2005), then keeps
 * polling the snapshot: the continuous push (method 6000) only carries the
 * values that changed, so the poll is what keeps the state whole.
 *
 * The CC1 speaks another protocol (SDCP): see drv_elegoo_sdcp.c. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "addr.h"
#include "cJSON.h"
#include "driver_util.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "mqtt_client.h"
#include "mqtt_rx.h"
#include "printer_driver.h"

static const char *TAG = "elegoo";

#define MAX_MSG (96 * 1024) /* a picture's answer is a base64 PNG: tens of KB */
#define POLL_US (5LL * 1000 * 1000)
#define PING_US (10LL * 1000 * 1000)
#define CANVAS_US (30LL * 1000 * 1000)
#define ONLINE_WINDOW_US (30LL * 1000 * 1000)
#define REGISTER_WAIT_US (1200LL * 1000)

typedef struct
{
    esp_mqtt_client_handle_t mqtt;
    SemaphoreHandle_t lock;
    char sn[40];
    char cid[16];
    char t_register[64], t_request[80], t_status[64], t_response[80];
    /* "elegoo/" + serial (39) + "/" + client id (15) + "_req/register_response": 85 bytes
     * at the longest, so 80 could cut the topic short and the reply never arrive. */
    char t_regresp[96];

    mqtt_rx_t rx;
    bool rx_is_regresp;

    bool connected;
    bool burst_sent;
    int64_t connected_at_us, last_msg_us, last_poll_us, last_ping_us, last_canvas_us;
    uint32_t seq;

    float nozzle, nozzle_target, bed, bed_target, chamber;
    int status, sub_status, progress, layer, left_s;
    char pstate[16];
    char file[48];
    char file_raw[128];  /* print_status.filename as the printer wrote it: the picture's request names it */
    char thumb_for[48];  /* the name (on the view) the picture being asked for belongs to */
    bool light_on; /* led.status of the live push */

    bool canvas;
    struct
    {
        bool present;
        uint32_t color;
        char material[12];
    } tray[4];
} elg_t;

/* ---- Requests ------------------------------------------------------------- */

static void send_method(elg_t *c, int method, const char *params_json)
{
    char buf[256];
    const int n = snprintf(buf, sizeof buf, "{\"id\":%u,\"method\":%d,\"params\":%s}", (unsigned)++c->seq, method,
                           params_json ? params_json : "{}");
    esp_mqtt_client_publish(c->mqtt, c->t_request, buf, n, 0, 0);
}

static void send_burst(elg_t *c)
{
    if (c->burst_sent)
    {
        return;
    }
    c->burst_sent = true;
    /* The first command a client sends is its hostname; the printer ignores
     * the rest until it has it. */
    send_method(c, 1043, "{\"hostname\":\"Tiger Touch\"}");
    send_method(c, 1002, "{}");
    send_method(c, 2005, "{}");
    c->last_poll_us = c->last_canvas_us = esp_timer_get_time();
}

/* ---- State merge ---------------------------------------------------------- */

static void apply_canvas(elg_t *c, const cJSON *info)
{
    const cJSON *list = cJSON_GetObjectItem(info, "canvas_list");
    const cJSON *cv = cJSON_GetArrayItem(list, 0);
    if (!cJSON_IsObject(cv))
    {
        return;
    }
    const cJSON *conn = cJSON_GetObjectItem(cv, "connected");
    c->canvas = !(cJSON_IsNumber(conn) && conn->valueint == 0);
    const cJSON *trays = cJSON_GetObjectItem(cv, "tray_list");
    for (int i = 0; i < 4; i++)
    {
        const cJSON *t = cJSON_GetArrayItem(trays, i);
        if (!cJSON_IsObject(t))
        {
            continue;
        }
        const cJSON *type = cJSON_GetObjectItem(t, "filament_type");
        const cJSON *col = cJSON_GetObjectItem(t, "filament_color");
        int st = 0;
        json_get_i(t, "status", &st);
        c->tray[i].present = st != 0 || (cJSON_IsString(type) && type->valuestring[0]);
        strlcpy(c->tray[i].material, cJSON_IsString(type) ? type->valuestring : "", sizeof c->tray[i].material);
        c->tray[i].color = color_from_hex(cJSON_IsString(col) ? col->valuestring : NULL);
    }
}

static void apply_result(elg_t *c, const cJSON *r)
{
    xSemaphoreTake(c->lock, portMAX_DELAY);
    const cJSON *ex = cJSON_GetObjectItem(r, "extruder");
    if (cJSON_IsObject(ex))
    {
        json_get_f(ex, "temperature", &c->nozzle);
        json_get_f(ex, "target", &c->nozzle_target);
    }
    const cJSON *hb = cJSON_GetObjectItem(r, "heater_bed");
    if (cJSON_IsObject(hb))
    {
        json_get_f(hb, "temperature", &c->bed);
        json_get_f(hb, "target", &c->bed_target);
    }
    const cJSON *zt = cJSON_GetObjectItem(r, "ztemperature_sensor");
    if (cJSON_IsObject(zt))
    {
        json_get_f(zt, "temperature", &c->chamber);
    }
    const cJSON *led = cJSON_GetObjectItem(r, "led");
    if (cJSON_IsObject(led))
    {
        const cJSON *st = cJSON_GetObjectItem(led, "status");
        if (cJSON_IsNumber(st))
        {
            c->light_on = st->valuedouble != 0.0;
        }
    }
    const cJSON *ms = cJSON_GetObjectItem(r, "machine_status");
    if (cJSON_IsObject(ms))
    {
        json_get_i(ms, "progress", &c->progress);
        json_get_i(ms, "status", &c->status);
        json_get_i(ms, "sub_status", &c->sub_status);
    }
    const cJSON *ps = cJSON_GetObjectItem(r, "print_status");
    if (cJSON_IsObject(ps))
    {
        const cJSON *s = cJSON_GetObjectItem(ps, "state");
        if (cJSON_IsString(s))
        {
            strlcpy(c->pstate, s->valuestring, sizeof c->pstate);
        }
        const cJSON *fn = cJSON_GetObjectItem(ps, "filename");
        if (cJSON_IsString(fn) && fn->valuestring[0])
        {
            const char *name = fn->valuestring;
            strlcpy(c->file_raw, name, sizeof c->file_raw);
            const char *slash = strrchr(name, '/');
            strlcpy(c->file, slash ? slash + 1 : name, sizeof c->file);
            char *dot = strrchr(c->file, '.');
            if (dot && strcasecmp(dot, ".gcode") == 0)
            {
                *dot = '\0';
            }
        }
        json_get_i(ps, "current_layer", &c->layer);
        json_get_i(ps, "remaining_time_sec", &c->left_s);
    }
    const cJSON *ci = cJSON_GetObjectItem(r, "canvas_info");
    if (cJSON_IsObject(ci))
    {
        apply_canvas(c, ci);
    }
    xSemaphoreGive(c->lock);
}

/* ---- The picture of the print --------------------------------------------------
 *
 * The CC2 sends it only on its MQTT link: method 1045 with {file_name, storage_media:"local"} is answered
 * by {method:1045, result:{thumbnail:<base64 PNG>}}, or error_code 1003 while it heats (not there yet),
 * as Tiger Studio Manager's notes say. The picture service cannot reach this link, so its `thumb_fetch`
 * here only leaves a request (below) that elg_snapshot picks up and sends, and returns nothing: the service
 * asks again every 30 s until the answer, decoded here, has been handed to it (thumb_provide). */

static struct
{
    char sn[40];
    char file[48];
    bool pending;
} s_req;
static portMUX_TYPE s_req_mux = portMUX_INITIALIZER_UNLOCKED;

static bool elegoo_thumb_fetch(const account_printer_t *p, const char *file, uint8_t **data, size_t *len)
{
    *data = NULL;
    *len = 0;
    taskENTER_CRITICAL(&s_req_mux);
    strlcpy(s_req.sn, p->sn, sizeof s_req.sn);
    strlcpy(s_req.file, file, sizeof s_req.file);
    s_req.pending = true;
    taskEXIT_CRITICAL(&s_req_mux);
    return false;
}

/* Sends the picture's request when the service left one for this printer's print in progress. */
static void ask_picture(elg_t *c)
{
    char want[48] = "";
    taskENTER_CRITICAL(&s_req_mux);
    const bool mine = s_req.pending && strcmp(s_req.sn, c->sn) == 0;
    if (mine)
    {
        s_req.pending = false;
        strlcpy(want, s_req.file, sizeof want);
    }
    taskEXIT_CRITICAL(&s_req_mux);
    if (!mine)
    {
        return;
    }
    char raw[128];
    xSemaphoreTake(c->lock, portMAX_DELAY);
    const bool same = c->file_raw[0] && strcmp(want, c->file) == 0; /* the print the view shows */
    if (same)
    {
        strlcpy(raw, c->file_raw, sizeof raw);
        strlcpy(c->thumb_for, want, sizeof c->thumb_for);
    }
    xSemaphoreGive(c->lock);
    if (!same)
    {
        return;
    }
    char esc[300], buf[400];
    size_t n = 0;
    for (const char *s = raw; *s && n + 3 < sizeof esc; s++)
    {
        if (*s == '"' || *s == '\\')
        {
            esc[n++] = '\\';
        }
        esc[n++] = *s;
    }
    esc[n] = '\0';
    const int bn = snprintf(buf, sizeof buf,
                            "{\"id\":%u,\"method\":1045,\"params\":{\"file_name\":\"%s\",\"storage_media\":\"local\"}}",
                            (unsigned)++c->seq, esc);
    if (bn > 0 && bn < (int)sizeof buf)
    {
        esp_mqtt_client_publish(c->mqtt, c->t_request, buf, bn, 0, 0);
    }
}

/* The answer to method 1045: the base64 PNG, decoded and handed to the picture service. */
static void handle_picture(elg_t *c, const cJSON *res)
{
    static const char *const KEYS[] = {"thumbnail", "thumbData", "base64", "imageData", "image"};
    const char *b64 = NULL;
    for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0] && !b64; i++)
    {
        const cJSON *v = cJSON_GetObjectItem(res, KEYS[i]);
        b64 = cJSON_IsString(v) && v->valuestring[0] ? v->valuestring : NULL;
    }
    if (!b64)
    {
        return; /* error_code 1003: not there yet, the service asks again */
    }
    if (!strncmp(b64, "data:", 5) && strchr(b64, ','))
    {
        b64 = strchr(b64, ',') + 1;
    }
    const size_t slen = strlen(b64);
    size_t olen = 0;
    mbedtls_base64_decode(NULL, 0, &olen, (const unsigned char *)b64, slen); /* sizes it */
    uint8_t *png = olen ? heap_caps_malloc(olen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    if (!png)
    {
        return;
    }
    if (mbedtls_base64_decode(png, olen, &olen, (const unsigned char *)b64, slen) == 0 && olen)
    {
        char name[48];
        xSemaphoreTake(c->lock, portMAX_DELAY);
        strlcpy(name, c->thumb_for, sizeof name);
        xSemaphoreGive(c->lock);
        if (name[0])
        {
            thumb_provide(c->sn, name, png, olen);
        }
    }
    heap_caps_free(png);
}

static void handle_message(elg_t *c, const char *data, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root)
    {
        return;
    }
    const cJSON *res = cJSON_GetObjectItem(root, "result");
    const cJSON *method = cJSON_GetObjectItem(root, "method");
    if (cJSON_IsObject(res) && cJSON_IsNumber(method) && method->valueint == 1045)
    {
        handle_picture(c, res);
    }
    else if (cJSON_IsObject(res))
    {
        apply_result(c, res);
    }
    cJSON_Delete(root);
}

/* ---- MQTT ----------------------------------------------------------------- */

/* The topic is not NUL-terminated: compare its tail. */
static bool ends_with(const char *topic, int len, const char *suffix)
{
    const int n = (int)strlen(suffix);
    return topic && len >= n && memcmp(topic + len - n, suffix, (size_t)n) == 0;
}

static void on_mqtt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    elg_t *c = (elg_t *)arg;
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)data;

    switch ((esp_mqtt_event_id_t)id)
    {
    case MQTT_EVENT_CONNECTED:
    {
        ESP_LOGI(TAG, "connected to the printer's broker");
        xSemaphoreTake(c->lock, portMAX_DELAY);
        c->connected = true;
        c->burst_sent = false;
        c->connected_at_us = esp_timer_get_time();
        xSemaphoreGive(c->lock);
        esp_mqtt_client_subscribe(c->mqtt, c->t_status, 0);
        esp_mqtt_client_subscribe(c->mqtt, c->t_response, 0);
        esp_mqtt_client_subscribe(c->mqtt, c->t_regresp, 0);
        char reg[96];
        const int n = snprintf(reg, sizeof reg, "{\"client_id\":\"%s\",\"request_id\":\"%s_req\"}", c->cid, c->cid);
        esp_mqtt_client_publish(c->mqtt, c->t_register, reg, n, 0, 0);
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected");
        xSemaphoreTake(c->lock, portMAX_DELAY);
        c->connected = false;
        xSemaphoreGive(c->lock);
        break;

    case MQTT_EVENT_DATA:
    {
        if (ev->total_data_len > 0 && (size_t)ev->total_data_len <= MAX_MSG && ev->current_data_offset == 0)
        {
            /* The topic only comes with the first chunk. */
            c->rx_is_regresp = ends_with(ev->topic, ev->topic_len, "register_response");
        }
        const char *text;
        size_t len;
        if (mqtt_rx_feed(&c->rx, ev, MAX_MSG, &text, &len))
        {
            c->last_msg_us = esp_timer_get_time();
            if (c->rx_is_regresp)
            {
                send_burst(c); /* registered: the questions can start */
            }
            else
            {
                handle_message(c, text, len);
            }
        }
        break;
    }

    default:
        break;
    }
}

/* ---- Driver --------------------------------------------------------------- */

static drv_kind_t elg_support(const account_printer_t *p)
{
    /* A Centauri Carbon 1 speaks SDCP: that is drv_elegoo_sdcp.c. */
    if (strcmp(p->proto, "sdcp") == 0)
    {
        return DRV_NONE;
    }
    return (p->ip[0] && p->sn[0] && !p->cloud) ? DRV_LAN : DRV_NONE;
}

static void *elg_start(const account_printer_t *p)
{
    elg_t *c = calloc(1, sizeof *c);
    if (!c)
    {
        return NULL;
    }
    c->lock = xSemaphoreCreateMutex();
    strlcpy(c->sn, p->sn, sizeof c->sn);
    snprintf(c->cid, sizeof c->cid, "TTG_%u", (unsigned)(1000 + esp_random() % 9000));
    snprintf(c->t_register, sizeof c->t_register, "elegoo/%s/api_register", c->sn);
    snprintf(c->t_request, sizeof c->t_request, "elegoo/%s/%s/api_request", c->sn, c->cid);
    snprintf(c->t_status, sizeof c->t_status, "elegoo/%s/api_status", c->sn);
    snprintf(c->t_response, sizeof c->t_response, "elegoo/%s/%s/api_response", c->sn, c->cid);
    snprintf(c->t_regresp, sizeof c->t_regresp, "elegoo/%s/%s_req/register_response", c->sn, c->cid);

    char uri[64], hp[ADDR_HOSTPORT_MAX];
    addr_with_port(hp, sizeof hp, p->ip, 1883);
    snprintf(uri, sizeof uri, "mqtt://%s", hp);
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = uri,
        .credentials.username = "elegoo",
        .credentials.client_id = c->cid,
        .credentials.authentication.password = p->cc[0] ? p->cc : "123456",
        .session.keepalive = 60,
        .buffer.size = 4096,
        .buffer.out_size = 1024,
        .task.stack_size = 6144,
        .network.reconnect_timeout_ms = 5000,
    };
    c->mqtt = esp_mqtt_client_init(&cfg);
    if (!c->mqtt)
    {
        vSemaphoreDelete(c->lock);
        free(c);
        return NULL;
    }
    esp_mqtt_client_register_event(c->mqtt, ESP_EVENT_ANY_ID, on_mqtt, c);
    esp_mqtt_client_start(c->mqtt);
    ESP_LOGI(TAG, "connecting to the printer's broker");
    return c;
}

static void elg_stop(void *ctx)
{
    elg_t *c = (elg_t *)ctx;
    if (!c)
    {
        return;
    }
    esp_mqtt_client_stop(c->mqtt);
    esp_mqtt_client_destroy(c->mqtt);
    vSemaphoreDelete(c->lock);
    mqtt_rx_free(&c->rx);
    free(c);
}

/* Pause, resume, stop: methods 1021 (pause), 1023 (resume) and 1022 (cancel) on the request topic. Written from Tiger Studio's protocol notes;
 * pause, resume, stop and the light were seen working on a real printer by the owner (2026-10-18). */
static bool elg_control(void *ctx, pv_control_t cmd)
{
    static const int METHOD[] = {[PV_CTL_PAUSE] = 1021, [PV_CTL_RESUME] = 1023, [PV_CTL_STOP] = 1022};
    elg_t *c = (elg_t *)ctx;
    if (c && c->mqtt && c->connected && (cmd == PV_CTL_LIGHT_ON || cmd == PV_CTL_LIGHT_OFF))
    {
        /* Method 1029, power 0 / 1 (Tiger Studio's notes, section 17.1). */
        send_method(c, 1029, cmd == PV_CTL_LIGHT_ON ? "{\"power\":1}" : "{\"power\":0}");
        return true;
    }
    if (!c || !c->mqtt || !c->connected || (int)cmd < 0 || (int)cmd > PV_CTL_STOP)
    {
        return false;
    }
    send_method(c, METHOD[cmd], NULL);
    return true;
}

static void elg_snapshot(void *ctx, printer_view_t *v)
{
    elg_t *c = (elg_t *)ctx;
    const int64_t now = esp_timer_get_time();

    /* The register answer is waited for a moment, then the questions go out
     * anyway: a firmware that does not answer it still answers the rest. */
    if (c->connected && !c->burst_sent && now - c->connected_at_us > REGISTER_WAIT_US)
    {
        send_burst(c);
    }
    if (c->connected && c->burst_sent)
    {
        if (now - c->last_poll_us > POLL_US)
        {
            c->last_poll_us = now;
            send_method(c, 1002, "{}");
        }
        if (now - c->last_canvas_us > CANVAS_US)
        {
            c->last_canvas_us = now;
            send_method(c, 2005, "{}");
        }
        ask_picture(c);
        if (now - c->last_ping_us > PING_US)
        {
            c->last_ping_us = now;
            esp_mqtt_client_publish(c->mqtt, c->t_request, "{\"type\":\"PING\"}", 15, 0, 0);
        }
    }

    xSemaphoreTake(c->lock, portMAX_DELAY);
    v->online = c->connected && c->last_msg_us != 0 && (now - c->last_msg_us) < ONLINE_WINDOW_US;
    v->can_control = v->online;
    v->can_light = v->online;
    v->light_on = c->light_on;

    /* print_status.state is the empty string at rest, not "standby". */
    const char *s = c->pstate;
    if (!strcmp(s, "printing") || !strcmp(s, "running") || !strcmp(s, "busy") || !strcmp(s, "preparing") ||
        !strcmp(s, "heating"))
    {
        v->state = PV_PRINTING;
    }
    else if (!strcmp(s, "paused"))
    {
        v->state = PV_PAUSED;
    }
    else if (!strcmp(s, "complete") || !strcmp(s, "completed"))
    {
        v->state = PV_DONE;
    }
    else if (!strcmp(s, "error") || !strcmp(s, "failed"))
    {
        v->state = PV_ERROR;
    }
    else if (c->status == 14)
    {
        v->state = PV_ERROR;
    }
    else if (c->status == 2 || c->status == 3)
    {
        v->state = (c->sub_status == 2501 || c->sub_status == 2502) ? PV_PAUSED : PV_PRINTING;
    }
    else
    {
        v->state = PV_IDLE;
    }

    v->progress = v->state == PV_IDLE ? 0 : (c->progress > 100 ? 100 : c->progress);
    v->layer = c->layer;
    v->total_layer = 0; /* not in the snapshot: only the layer in progress is */
    v->left_s = (v->state == PV_PRINTING || v->state == PV_PAUSED) ? c->left_s : 0;
    v->nozzle = c->nozzle;
    v->nozzle_target = c->nozzle_target;
    v->bed = c->bed;
    v->bed_target = c->bed_target;
    v->chamber = c->chamber;
    strlcpy(v->file, c->file, sizeof v->file);

    memset(v->unit, 0, sizeof v->unit);
    if (c->canvas)
    {
        pv_unit_t *u = &v->unit[0];
        u->present = true;
        u->slots = 4;
        snprintf(u->label, sizeof u->label, "Canvas");
        for (int i = 0; i < 4; i++)
        {
            u->slot[i].present = c->tray[i].present;
            u->slot[i].color = c->tray[i].color;
            u->slot[i].percent = -1;
            strlcpy(u->slot[i].material, c->tray[i].material, sizeof u->slot[i].material);
        }
    }
    xSemaphoreGive(c->lock);
}

const printer_driver_t DRIVER_ELEGOO = {
    .brand = "elegoo",
    .support = elg_support,
    .start = elg_start,
    .stop = elg_stop,
    .snapshot = elg_snapshot,
    .control = elg_control,
    .thumb_fetch = elegoo_thumb_fetch,
};
