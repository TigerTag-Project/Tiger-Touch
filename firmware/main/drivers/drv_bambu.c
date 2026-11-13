/* Bambu Lab through Bambu's cloud (the way a printer not on this network is
 * reached): MQTT over TLS to <region>.mqtt.bambulab.com:8883.
 *
 * The device never signs in to Bambu. Tiger Studio does, and stores the
 * session in the TigerSystem account (printers/bambulab/secrets/cloud_session);
 * this reads it from there, exactly as the TigerSpool does.
 *
 * Reports are JSON and a full one is tens of kilobytes. A tree parser would
 * spend the scarce internal RAM on thousands of small nodes, so the few
 * scalar fields wanted are looked up in the raw text, and only the two small
 * objects that need real parsing (device, ams) are handed to cJSON. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "addr.h"
#include "cJSON.h"
#include "i18n.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mqtt_client.h"
#include "mqtt_rx.h"
#include "printer_driver.h"

static const char *TAG = "bambu";

#define MAX_REPORT (192 * 1024)
#define ONLINE_WINDOW_US (75LL * 1000 * 1000)
#define PUSHALL_EVERY_US (40LL * 1000 * 1000)

typedef struct
{
    esp_mqtt_client_handle_t mqtt;
    SemaphoreHandle_t lock;
    char sn[40];
    char topic_report[64];
    char topic_request[64];

    mqtt_rx_t rx;

    bool connected;
    bool lan; /* the local broker: the only connection that takes pause / resume / stop */
    bool shared; /* rides on the account's one cloud session (below), which it does not own */
    char light_node[20]; /* "chamber_light", "work_light"...: what lights_report names */
    bool light_on;
    /* The chamber lights, each by its own node: the H2S, H2D, H2D Pro, H2C and X2D have two
     * strips (chamber_light and chamber_light2). -1 not reported yet, 0 off, 1 on. A report
     * may name only one of them, so each is kept on its own. */
    int8_t chamber_light[2];
    bool two_chamber_lights; /* by model (the account's record), or because a report names chamber_light2 */
    uint32_t seq; /* sequence_id of the commands: a new one each time, as Tiger Studio's */
    int64_t last_report_us;
    int64_t last_pushall_us;
    char notice[160];

    float nozzle, nozzle_target, bed, bed_target, chamber;
    /* The extruders a dual-head printer reports (device.extruder.info[]), kept
     * by their own id: 0 is the right nozzle, 1 the left. */
    uint8_t nz_seen;  /* bit i: id i has been read */
    int nz_active;    /* the id at work, -1 when not said */
    float nz_temp[PV_MAX_NOZZLES], nz_target[PV_MAX_NOZZLES];
    int state, progress, layer, total_layer, left_s;
    char file[48];
    pv_unit_t unit[PV_MAX_UNITS];
    int unit_uid[PV_MAX_UNITS]; /* the AMS id behind each AMS unit, -1 for none / the external spool */
    int ams_retries;            /* extra full reports asked for while no AMS has been seen */
} bambu_t;

/* ---- Raw JSON lookups ----------------------------------------------------- */

/* Points just after `"key":` (key given with its quotes). */
/* The A1, A1 mini, P1P and P1S have no chamber sensor, yet report a
 * chamber_temper (5 on an A1 seen on the bench, 2026-10-15). They are told by
 * the first three characters of their serial, the model code Bambu Lab puts
 * there. */
static bool has_chamber_sensor(const char *sn)
{
    static const char *const NONE[] = {"039", "030", "01S", "01P"};
    for (size_t i = 0; i < sizeof NONE / sizeof NONE[0]; i++)
    {
        if (strncmp(sn, NONE[i], 3) == 0)
        {
            return false;
        }
    }
    return true;
}

/* The printers with TWO chamber light strips, told by the catalogue id of the
 * account's record (the same ids as Tiger Studio): H2S 7, H2D 8, H2D Pro 9, H2C 11,
 * X2D 12. The H2C was reported switching one strip only. */
static bool has_two_chamber_lights(const char *model_id)
{
    static const char *const TWO[] = {"7", "8", "9", "11", "12"};
    for (size_t i = 0; i < sizeof TWO / sizeof TWO[0]; i++)
    {
        if (strcmp(model_id, TWO[i]) == 0)
        {
            return true;
        }
    }
    return false;
}

static const char *find_value(const char *buf, const char *key)
{
    const char *p = strstr(buf, key);
    if (!p)
    {
        return NULL;
    }
    p += strlen(key);
    while (*p == ' ' || *p == '\t')
    {
        p++;
    }
    if (*p != ':')
    {
        return NULL;
    }
    p++;
    while (*p == ' ' || *p == '\t')
    {
        p++;
    }
    return p;
}

static bool get_num(const char *buf, const char *key, double *out)
{
    const char *v = find_value(buf, key);
    if (!v)
    {
        return false;
    }
    if (*v == '"')
    {
        v++; /* numbers sometimes arrive as strings */
    }
    char *end;
    double d = strtod(v, &end);
    if (end == v)
    {
        return false;
    }
    *out = d;
    return true;
}

static bool get_str(const char *buf, const char *key, char *out, size_t n)
{
    const char *v = find_value(buf, key);
    if (!v || *v != '"')
    {
        return false;
    }
    v++;
    size_t i = 0;
    while (*v && *v != '"' && i + 1 < n)
    {
        if (*v == '\\' && v[1])
        {
            v++;
        }
        out[i++] = *v++;
    }
    out[i] = '\0';
    return true;
}

/* Locates the object or array that follows `key`. */
static bool extract_block(const char *buf, const char *key, const char **start, size_t *len)
{
    const char *v = find_value(buf, key);
    if (!v || (*v != '{' && *v != '['))
    {
        return false;
    }
    const char open = *v;
    const char close = open == '{' ? '}' : ']';
    int depth = 0;
    bool in_str = false;
    for (const char *p = v; *p; p++)
    {
        const char c = *p;
        if (in_str)
        {
            if (c == '\\' && p[1])
            {
                p++;
            }
            else if (c == '"')
            {
                in_str = false;
            }
            continue;
        }
        if (c == '"')
        {
            in_str = true;
        }
        else if (c == open)
        {
            depth++;
        }
        else if (c == close && --depth == 0)
        {
            *start = v;
            *len = (size_t)(p - v) + 1;
            return true;
        }
    }
    return false;
}

