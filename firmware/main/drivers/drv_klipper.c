/* Klipper, through Moonraker: HTTP on port 7125 (or the port written in the
 * address), polled every two seconds - Tiger Studio's Klipper plugin, read in
 * its source (renderer/printers/klipper, core.js and PROTOCOL.md).
 *
 * The objects a printer has are listed once (GET /printer/objects/list): its
 * extruders, a chamber sensor, a light (an LED chain or an output pin named
 * like one), a Happy Hare MMU or AFC lanes. Each poll then asks exactly those
 * (POST /printer/objects/query). The time left comes from the file's metadata
 * (estimated_time), asked once per file. An API key is sent only when the
 * account has one: most Moonraker installs trust the local network.
 *
 * A Snapmaker U1 is Moonraker too, but has a driver of its own (drv_snapmaker.c)
 * for its proprietary filament data; this one is for every other Klipper
 * printer. Written from Studio's plugin; the reading, pause, resume, stop
 * and the light were seen working on a real printer by the owner (2026-10-18). */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "addr.h"
#include "cJSON.h"
#include "driver_util.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "http_lite.h"
#include "i18n.h"
#include "moonraker_thumb.h"
#include "printer_driver.h"

static const char *TAG = "klipper";

#define POLL_MS 2000
#define ONLINE_WINDOW_US (10LL * 1000 * 1000)
#define REPLY_MAX (16 * 1024) /* the object list of a large printer */
#define QUERY_MAX 2048
#define MAX_LANES 16 /* MMU gates or AFC lanes: four cards of four */

typedef struct
{
    bool present;
    uint32_t color;
    char material[12];
    bool active;
} lane_t;

typedef struct
{
    TaskHandle_t task;
    volatile bool run;
    volatile bool exited;
    SemaphoreHandle_t lock;
    char base[64]; /* http://host:port */
    char key[48];

    /* What the printer has, from the object list. */
    bool detected;
    int n_ext;
    char chamber[48];
    char light[48];   /* the whole object name, "led chamber" / "output_pin caselight" */
    bool light_is_pin;
    bool mmu;
    char afc_prefix[16]; /* "AFC_stepper" or "AFC_lane" */
    char afc_lane[MAX_LANES][16];
    int n_afc;
    char *query; /* the body of the poll, built from the above */

    /* What it says. */
    int64_t last_ok_us;
    int http_err;        /* 0 ok, 401/403 refused, -1 no answer */
    bool ready;          /* Klippy is ready */
    char not_ready[96];  /* its state_message when it is not */
    char pstate[12];     /* standby printing paused complete cancelled error */
    char message[96];    /* print_stats.message: why, on an error */
    float progress;      /* 0..1 */
    char filename[128];
    char meta_for[128];  /* the file whose estimated_time is known */
    float est_s, elapsed_s;
    int layer, total_layer;
    float temp[4], target[4];
    char active_ext[12];
    float bed, bed_target, chamber_t;
    bool light_on;
    lane_t lane[MAX_LANES];
    int n_lanes;
} klip_t;

/* ---- Requests --------------------------------------------------------------- */

static int request(klip_t *c, esp_http_client_method_t method, const char *path, const char *body, char *out,
                   size_t cap)
{
    char url[256];
    snprintf(url, sizeof url, "%s%s", c->base, path);
    const http_lite_req_t req = {
        .url = url, .method = method, .body = body, .api_key = c->key, .timeout_ms = 6000};
    return http_lite(&req, out, cap);
}

/* The object's name after its type: "led chamber" -> "chamber". */
static const char *second_token(const char *obj)
{
    const char *sp = strchr(obj, ' ');
    return sp ? sp + 1 : obj;
}

static bool icontains(const char *s, const char *needle)
{
    const size_t n = strlen(needle);
    for (; *s; s++)
    {
        if (strncasecmp(s, needle, n) == 0)
        {
            return true;
        }
    }
    return false;
}

/* Happy Hare may name a colour ("red"): only hexadecimal is read, as in Studio. */
static uint32_t color_of(const char *s)
{
    const char *h = s[0] == '#' ? s + 1 : s;
    const size_t n = strlen(h);
    if (n != 6 && n != 8)
    {
        return COLOR_UNKNOWN;
    }
    for (size_t i = 0; i < n; i++)
    {
        if (!isxdigit((unsigned char)h[i]))
        {
            return COLOR_UNKNOWN;
        }
    }
    return color_from_hex(s);
}

