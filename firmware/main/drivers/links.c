#include "links.h"
#include "demo_mode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "settings.h"

static const char *TAG = "links";

/* The budget and the costs are in LOAD SLOTS, as on TigerSpool
 * (printer_budget.h there): one load slot is one kilobyte of internal RAM a
 * printer's connection occupies. "Load slot", never plain "slot": a slot is a
 * spool tray everywhere else in this code.
 *
 * A cost is the WORST measured for that connection, rounded up to the next
 * kilobyte - a fact about the connection, not a guess; the safety margin lives
 * once, in the budget. Every number, how it was measured and when to measure it
 * again: docs/CONNECTION-BUDGET.md. If that file and this one disagree, this is
 * what runs: fix whichever is wrong, in the same change. */
#define BUDGET 144
#define COST_CREALITY 15        /* worst measured 14.9 KB (WebSocket, the same client as Snapmaker's) */
#define COST_FLASHFORGE 5       /* worst measured 4.7 KB (AD5X, HTTP poller; 1.8 KB on most passes) */
#define COST_SNAPMAKER 15       /* worst measured 14.9 KB (U1, WebSocket) */
#define COST_ELEGOO 16          /* worst measured 15.2 KB (Centauri Carbon 2, MQTT) */
#define COST_BAMBU_CLOUD 21     /* the first cloud Bambu opens the session: worst measured 21.0 KB */
#define COST_BAMBU_CLOUD_MORE 4 /* each further one, a subscription on it: worst measured 3.3 KB */
#define COST_BAMBU_LAN 21       /* a session of its own: worst measured 20.5 KB (X1C, A1; four passes) */
#define COST_ANYCUBIC_LAN 21    /* MQTT over TLS like a Bambu session: NOT measured */
#define COST_ANYCUBIC_CLOUD 19  /* the first cloud Anycubic opens its session, mutual TLS: worst measured 18.1 KB (Kobra X) */
#define COST_ANYCUBIC_CLOUD_MORE 4 /* each further one, a subscription on it, as for Bambu: NOT measured */
#define COST_PRUSA 5            /* an HTTP poller like FlashForge's: NOT measured */
#define COST_KLIPPER 5          /* an HTTP poller like FlashForge's: 3.5 KB against Tiger Studio's Moonraker mock */
#define COST_OCTOPRINT 5        /* an HTTP poller like FlashForge's: lost in the noise against Studio's mock */

/* Below this much free internal RAM nothing new is opened, and if the device
 * stays below it for SUSTAIN_US the last link opened is closed and its printer
 * switched off, so the device lives (TigerSpool's survival floor). */
#define FLOOR_FREE (32 * 1024)
#define SUSTAIN_US (3LL * 1000 * 1000)
/* At most this many links, whatever the budget says (TigerSpool's MAX_LINKS). */
#define MAX_LINKS 10

/* The switched-on set is kept as ",id,id," so that "nobody" is not an empty
 * string, which the settings store reads as "never chosen". */
#define KEY "pr_on"
#define CSV_MAX 1100

#define RETRY_US (10LL * 1000 * 1000)
/* A printer that has not answered after this long is shown offline instead of
 * being waited for: it is switched off, or out of reach. The row follows it
 * the moment it does answer, so being quick costs nothing. */
#define SETTLE_US (5LL * 1000 * 1000)

typedef struct
{
    const printer_driver_t *drv;
    void *ctx;
    printer_view_t *view; /* PSRAM: a report is ~750 bytes and there are many */
    char id[40];
    uint32_t fp;          /* what the link was opened with */
    int64_t started_us;
    int64_t retry_at_us;
} link_t;

static account_printer_t *s_p;
static const int *s_n;
static bool s_on[CLOUD_MAX_PRINTERS];
static link_t s_l[CLOUD_MAX_PRINTERS];
static SemaphoreHandle_t s_lock;

