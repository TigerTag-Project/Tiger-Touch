/* Prusa, through PrusaLink: HTTP on port 80 with Digest authentication
 * (reference: Tiger Studio's prusa/PROTOCOL.md, itself written against Prusa's
 * published OpenAPI). The reading, pause, resume and stop were seen working on a
 * real Prusa by the owner (2026-10-18).
 *
 * GET /api/v1/status every two seconds gives the state, the temperatures and
 * the job's progress; GET /api/v1/job gives the name of the file, asked again
 * only when the job changes. PrusaLink does not report the loaded filament, so
 * there are no filament cards for this brand.
 *
 * The credentials are the user (maker, unless the account says otherwise) and
 * the password shown by the printer under Settings > Network > PrusaLink. An
 * older account may hold an `apiKey` instead: it is sent as X-Api-Key, with no
 * Digest, as Tiger Studio does. When the printer says something is wrong
 * (status_printer.ok false), its own message is shown. */

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
#include "prusa_thumb.h"
#include "printer_driver.h"

static const char *TAG = "prusa";

#define POLL_MS 2000
#define ONLINE_WINDOW_US (10LL * 1000 * 1000)
#define REPLY_MAX 4096

typedef struct
{
    TaskHandle_t task;
    volatile bool run;
    volatile bool exited;
    SemaphoreHandle_t lock;
    char ip[40], user[24], pass[40], key[48];
    char hp[ADDR_HOSTPORT_MAX]; /* host:port, 80 unless the address names one */
    char message[96]; /* status_printer.message while status_printer.ok is false */

    int64_t last_ok_us;
    int http_err; /* 0 ok, 401/403 refused, -1 no answer, 404 old API */
    int job_id;   /* the job whose file name is known */

    char pstate[16];
    float nozzle, nozzle_target, bed, bed_target, chamber;
    int progress, left_s;
    char file[48];
} prusa_t;