static int json_int(const cJSON *it, int def)
{
    if (cJSON_IsNumber(it))
    {
        return (int)it->valuedouble;
    }
    if (cJSON_IsString(it))
    {
        return atoi(it->valuestring);
    }
    return def;
}

/* ---- Report handling ------------------------------------------------------ */

/* Newer firmwares pack current and target into one integer. */
static void unpack_temp(const cJSON *raw, float *cur, float *tgt)
{
    if (!cJSON_IsNumber(raw))
    {
        return;
    }
    uint32_t v = (uint32_t)raw->valuedouble;
    *cur = (float)(v & 0xFFFF);
    *tgt = (float)((v >> 16) & 0xFFFF);
}

static uint32_t parse_rgb(const char *s)
{
    if (!s || strlen(s) < 6)
    {
        return 0x5a5a5a;
    }
    char six[7];
    memcpy(six, s, 6);
    six[6] = '\0';
    return (uint32_t)strtoul(six, NULL, 16) & 0xffffff; /* RRGGBBAA: drop the alpha */
}

/* The `ams` block. While a print runs the printer sends partial updates: a
 * block with only `tray_now`, or one unit with one tray. So the units are kept
 * and only what the block carries is changed: a block that lists no units
 * (no `ams` array) must never empty the cards. */
static void apply_ams(bambu_t *b, const char *text, size_t len)
{
    cJSON *ams = cJSON_ParseWithLength(text, len);
    if (!ams)
    {
        return;
    }
    const cJSON *list = cJSON_GetObjectItem(ams, "ams");
    const cJSON *now_item = cJSON_GetObjectItem(ams, "tray_now");
    const bool has_now = now_item != NULL;
    int now_id = -1;
    if (cJSON_IsString(now_item))
    {
        now_id = atoi(now_item->valuestring);
    }
    else if (cJSON_IsNumber(now_item))
    {
        now_id = now_item->valueint;
    }

    /* What is known: the AMS units (without the external spool). */
    pv_unit_t keep[PV_MAX_UNITS];
    int keep_uid[PV_MAX_UNITS];
    int k = 0;
    pv_unit_t ext;
    bool have_ext = false;
    memset(keep, 0, sizeof keep);
    memset(&ext, 0, sizeof ext);

    xSemaphoreTake(b->lock, portMAX_DELAY);
    for (int j = 0; j < PV_MAX_UNITS; j++)
    {
        if (!b->unit[j].present)
        {
            continue;
        }
        if (strcmp(b->unit[j].label, PV_LABEL_EXT) == 0)
        {
            ext = b->unit[j];
            have_ext = true;
        }
        else
        {
            keep[k] = b->unit[j];
            keep_uid[k] = b->unit_uid[j];
            k++;
        }
    }
    xSemaphoreGive(b->lock);

    if (cJSON_IsArray(list))
    {
        const cJSON *u;
        cJSON_ArrayForEach(u, list)
        {
            const int uid = json_int(cJSON_GetObjectItem(u, "id"), -1);
            if (uid < 0)
            {
                continue;
            }
            int idx = -1;
            for (int j = 0; j < k; j++)
            {
                if (keep_uid[j] == uid)
                {
                    idx = j;
                    break;
                }
            }
            if (idx < 0)
            {
                if (k >= PV_MAX_UNITS - (have_ext ? 1 : 0))
                {
                    continue;
                }
                idx = k++;
                memset(&keep[idx], 0, sizeof keep[idx]);
                keep_uid[idx] = uid;
            }
            pv_unit_t *pu = &keep[idx];
            pu->present = true;
            pu->slots = uid >= 128 ? 1 : 4; /* one heated slot, numbered from 128 */

            const cJSON *t;
            cJSON_ArrayForEach(t, cJSON_GetObjectItem(u, "tray"))
            {
                const int id = json_int(cJSON_GetObjectItem(t, "id"), -1);
                if (id < 0 || id > 3)
                {
                    continue;
                }
                pv_slot_t *sl = &pu->slot[id];
                const cJSON *type = cJSON_GetObjectItem(t, "tray_type");
                const cJSON *color = cJSON_GetObjectItem(t, "tray_color");
                const cJSON *remain = cJSON_GetObjectItem(t, "remain");
                /* A tray in a partial update carries only what changed. */
                if (type)
                {
                    sl->present = cJSON_IsString(type) && type->valuestring[0];
                    strlcpy(sl->material, cJSON_IsString(type) ? type->valuestring : "", sizeof sl->material);
                }
                if (color && cJSON_IsString(color))
                {
                    sl->color = parse_rgb(color->valuestring);
                }
                if (remain)
                {
                    const int r = json_int(remain, -1);
                    sl->percent = (r < 0 || r > 100) ? -1 : r;
                }
            }
        }
    }

    /* Labels: "AMS" alone, "AMS A".. when there are several, "AMS HT". */
    int plain = 0;
    for (int j = 0; j < k; j++)
    {
        if (keep_uid[j] < 128)
        {
            plain++;
        }
    }
    for (int j = 0; j < k; j++)
    {
        if (keep_uid[j] >= 128)
        {
            snprintf(keep[j].label, sizeof keep[j].label, PV_LABEL_HT);
        }
        else if (plain > 1)
        {
            snprintf(keep[j].label, sizeof keep[j].label, "AMS %c", 'A' + (keep_uid[j] % 26));
        }
        else
        {
            snprintf(keep[j].label, sizeof keep[j].label, "AMS");
        }
    }

    /* The tray in use, when this block says which. */
    if (has_now)
    {
        for (int j = 0; j < k; j++)
        {
            for (int i = 0; i < 4; i++)
            {
                keep[j].slot[i].selected = keep_uid[j] >= 128 ? (i == 0 && now_id == keep_uid[j])
                                                              : (now_id == keep_uid[j] * 4 + i);
            }
        }
    }
    cJSON_Delete(ams);

    xSemaphoreTake(b->lock, portMAX_DELAY);
    memset(b->unit, 0, sizeof b->unit);
    for (int j = 0; j < PV_MAX_UNITS; j++)
    {
        b->unit_uid[j] = -1;
    }
    int n = 0;
    for (int j = 0; j < k && n < PV_MAX_UNITS; j++)
    {
        b->unit[n] = keep[j];
        b->unit_uid[n] = keep_uid[j];
        n++;
    }
    if (have_ext && n < PV_MAX_UNITS)
    {
        b->unit[n] = ext;
    }
    xSemaphoreGive(b->lock);
}