static uint32_t fingerprint(const account_printer_t *p)
{
    uint32_t h = 2166136261u;
    const char *parts[] = {p->brand, p->ip, p->sn, p->cc, p->proto, p->board_id};
    for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++)
    {
        for (const char *c = parts[i]; *c; c++)
        {
            h = (h ^ (uint8_t)*c) * 16777619u;
        }
        h = (h ^ 0xff) * 16777619u;
    }
    return h ^ (p->cloud ? 1u : 0u);
}

/* Load slots for one printer. `shares_cloud`: a cloud Bambu has already been
 * counted, so this one rides on its session. A brand missing here costs 0 and is
 * never refused: a new driver is not finished until it is measured and listed. */
static int cost_for(const account_printer_t *p, bool shares_cloud)
{
    if (!drivers_for(p))
    {
        return 0;
    }
    if (strcmp(p->brand, "bambulab") == 0)
    {
        return p->cloud ? (shares_cloud ? COST_BAMBU_CLOUD_MORE : COST_BAMBU_CLOUD) : COST_BAMBU_LAN;
    }
    if (strcmp(p->brand, "anycubic") == 0)
    {
        return p->cloud ? (shares_cloud ? COST_ANYCUBIC_CLOUD_MORE : COST_ANYCUBIC_CLOUD) : COST_ANYCUBIC_LAN;
    }
    static const struct
    {
        const char *brand;
        int cost;
    } COSTS[] = {
        {"creality", COST_CREALITY}, {"flashforge", COST_FLASHFORGE}, {"snapmaker", COST_SNAPMAKER},
        {"elegoo", COST_ELEGOO},     {"prusa", COST_PRUSA},       {"klipper", COST_KLIPPER},
        {"octoprint", COST_OCTOPRINT},
    };
    for (size_t i = 0; i < sizeof COSTS / sizeof COSTS[0]; i++)
    {
        if (strcmp(p->brand, COSTS[i].brand) == 0)
        {
            return COSTS[i].cost;
        }
    }
    return 0;
}

/* Load slots taken by the switched-on printers. `flip` asks "and if this one
 * were toggled" - the question a switch asks before it may move; -1 for the
 * state as it is. Each brand's cloud session (Bambu Lab, Anycubic) is charged
 * once, to its first printer. */
static int used_slots_if(int flip)
{
    int sum = 0;
    bool bambu_cloud = false, acu_cloud = false;
    for (int i = 0; i < *s_n; i++)
    {
        const bool on = i == flip ? !s_on[i] : s_on[i];
        if (!on)
        {
            continue;
        }
        const bool acu = strcmp(s_p[i].brand, "anycubic") == 0;
        sum += cost_for(&s_p[i], acu ? acu_cloud : bambu_cloud);
        if (s_p[i].cloud && acu)
        {
            acu_cloud = true;
        }
        else if (s_p[i].cloud && strcmp(s_p[i].brand, "bambulab") == 0)
        {
            bambu_cloud = true;
        }
    }
    return sum;
}

static int used_slots(void)
{
    return used_slots_if(-1);
}

static int on_count_if(int flip)
{
    int n = 0;
    for (int i = 0; i < *s_n; i++)
    {
        n += (i == flip ? !s_on[i] : s_on[i]) ? 1 : 0;
    }
    return n;
}

static bool csv_has(const char *csv, const char *id)
{
    char needle[48];
    snprintf(needle, sizeof needle, ",%s,", id);
    return strstr(csv, needle) != NULL;
}

static void persist(void)
{
    if (demo_mode_on())
    {
        return; /* the owner's real choice of printers is kept as it was */
    }
    static char csv[CSV_MAX];
    size_t at = 0;
    csv[at++] = ',';
    csv[at] = '\0';
    for (int i = 0; i < *s_n; i++)
    {
        if (!s_on[i])
        {
            continue;
        }
        const size_t len = strlen(s_p[i].id);
        if (at + len + 2 > sizeof csv)
        {
            break;
        }
        memcpy(csv + at, s_p[i].id, len);
        at += len;
        csv[at++] = ',';
        csv[at] = '\0';
    }
    settings_set(KEY, csv);
}

