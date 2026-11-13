/* OctoPrint (a Marlin printer behind it): its REST API over HTTP, port 80 on
 * an OctoPi (or the port written in the address), with the API key the account
 * holds - Tiger Studio's OctoPrint plugin, read in its source
 * (renderer/printers/octoprint, core.js and PROTOCOL.md).
 *
 * Every two seconds: GET /api/connection (is the printer plugged into
 * OctoPrint?), /api/printer (state flags and temperatures; 409 when it is not)
 * and /api/job (file, progress, times). OctoPrint reports no layers, no light
 * and no filament, so this brand has none of them.
 *
 * Written from Studio's plugin, itself marked beta; this driver has not been
 * run against a real printer. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "octoprint_thumb.h"
#include "printer_driver.h"

static const char *TAG = "octoprint";

#define POLL_MS 2000
#define ONLINE_WINDOW_US (10LL * 1000 * 1000)
#define REPLY_MAX (8 * 1024)

typedef struct
{
    TaskHandle_t task;
    volatile bool run;
    volatile bool exited;
    SemaphoreHandle_t lock;
    char base[64];
    char key[48];

    int64_t last_ok_us;
    int http_err;   /* 0 ok, 401/403 refused, -1 no answer */
    bool link_up;   /* the printer is connected to OctoPrint */
    bool error, cancelling, paused, printing;
    char state_text[64]; /* printer.state.text: shown on an error */
    float completion; /* 0..100 */
    int left_s;
    char file[64];
    int n_tools;
    float temp[4], target[4];
    float bed, bed_target, chamber;
} octo_t;

static int request(octo_t *c, esp_http_client_method_t method, const char *path, const char *body, char *out,
                   size_t cap)
{
    char url[160];
    snprintf(url, sizeof url, "%s%s", c->base, path);
    const http_lite_req_t req = {
        .url = url, .method = method, .body = body, .api_key = c->key, .timeout_ms = 6000};
    return http_lite(&req, out, cap);
}

static bool is_flag(const cJSON *flags, const char *name)
{
    return cJSON_IsTrue(cJSON_GetObjectItem(flags, name));
}

static void read_printer(octo_t *c, const cJSON *root)
{
    const cJSON *state = cJSON_GetObjectItem(root, "state");
    const cJSON *flags = cJSON_GetObjectItem(state, "flags");
    strlcpy(c->state_text, json_str(state, "text"), sizeof c->state_text);
    c->error = is_flag(flags, "error");
    c->cancelling = is_flag(flags, "cancelling");
    c->paused = is_flag(flags, "paused") || is_flag(flags, "pausing");
    c->printing = is_flag(flags, "printing");

    const cJSON *t = cJSON_GetObjectItem(root, "temperature");
    c->n_tools = 0;
    for (int i = 0; i < 4; i++)
    {
        char name[8];
        snprintf(name, sizeof name, "tool%d", i);
        const cJSON *tool = cJSON_GetObjectItem(t, name);
        if (!cJSON_IsObject(tool))
        {
            break;
        }
        json_get_f(tool, "actual", &c->temp[i]);
        json_get_f(tool, "target", &c->target[i]);
        c->n_tools = i + 1;
    }
    const cJSON *bed = cJSON_GetObjectItem(t, "bed");
    json_get_f(bed, "actual", &c->bed);
    json_get_f(bed, "target", &c->bed_target);
    json_get_f(cJSON_GetObjectItem(t, "chamber"), "actual", &c->chamber);
}