/* The external spool: `vt_tray` on older firmwares, the first `vir_slot` on
 * newer ones. It becomes the "Ext." unit, after the AMS units. */
static void apply_ext(bambu_t *b, const char *buf)
{
    const char *blk;
    size_t blen;
    cJSON *ext = NULL;
    if (extract_block(buf, "\"vt_tray\"", &blk, &blen) && blen < 2048)
    {
        ext = cJSON_ParseWithLength(blk, blen);
    }
    else if (extract_block(buf, "\"vir_slot\"", &blk, &blen) && blen < 4096)
    {
        cJSON *arr = cJSON_ParseWithLength(blk, blen);
        if (arr)
        {
            const cJSON *first = cJSON_GetArrayItem(arr, 0);
            if (first)
            {
                ext = cJSON_Duplicate(first, true);
            }
            cJSON_Delete(arr);
        }
    }
    if (!ext)
    {
        return;
    }

    char now[16] = {0};
    int now_id = -1;
    if (get_str(buf, "\"tray_now\"", now, sizeof now))
    {
        now_id = atoi(now);
    }
    const cJSON *type = cJSON_GetObjectItem(ext, "tray_type");
    const cJSON *color = cJSON_GetObjectItem(ext, "tray_color");

    pv_unit_t u;
    memset(&u, 0, sizeof u);
    u.present = true;
    u.slots = 1;
    snprintf(u.label, sizeof u.label, PV_LABEL_EXT);
    u.slot[0].present = cJSON_IsString(type) && type->valuestring[0];
    u.slot[0].color = parse_rgb(cJSON_IsString(color) ? color->valuestring : NULL);
    u.slot[0].percent = -1;
    u.slot[0].selected = (now_id == 254);
    if (cJSON_IsString(type))
    {
        strlcpy(u.slot[0].material, type->valuestring, sizeof u.slot[0].material);
    }
    cJSON_Delete(ext);

    xSemaphoreTake(b->lock, portMAX_DELAY);
    int at = -1;
    for (int j = 0; j < PV_MAX_UNITS; j++)
    {
        if (b->unit[j].present && strcmp(b->unit[j].label, PV_LABEL_EXT) == 0)
        {
            at = j;
            break;
        }
    }
    for (int j = 0; at < 0 && j < PV_MAX_UNITS; j++)
    {
        if (!b->unit[j].present)
        {
            at = j;
        }
    }
    if (at >= 0)
    {
        b->unit[at] = u;
    }
    xSemaphoreGive(b->lock);
}

static void apply_device(bambu_t *b, const char *text, size_t len, float *nz, float *nzt, float *bd,
                         float *bdt, float *ch)
{
    cJSON *dev = cJSON_ParseWithLength(text, len);
    if (!dev)
    {
        return;
    }
    const cJSON *ext = cJSON_GetObjectItem(dev, "extruder");
    if (ext)
    {
        int active = (json_int(cJSON_GetObjectItem(ext, "state"), 0) >> 4) & 0xF;
        const cJSON *info = cJSON_GetObjectItem(ext, "info");
        const cJSON *pick = NULL;
        const cJSON *e;
        cJSON_ArrayForEach(e, info)
        {
            if (!pick)
            {
                pick = e;
            }
            const int id = json_int(cJSON_GetObjectItem(e, "id"), -1);
            if (id >= 0 && id < PV_MAX_NOZZLES)
            {
                float t = 0, tg = 0;
                unpack_temp(cJSON_GetObjectItem(e, "temp"), &t, &tg);
                xSemaphoreTake(b->lock, portMAX_DELAY);
                b->nz_temp[id] = t;
                b->nz_target[id] = tg;
                b->nz_seen |= (uint8_t)(1u << id);
                b->nz_active = active;
                xSemaphoreGive(b->lock);
            }
            if (id == active)
            {
                pick = e;
            }
        }
        if (pick)
        {
            unpack_temp(cJSON_GetObjectItem(pick, "temp"), nz, nzt);
        }
    }
    const cJSON *bed = cJSON_GetObjectItem(dev, "bed");
    if (bed)
    {
        float tgt_unused;
        unpack_temp(cJSON_GetObjectItem(cJSON_GetObjectItem(bed, "info"), "temp"), bd, bdt);
        (void)tgt_unused;
    }
    const cJSON *ctc = cJSON_GetObjectItem(dev, "ctc");
    if (ctc)
    {
        float unused_target = 0;
        unpack_temp(cJSON_GetObjectItem(cJSON_GetObjectItem(ctc, "info"), "temp"), ch, &unused_target);
    }
    cJSON_Delete(dev);
    (void)b;
}