static void stop_link(link_t *l)
{
    if (l->ctx && l->drv)
    {
        l->drv->stop(l->ctx);
    }
    free(l->view);
    memset(l, 0, sizeof *l);
}

void links_init(account_printer_t *printers, const int *count)
{
    s_p = printers;
    s_n = count;
    s_lock = xSemaphoreCreateRecursiveMutex();
}

void links_lock(void)
{
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
}

void links_unlock(void)
{
    xSemaphoreGiveRecursive(s_lock);
}

void links_reload(void)
{
    links_lock();
    static char csv[CSV_MAX];
    const bool chosen = settings_get(KEY, csv, sizeof csv);
    const int n = *s_n;

    memset(s_on, 0, sizeof s_on);
    for (int i = 0; i < n; i++)
    {
        if (!drivers_for(&s_p[i]))
        {
            continue; /* no driver: nothing to switch on */
        }
        /* Never chosen: every printer in the account's order, until the budget
         * or the link count is full. That first answer then becomes the user's. */
        const bool want = chosen && !demo_mode_on() ? csv_has(csv, s_p[i].id) : true;
        /* A demo printer costs no connection: only the link count holds it. */
        const bool fits = demo_mode_on() || used_slots_if(i) <= BUDGET;
        if (want && fits && on_count_if(i) <= MAX_LINKS)
        {
            s_on[i] = true;
        }
    }
    if (!chosen)
    {
        persist();
    }

    /* The list may have been reordered: bring each open link to the position
     * of its printer, then close what has no printer, changed, or is off. */
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < CLOUD_MAX_PRINTERS; j++)
        {
            if (s_l[j].ctx && strcmp(s_l[j].id, s_p[i].id) == 0)
            {
                if (j != i)
                {
                    link_t tmp = s_l[i];
                    s_l[i] = s_l[j];
                    s_l[j] = tmp;
                }
                break;
            }
        }
    }
    for (int k = 0; k < CLOUD_MAX_PRINTERS; k++)
    {
        if (!s_l[k].ctx)
        {
            continue;
        }
        const bool keep = k < n && s_on[k] && strcmp(s_l[k].id, s_p[k].id) == 0 &&
                          s_l[k].fp == fingerprint(&s_p[k]);
        if (!keep)
        {
            ESP_LOGI(TAG, "closing the link of printer %d", k);
            stop_link(&s_l[k]);
        }
    }
    links_unlock();
}

bool links_enabled(int idx)
{
    return idx >= 0 && idx < *s_n && s_on[idx];
}

bool links_supported(int idx)
{
    return idx >= 0 && idx < *s_n && drivers_for(&s_p[idx]) != NULL;
}

bool links_set_enabled(int idx, bool on)
{
    bool granted = true;
    links_lock();
    if (!links_supported(idx))
    {
        granted = false;
    }
    else if (s_on[idx] != on)
    {
        if (on && (used_slots_if(idx) > BUDGET || on_count_if(idx) > MAX_LINKS))
        {
            ESP_LOGW(TAG, "a printer was refused: %d of %d load slots, %d of %d links", used_slots_if(idx), BUDGET,
                     on_count_if(idx), MAX_LINKS);
            granted = false;
        }
        else
        {
            s_on[idx] = on;
            if (!on)
            {
                stop_link(&s_l[idx]);
            }
            persist();
        }
    }
    links_unlock();
    return granted;
}

const char *links_brand(int idx)
{
    return idx >= 0 && idx < *s_n ? s_p[idx].brand : "";
}

int links_used_pct(void)
{
    links_lock();
    const int used = used_slots();
    links_unlock();
    return (used * 100 + BUDGET / 2) / BUDGET;
}

bool links_view(int idx, printer_view_t *v, bool *settled)
{
    bool linked = false;
    links_lock();
    if (idx >= 0 && idx < *s_n && s_l[idx].ctx && s_l[idx].view)
    {
        *v = *s_l[idx].view;
        if (settled)
        {
            *settled = v->online || (v->notice[0] && v->notice_error) ||
                       esp_timer_get_time() - s_l[idx].started_us > SETTLE_US;
        }
        linked = true;
    }
    links_unlock();
    return linked;
}