static void read_job(octo_t *c, const cJSON *root)
{
    const cJSON *prog = cJSON_GetObjectItem(root, "progress");
    const cJSON *job = cJSON_GetObjectItem(root, "job");
    float completion = 0, elapsed = 0, left = -1, est = 0;
    json_get_f(prog, "completion", &completion);
    json_get_f(prog, "printTime", &elapsed);
    json_get_f(prog, "printTimeLeft", &left);
    json_get_f(job, "estimatedPrintTime", &est);
    c->completion = completion < 0 ? 0 : (completion > 100 ? 100 : completion);
    /* OctoPrint's own estimate, else the file's less the time printed, else
     * extrapolated from the progress - Studio's order. */
    if (left >= 0)
    {
        c->left_s = (int)left;
    }
    else if (est > elapsed)
    {
        c->left_s = (int)(est - elapsed);
    }
    else if (c->completion > 0 && c->completion < 100)
    {
        c->left_s = (int)(elapsed * (100 - c->completion) / c->completion);
    }
    else
    {
        c->left_s = 0;
    }
    const cJSON *file = cJSON_GetObjectItem(job, "file");
    const char *path = json_str(file, "path");
    const char *name = path[0] ? path : json_str(file, "name");
    const char *slash = strrchr(name, '/');
    strlcpy(c->file, slash ? slash + 1 : name, sizeof c->file);
    char *dot = strrchr(c->file, '.');
    if (dot && (strcasecmp(dot, ".gcode") == 0 || strcasecmp(dot, ".gco") == 0))
    {
        *dot = '\0';
    }
}