static void handle_report(bambu_t *b, const char *buf, size_t len)
{
    (void)len;
    if (!strstr(buf, "\"print\""))
    {
        return; /* info, system, ... */
    }

    xSemaphoreTake(b->lock, portMAX_DELAY);
    float nz = b->nozzle, nzt = b->nozzle_target, bd = b->bed, bdt = b->bed_target, ch = b->chamber;
    xSemaphoreGive(b->lock);

    double d;
    char s[64];
    int state = -1, progress = -1, layer = -1, total = -1, left = -1;
    char file[48] = {0};
    bool have_file = false;

    if (get_str(buf, "\"gcode_state\"", s, sizeof s))
    {
        for (char *c = s; *c; c++)
        {
            if (*c >= 'a' && *c <= 'z')
            {
                *c -= 32;
            }
        }
        if (!strcmp(s, "RUNNING") || !strcmp(s, "PREPARE") || !strcmp(s, "SLICING"))
            state = 1;
        else if (!strcmp(s, "PAUSE"))
            state = 2;
        else if (!strcmp(s, "FINISH"))
            state = 3;
        else if (!strcmp(s, "FAILED"))
            state = 4;
        else
            state = 0;
    }
    if (get_num(buf, "\"mc_percent\"", &d))
        progress = (int)d;
    if (get_num(buf, "\"layer_num\"", &d))
        layer = (int)d;
    if (get_num(buf, "\"total_layer_num\"", &d))
        total = (int)d;
    if (get_num(buf, "\"mc_remaining_time\"", &d) || get_num(buf, "\"remaining_time\"", &d))
        left = (int)d * 60; /* minutes on the wire */
    if (get_str(buf, "\"subtask_name\"", file, sizeof file) && file[0])
    {
        have_file = true;
    }
    else if (get_str(buf, "\"gcode_file\"", s, sizeof s) && s[0])
    {
        const char *slash = strrchr(s, '/');
        strlcpy(file, slash ? slash + 1 : s, sizeof file);
        have_file = true;
    }
    if (have_file)
    {
        char *dot = strstr(file, ".gcode");
        if (!dot)
        {
            dot = strstr(file, ".3mf");
        }
        if (dot)
        {
            *dot = '\0';
        }
    }

    /* The light: `lights_report` is a list of {"node": ..., "mode": ...}, in either
     * key order. The chamber lights (one, or two on the H2 series) are read each on
     * its own; any other light (the A1's toolhead) is the first one named. Each
     * element is read on its own: reading a "mode" that follows a node would take
     * the next element's when the keys come the other way round. */
    char light_node[20] = {0};
    int light = -1;
    int chamber[2] = {-1, -1};
    const char *lr = strstr(buf, "\"lights_report\"");
    const char *arr = lr ? strchr(lr, '[') : NULL;
    const char *arr_end = arr ? strchr(arr, ']') : NULL;
    for (const char *cur = arr; cur && arr_end && cur < arr_end;)
    {
        const char *open = strchr(cur, '{');
        const char *close = open ? strchr(open, '}') : NULL;
        if (!open || !close || close > arr_end)
        {
            break;
        }
        char obj[160];
        size_t n = (size_t)(close - open + 1);
        if (n >= sizeof obj)
        {
            n = sizeof obj - 1;
        }
        memcpy(obj, open, n);
        obj[n] = '\0';
        char node[20] = {0}, mode[12] = {0};
        if (get_str(obj, "\"node\"", node, sizeof node) && get_str(obj, "\"mode\"", mode, sizeof mode))
        {
            const int on = strcmp(mode, "on") == 0 ? 1 : 0;
            if (strcmp(node, "chamber_light") == 0)
            {
                chamber[0] = on;
            }
            else if (strcmp(node, "chamber_light2") == 0)
            {
                chamber[1] = on;
            }
            else if (light < 0)
            {
                strlcpy(light_node, node, sizeof light_node);
                light = on;
            }
        }
        cur = close + 1;
    }

    /* Older firmwares: plain keys. */
    if (get_num(buf, "\"nozzle_temper\"", &d))
        nz = (float)d;
    if (get_num(buf, "\"nozzle_target_temper\"", &d))
        nzt = (float)d;
    if (get_num(buf, "\"bed_temper\"", &d))
        bd = (float)d;
    if (get_num(buf, "\"bed_target_temper\"", &d))
        bdt = (float)d;
    if (get_num(buf, "\"chamber_temper\"", &d))
        ch = (float)d;

    /* Newer firmwares (the P2S among them): packed in `device`. Read after the
     * plain keys so that it wins when both are present. */
    const char *blk;
    size_t blen;
    if (extract_block(buf, "\"device\"", &blk, &blen) && blen < 4096)
    {
        apply_device(b, blk, blen, &nz, &nzt, &bd, &bdt, &ch);
    }
    bool have_ams = extract_block(buf, "\"ams\"", &blk, &blen) && blen < 12288;
    if (have_ams)
    {
        apply_ams(b, blk, blen);
    }
    apply_ext(b, buf);

    xSemaphoreTake(b->lock, portMAX_DELAY);
    b->nozzle = nz;
    b->nozzle_target = nzt;
    b->bed = bd;
    b->bed_target = bdt;
    b->chamber = has_chamber_sensor(b->sn) ? ch : 0.0f; /* 0 is drawn "--" */
    if (chamber[0] >= 0)
    {
        b->chamber_light[0] = (int8_t)chamber[0];
    }
    if (chamber[1] >= 0)
    {
        b->chamber_light[1] = (int8_t)chamber[1];
        b->two_chamber_lights = true; /* it names a second strip: both are driven */
    }
    if (b->chamber_light[0] >= 0 || b->chamber_light[1] >= 0)
    {
        const bool on = b->chamber_light[0] == 1 || b->chamber_light[1] == 1;
        if (b->light_on != on)
        {
            ESP_LOGI(TAG, "light report: the chamber light is %s", on ? "on" : "off");
        }
        b->light_on = on;
        strlcpy(b->light_node, "chamber_light", sizeof b->light_node);
    }
    else if (light >= 0)
    {
        if (b->light_on != (light == 1) || strcmp(b->light_node, light_node) != 0)
        {
            ESP_LOGI(TAG, "light report: %s is %s", light_node, light == 1 ? "on" : "off");
        }
        b->light_on = light == 1;
        strlcpy(b->light_node, light_node, sizeof b->light_node);
    }
    if (state >= 0)
        b->state = state;
    if (progress >= 0)
        b->progress = progress;
    if (layer >= 0)
        b->layer = layer;
    if (total >= 0)
        b->total_layer = total;
    if (left >= 0)
        b->left_s = left;
    if (have_file)
        strlcpy(b->file, file, sizeof b->file);
    b->last_report_us = esp_timer_get_time();
    xSemaphoreGive(b->lock);
}

/* ---- MQTT ----------------------------------------------------------------- */

static void pushall(bambu_t *b)
{
    static const char REQ[] = "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}";
    if (b->mqtt && b->connected)
    {
        esp_mqtt_client_publish(b->mqtt, b->topic_request, REQ, sizeof REQ - 1, 0, 0);
        b->last_pushall_us = esp_timer_get_time();
    }
}