static void poll_task(void *arg)
{
    prusa_t *c = (prusa_t *)arg;
    char url[96];
    char *reply = malloc(REPLY_MAX);

    while (c->run && reply)
    {
        snprintf(url, sizeof url, "http://%s/api/v1/status", c->hp);
        http_lite_req_t req = {.url = url, .method = HTTP_METHOD_GET, .user = c->key[0] ? NULL : c->user,
                               .pass = c->pass, .api_key = c->key, .timeout_ms = 4000};
        int st = http_lite(&req, reply, REPLY_MAX);
        int want_job = 0;
        xSemaphoreTake(c->lock, portMAX_DELAY);
        c->http_err = st == 200 ? 0 : st;
        xSemaphoreGive(c->lock);

        if (st == 200)
        {
            cJSON *root = cJSON_Parse(reply);
            if (root)
            {
                xSemaphoreTake(c->lock, portMAX_DELAY);
                const cJSON *pr = cJSON_GetObjectItem(root, "printer");
                if (cJSON_IsObject(pr))
                {
                    const cJSON *s = cJSON_GetObjectItem(pr, "state");
                    if (cJSON_IsString(s))
                    {
                        strlcpy(c->pstate, s->valuestring, sizeof c->pstate);
                    }
                    json_get_f(pr, "temp_nozzle", &c->nozzle);
                    json_get_f(pr, "target_nozzle", &c->nozzle_target);
                    json_get_f(pr, "temp_bed", &c->bed);
                    json_get_f(pr, "target_bed", &c->bed_target);
                    json_get_f(pr, "temp_chamber", &c->chamber);
                    const cJSON *sp = cJSON_GetObjectItem(pr, "status_printer");
                    const bool bad = cJSON_IsFalse(cJSON_GetObjectItem(sp, "ok"));
                    strlcpy(c->message, bad ? json_str(sp, "message") : "", sizeof c->message);
                }
                const cJSON *job = cJSON_GetObjectItem(root, "job");
                if (cJSON_IsObject(job))
                {
                    const cJSON *p = cJSON_GetObjectItem(job, "progress");
                    c->progress = cJSON_IsNumber(p) ? (int)(p->valuedouble + 0.5) : 0;
                    const cJSON *tr_ = cJSON_GetObjectItem(job, "time_remaining");
                    c->left_s = cJSON_IsNumber(tr_) ? tr_->valueint : 0;
                    const cJSON *id = cJSON_GetObjectItem(job, "id");
                    if (cJSON_IsNumber(id) && id->valueint != c->job_id)
                    {
                        want_job = id->valueint;
                    }
                }
                else if (c->job_id != 0)
                {
                    c->job_id = 0; /* no job any more */
                    c->file[0] = '\0';
                }
                c->last_ok_us = esp_timer_get_time();
                xSemaphoreGive(c->lock);
                cJSON_Delete(root);
            }
        }

        if (want_job)
        {
            snprintf(url, sizeof url, "http://%s/api/v1/job", c->hp);
            req.url = url;
            if (http_lite(&req, reply, REPLY_MAX) == 200)
            {
                cJSON *root = cJSON_Parse(reply);
                if (root)
                {
                    const cJSON *f = cJSON_GetObjectItem(root, "file");
                    const char *name = NULL;
                    if (cJSON_IsObject(f))
                    {
                        const cJSON *dn = cJSON_GetObjectItem(f, "display_name");
                        const cJSON *nm = cJSON_GetObjectItem(f, "name");
                        name = cJSON_IsString(dn) ? dn->valuestring : (cJSON_IsString(nm) ? nm->valuestring : NULL);
                    }
                    xSemaphoreTake(c->lock, portMAX_DELAY);
                    if (name)
                    {
                        strlcpy(c->file, name, sizeof c->file);
                        char *dot = strrchr(c->file, '.');
                        if (dot && (strcasecmp(dot, ".gcode") == 0 || strcasecmp(dot, ".bgcode") == 0))
                        {
                            *dot = '\0';
                        }
                    }
                    c->job_id = want_job;
                    xSemaphoreGive(c->lock);
                    cJSON_Delete(root);
                }
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

static drv_kind_t prusa_support(const account_printer_t *p)
{
    return (p->ip[0] && !p->cloud) ? DRV_LAN : DRV_NONE;
}

static void *prusa_start(const account_printer_t *p)
{
    prusa_t *c = calloc(1, sizeof *c);
    if (!c)
    {
        return NULL;
    }
    c->lock = xSemaphoreCreateMutex();
    strlcpy(c->ip, p->ip, sizeof c->ip);
    addr_with_port(c->hp, sizeof c->hp, c->ip, 80);
    strlcpy(c->user, p->user[0] ? p->user : "maker", sizeof c->user);
    strlcpy(c->pass, p->cc, sizeof c->pass);
    strlcpy(c->key, p->key, sizeof c->key);
    c->run = true;
    if (xTaskCreatePinnedToCoreWithCaps(poll_task, "prusa_poll", 8 * 1024, c, 3, &c->task, 0,
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

static void prusa_stop(void *ctx)
{
    prusa_t *c = (prusa_t *)ctx;
    if (!c)
    {
        return;
    }
    c->run = false;
    for (int i = 0; i < 60 && !c->exited; i++)
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

/* Pause, resume, stop: PrusaLink PUT /api/v1/job/<id>/pause and /resume, DELETE /api/v1/job/<id>, with Digest authentication. Written from Tiger Studio's protocol notes;
 * the three were seen working on a real printer by the owner (2026-10-18). */
static bool prusa_control(void *ctx, pv_control_t cmd)
{
    prusa_t *c = (prusa_t *)ctx;
    if (!c || c->job_id <= 0 || (int)cmd < 0 || (int)cmd > PV_CTL_STOP)
    {
        return false;
    }
    char url[112], reply[64];
    esp_http_client_method_t method = HTTP_METHOD_PUT;
    const char *tail = cmd == PV_CTL_PAUSE ? "/pause" : "/resume";
    if (cmd == PV_CTL_STOP)
    {
        method = HTTP_METHOD_DELETE;
        tail = "";
    }
    snprintf(url, sizeof url, "http://%s/api/v1/job/%d%s", c->hp, c->job_id, tail);
    const http_lite_req_t req = {.url = url, .method = method, .user = c->key[0] ? NULL : c->user, .pass = c->pass,
                                 .api_key = c->key, .timeout_ms = 5000};
    const int st = http_lite(&req, reply, sizeof reply);
    return st >= 200 && st < 300;
}

static void prusa_snapshot(void *ctx, printer_view_t *v)
{
    prusa_t *c = (prusa_t *)ctx;
    xSemaphoreTake(c->lock, portMAX_DELAY);
    const int64_t now = esp_timer_get_time();
    v->online = c->last_ok_us != 0 && (now - c->last_ok_us) < ONLINE_WINDOW_US;
    v->can_control = v->online;
    if (c->http_err == 401 || c->http_err == 403)
    {
        snprintf(v->notice, sizeof v->notice, "%s", tr(c->key[0] ? S_ERR_API_KEY : S_ERR_CREDENTIALS));
        v->notice_error = true;
    }
    else if (c->message[0])
    {
        snprintf(v->notice, sizeof v->notice, "%s", c->message); /* the printer's own words */
        v->notice_error = true;
    }

    const char *s = c->pstate;
    if (!strcmp(s, "PRINTING") || !strcmp(s, "BUSY") || !strcmp(s, "ATTENTION"))
    {
        v->state = PV_PRINTING;
    }
    else if (!strcmp(s, "PAUSED"))
    {
        v->state = PV_PAUSED;
    }
    else if (!strcmp(s, "FINISHED"))
    {
        v->state = PV_DONE;
    }
    else if (!strcmp(s, "ERROR"))
    {
        v->state = PV_ERROR;
    }
    else
    {
        v->state = PV_IDLE; /* IDLE, READY, STOPPED */
    }
    v->progress = v->state == PV_IDLE ? 0 : (c->progress > 100 ? 100 : c->progress);
    v->layer = 0;
    v->total_layer = 0;
    v->left_s = (v->state == PV_PRINTING || v->state == PV_PAUSED) ? c->left_s : 0;
    v->nozzle = c->nozzle;
    v->nozzle_target = c->nozzle_target;
    v->bed = c->bed;
    v->bed_target = c->bed_target;
    v->chamber = c->chamber;
    strlcpy(v->file, c->file, sizeof v->file);
    memset(v->unit, 0, sizeof v->unit); /* PrusaLink does not report the filament */
    xSemaphoreGive(c->lock);
}

const printer_driver_t DRIVER_PRUSA = {
    .brand = "prusa",
    .support = prusa_support,
    .start = prusa_start,
    .stop = prusa_stop,
    .snapshot = prusa_snapshot,
    .control = prusa_control,
    .thumb_fetch = prusa_thumb_fetch,
};