static void poll_task(void *arg)
{
    octo_t *c = (octo_t *)arg;
    char *reply = heap_caps_malloc(REPLY_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    while (c->run && reply)
    {
        /* The serial link first: OctoPrint answers even with no printer on it. */
        int st = request(c, HTTP_METHOD_GET, "/api/connection", NULL, reply, REPLY_MAX);
        bool link = false;
        if (st == 200)
        {
            cJSON *root = cJSON_Parse(reply);
            const char *s = json_str(cJSON_GetObjectItem(root, "current"), "state");
            static const char *const DOWN[] = {"closed", "offline", "error", "detecting", "connecting", "opening"};
            link = s[0] != '\0';
            for (size_t i = 0; i < sizeof DOWN / sizeof DOWN[0]; i++)
            {
                link = link && strncasecmp(s, DOWN[i], strlen(DOWN[i])) != 0;
            }
            cJSON_Delete(root);
        }
        int pst = st == 200 ? request(c, HTTP_METHOD_GET, "/api/printer", NULL, reply, REPLY_MAX) : st;
        cJSON *pr = pst == 200 ? cJSON_Parse(reply) : NULL;
        xSemaphoreTake(c->lock, portMAX_DELAY);
        c->http_err = st == 200 ? 0 : st;
        if (st == 200)
        {
            c->last_ok_us = esp_timer_get_time();
        }
        c->link_up = link && pst != 409; /* 409: no printer connected */
        if (pr)
        {
            read_printer(c, pr);
        }
        xSemaphoreGive(c->lock);
        cJSON_Delete(pr);

        if (st == 200 && request(c, HTTP_METHOD_GET, "/api/job", NULL, reply, REPLY_MAX) == 200)
        {
            cJSON *job = cJSON_Parse(reply);
            if (job)
            {
                xSemaphoreTake(c->lock, portMAX_DELAY);
                read_job(c, job);
                xSemaphoreGive(c->lock);
                cJSON_Delete(job);
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

static drv_kind_t octo_support(const account_printer_t *p)
{
    return (p->ip[0] && !p->cloud) ? DRV_LAN : DRV_NONE;
}

static void *octo_start(const account_printer_t *p)
{
    octo_t *c = heap_caps_calloc(1, sizeof *c, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!c)
    {
        return NULL;
    }
    c->lock = xSemaphoreCreateMutex();
    snprintf(c->base, sizeof c->base, "http://%s", p->ip); /* "host" or "host:port"; port 80 when none */
    strlcpy(c->key, p->key, sizeof c->key);
    if (!c->key[0])
    {
        ESP_LOGW(TAG, "an OctoPrint printer has no apiKey in the account");
    }
    c->run = true;
    if (xTaskCreatePinnedToCoreWithCaps(poll_task, "octo_poll", 8 * 1024, c, 3, &c->task, 0,
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

static void octo_stop(void *ctx)
{
    octo_t *c = (octo_t *)ctx;
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
    free(c);
}

/* Pause, resume, cancel: POST /api/job, as Studio sends them. No light: OctoPrint
 * has no light command. Not run against a real printer. */
static bool octo_control(void *ctx, pv_control_t cmd)
{
    octo_t *c = (octo_t *)ctx;
    static const char *const BODY[] = {[PV_CTL_PAUSE] = "{\"command\":\"pause\",\"action\":\"pause\"}",
                                       [PV_CTL_RESUME] = "{\"command\":\"pause\",\"action\":\"resume\"}",
                                       [PV_CTL_STOP] = "{\"command\":\"cancel\"}"};
    if (!c || (int)cmd < 0 || (int)cmd > PV_CTL_STOP)
    {
        return false;
    }
    char reply[96];
    const int st = request(c, HTTP_METHOD_POST, "/api/job", BODY[cmd], reply, sizeof reply);
    return st >= 200 && st < 300;
}

static void octo_snapshot(void *ctx, printer_view_t *v)
{
    octo_t *c = (octo_t *)ctx;
    xSemaphoreTake(c->lock, portMAX_DELAY);
    const int64_t now = esp_timer_get_time();
    const bool reached = c->last_ok_us != 0 && (now - c->last_ok_us) < ONLINE_WINDOW_US;
    v->online = reached && c->link_up;
    if (!c->key[0] || c->http_err == 401 || c->http_err == 403)
    {
        snprintf(v->notice, sizeof v->notice, "%s", tr(S_ERR_API_KEY));
        v->notice_error = true;
    }
    else if (reached && !c->link_up)
    {
        snprintf(v->notice, sizeof v->notice, "%s", tr(S_OCTO_NO_PRINTER));
        v->notice_error = true;
    }
    else if (v->online && c->error && c->state_text[0])
    {
        snprintf(v->notice, sizeof v->notice, "%s", c->state_text); /* OctoPrint's own words */
        v->notice_error = true;
    }
    v->can_control = v->online;
    v->can_light = false;

    /* Studio's order: error, cancelling, paused, printing, finished, idle. */
    v->state = c->error                ? PV_ERROR
               : c->cancelling         ? PV_IDLE
               : c->paused             ? PV_PAUSED
               : c->printing           ? PV_PRINTING
               : c->completion >= 99.9f ? PV_DONE
                                       : PV_IDLE;
    v->progress = v->state == PV_IDLE ? 0 : (int)(c->completion + 0.5f);
    v->layer = v->total_layer = 0; /* OctoPrint does not report layers */
    v->left_s = (v->state == PV_PRINTING || v->state == PV_PAUSED) ? c->left_s : 0;
    strlcpy(v->file, c->file, sizeof v->file);

    v->nozzle = c->temp[0];
    v->nozzle_target = c->target[0];
    v->nozzle_count = 0;
    v->nozzle_active = -1;
    if (c->n_tools >= 2)
    {
        v->nozzle_count = c->n_tools;
        for (int i = 0; i < c->n_tools; i++)
        {
            v->nozzles[i].temp = c->temp[i];
            v->nozzles[i].target = c->target[i];
            snprintf(v->nozzles[i].label, sizeof v->nozzles[i].label, "T%d", i);
        }
    }
    v->bed = c->bed;
    v->bed_target = c->bed_target;
    v->chamber = c->chamber;
    memset(v->unit, 0, sizeof v->unit); /* no filament data */
    xSemaphoreGive(c->lock);
}

const printer_driver_t DRIVER_OCTOPRINT = {
    .brand = "octoprint",
    .support = octo_support,
    .start = octo_start,
    .stop = octo_stop,
    .snapshot = octo_snapshot,
    .control = octo_control,
    .thumb_fetch = octoprint_thumb_fetch,
};