static void on_mqtt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    bambu_t *b = (bambu_t *)arg;
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)data;

    switch ((esp_mqtt_event_id_t)id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected to the printer's own broker");
        xSemaphoreTake(b->lock, portMAX_DELAY);
        b->connected = true;
        b->ams_retries = 0;
        b->notice[0] = '\0';
        xSemaphoreGive(b->lock);
        /* A session starts clean: the subscription must be asked for again,
         * and a first full report is what the partial ones apply to. */
        esp_mqtt_client_subscribe(b->mqtt, b->topic_report, 0);
        pushall(b);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected");
        xSemaphoreTake(b->lock, portMAX_DELAY);
        b->connected = false;
        xSemaphoreGive(b->lock);
        break;

    case MQTT_EVENT_ERROR:
        if (ev->error_handle && ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
        {
            ESP_LOGW(TAG, "broker refused the session (code %d)",
                     (int)ev->error_handle->connect_return_code);
            xSemaphoreTake(b->lock, portMAX_DELAY);
            snprintf(b->notice, sizeof b->notice, "%s", tr(S_BAMBU_REFUSED));
            xSemaphoreGive(b->lock);
        }
        break;

    case MQTT_EVENT_DATA:
    {
        const char *text;
        size_t len;
        if (mqtt_rx_feed(&b->rx, ev, MAX_REPORT, &text, &len))
        {
            handle_report(b, text, len);
        }
        break;
    }

    default:
        break;
    }
}

/* ---- One cloud session for every cloud printer of the account ------------
 *
 * Every Bambu Lab reached through the cloud talks to the same regional broker
 * with the same credentials, so they share ONE MQTT session, as on TigerSpool:
 * the first printer opens it and pays for it (its task stack, its socket, its
 * TLS session), each further one is a subscription on it. Messages are routed
 * to their printer by topic. A printer in LAN mode keeps a client of its own.
 *
 * Measured on a Panda Touch (docs/CONNECTION-BUDGET.md): a second cloud Bambu
 * cost 18 KB of internal RAM with a session of its own. */
#define CLOUD_MAX_SUBS 16

static struct
{
    esp_mqtt_client_handle_t mqtt;
    bool connected;
    bambu_t *subs[CLOUD_MAX_SUBS];
    int nsubs;
    bambu_t *rx_target; /* the printer the message being received is for */
} s_cs;

static SemaphoreHandle_t cs_lock(void)
{
    static StaticSemaphore_t buf;
    static SemaphoreHandle_t h;
    if (!h)
    {
        h = xSemaphoreCreateMutexStatic(&buf);
    }
    return h;
}

static void sub_connected(bambu_t *b, bool on)
{
    xSemaphoreTake(b->lock, portMAX_DELAY);
    b->connected = on;
    if (on)
    {
        b->ams_retries = 0;
        b->notice[0] = '\0';
    }
    xSemaphoreGive(b->lock);
}

static void on_cloud_mqtt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)data;
    xSemaphoreTake(cs_lock(), portMAX_DELAY);
    switch ((esp_mqtt_event_id_t)id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected to the cloud broker (%d printer(s) on the session)", s_cs.nsubs);
        s_cs.connected = true;
        for (int i = 0; i < s_cs.nsubs; i++)
        {
            /* A session starts clean: every subscription is asked for again. */
            sub_connected(s_cs.subs[i], true);
            esp_mqtt_client_subscribe(s_cs.mqtt, s_cs.subs[i]->topic_report, 0);
            pushall(s_cs.subs[i]);
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "cloud session disconnected");
        s_cs.connected = false;
        s_cs.rx_target = NULL;
        for (int i = 0; i < s_cs.nsubs; i++)
        {
            sub_connected(s_cs.subs[i], false);
        }
        break;
    case MQTT_EVENT_ERROR:
        if (ev->error_handle && ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
        {
            ESP_LOGW(TAG, "broker refused the session (code %d)", (int)ev->error_handle->connect_return_code);
            for (int i = 0; i < s_cs.nsubs; i++)
            {
                bambu_t *b = s_cs.subs[i];
                xSemaphoreTake(b->lock, portMAX_DELAY);
                snprintf(b->notice, sizeof b->notice, "%s", tr(S_BAMBU_REFUSED));
                xSemaphoreGive(b->lock);
            }
        }
        break;
    case MQTT_EVENT_DATA:
    {
        /* The topic comes with the first chunk only: it decides who the
         * following chunks are for. */
        if (ev->current_data_offset == 0)
        {
            s_cs.rx_target = NULL;
            for (int i = 0; i < s_cs.nsubs; i++)
            {
                const char *t = s_cs.subs[i]->topic_report;
                if (ev->topic && (int)strlen(t) == ev->topic_len && memcmp(t, ev->topic, ev->topic_len) == 0)
                {
                    s_cs.rx_target = s_cs.subs[i];
                    break;
                }
            }
        }
        bambu_t *b = s_cs.rx_target;
        const char *text;
        size_t len;
        if (b && mqtt_rx_feed(&b->rx, ev, MAX_REPORT, &text, &len))
        {
            handle_report(b, text, len);
        }
        break;
    }
    default:
        break;
    }
    xSemaphoreGive(cs_lock());
}

/* Joins the account's cloud session, opening it with these credentials when
 * this is the first cloud printer. Returns false when it could not be opened. */
static bool cloud_join(bambu_t *b, const esp_mqtt_client_config_t *cfg)
{
    xSemaphoreTake(cs_lock(), portMAX_DELAY);
    if (s_cs.nsubs >= CLOUD_MAX_SUBS)
    {
        xSemaphoreGive(cs_lock());
        return false;
    }
    if (!s_cs.mqtt)
    {
        s_cs.mqtt = esp_mqtt_client_init(cfg); /* copies the strings it needs */
        if (!s_cs.mqtt)
        {
            xSemaphoreGive(cs_lock());
            return false;
        }
        s_cs.connected = false;
        esp_mqtt_client_register_event(s_cs.mqtt, ESP_EVENT_ANY_ID, on_cloud_mqtt, NULL);
        esp_mqtt_client_start(s_cs.mqtt);
        ESP_LOGI(TAG, "opening the cloud session");
    }
    b->mqtt = s_cs.mqtt;
    b->shared = true;
    s_cs.subs[s_cs.nsubs++] = b;
    if (s_cs.connected)
    {
        sub_connected(b, true);
        esp_mqtt_client_subscribe(s_cs.mqtt, b->topic_report, 0);
        pushall(b);
    }
    xSemaphoreGive(cs_lock());
    return true;
}

