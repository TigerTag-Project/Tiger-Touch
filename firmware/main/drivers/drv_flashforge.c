/* FlashForge (Adventurer 5M family, Creator 5), over its HTTP interface on
 * port 8898 (reference: Tiger Studio's flashforge/PROTOCOL.md).
 *
 * Seen on a real machine: an AD5X (2026-10-14, 2026-10-18) - state, nozzle and bed,
 * pause, stop and the light; the rest follows Tiger Studio's validated protocol.
 *
 * There is no push: POST /detail with the serial number and the check code in
 * the JSON body answers the whole state, and is repeated every two seconds by
 * a task of its own. The stack of that task is in PSRAM; the internal RAM is
 * only touched for the length of a request. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "addr.h"
#include "cJSON.h"
#include "driver_util.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "http_lite.h"
#include "i18n.h"
#include "flashforge_thumb.h"
#include "printer_driver.h"

static const char *TAG = "flashforge";

#define POLL_MS 2000
#define ONLINE_WINDOW_US (10LL * 1000 * 1000)
#define REPLY_MAX 6144

typedef struct
{
    TaskHandle_t task;
    volatile bool run;
    volatile bool exited;
    SemaphoreHandle_t lock;
    char ip[40], sn[48], code[40];
    char hp[ADDR_HOSTPORT_MAX]; /* host:port, 8898 unless the address names one */

    int64_t last_ok_us;
    int code_err; /* 0 ok, 1 refused (serial or code), -1 no answer */

    char status[16];
    float progress; /* 0..100 */
    int layer, total_layer, left_s;
    char file[48];
    float nozzle, nozzle_target, bed, chamber;
    /* A tool-changer (Creator 5 Pro) reports one reading per nozzle in
     * nozzleTemps[]; a dual-extruder model, leftTemp and rightTemp. */
    int nz_count;
    float nz_temp[4], nz_target[4];
    bool station;
    int current_slot;
    struct
    {
        bool present;
        uint32_t color;
        char material[12];
    } slot[4];
    bool ext_present;
    uint32_t ext_color;
    char ext_material[12];
    int light; /* detail.lightStatus: 1 "open", 0 "close", -1 not said */
} ff_t;

