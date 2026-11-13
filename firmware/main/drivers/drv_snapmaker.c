/* Snapmaker, through Moonraker: a WebSocket on port 7125 carrying JSON-RPC 2.0,
 * no authentication (reference: Tiger Studio's snapmaker/PROTOCOL.md).
 *
 * Seen on a real machine: a U1, idle (2026-10-14) - state, the four nozzles with
 * the active one, bed. Pause, resume, stop and the light follow Tiger Studio's
 * validated protocol.
 *
 * The connection is opened, the printer is asked to subscribe us to the few
 * objects the dashboard shows, and from then on it pushes the changes. The
 * state is kept merged here, field by field, because a push carries only what
 * moved.
 *
 * No Origin header is sent: Moonraker refuses a handshake whose origin is not
 * in its cors_domains, and esp_websocket_client sends none by default. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "addr.h"
#include "cJSON.h"
#include "driver_util.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "ws_rx.h"
#include "i18n.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "moonraker_thumb.h"
#include "printer_driver.h"

static const char *TAG = "snapmaker";

#define MAX_FRAME (48 * 1024)
#define SLOTS 4

typedef struct
{
    esp_websocket_client_handle_t ws;
    SemaphoreHandle_t lock;
    char ip[40];

    ws_rx_t rx; /* reassembly of a frame handed over in chunks */

    bool connected;
    bool got;

    char pstate[16];
    char file[48];
    float p_display, p_vsd; /* 0..1, -1 when not reported */
    float duration;         /* print_duration, seconds */
    int layer, total_layer;
    float temp[SLOTS], target[SLOTS];
    bool have_ext[SLOTS]; /* the printer has this extruder (it was reported) */
    int tool;             /* toolhead.extruder: the one at work, -1 when not said */
    float bed, bed_target;
    bool have_led; /* the cavity LED was reported: the printer has one to switch */
    bool led_on;
    struct
    {
        bool present;
        uint32_t color;
        char material[12];
    } slot[SLOTS];
} snap_t;

/* ---- JSON helpers --------------------------------------------------------- */

/* ---- State merge ---------------------------------------------------------- */

static void apply_config(snap_t *c, const cJSON *cfg)
{
    const cJSON *col = cJSON_GetObjectItem(cfg, "filament_color_rgba");
    const cJSON *typ = cJSON_GetObjectItem(cfg, "filament_type");
    const cJSON *sub = cJSON_GetObjectItem(cfg, "filament_sub_type");
    if (!cJSON_IsArray(typ) && !cJSON_IsArray(col))
    {
        return;
    }
    for (int i = 0; i < SLOTS; i++)
    {
        const char *t = json_str_at(typ, i);
        const char *st = json_str_at(sub, i);
        c->slot[i].present = t[0] != '\0';
        /* The family and the variant, the way the printer's own screen writes
         * them: "PLA Silk", not "PLA". Cut to what a slot label can hold. */
        if (st[0])
        {
            snprintf(c->slot[i].material, sizeof c->slot[i].material, "%s %s", t, st);
        }
        else
        {
            strlcpy(c->slot[i].material, t, sizeof c->slot[i].material);
        }
        c->slot[i].color = color_from_hex(json_str_at(col, i));
    }
}