static void cloud_leave(bambu_t *b)
{
    esp_mqtt_client_handle_t closing = NULL;
    xSemaphoreTake(cs_lock(), portMAX_DELAY);
    for (int i = 0; i < s_cs.nsubs; i++)
    {
        if (s_cs.subs[i] == b)
        {
            s_cs.subs[i] = s_cs.subs[--s_cs.nsubs];
            break;
        }
    }
    if (s_cs.rx_target == b)
    {
        s_cs.rx_target = NULL;
    }
    if (s_cs.connected && s_cs.nsubs > 0)
    {
        esp_mqtt_client_unsubscribe(s_cs.mqtt, b->topic_report);
    }
    if (s_cs.nsubs == 0)
    {
        closing = s_cs.mqtt;
        s_cs.mqtt = NULL;
        s_cs.connected = false;
    }
    xSemaphoreGive(cs_lock());
    /* Outside the lock: stopping waits for the client's task, whose event
     * handler takes that lock. */
    if (closing)
    {
        esp_mqtt_client_stop(closing);
        esp_mqtt_client_destroy(closing);
        ESP_LOGI(TAG, "cloud session closed");
    }
}

/* ---- Driver --------------------------------------------------------------- */

static drv_kind_t bambu_support(const account_printer_t *p)
{
    if (!p->sn[0])
    {
        return DRV_NONE;
    }
    if (p->cloud)
    {
        return DRV_CLOUD;
    }
    /* In LAN mode the printer is its own broker: its address and its access
     * code are what is needed. */
    return (p->ip[0] && p->cc[0]) ? DRV_LAN : DRV_NONE;
}

static void *bambu_start(const account_printer_t *p)
{
    bambu_t *b = calloc(1, sizeof *b);
    if (!b)
    {
        return NULL;
    }
    b->lock = xSemaphoreCreateMutex();
    strlcpy(b->sn, p->sn, sizeof b->sn);
    b->chamber_light[0] = b->chamber_light[1] = -1;
    b->two_chamber_lights = has_two_chamber_lights(p->model_id);
    snprintf(b->topic_report, sizeof b->topic_report, "device/%s/report", b->sn);
    snprintf(b->topic_request, sizeof b->topic_request, "device/%s/request", b->sn);

    const bool lan = !p->cloud;
    b->lan = lan;
    char user[96], token[1100], region[8];
    if (lan)
    {
        strlcpy(user, "bblp", sizeof user); /* fixed user, the access code is the password */
        strlcpy(token, p->cc, sizeof token);
        region[0] = '\0';
    }
    else
    {
        cloud_err_t e = cloud_bambu_session(user, sizeof user, token, sizeof token, region, sizeof region);
        if (e != CLOUD_OK)
        {
            snprintf(b->notice, sizeof b->notice, "%s",
                     e == CLOUD_ERR_NOT_FOUND ? tr(S_BAMBU_MISSING) : tr(S_ERR_ACCOUNT_SILENT));
            ESP_LOGW(TAG, "no cloud session (%d)", e);
            return b; /* alive, but silent: the notice tells why */
        }
    }

    char uri[96], cid[40];
    if (lan)
    {
        char hp[ADDR_HOSTPORT_MAX];
        addr_with_port(hp, sizeof hp, p->ip, 8883);
        snprintf(uri, sizeof uri, "mqtts://%s", hp);
    }
    else
    {
        snprintf(uri, sizeof uri, "mqtts://%s.mqtt.bambulab.com:8883", region);
    }
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    /* One id per device: a shared one makes the broker drop the other session. */
    snprintf(cid, sizeof cid, "tigertouch-%02x%02x%02x-%02x", mac[3], mac[4], mac[5], (unsigned)(esp_random() & 0xff));

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = uri,
        /* The cloud broker has a public certificate. A printer in LAN mode
         * presents its own, which no bundle holds: it is reached by address,
         * on the local network, with its access code. */
        .broker.verification.crt_bundle_attach = lan ? NULL : esp_crt_bundle_attach,
        .broker.verification.skip_cert_common_name_check = lan,
        .credentials.username = user,
        .credentials.client_id = cid,
        .credentials.authentication.password = token,
        .session.keepalive = 30,
        .buffer.size = 4096,
        .buffer.out_size = 1024,
        .task.stack_size = 8192,
        .network.reconnect_timeout_ms = 5000,
    };
    if (!lan)
    {
        const bool joined = cloud_join(b, &cfg);
        memset(token, 0, sizeof token);
        if (!joined)
        {
            snprintf(b->notice, sizeof b->notice, "%s", tr(S_CLOUD_FAIL));
        }
        return b;
    }
    b->mqtt = esp_mqtt_client_init(&cfg); /* copies the strings it needs */
    memset(token, 0, sizeof token);
    if (!b->mqtt)
    {
        snprintf(b->notice, sizeof b->notice, "%s", tr(S_CLOUD_FAIL));
        return b;
    }
    esp_mqtt_client_register_event(b->mqtt, ESP_EVENT_ANY_ID, on_mqtt, b);
    esp_mqtt_client_start(b->mqtt);
    ESP_LOGI(TAG, "connecting to a Bambu Lab broker");
    return b;
}

static void bambu_stop(void *ctx)
{
    bambu_t *b = (bambu_t *)ctx;
    if (!b)
    {
        return;
    }
    if (b->shared)
    {
        cloud_leave(b);
    }
    else if (b->mqtt)
    {
        esp_mqtt_client_stop(b->mqtt);
        esp_mqtt_client_destroy(b->mqtt);
    }
    vSemaphoreDelete(b->lock);
    mqtt_rx_free(&b->rx);
    free(b);
}

/* Pause, resume, stop: the frames Tiger Studio sends on device/<sn>/request.
 * Refused on a cloud connection, as Studio does: the cloud broker takes no such
 * command from this session, and a refused command is never reported back, so
 * it is better not offered. Written from the protocol notes; not run against a
 * printer in LAN mode. */