bool links_control(int idx, pv_control_t cmd)
{
    bool ok = false;
    /* Under the lock: the link is not closed while its connection is written to. */
    links_lock();
    if (idx >= 0 && idx < *s_n && s_l[idx].ctx && s_l[idx].drv && s_l[idx].drv->control)
    {
        ok = s_l[idx].drv->control(s_l[idx].ctx, cmd);
    }
    links_unlock();
    return ok;
}

static void open_link(int i)
{
    const account_printer_t *p = &s_p[i];
    link_t *l = &s_l[i];
    const printer_driver_t *drv = drivers_for(p);
    if (!drv)
    {
        return;
    }
    memset(l, 0, sizeof *l);
    l->started_us = esp_timer_get_time();
    l->view = heap_caps_calloc(1, sizeof *l->view, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *ctx = l->view ? drv->start(p) : NULL;
    if (!ctx)
    {
        free(l->view);
        memset(l, 0, sizeof *l);
        l->retry_at_us = esp_timer_get_time() + RETRY_US;
        ESP_LOGW(TAG, "could not open a %s link", p->brand);
        return;
    }
    l->drv = drv;
    l->ctx = ctx;
    l->fp = fingerprint(p);
    strlcpy(l->id, p->id, sizeof l->id);
    ESP_LOGI(TAG, "opened a %s link", p->brand);
}

static volatile bool s_net_ready;

void links_set_network_ready(bool ready)
{
    s_net_ready = ready;
}

void links_tick(const char *prefer_id)
{
    links_lock();
    const int n = *s_n;
    const int64_t now = esp_timer_get_time();

    int pick = -1;
    for (int i = 0; i < n; i++)
    {
        if (!s_net_ready || !s_on[i] || s_l[i].ctx || now < s_l[i].retry_at_us)
        {
            continue;
        }
        if (prefer_id && prefer_id[0] && strcmp(prefer_id, s_p[i].id) == 0)
        {
            pick = i;
            break;
        }
        if (pick < 0)
        {
            pick = i;
        }
    }
    const size_t free_now = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (pick >= 0)
    {
        if (free_now >= FLOOR_FREE)
        {
            open_link(pick); /* one at a time: a TLS handshake is a spike */
        }
        else
        {
            ESP_LOGW(TAG, "not enough internal RAM to open a %s link", s_p[pick].brand);
        }
    }

    /* The survival floor: the budget makes the choice visible, it does not
     * guard the heap. Below the floor for SUSTAIN_US, the last link opened goes
     * and its printer is switched off, rather than the device falling over. */
    static int64_t s_low_since;
    if (free_now >= FLOOR_FREE)
    {
        s_low_since = 0;
    }
    else if (!s_low_since)
    {
        s_low_since = now;
    }
    else if (now - s_low_since > SUSTAIN_US)
    {
        int last = -1;
        for (int i = 0; i < n; i++)
        {
            if (s_l[i].ctx && (last < 0 || s_l[i].started_us > s_l[last].started_us))
            {
                last = i;
            }
        }
        if (last >= 0)
        {
            ESP_LOGW(TAG, "internal RAM below %d KB for %lld s: closing the last %s link", FLOOR_FREE / 1024,
                     SUSTAIN_US / 1000000, s_p[last].brand);
            stop_link(&s_l[last]);
            s_on[last] = false;
            persist();
        }
        s_low_since = 0;
    }

    for (int i = 0; i < n; i++)
    {
        link_t *l = &s_l[i];
        if (!l->ctx || !l->view)
        {
            continue;
        }
        printer_view_t *v = l->view;
        strlcpy(v->name, s_p[i].name, sizeof v->name);
        v->notice[0] = '\0';
        v->notice_error = false;
        l->drv->snapshot(l->ctx, v);
    }
    links_unlock();
}