static void apply_detail(ff_t *c, const cJSON *d)
{
    xSemaphoreTake(c->lock, portMAX_DELAY);
    strlcpy(c->status, json_str(d, "status"), sizeof c->status);
    const char *ls = json_str(d, "lightStatus");
    if (ls[0])
    {
        c->light = strcasecmp(ls, "open") == 0 ? 1 : 0;
    }

    const cJSON *pp = cJSON_GetObjectItem(d, "printProgress");
    if (cJSON_IsNumber(pp))
    {
        /* 0..100 on some firmwares, 0..1 on others. */
        c->progress = pp->valuedouble <= 1.0 ? (float)(pp->valuedouble * 100.0) : (float)pp->valuedouble;
    }
    const cJSON *l = cJSON_GetObjectItem(d, "printLayer");
    c->layer = cJSON_IsNumber(l) ? l->valueint : 0;
    const cJSON *tl = cJSON_GetObjectItem(d, "targetPrintLayer");
    c->total_layer = cJSON_IsNumber(tl) ? tl->valueint : 0;
    const cJSON *et = cJSON_GetObjectItem(d, "estimatedTime");
    c->left_s = cJSON_IsNumber(et) ? (int)et->valuedouble : 0;

    const char *fn = json_str(d, "printFileName");
    if (!fn[0])
    {
        fn = json_str(d, "fileName");
    }
    strlcpy(c->file, fn, sizeof c->file);
    char *dot = strrchr(c->file, '.');
    if (dot && (strcasecmp(dot, ".gx") == 0 || strcasecmp(dot, ".gcode") == 0))
    {
        *dot = '\0';
    }

    const cJSON *t;
    t = cJSON_GetObjectItem(d, "rightTemp");
    c->nozzle = cJSON_IsNumber(t) ? (float)t->valuedouble : 0;
    if (c->nozzle <= 0)
    {
        t = cJSON_GetObjectItem(d, "leftTemp");
        c->nozzle = cJSON_IsNumber(t) ? (float)t->valuedouble : 0;
    }
    /* Set points, when the model gives them (the Creator 5 Pro does). */
    c->nozzle_target = 0;
    const cJSON *tg = cJSON_GetObjectItem(d, "rightTargetTemp");
    if (!cJSON_IsNumber(tg))
    {
        tg = cJSON_GetObjectItem(d, "leftTargetTemp");
    }
    if (cJSON_IsNumber(tg))
    {
        c->nozzle_target = (float)tg->valuedouble;
    }

    c->nz_count = 0;
    const cJSON *nts = cJSON_GetObjectItem(d, "nozzleTemps");
    const cJSON *ngs = cJSON_GetObjectItem(d, "nozzleTargetTemps");
    if (cJSON_IsArray(nts) && cJSON_GetArraySize(nts) > 1)
    {
        int n = cJSON_GetArraySize(nts);
        n = n > 4 ? 4 : n;
        for (int i = 0; i < n; i++)
        {
            const cJSON *a = cJSON_GetArrayItem(nts, i);
            const cJSON *g = cJSON_IsArray(ngs) ? cJSON_GetArrayItem(ngs, i) : NULL;
            c->nz_temp[i] = cJSON_IsNumber(a) ? (float)a->valuedouble : 0;
            c->nz_target[i] = cJSON_IsNumber(g) ? (float)g->valuedouble : 0;
        }
        c->nz_count = n;
    }
    else
    {
        const cJSON *cnt = cJSON_GetObjectItem(d, "nozzleCnt");
        const cJSON *lt = cJSON_GetObjectItem(d, "leftTemp");
        const cJSON *rt = cJSON_GetObjectItem(d, "rightTemp");
        if (cJSON_IsNumber(cnt) && cnt->valueint == 2 && cJSON_IsNumber(lt) && cJSON_IsNumber(rt))
        {
            const cJSON *lg = cJSON_GetObjectItem(d, "leftTargetTemp");
            const cJSON *rg = cJSON_GetObjectItem(d, "rightTargetTemp");
            c->nz_temp[0] = (float)lt->valuedouble;
            c->nz_temp[1] = (float)rt->valuedouble;
            c->nz_target[0] = cJSON_IsNumber(lg) ? (float)lg->valuedouble : 0;
            c->nz_target[1] = cJSON_IsNumber(rg) ? (float)rg->valuedouble : 0;
            c->nz_count = 2;
        }
    }

    t = cJSON_GetObjectItem(d, "platTemp");
    c->bed = cJSON_IsNumber(t) ? (float)t->valuedouble : 0;
    t = cJSON_GetObjectItem(d, "chamberTemp");
    c->chamber = cJSON_IsNumber(t) ? (float)t->valuedouble : 0;

    const cJSON *ind = cJSON_GetObjectItem(d, "indepMatlInfo");
    const char *em = cJSON_IsObject(ind) ? json_str(ind, "materialName") : "";
    c->ext_present = em[0] != '\0';
    strlcpy(c->ext_material, em, sizeof c->ext_material);
    c->ext_color = color_from_hex(cJSON_IsObject(ind) ? json_str(ind, "materialColor") : "");

    const cJSON *hs = cJSON_GetObjectItem(d, "hasMatlStation");
    c->station = cJSON_IsTrue(hs) || (cJSON_IsNumber(hs) && hs->valueint != 0);
    const cJSON *ms = cJSON_GetObjectItem(d, "matlStationInfo");
    if (c->station && cJSON_IsObject(ms))
    {
        const cJSON *cur = cJSON_GetObjectItem(ms, "currentSlot");
        c->current_slot = cJSON_IsNumber(cur) ? cur->valueint : 0;
        const cJSON *slots = cJSON_GetObjectItem(ms, "slotInfos");
        const cJSON *s;
        cJSON_ArrayForEach(s, slots)
        {
            const cJSON *id = cJSON_GetObjectItem(s, "slotId");
            const int i = cJSON_IsNumber(id) ? id->valueint - 1 : -1; /* 1-based */
            if (i < 0 || i > 3)
            {
                continue;
            }
            const cJSON *has = cJSON_GetObjectItem(s, "hasFilament");
            c->slot[i].present = cJSON_IsTrue(has) || (cJSON_IsNumber(has) && has->valueint != 0);
            strlcpy(c->slot[i].material, json_str(s, "materialName"), sizeof c->slot[i].material);
            c->slot[i].color = color_from_hex(json_str(s, "materialColor"));
        }
    }
    xSemaphoreGive(c->lock);
}