static bool bambu_control(void *ctx, pv_control_t cmd)
{
    static const char *const REQ[] = {
        [PV_CTL_PAUSE] = "{\"print\":{\"sequence_id\":\"1\",\"command\":\"pause\"}}",
        [PV_CTL_RESUME] = "{\"print\":{\"sequence_id\":\"2\",\"command\":\"resume\"}}",
        [PV_CTL_STOP] = "{\"print\":{\"sequence_id\":\"3\",\"command\":\"stop\"}}",
    };
    bambu_t *b = (bambu_t *)ctx;
    /* The light goes through the cloud broker as well as the local one, as in
     * Tiger Studio; a pause, a resume and a stop only through the local one. */
    if (b && b->connected && b->mqtt && (cmd == PV_CTL_LIGHT_ON || cmd == PV_CTL_LIGHT_OFF))
    {
        const bool on = cmd == PV_CTL_LIGHT_ON;
        /* The light is one touch, whatever the number of strips: a printer with two chamber
         * lights (H2 series, X2D) gets the command on both nodes, one message each. */
        char nodes[2][sizeof b->light_node];
        int count = 1;
        xSemaphoreTake(b->lock, portMAX_DELAY);
        if (b->two_chamber_lights)
        {
            strlcpy(nodes[0], "chamber_light", sizeof nodes[0]);
            strlcpy(nodes[1], "chamber_light2", sizeof nodes[1]);
            count = 2;
        }
        else
        {
            strlcpy(nodes[0], b->light_node[0] ? b->light_node : "chamber_light", sizeof nodes[0]);
        }
        xSemaphoreGive(b->lock);
        bool all = true;
        for (int i = 0; i < count; i++)
        {
            char req[256];
            uint32_t seq;
            xSemaphoreTake(b->lock, portMAX_DELAY);
            seq = b->seq++ % 4087;
            xSemaphoreGive(b->lock);
            snprintf(req, sizeof req,
                     "{\"system\":{\"sequence_id\":\"%u\",\"command\":\"ledctrl\",\"led_node\":\"%s\","
                     "\"led_mode\":\"%s\",\"led_on_time\":500,\"led_off_time\":500,\"loop_times\":0,"
                     "\"interval_time\":0}}",
                     (unsigned)seq, nodes[i], on ? "on" : "off");
            /* QoS 1, as Tiger Studio publishes to the cloud broker: at QoS 0 the broker
             * may drop it without a word. */
            const int id = esp_mqtt_client_publish(b->mqtt, b->topic_request, req, (int)strlen(req), 1, 0);
            ESP_LOGI(TAG, "light %s on %s (%s): %s", on ? "on" : "off", nodes[i], b->lan ? "local broker" : "cloud broker",
                     id >= 0 ? "published" : "refused");
            all = all && id >= 0;
        }
        return all;
    }
    if (!b || !b->lan || !b->connected || !b->mqtt || (int)cmd < 0 || (int)cmd > PV_CTL_STOP)
    {
        return false;
    }
    return esp_mqtt_client_publish(b->mqtt, b->topic_request, REQ[cmd], (int)strlen(REQ[cmd]), 0, 0) >= 0;
}

static void bambu_snapshot(void *ctx, printer_view_t *v)
{
    bambu_t *b = (bambu_t *)ctx;
    if (!b)
    {
        return;
    }
    int64_t now = esp_timer_get_time();
    /* A printer that went quiet is asked again, not left on stale numbers. */
    if (b->connected && now - b->last_report_us > PUSHALL_EVERY_US && now - b->last_pushall_us > PUSHALL_EVERY_US)
    {
        pushall(b);
    }

    /* Joined in the middle of a print, the first full report can come without
     * the AMS: ask a few more times before concluding there is none. */
    if (b->connected && b->last_report_us != 0 && b->ams_retries < 3 && now - b->last_pushall_us > 20LL * 1000 * 1000)
    {
        bool any = false;
        xSemaphoreTake(b->lock, portMAX_DELAY);
        for (int j = 0; j < PV_MAX_UNITS; j++)
        {
            any |= b->unit[j].present && strcmp(b->unit[j].label, PV_LABEL_EXT) != 0;
        }
        xSemaphoreGive(b->lock);
        if (!any)
        {
            b->ams_retries++;
            pushall(b);
        }
    }

    xSemaphoreTake(b->lock, portMAX_DELAY);
    v->online = b->connected && b->last_report_us != 0 && (now - b->last_report_us) < ONLINE_WINDOW_US;
    v->can_control = v->online && b->lan;
    v->can_light = v->online;
    v->light_on = b->light_on;
    switch (b->state)
    {
    case 1:  v->state = PV_PRINTING; break;
    case 2:  v->state = PV_PAUSED; break;
    case 3:  v->state = PV_DONE; break;
    case 4:  v->state = PV_ERROR; break;
    default: v->state = PV_IDLE; break;
    }
    v->progress = v->state == PV_IDLE ? 0 : b->progress;
    v->layer = b->layer;
    v->total_layer = b->total_layer;
    v->left_s = (v->state == PV_PRINTING || v->state == PV_PAUSED) ? b->left_s : 0;
    v->nozzle = b->nozzle;
    v->nozzle_target = b->nozzle_target;
    /* Two extruders or more: one tile each. With two, the tiles read left to
     * right, which is id 1 then id 0 (id 0 is the right nozzle). */
    v->nozzle_count = 0;
    v->nozzle_active = -1;
    int seen = 0;
    for (int i = 0; i < PV_MAX_NOZZLES; i++)
    {
        seen += (b->nz_seen >> i) & 1;
    }
    if (seen >= 2)
    {
        const int n = seen > PV_MAX_NOZZLES ? PV_MAX_NOZZLES : seen;
        v->nozzle_count = n;
        for (int pos = 0; pos < n; pos++)
        {
            const int id = (n == 2) ? 1 - pos : pos;
            v->nozzles[pos].temp = b->nz_temp[id];
            v->nozzles[pos].target = b->nz_target[id];
            if (n == 2)
            {
                strlcpy(v->nozzles[pos].label, tr(pos == 0 ? S_NOZZLE_LEFT : S_NOZZLE_RIGHT),
                        sizeof v->nozzles[pos].label);
            }
            else
            {
                snprintf(v->nozzles[pos].label, sizeof v->nozzles[pos].label, tr(S_NOZZLE_N), pos + 1);
            }
            if (id == b->nz_active)
            {
                v->nozzle_active = pos;
            }
        }
        if (v->nozzle_active >= 0)
        {
            v->nozzle = v->nozzles[v->nozzle_active].temp;
            v->nozzle_target = v->nozzles[v->nozzle_active].target;
        }
    }
    v->bed = b->bed;
    v->bed_target = b->bed_target;
    v->chamber = b->chamber;
    strlcpy(v->file, b->file, sizeof v->file);
    strlcpy(v->notice, b->notice, sizeof v->notice);
    v->notice_error = b->notice[0] != '\0';
    memcpy(v->unit, b->unit, sizeof v->unit);
    xSemaphoreGive(b->lock);
}