static void apply_status(snap_t *c, const cJSON *st)
{
    xSemaphoreTake(c->lock, portMAX_DELAY);

    /* The cavity LED: Klipper's `led` object, color_data [[R,G,B,W]] in 0..1. */
    const cJSON *led = cJSON_GetObjectItem(st, "led cavity_led");
    if (cJSON_IsObject(led))
    {
        const cJSON *px = cJSON_GetArrayItem(cJSON_GetObjectItem(led, "color_data"), 0);
        if (cJSON_IsArray(px))
        {
            bool any = false;
            const cJSON *ch;
            cJSON_ArrayForEach(ch, px)
            {
                any = any || (cJSON_IsNumber(ch) && ch->valuedouble > 0.0);
            }
            c->have_led = true;
            c->led_on = any;
        }
    }

    const cJSON *ps = cJSON_GetObjectItem(st, "print_stats");
    if (cJSON_IsObject(ps))
    {
        const cJSON *s = cJSON_GetObjectItem(ps, "state");
        if (cJSON_IsString(s))
        {
            strlcpy(c->pstate, s->valuestring, sizeof c->pstate);
        }
        const cJSON *fn = cJSON_GetObjectItem(ps, "filename");
        if (cJSON_IsString(fn))
        {
            const char *name = fn->valuestring;
            const char *slash = strrchr(name, '/');
            strlcpy(c->file, slash ? slash + 1 : name, sizeof c->file);
            char *dot = strrchr(c->file, '.');
            if (dot && strcasecmp(dot, ".gcode") == 0)
            {
                *dot = '\0';
            }
        }
        json_get_f(ps, "print_duration", &c->duration);
        const cJSON *info = cJSON_GetObjectItem(ps, "info");
        if (cJSON_IsObject(info))
        {
            json_get_i(info, "current_layer", &c->layer);
            json_get_i(info, "total_layer", &c->total_layer);
        }
    }

    const cJSON *vs = cJSON_GetObjectItem(st, "virtual_sdcard");
    if (cJSON_IsObject(vs))
    {
        json_get_f(vs, "progress", &c->p_vsd);
    }
    const cJSON *ds = cJSON_GetObjectItem(st, "display_status");
    if (cJSON_IsObject(ds))
    {
        json_get_f(ds, "progress", &c->p_display);
    }

    static const char *const HEATERS[SLOTS] = {"extruder", "extruder1", "extruder2", "extruder3"};
    for (int i = 0; i < SLOTS; i++)
    {
        const cJSON *h = cJSON_GetObjectItem(st, HEATERS[i]);
        if (cJSON_IsObject(h))
        {
            c->have_ext[i] = true;
            json_get_f(h, "temperature", &c->temp[i]);
            json_get_f(h, "target", &c->target[i]);
        }
    }
    /* Klipper names the extruder in use: "extruder", "extruder1"... */
    const cJSON *th = cJSON_GetObjectItem(st, "toolhead");
    if (cJSON_IsObject(th))
    {
        const cJSON *ex = cJSON_GetObjectItem(th, "extruder");
        if (cJSON_IsString(ex))
        {
            const char *name = ex->valuestring;
            if (strcmp(name, "extruder") == 0)
            {
                c->tool = 0;
            }
            else if (strncmp(name, "extruder", 8) == 0 && name[8] >= '1' && name[8] < '0' + SLOTS)
            {
                c->tool = name[8] - '0';
            }
        }
    }
    const cJSON *bed = cJSON_GetObjectItem(st, "heater_bed");
    if (cJSON_IsObject(bed))
    {
        json_get_f(bed, "temperature", &c->bed);
        json_get_f(bed, "target", &c->bed_target);
    }

    const cJSON *cfg = cJSON_GetObjectItem(st, "print_task_config");
    if (cJSON_IsObject(cfg))
    {
        apply_config(c, cfg);
    }
    c->got = true;
    xSemaphoreGive(c->lock);
}

static void handle_frame(snap_t *c, const char *data, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root)
    {
        return;
    }
    /* An answer to the subscribe / query: result.status. */
    const cJSON *res = cJSON_GetObjectItem(root, "result");
    const cJSON *st = cJSON_IsObject(res) ? cJSON_GetObjectItem(res, "status") : NULL;
    if (cJSON_IsObject(st))
    {
        apply_status(c, st);
    }
    /* A push: {"method":"notify_status_update","params":[{...}, eventtime]}. */
    const cJSON *m = cJSON_GetObjectItem(root, "method");
    if (cJSON_IsString(m) && (strcmp(m->valuestring, "notify_status_update") == 0 ||
                              strcmp(m->valuestring, "notify_status_changed") == 0))
    {
        const cJSON *p0 = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "params"), 0);
        if (cJSON_IsObject(p0))
        {
            apply_status(c, p0);
        }
    }
    cJSON_Delete(root);
}

/* ---- WebSocket ------------------------------------------------------------ */

static const char SUBSCRIBE[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"printer.objects.subscribe\",\"params\":{\"objects\":{"
    "\"print_task_config\":null,\"print_stats\":null,\"virtual_sdcard\":null,\"display_status\":null,"
    "\"extruder\":[\"temperature\",\"target\"],\"extruder1\":[\"temperature\",\"target\"],"
    "\"extruder2\":[\"temperature\",\"target\"],\"extruder3\":[\"temperature\",\"target\"],"
    "\"toolhead\":[\"extruder\"],\"led cavity_led\":[\"color_data\"],"
    "\"heater_bed\":[\"temperature\",\"target\"]}},\"id\":1}";

static void on_ws(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)base;
    snap_t *c = (snap_t *)arg;
    esp_websocket_event_data_t *d = (esp_websocket_event_data_t *)event_data;

    switch (id)
    {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected");
        xSemaphoreTake(c->lock, portMAX_DELAY);
        c->connected = true;
        xSemaphoreGive(c->lock);
        /* The answer carries the full current state; pushes follow. */
        esp_websocket_client_send_text(c->ws, SUBSCRIBE, sizeof SUBSCRIBE - 1, pdMS_TO_TICKS(1000));
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        ESP_LOGW(TAG, "disconnected");
        xSemaphoreTake(c->lock, portMAX_DELAY);
        c->connected = false;
        c->got = false;
        xSemaphoreGive(c->lock);
        break;

    case WEBSOCKET_EVENT_DATA:
    {
        const char *text;
        size_t len;
        if (ws_rx_feed(&c->rx, d, MAX_FRAME, &text, &len))
        {
            handle_frame(c, text, len);
        }
        break;
    }

    default:
        break;
    }
}