static void poll_task(void *arg)
{
    ff_t *c = (ff_t *)arg;
    char url[64], body[160];
    snprintf(url, sizeof url, "http://%s/detail", c->hp);
    snprintf(body, sizeof body, "{\"serialNumber\":\"%s\",\"checkCode\":\"%s\"}", c->sn, c->code);
    char *reply = malloc(REPLY_MAX);

    while (c->run && reply)
    {
        const http_lite_req_t req = {.url = url, .method = HTTP_METHOD_POST, .body = body, .timeout_ms = 4000};
        const int st = http_lite(&req, reply, REPLY_MAX);
        int verdict = -1;
        if (st == 200)
        {
            cJSON *root = cJSON_Parse(reply);
            if (root)
            {
                const cJSON *code = cJSON_GetObjectItem(root, "code");
                const cJSON *detail = cJSON_GetObjectItem(root, "detail");
                if (cJSON_IsNumber(code) && code->valueint == 0 && cJSON_IsObject(detail))
                {
                    apply_detail(c, detail);
                    verdict = 0;
                }
                else
                {
                    verdict = 1; /* "SN is different" / "access code is different" */
                }
                cJSON_Delete(root);
            }
        }
        xSemaphoreTake(c->lock, portMAX_DELAY);
        c->code_err = verdict;
        if (verdict == 0)
        {
            c->last_ok_us = esp_timer_get_time();
        }
        xSemaphoreGive(c->lock);
        for (int waited = 0; waited < POLL_MS && c->run; waited += 100)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    free(reply);
    c->exited = true;
    vTaskSuspend(NULL); /* deleted from outside: its stack came from the caps allocator */
}

/* ---- Driver --------------------------------------------------------------- */

static drv_kind_t ff_support(const account_printer_t *p)
{
    return (p->ip[0] && p->sn[0] && !p->cloud) ? DRV_LAN : DRV_NONE;
}

static void *ff_start(const account_printer_t *p)
{
    ff_t *c = calloc(1, sizeof *c);
    if (!c)
    {
        return NULL;
    }
    c->lock = xSemaphoreCreateMutex();
    strlcpy(c->ip, p->ip, sizeof c->ip);
    addr_with_port(c->hp, sizeof c->hp, c->ip, 8898);
    /* The serial travels with its "SN" prefix, whatever the account stores. */
    snprintf(c->sn, sizeof c->sn, "%s%s", strncmp(p->sn, "SN", 2) == 0 ? "" : "SN", p->sn);
    strlcpy(c->code, p->cc, sizeof c->code);
    c->run = true;
    if (xTaskCreatePinnedToCoreWithCaps(poll_task, "ff_poll", 8 * 1024, c, 3, &c->task, 0,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS)
    {
        ESP_LOGW(TAG, "poll task not started");
        vSemaphoreDelete(c->lock);
        free(c);
        return NULL;
    }
    ESP_LOGI(TAG, "polling started");
    return c;
}

static void ff_stop(void *ctx)
{
    ff_t *c = (ff_t *)ctx;
    if (!c)
    {
        return;
    }
    c->run = false;
    for (int i = 0; i < 60 && !c->exited; i++) /* a request in flight: up to its timeout */
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (c->exited)
    {
        vTaskDeleteWithCaps(c->task);
    }
    vSemaphoreDelete(c->lock);
    free(c);
}

/* Pause, resume, stop: POST /control with jobCtl_cmd pause / continue / cancel, the form the recent models use. Written from Tiger Studio's protocol notes,
 * which are validated on the printers. */
static bool ff_control(void *ctx, pv_control_t cmd)
{
    static const char *const ACTION[] = {[PV_CTL_PAUSE] = "pause", [PV_CTL_RESUME] = "continue", [PV_CTL_STOP] = "cancel"};
    ff_t *c = (ff_t *)ctx;
    if (c && (cmd == PV_CTL_LIGHT_ON || cmd == PV_CTL_LIGHT_OFF))
    {
        /* lightControl_cmd, "open" / "close": checked against a slicer capture in
         * Tiger Studio's notes (section 13.6). */
        char lurl[64], lbody[200], lreply[96];
        snprintf(lurl, sizeof lurl, "http://%s/control", c->hp);
        snprintf(lbody, sizeof lbody,
                 "{\"serialNumber\":\"%s\",\"checkCode\":\"%s\",\"payload\":{\"cmd\":\"lightControl_cmd\","
                 "\"args\":{\"status\":\"%s\"}}}",
                 c->sn, c->code, cmd == PV_CTL_LIGHT_ON ? "open" : "close");
        const http_lite_req_t lreq = {.url = lurl, .method = HTTP_METHOD_POST, .body = lbody, .timeout_ms = 4000};
        return http_lite(&lreq, lreply, sizeof lreply) == 200;
    }
    if (!c || (int)cmd < 0 || (int)cmd > PV_CTL_STOP)
    {
        return false;
    }
    char url[64], body[256], reply[96];
    snprintf(url, sizeof url, "http://%s/control", c->hp);
    snprintf(body, sizeof body,
             "{\"serialNumber\":\"%s\",\"checkCode\":\"%s\",\"payload\":{\"cmd\":\"jobCtl_cmd\","
             "\"args\":{\"jobID\":\"\",\"action\":\"%s\"}}}",
             c->sn, c->code, ACTION[cmd]);
    const http_lite_req_t req = {.url = url, .method = HTTP_METHOD_POST, .body = body, .timeout_ms = 4000};
    return http_lite(&req, reply, sizeof reply) == 200;
}

static void ff_snapshot(void *ctx, printer_view_t *v)
{
    ff_t *c = (ff_t *)ctx;
    xSemaphoreTake(c->lock, portMAX_DELAY);
    const int64_t now = esp_timer_get_time();
    v->online = c->last_ok_us != 0 && (now - c->last_ok_us) < ONLINE_WINDOW_US;
    v->can_control = v->online;
    v->can_light = v->online;
    v->light_on = c->light == 1;
    if (c->code_err == 1)
    {
        snprintf(v->notice, sizeof v->notice, "%s", tr(S_ERR_CREDENTIALS));
        v->notice_error = true;
    }

    const char *s = c->status;
    if (!strcmp(s, "printing") || !strcmp(s, "preparing") || !strcmp(s, "prepare") || !strcmp(s, "busy") ||
        !strcmp(s, "heating"))
    {
        v->state = PV_PRINTING;
    }
    else if (!strcmp(s, "paused") || !strcmp(s, "pause") || !strcmp(s, "pausing"))
    {
        v->state = PV_PAUSED;
    }
    else if (!strcmp(s, "complete"))
    {
        v->state = PV_DONE;
    }
    else
    {
        v->state = PV_IDLE;
    }
    int pct = (int)(c->progress + 0.5f);
    v->progress = v->state == PV_IDLE ? 0 : (pct > 100 ? 100 : pct);
    v->layer = c->layer;
    v->total_layer = c->total_layer;
    v->left_s = (v->state == PV_PRINTING || v->state == PV_PAUSED) ? c->left_s : 0;
    v->nozzle = c->nozzle;
    v->nozzle_target = c->nozzle_target;
    v->nozzle_count = 0;
    v->nozzle_active = -1;
    if (c->nz_count >= 2)
    {
        /* The head's current slot, 1-based, is the nozzle in use; a dual-extruder
         * model that heats only one says which by its set point. */
        int active = -1;
        if (c->station && c->current_slot >= 1 && c->current_slot <= c->nz_count)
        {
            active = c->current_slot - 1;
        }
        else
        {
            for (int i = 0; i < c->nz_count; i++)
            {
                if (c->nz_target[i] > 0 && (active < 0 || c->nz_target[i] > c->nz_target[active]))
                {
                    active = i;
                }
            }
        }
        v->nozzle_count = c->nz_count;
        v->nozzle_active = active;
        for (int i = 0; i < c->nz_count; i++)
        {
            v->nozzles[i].temp = c->nz_temp[i];
            v->nozzles[i].target = c->nz_target[i];
            if (c->nz_count == 2 && !c->station)
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
            v->nozzle = c->nz_temp[active];
            v->nozzle_target = c->nz_target[active];
        }
    }
    v->bed = c->bed;
    v->bed_target = 0;
    v->chamber = c->chamber;
    strlcpy(v->file, c->file, sizeof v->file);

    memset(v->unit, 0, sizeof v->unit);
    int n = 0;
    if (c->ext_present)
    {
        pv_unit_t *u = &v->unit[n++];
        u->present = true;
        u->slots = 1;
        snprintf(u->label, sizeof u->label, PV_LABEL_EXT);
        u->slot[0].present = true;
        u->slot[0].color = c->ext_color;
        u->slot[0].percent = -1;
        strlcpy(u->slot[0].material, c->ext_material, sizeof u->slot[0].material);
    }
    if (c->station && n < PV_MAX_UNITS)
    {
        pv_unit_t *u = &v->unit[n++];
        u->present = true;
        u->slots = 4;
        snprintf(u->label, sizeof u->label, "Station");
        for (int i = 0; i < 4; i++)
        {
            u->slot[i].present = c->slot[i].present;
            u->slot[i].color = c->slot[i].color;
            u->slot[i].percent = -1;
            u->slot[i].selected = c->current_slot == i + 1;
            strlcpy(u->slot[i].material, c->slot[i].material, sizeof u->slot[i].material);
        }
    }
    xSemaphoreGive(c->lock);
}

const printer_driver_t DRIVER_FLASHFORGE = {
    .brand = "flashforge",
    .support = ff_support,
    .start = ff_start,
    .stop = ff_stop,
    .snapshot = ff_snapshot,
    .control = ff_control,
    .thumb_fetch = flashforge_thumb_fetch,
};