/* ---- The picture of the print, through the maker's cloud ----------------------
 *
 * The account's task list names every print and carries, for each, the address of
 * its picture (`cover`, a PNG on the maker's storage, fetched without a token).
 * The print in progress is the task whose title is the file name the printer
 * reports. Checked on a P2S in cloud mode (2026-11-13): the list answers 200 to
 * a client with its own name and 403 (Cloudflare) to the OrcaSlicer one. The
 * list is read with `limit=5`, about 24 KB instead of 100 KB for twenty tasks.
 * It is read as text, not as a cJSON tree: a tree is hundreds of small blocks,
 * and those land in internal RAM. */

/* The JSON string whose opening quote is at `p`, into `out`, with its escapes
 * undone; the pointer after the closing quote, or NULL. */
static const char *json_str(const char *p, char *out, size_t cap)
{
    size_t n = 0;
    for (p++; *p && *p != '"'; p++)
    {
        char c = *p;
        if (c == '\\')
        {
            c = *++p;
            if (c == 'u')
            {
                unsigned u = 0;
                for (int i = 0; i < 4; i++)
                {
                    const char h = *++p;
                    if (!h)
                    {
                        return NULL;
                    }
                    u = (u << 4) | (unsigned)(h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
                }
                char b[3];
                int k;
                if (u < 0x80)
                {
                    b[0] = (char)u;
                    k = 1;
                }
                else if (u < 0x800)
                {
                    b[0] = (char)(0xC0 | (u >> 6));
                    b[1] = (char)(0x80 | (u & 0x3F));
                    k = 2;
                }
                else
                {
                    b[0] = (char)(0xE0 | (u >> 12));
                    b[1] = (char)(0x80 | ((u >> 6) & 0x3F));
                    b[2] = (char)(0x80 | (u & 0x3F));
                    k = 3;
                }
                for (int i = 0; i < k && n + 1 < cap; i++)
                {
                    out[n++] = b[i];
                }
                continue;
            }
            if (c == 'n')
            {
                c = '\n';
            }
            else if (c == 't')
            {
                c = '\t';
            }
            else if (!c)
            {
                break;
            }
        }
        if (n + 1 < cap)
        {
            out[n++] = c;
        }
    }
    out[n] = '\0';
    return *p == '"' ? p + 1 : NULL;
}

/* Just after the '}' that closes the object opening at `p`, or NULL. */
static const char *obj_end(const char *p)
{
    int depth = 0;
    bool in = false;
    for (; *p; p++)
    {
        if (in)
        {
            if (*p == '\\' && p[1])
            {
                p++;
            }
            else if (*p == '"')
            {
                in = false;
            }
        }
        else if (*p == '"')
        {
            in = true;
        }
        else if (*p == '{')
        {
            depth++;
        }
        else if (*p == '}' && --depth == 0)
        {
            return p + 1;
        }
    }
    return NULL;
}

/* The string value of `key` inside [s, e). */
static bool obj_str(const char *s, const char *e, const char *key, char *out, size_t cap)
{
    char pat[16];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const size_t pl = strlen(pat);
    for (const char *q = s; (q = strstr(q, pat)) && q < e; q += pl)
    {
        const char *v = q + pl;
        while (*v == ' ' || *v == ':')
        {
            v++;
        }
        if (*v == '"')
        {
            return json_str(v, out, cap) != NULL;
        }
    }
    return false;
}

static bool bambu_thumb_fetch(const account_printer_t *p, const char *file, uint8_t **data, size_t *len)
{
    *data = NULL;
    *len = 0;
    /* In LAN mode the picture is in the printer's own storage (a .3mf over FTPS): not read yet. */
    if (!p->cloud || !p->sn[0] || !file[0])
    {
        return false;
    }
    char user[96], token[1100], region[8];
    if (cloud_bambu_session(user, sizeof user, token, sizeof token, region, sizeof region) != CLOUD_OK)
    {
        memset(token, 0, sizeof token);
        return false;
    }
    char url[200];
    snprintf(url, sizeof url, "https://api.bambulab.%s/v1/user-service/my/tasks?deviceId=%s&limit=5",
             strcmp(region, "cn") == 0 ? "cn" : "com", p->sn);
    uint8_t *js = NULL;
    size_t jl = 0;
    const int st = thumb_http_get(url, token, true, 128 * 1024, &js, &jl);
    memset(token, 0, sizeof token);
    if (st != 200)
    {
        ESP_LOGW(TAG, "task list: HTTP %d", st);
        return false;
    }
    char cover[640] = "";
    const char *hits = strstr((const char *)js, "\"hits\"");
    const char *q = hits ? strchr(hits, '[') : NULL;
    const size_t flen = strlen(file);
    /* `file` is the name cut to the view's width: a longer title still starts with it. */
    for (q = q ? q + 1 : NULL; q && (q = strchr(q, '{'));)
    {
        const char *e = obj_end(q);
        if (!e)
        {
            break;
        }
        char title[96], dev[48], cv[640];
        const bool mine = !obj_str(q, e, "deviceId", dev, sizeof dev) || strcmp(dev, p->sn) == 0;
        if (mine && obj_str(q, e, "title", title, sizeof title) && strncmp(title, file, flen) == 0 &&
            obj_str(q, e, "cover", cv, sizeof cv) && cv[0])
        {
            strlcpy(cover, cv, sizeof cover);
            break;
        }
        q = e;
    }
    heap_caps_free(js);
    if (!cover[0])
    {
        ESP_LOGI(TAG, "no task of the list is titled like the print");
        return false;
    }
    const int cs = thumb_http_get(cover, NULL, false, 2 * 1024 * 1024, data, len);
    if (cs != 200)
    {
        ESP_LOGW(TAG, "cover: HTTP %d", cs);
        return false;
    }
    return true;
}

const printer_driver_t DRIVER_BAMBU = {
    .brand = "bambulab",
    .support = bambu_support,
    .start = bambu_start,
    .stop = bambu_stop,
    .snapshot = bambu_snapshot,
    .control = bambu_control,
    .thumb_fetch = bambu_thumb_fetch,
};