/* ---- Driver --------------------------------------------------------------- */

static drv_kind_t snap_support(const account_printer_t *p)
{
    return (p->ip[0] && !p->cloud) ? DRV_LAN : DRV_NONE;
}

static void *snap_start(const account_printer_t *p)
{
    snap_t *c = calloc(1, sizeof *c);
    if (!c)
    {
        return NULL;
    }
    c->lock = xSemaphoreCreateMutex();
    c->p_display = c->p_vsd = -1.0f;
    c->tool = -1;
    strlcpy(c->ip, p->ip, sizeof c->ip);

    char uri[80], hp[ADDR_HOSTPORT_MAX];
    addr_with_port(hp, sizeof hp, p->ip, 7125);
    snprintf(uri, sizeof uri, "ws://%s/websocket", hp);
    esp_websocket_client_config_t cfg = {
        .uri = uri,
        .buffer_size = 2048,
        .reconnect_timeout_ms = 10000,
        .network_timeout_ms = 8000,
        /* 4 KB: measured 2.2 KB used at worst on the bench (docs/CONNECTION-BUDGET.md);
         * the stack is in internal RAM, which is what printers are budgeted in. */
        .task_stack = 4 * 1024,
        /* Core 0, with the rest of the network: core 1 draws the screen. */
        .task_core_id_set = true,
        .task_core_id = 0,
        .ping_interval_sec = 5,
        .pingpong_timeout_sec = 10,
    };
    c->ws = esp_websocket_client_init(&cfg);
    if (!c->ws)
    {
        vSemaphoreDelete(c->lock);
        free(c);
        return NULL;
    }
    esp_websocket_register_events(c->ws, WEBSOCKET_EVENT_ANY, on_ws, c);
    esp_websocket_client_start(c->ws);
    ESP_LOGI(TAG, "connecting to the printer");
    return c;
}

static void snap_stop(void *ctx)
{
    snap_t *c = (snap_t *)ctx;
    if (!c)
    {
        return;
    }
    esp_websocket_client_stop(c->ws);
    esp_websocket_client_destroy(c->ws);
    vSemaphoreDelete(c->lock);
    ws_rx_free(&c->rx);
    free(c);
}

/* Pause, resume, stop: Moonraker's printer.print.pause / resume / cancel over the socket already open. Written from Tiger Studio's protocol notes,
 * which are validated on the printers. */
static bool snap_control(void *ctx, pv_control_t cmd)
{
    static const char *const BODY[] = {
        [PV_CTL_PAUSE] = "{\"jsonrpc\":\"2.0\",\"method\":\"printer.print.pause\",\"params\":{},\"id\":300}",
        [PV_CTL_RESUME] = "{\"jsonrpc\":\"2.0\",\"method\":\"printer.print.resume\",\"params\":{},\"id\":301}",
        [PV_CTL_STOP] = "{\"jsonrpc\":\"2.0\",\"method\":\"printer.print.cancel\",\"params\":{},\"id\":302}",
    };
    snap_t *c = (snap_t *)ctx;
    if (c && c->ws && c->connected && (cmd == PV_CTL_LIGHT_ON || cmd == PV_CTL_LIGHT_OFF))
    {
        /* SET_LED on the cavity LED, as Tiger Studio does it. */
        static const char *const LIGHT[] = {
            "{\"jsonrpc\":\"2.0\",\"method\":\"printer.gcode.script\",\"params\":{\"script\":"
            "\"SET_LED LED=cavity_led RED=1 GREEN=1 BLUE=1 WHITE=1 SYNC=0\"},\"id\":303}",
            "{\"jsonrpc\":\"2.0\",\"method\":\"printer.gcode.script\",\"params\":{\"script\":"
            "\"SET_LED LED=cavity_led RED=0 GREEN=0 BLUE=0 WHITE=0 SYNC=0\"},\"id\":304}",
        };
        const char *body = LIGHT[cmd == PV_CTL_LIGHT_ON ? 0 : 1];
        return esp_websocket_client_send_text(c->ws, body, (int)strlen(body), pdMS_TO_TICKS(1000)) >= 0;
    }
    if (!c || !c->ws || !c->connected || (int)cmd < 0 || (int)cmd > PV_CTL_STOP)
    {
        return false;
    }
    return esp_websocket_client_send_text(c->ws, BODY[cmd], (int)strlen(BODY[cmd]), pdMS_TO_TICKS(1000)) >= 0;
}