static bool starts(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* The objects, once: what to ask for in every poll. */
static bool detect(klip_t *c, char *reply)
{
    int st = request(c, HTTP_METHOD_GET, "/printer/objects/list", NULL, reply, REPLY_MAX);
    c->http_err = st == 200 ? 0 : st;
    if (st != 200)
    {
        return false;
    }
    cJSON *root = cJSON_Parse(reply);
    const cJSON *objs = root ? cJSON_GetObjectItem(cJSON_GetObjectItem(root, "result"), "objects") : NULL;
    if (!cJSON_IsArray(objs))
    {
        cJSON_Delete(root);
        return false;
    }
    c->n_ext = 0;
    c->chamber[0] = c->light[0] = c->afc_prefix[0] = '\0';
    c->mmu = false;
    c->n_afc = 0;
    char chamber_any[48] = "";
    const cJSON *o;
    cJSON_ArrayForEach(o, objs)
    {
        if (!cJSON_IsString(o))
        {
            continue;
        }
        const char *n = o->valuestring;
        /* extruder, extruder1, extruder2...: the count of the highest seen */
        if (starts(n, "extruder") && (n[8] == '\0' || isdigit((unsigned char)n[8])))
        {
            const int i = n[8] ? atoi(n + 8) : 0;
            if (i < 4 && i + 1 > c->n_ext)
            {
                c->n_ext = i + 1;
            }
        }
        else if (!strcmp(n, "mmu"))
        {
            c->mmu = true;
        }
        else if ((starts(n, "AFC_stepper ") || starts(n, "AFC_lane ")) && c->n_afc < MAX_LANES)
        {
            if (!c->afc_prefix[0])
            {
                snprintf(c->afc_prefix, sizeof c->afc_prefix, "%.*s", (int)(strchr(n, ' ') - n), n);
            }
            strlcpy(c->afc_lane[c->n_afc++], second_token(n), sizeof c->afc_lane[0]);
        }
        else if (!strcmp(n, "heater_generic chamber") || !strcmp(n, "temperature_sensor chamber"))
        {
            if (!c->chamber[0] || starts(n, "heater_generic"))
            {
                strlcpy(c->chamber, n, sizeof c->chamber);
            }
        }
        else if ((starts(n, "heater_generic ") || starts(n, "temperature_sensor ")) && icontains(n, "chamber"))
        {
            if (!chamber_any[0])
            {
                strlcpy(chamber_any, n, sizeof chamber_any);
            }
        }
        else if (!c->light[0] && (starts(n, "led ") || starts(n, "neopixel ") || starts(n, "dotstar ") ||
                                  starts(n, "pca9533 ") || starts(n, "pca9632 ")))
        {
            strlcpy(c->light, n, sizeof c->light);
            c->light_is_pin = false;
        }
        else if (!c->light[0] && starts(n, "output_pin ") &&
                 (icontains(n, "light") || icontains(n, "led") || icontains(n, "lamp")))
        {
            strlcpy(c->light, n, sizeof c->light);
            c->light_is_pin = true;
        }
    }
    cJSON_Delete(root);
    if (!c->chamber[0])
    {
        strlcpy(c->chamber, chamber_any, sizeof c->chamber);
    }
    if (c->n_ext == 0)
    {
        c->n_ext = 1;
    }

    /* The poll's body: only what this printer has. */
    int at = snprintf(c->query, QUERY_MAX,
                      "{\"objects\":{\"webhooks\":[\"state\",\"state_message\"],"
                      "\"print_stats\":[\"state\",\"filename\",\"print_duration\",\"info\",\"message\"],"
                      "\"display_status\":[\"progress\"],\"virtual_sdcard\":[\"progress\",\"is_active\"],"
                      "\"heater_bed\":[\"temperature\",\"target\"],\"toolhead\":[\"extruder\"]");
    for (int i = 0; i < c->n_ext && at < QUERY_MAX; i++)
    {
        at += i ? snprintf(c->query + at, QUERY_MAX - at, ",\"extruder%d\":[\"temperature\",\"target\"]", i)
                : snprintf(c->query + at, QUERY_MAX - at, ",\"extruder\":[\"temperature\",\"target\"]");
    }
    if (c->chamber[0] && at < QUERY_MAX)
    {
        at += snprintf(c->query + at, QUERY_MAX - at, ",\"%s\":[\"temperature\",\"target\"]", c->chamber);
    }
    if (c->light[0] && at < QUERY_MAX)
    {
        at += snprintf(c->query + at, QUERY_MAX - at, ",\"%s\":[\"%s\"]", c->light,
                       c->light_is_pin ? "value" : "color_data");
    }
    if (c->mmu && at < QUERY_MAX)
    {
        at += snprintf(c->query + at, QUERY_MAX - at,
                       ",\"mmu\":[\"num_gates\",\"gate\",\"gate_status\",\"gate_material\",\"gate_color\"]");
    }
    else if (c->n_afc && at < QUERY_MAX)
    {
        at += snprintf(c->query + at, QUERY_MAX - at, ",\"AFC\":[\"current_lane\"]");
        for (int i = 0; i < c->n_afc && at < QUERY_MAX; i++)
        {
            at += snprintf(c->query + at, QUERY_MAX - at,
                           ",\"%s %s\":[\"material\",\"color\",\"load\",\"prep\",\"tool_loaded\"]", c->afc_prefix,
                           c->afc_lane[i]);
        }
    }
    if (at < QUERY_MAX)
    {
        snprintf(c->query + at, QUERY_MAX - at, "}}");
    }
    ESP_LOGI(TAG, "detected: %d extruder(s), chamber %s, light %s, %s", c->n_ext, c->chamber[0] ? "yes" : "no",
             c->light[0] ? "yes" : "no", c->mmu ? "Happy Hare" : (c->n_afc ? "AFC" : "no filament system"));
    return true;
}

static void parse_status(klip_t *c, const cJSON *s)
{
    const cJSON *wh = cJSON_GetObjectItem(s, "webhooks");
    const char *wst = json_str(wh, "state");
    c->ready = !wst[0] || !strcmp(wst, "ready");
    strlcpy(c->not_ready, json_str(wh, "state_message"), sizeof c->not_ready);

    const cJSON *ps = cJSON_GetObjectItem(s, "print_stats");
    const char *st = json_str(ps, "state");
    strlcpy(c->pstate, st[0] ? st : "standby", sizeof c->pstate);
    strlcpy(c->filename, json_str(ps, "filename"), sizeof c->filename);
    strlcpy(c->message, json_str(ps, "message"), sizeof c->message);
    json_get_f(ps, "print_duration", &c->elapsed_s);
    const cJSON *info = cJSON_GetObjectItem(ps, "info");
    c->layer = c->total_layer = 0;
    json_get_i(info, "current_layer", &c->layer);
    json_get_i(info, "total_layer", &c->total_layer);

    /* The SD card's own progress while it prints, else the display's. */
    const cJSON *vs = cJSON_GetObjectItem(s, "virtual_sdcard");
    const cJSON *active = cJSON_GetObjectItem(vs, "is_active");
    float p = 0;
    if (cJSON_IsTrue(active))
    {
        json_get_f(vs, "progress", &p);
    }
    else
    {
        json_get_f(cJSON_GetObjectItem(s, "display_status"), "progress", &p);
    }
    c->progress = p < 0 ? 0 : (p > 1 ? 1 : p);

    for (int i = 0; i < c->n_ext; i++)
    {
        char name[12];
        snprintf(name, sizeof name, i ? "extruder%d" : "extruder", i);
        const cJSON *e = cJSON_GetObjectItem(s, name);
        json_get_f(e, "temperature", &c->temp[i]);
        json_get_f(e, "target", &c->target[i]);
    }
    strlcpy(c->active_ext, json_str(cJSON_GetObjectItem(s, "toolhead"), "extruder"), sizeof c->active_ext);
    const cJSON *bed = cJSON_GetObjectItem(s, "heater_bed");
    json_get_f(bed, "temperature", &c->bed);
    json_get_f(bed, "target", &c->bed_target);
    if (c->chamber[0])
    {
        json_get_f(cJSON_GetObjectItem(s, c->chamber), "temperature", &c->chamber_t);
    }

    if (c->light[0])
    {
        const cJSON *l = cJSON_GetObjectItem(s, c->light);
        if (c->light_is_pin)
        {
            float v = 0;
            json_get_f(l, "value", &v);
            c->light_on = v > 0;
        }
        else
        {
            /* On when any channel of any LED in the chain is lit. */
            bool on = false;
            const cJSON *led, *ch;
            cJSON_ArrayForEach(led, cJSON_GetObjectItem(l, "color_data"))
            {
                cJSON_ArrayForEach(ch, led)
                {
                    on = on || (cJSON_IsNumber(ch) && ch->valuedouble > 0);
                }
            }
            c->light_on = on;
        }
    }

    if (c->mmu)
    {
        const cJSON *m = cJSON_GetObjectItem(s, "mmu");
        const cJSON *status = cJSON_GetObjectItem(m, "gate_status");
        int n = 0;
        json_get_i(m, "num_gates", &n);
        if (n <= 0)
        {
            n = cJSON_GetArraySize(status);
        }
        int gate = -1;
        json_get_i(m, "gate", &gate);
        c->n_lanes = n > MAX_LANES ? MAX_LANES : n;
        for (int g = 0; g < c->n_lanes; g++)
        {
            const cJSON *gs = cJSON_GetArrayItem(status, g);
            lane_t *l = &c->lane[g];
            l->present = cJSON_IsNumber(gs) && gs->valueint != 0; /* 0 empty, -1 unknown, 1/2 loaded */
            strlcpy(l->material, json_str_at(cJSON_GetObjectItem(m, "gate_material"), g), sizeof l->material);
            l->color = color_of(json_str_at(cJSON_GetObjectItem(m, "gate_color"), g));
            l->active = g == gate;
        }
    }
    else if (c->n_afc)
    {
        const char *cur = json_str(cJSON_GetObjectItem(s, "AFC"), "current_lane");
        c->n_lanes = c->n_afc;
        for (int i = 0; i < c->n_afc; i++)
        {
            char name[40];
            snprintf(name, sizeof name, "%s %s", c->afc_prefix, c->afc_lane[i]);
            const cJSON *o = cJSON_GetObjectItem(s, name);
            lane_t *l = &c->lane[i];
            l->present = !(cJSON_IsFalse(cJSON_GetObjectItem(o, "load")) &&
                           cJSON_IsFalse(cJSON_GetObjectItem(o, "prep")));
            strlcpy(l->material, json_str(o, "material"), sizeof l->material);
            l->color = color_of(json_str(o, "color"));
            l->active = !strcmp(cur, c->afc_lane[i]) || cJSON_IsTrue(cJSON_GetObjectItem(o, "tool_loaded"));
        }
    }
}

/* The file's estimated time, once per file: Moonraker has no live "time left". */
static void fetch_estimate(klip_t *c, char *reply)
{
    char path[200] = "/server/files/metadata?filename=";
    size_t at = strlen(path);
    for (const unsigned char *s = (const unsigned char *)c->filename; *s && at + 4 < sizeof path; s++)
    {
        if (isalnum(*s) || strchr("-_.~/", *s))
        {
            path[at++] = (char)*s;
        }
        else
        {
            at += snprintf(path + at, sizeof path - at, "%%%02X", *s);
        }
    }
    path[at] = '\0';
    float est = 0;
    if (request(c, HTTP_METHOD_GET, path, NULL, reply, REPLY_MAX) == 200)
    {
        cJSON *root = cJSON_Parse(reply);
        json_get_f(root ? cJSON_GetObjectItem(root, "result") : NULL, "estimated_time", &est);
        cJSON_Delete(root);
    }
    xSemaphoreTake(c->lock, portMAX_DELAY);
    c->est_s = est;
    strlcpy(c->meta_for, c->filename, sizeof c->meta_for);
    xSemaphoreGive(c->lock);
}

static void poll_task(void *arg)
{
    klip_t *c = (klip_t *)arg;
    char *reply = heap_caps_malloc(REPLY_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    while (c->run && reply)
    {
        if (!c->detected)
        {
            c->detected = detect(c, reply);
        }
        if (c->detected)
        {
            const int st = request(c, HTTP_METHOD_POST, "/printer/objects/query", c->query, reply, REPLY_MAX);
            cJSON *root = st == 200 ? cJSON_Parse(reply) : NULL;
            const cJSON *status = root ? cJSON_GetObjectItem(cJSON_GetObjectItem(root, "result"), "status") : NULL;
            xSemaphoreTake(c->lock, portMAX_DELAY);
            c->http_err = st == 200 ? 0 : st;
            if (cJSON_IsObject(status))
            {
                parse_status(c, status);
                c->last_ok_us = esp_timer_get_time();
            }
            const bool new_file = c->filename[0] && strcmp(c->filename, c->meta_for) != 0;
            xSemaphoreGive(c->lock);
            cJSON_Delete(root);
            if (st >= 400 && st < 500 && st != 401 && st != 403)
            {
                c->detected = false; /* Klippy restarted: its objects may have changed */
            }
            if (new_file)
            {
                fetch_estimate(c, reply);
            }
        }
        for (int waited = 0; waited < POLL_MS && c->run; waited += 100)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    free(reply);
    c->exited = true;
    vTaskSuspend(NULL);
}

/* ---- Driver --------------------------------------------------------------- */

static drv_kind_t klip_support(const account_printer_t *p)
{
    return (p->ip[0] && !p->cloud) ? DRV_LAN : DRV_NONE;
}

static void *klip_start(const account_printer_t *p)
{
    klip_t *c = heap_caps_calloc(1, sizeof *c, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *query = heap_caps_malloc(QUERY_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!c || !query)
    {
        free(c);
        free(query);
        return NULL;
    }
    c->query = query;
    c->lock = xSemaphoreCreateMutex();
    /* "host" or "host:port"; Moonraker's port when none is written. */
    char hp[ADDR_HOSTPORT_MAX];
    addr_with_port(hp, sizeof hp, p->ip, 7125);
    snprintf(c->base, sizeof c->base, "http://%s", hp);
    strlcpy(c->key, p->key, sizeof c->key);
    strlcpy(c->pstate, "standby", sizeof c->pstate);
    c->run = true;
    if (xTaskCreatePinnedToCoreWithCaps(poll_task, "klipper_poll", 8 * 1024, c, 3, &c->task, 0,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS)
    {
        ESP_LOGW(TAG, "poll task not started");
        vSemaphoreDelete(c->lock);
        free(c->query);
        free(c);
        return NULL;
    }
    ESP_LOGI(TAG, "polling started");
    return c;
}

static void klip_stop(void *ctx)
{
    klip_t *c = (klip_t *)ctx;
    if (!c)
    {
        return;
    }
    c->run = false;
    for (int i = 0; i < 80 && !c->exited; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (c->exited)
    {
        vTaskDeleteWithCaps(c->task);
    }
    vSemaphoreDelete(c->lock);
    free(c->query);
    free(c);
}

/* Pause, resume, cancel: POST /printer/print/<action>. The light: a G-code
 * script, SET_PIN for an output pin, SET_LED for an LED chain, as Studio does.
 * Not run against a real printer. */
static bool klip_control(void *ctx, pv_control_t cmd)
{
    klip_t *c = (klip_t *)ctx;
    if (!c)
    {
        return false;
    }
    char reply[96];
    if (cmd == PV_CTL_LIGHT_ON || cmd == PV_CTL_LIGHT_OFF)
    {
        if (!c->light[0])
        {
            return false;
        }
        const int on = cmd == PV_CTL_LIGHT_ON;
        char body[160];
        if (c->light_is_pin)
        {
            snprintf(body, sizeof body, "{\"script\":\"SET_PIN PIN=%s VALUE=%d\"}", second_token(c->light), on);
        }
        else
        {
            snprintf(body, sizeof body, "{\"script\":\"SET_LED LED=%s RED=%d GREEN=%d BLUE=%d WHITE=%d\"}",
                     second_token(c->light), on, on, on, on);
        }
        const int st = request(c, HTTP_METHOD_POST, "/printer/gcode/script", body, reply, sizeof reply);
        return st >= 200 && st < 300;
    }
    static const char *const PATH[] = {[PV_CTL_PAUSE] = "/printer/print/pause",
                                       [PV_CTL_RESUME] = "/printer/print/resume",
                                       [PV_CTL_STOP] = "/printer/print/cancel"};
    if ((int)cmd < 0 || (int)cmd > PV_CTL_STOP)
    {
        return false;
    }
    const int st = request(c, HTTP_METHOD_POST, PATH[cmd], NULL, reply, sizeof reply);
    return st >= 200 && st < 300;
}

static void klip_snapshot(void *ctx, printer_view_t *v)
{
    klip_t *c = (klip_t *)ctx;
    xSemaphoreTake(c->lock, portMAX_DELAY);
    const int64_t now = esp_timer_get_time();
    v->online = c->last_ok_us != 0 && (now - c->last_ok_us) < ONLINE_WINDOW_US;
    if (c->http_err == 401 || c->http_err == 403)
    {
        snprintf(v->notice, sizeof v->notice, "%s", tr(S_ERR_API_KEY));
        v->notice_error = true;
    }
    else if (v->online && !c->ready)
    {
        snprintf(v->notice, sizeof v->notice, "%s %s", tr(S_KLIPPY_NOT_READY), c->not_ready);
        v->notice_error = true;
    }
    else if (v->online && !strcmp(c->pstate, "error") && c->message[0])
    {
        snprintf(v->notice, sizeof v->notice, "%s", c->message); /* the printer's own words */
        v->notice_error = true;
    }
    v->can_control = v->online && c->ready;
    v->can_light = v->online && c->ready && c->light[0];
    v->light_on = c->light_on;

    const char *s = c->pstate;
    v->state = !strcmp(s, "printing")    ? PV_PRINTING
               : !strcmp(s, "paused")    ? PV_PAUSED
               : !strcmp(s, "complete")  ? PV_DONE
               : !strcmp(s, "error")     ? PV_ERROR
                                         : PV_IDLE; /* standby, cancelled */
    if (!c->ready)
    {
        v->state = PV_IDLE; /* Klippy restarting: no job to speak of until it is back */
    }
    v->progress = v->state == PV_DONE ? 100 : (v->state == PV_IDLE ? 0 : (int)(c->progress * 100 + 0.5f));
    v->layer = c->ready ? c->layer : 0;
    v->total_layer = c->ready ? c->total_layer : 0;
    /* Studio's rule: the estimate less the time printed, else extrapolated. */
    int left = 0;
    if (c->est_s > c->elapsed_s && !strcmp(c->meta_for, c->filename))
    {
        left = (int)(c->est_s - c->elapsed_s);
    }
    else if (c->progress > 0 && c->progress < 1)
    {
        left = (int)(c->elapsed_s * (1 - c->progress) / c->progress);
    }
    v->left_s = (v->state == PV_PRINTING || v->state == PV_PAUSED) ? left : 0;

    const char *slash = strrchr(c->filename, '/');
    strlcpy(v->file, !c->ready ? "" : (slash ? slash + 1 : c->filename), sizeof v->file);
    char *dot = strrchr(v->file, '.');
    if (dot && strcasecmp(dot, ".gcode") == 0)
    {
        *dot = '\0';
    }

    v->nozzle = c->temp[0];
    v->nozzle_target = c->target[0];
    v->nozzle_count = 0;
    v->nozzle_active = -1;
    if (c->n_ext >= 2)
    {
        v->nozzle_count = c->n_ext;
        const char *a = c->active_ext;
        v->nozzle_active = !strcmp(a, "extruder") ? 0 : (starts(a, "extruder") ? atoi(a + 8) : -1);
        for (int i = 0; i < c->n_ext; i++)
        {
            v->nozzles[i].temp = c->temp[i];
            v->nozzles[i].target = c->target[i];
            snprintf(v->nozzles[i].label, sizeof v->nozzles[i].label, "T%d", i);
        }
        if (v->nozzle_active >= 0 && v->nozzle_active < c->n_ext)
        {
            v->nozzle = c->temp[v->nozzle_active];
            v->nozzle_target = c->target[v->nozzle_active];
        }
    }
    v->bed = c->bed;
    v->bed_target = c->bed_target;
    v->chamber = c->chamber[0] ? c->chamber_t : 0;

    /* The MMU's gates or the AFC lanes, four to a card. */
    memset(v->unit, 0, sizeof v->unit);
    for (int u = 0; u * 4 < c->n_lanes && u < PV_MAX_UNITS; u++)
    {
        pv_unit_t *pu = &v->unit[u];
        pu->present = true;
        pu->slots = c->n_lanes - u * 4 >= 4 ? 4 : c->n_lanes - u * 4;
        const char *kind = c->mmu ? "MMU" : "AFC";
        if (c->n_lanes > 4)
        {
            snprintf(pu->label, sizeof pu->label, "%s %d", kind, u + 1);
        }
        else
        {
            snprintf(pu->label, sizeof pu->label, "%s", kind);
        }
        for (int k = 0; k < pu->slots; k++)
        {
            const lane_t *l = &c->lane[u * 4 + k];
            pu->slot[k].present = l->present;
            pu->slot[k].color = l->color;
            pu->slot[k].percent = -1;
            pu->slot[k].selected = l->active;
            strlcpy(pu->slot[k].material, l->material, sizeof pu->slot[k].material);
        }
    }
    xSemaphoreGive(c->lock);
}

const printer_driver_t DRIVER_KLIPPER = {
    .brand = "klipper",
    .support = klip_support,
    .start = klip_start,
    .stop = klip_stop,
    .snapshot = klip_snapshot,
    .control = klip_control,
    .thumb_fetch = moonraker_thumb_fetch,
};