static void snap_snapshot(void *ctx, printer_view_t *v)
{
    snap_t *c = (snap_t *)ctx;
    xSemaphoreTake(c->lock, portMAX_DELAY);
    v->online = c->connected && c->got;
    v->can_control = v->online;
    v->can_light = v->online && c->have_led;
    v->light_on = c->led_on;

    const char *s = c->pstate;
    if (!strcmp(s, "printing") || !strcmp(s, "busy") || !strcmp(s, "heating"))
    {
        v->state = PV_PRINTING;
    }
    else if (!strcmp(s, "paused"))
    {
        v->state = PV_PAUSED;
    }
    else if (!strcmp(s, "complete"))
    {
        v->state = PV_DONE;
    }
    else if (!strcmp(s, "error"))
    {
        v->state = PV_ERROR;
    }
    else
    {
        v->state = PV_IDLE;
    }

    /* display_status first, then the file position; a value above 1 is a
     * percentage already. */
    float p = c->p_display >= 0 ? c->p_display : (c->p_vsd >= 0 ? c->p_vsd : 0);
    if (p > 1.0f)
    {
        p /= 100.0f;
    }
    int pct = (int)(p * 100.0f + 0.5f);
    v->progress = v->state == PV_IDLE ? 0 : (pct > 100 ? 100 : pct);
    v->layer = c->layer;
    v->total_layer = c->total_layer;
    /* Moonraker reports no estimate in this feed: the time left is deduced
     * from the time spent and the share done. */
    v->left_s = 0;
    if ((v->state == PV_PRINTING || v->state == PV_PAUSED) && p > 0.01f && c->duration > 0)
    {
        v->left_s = (int)(c->duration * (1.0f - p) / p);
    }

    /* The hottest extruder is the one working. */
    int e = 0;
    for (int i = 1; i < SLOTS; i++)
    {
        if (c->target[i] > c->target[e])
        {
            e = i;
        }
    }
    v->nozzle = c->temp[e];
    v->nozzle_target = c->target[e];

    /* More than one extruder reported (a U1 has four): one tile each, the one
     * at work ringed. Klipper's toolhead says which; failing that, the one
     * that is being heated. */
    int count = 0;
    for (int i = 0; i < SLOTS; i++)
    {
        if (c->have_ext[i])
        {
            count = i + 1;
        }
    }
    int active = c->tool;
    if (active < 0 || active >= count)
    {
        active = c->target[e] > 0 ? e : -1;
    }
    v->nozzle_count = 0;
    v->nozzle_active = -1;
    if (count >= 2)
    {
        v->nozzle_count = count;
        v->nozzle_active = active;
        for (int i = 0; i < count; i++)
        {
            v->nozzles[i].temp = c->temp[i];
            v->nozzles[i].target = c->target[i];
            if (count == 2)
            {
                strlcpy(v->nozzles[i].label, tr(i == 0 ? S_NOZZLE_LEFT : S_NOZZLE_RIGHT),
                        sizeof v->nozzles[i].label);
            }
            else
            {
                snprintf(v->nozzles[i].label, sizeof v->nozzles[i].label, tr(S_NOZZLE_N), i + 1);
            }
        }
        if (active >= 0)
        {
            v->nozzle = c->temp[active];
            v->nozzle_target = c->target[active];
        }
    }
    v->bed = c->bed;
    v->bed_target = c->bed_target;
    v->chamber = 0;
    strlcpy(v->file, c->file, sizeof v->file);

    memset(v->unit, 0, sizeof v->unit);
    bool any = false;
    for (int i = 0; i < SLOTS; i++)
    {
        any |= c->slot[i].present;
    }
    if (any)
    {
        pv_unit_t *u = &v->unit[0];
        u->present = true;
        u->slots = SLOTS;
        snprintf(u->label, sizeof u->label, "E1-E4");
        for (int i = 0; i < SLOTS; i++)
        {
            u->slot[i].present = c->slot[i].present;
            u->slot[i].color = c->slot[i].color;
            u->slot[i].percent = -1;
            /* On a tool-changer each head carries its own spool: the active head's
             * is the one in use. */
            u->slot[i].selected = v->nozzle_count >= 2 && i == v->nozzle_active;
            strlcpy(u->slot[i].material, c->slot[i].material, sizeof u->slot[i].material);
        }
    }
    xSemaphoreGive(c->lock);
}

const printer_driver_t DRIVER_SNAPMAKER = {
    .brand = "snapmaker",
    .support = snap_support,
    .start = snap_start,
    .stop = snap_stop,
    .snapshot = snap_snapshot,
    .control = snap_control,
    .thumb_fetch = moonraker_thumb_fetch,
};
