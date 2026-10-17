#include "demo_mode.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_random.h"
#include "esp_timer.h"
#include "settings.h"

/* ---- The setting ------------------------------------------------------------ */

static int s_on = -1; /* -1 until read */

bool demo_mode_on(void)
{
    if (s_on < 0)
    {
        char v[4] = "";
        s_on = settings_get("demo", v, sizeof v) && v[0] == '1';
    }
    return s_on == 1;
}

void demo_mode_set(bool on)
{
    settings_set("demo", on ? "1" : "0"); /* applied by the restart that follows */
}

/* ---- The printers ------------------------------------------------------------ */

typedef enum
{
    U_NONE,
    U_AMS,     /* Bambu Lab: an AMS of four */
    U_CFS,     /* Creality */
    U_CANVAS,  /* Elegoo CC2 */
    U_ACE,     /* Anycubic */
    U_STATION, /* FlashForge AD5X: external spool + its station */
    U_TOOLS,   /* Snapmaker U1: one spool per tool head */
    U_AFC,     /* Klipper with a Box Turtle */
} demo_unit_t;

typedef struct
{
    const char *brand;
    const char *name;
    int nozzles;      /* 1, or 4 for the U1 */
    demo_unit_t unit;
    bool light;       /* the driver of that brand has a light command */
    bool chamber;     /* a chamber sensor */
    pv_state_t state; /* how it starts: most print */
    int progress;     /* where it starts */
    const char *file;
} demo_printer_t;

static const demo_printer_t DEMO[] = {
    {"bambulab", "X1 Carbon", 1, U_AMS, true, true, PV_PRINTING, 42, "Benchy"},
    {"creality", "K2 Plus", 1, U_CFS, true, true, PV_PRINTING, 67, "Gridfinity bin"},
    {"elegoo", "Centauri Carbon 2", 1, U_CANVAS, true, true, PV_PRINTING, 18, "Spool holder"},
    {"anycubic", "Kobra S1", 1, U_ACE, true, false, PV_PRINTING, 88, "Phone stand"},
    {"snapmaker", "U1", 4, U_TOOLS, true, false, PV_PRINTING, 55, "Four-colour vase"},
    {"flashforge", "AD5X", 1, U_STATION, true, false, PV_PAUSED, 31, "Cable clips"},
    {"prusa", "Core One", 1, U_NONE, false, true, PV_PRINTING, 74, "Lamp shade"},
    {"klipper", "Voron 2.4", 1, U_AFC, true, true, PV_PRINTING, 9, "Gear set"},
    {"bambulab", "P2S", 1, U_AMS, true, true, PV_PRINTING, 95, "Planter"},
    {"octoprint", "Ender-3 S1", 1, U_NONE, false, false, PV_PRINTING, 61, "Wall bracket"},
};
#define DEMO_N ((int)(sizeof DEMO / sizeof DEMO[0]))

int demo_mode_printers(account_printer_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < DEMO_N && n < max; i++, n++)
    {
        memset(&out[n], 0, sizeof out[n]);
        strlcpy(out[n].brand, DEMO[i].brand, sizeof out[n].brand);
        snprintf(out[n].id, sizeof out[n].id, "demo-%d", i);
        strlcpy(out[n].name, DEMO[i].name, sizeof out[n].name);
    }
    return n;
}

/* ---- Filament: Tiger Studio's palette, drawn anew at each start --------------- */

static const uint32_t PALETTE[] = {
    0x000000, 0x6E6E6E, 0xC4C4C4, 0xFFFFFF, 0x1A4E6E, 0xA9B6C1, 0x3DA4F0, 0x2226E6,
    0x2ED89A, 0x1A6E4A, 0x1EA51E, 0xBDE680, 0xFFE00A, 0xFF8A2A, 0x5C3A18, 0xA97430,
    0xEFE2CF, 0xCDC09A, 0xE8001C, 0xE8476E, 0xFF00FF, 0xC192D2, 0x7B00CC, 0x3D0B85,
};
static const char *const MATERIAL[] = {"PLA", "PLA", "PLA", "PETG", "PETG", "ABS", "ASA", "TPU", "PLA+"};

static void random_slot(pv_slot_t *s, bool selected)
{
    s->present = true;
    s->color = PALETTE[esp_random() % (sizeof PALETTE / sizeof PALETTE[0])];
    s->percent = 15 + (int)(esp_random() % 86);
    strlcpy(s->material, MATERIAL[esp_random() % (sizeof MATERIAL / sizeof MATERIAL[0])], sizeof s->material);
    s->selected = selected;
}

static void fill_unit(pv_unit_t *u, const char *label, int slots, int active)
{
    u->present = true;
    strlcpy(u->label, label, sizeof u->label);
    u->slots = slots;
    for (int i = 0; i < slots; i++)
    {
        random_slot(&u->slot[i], i == active);
    }
}

/* ---- The driver: a print that moves ------------------------------------------- */

#define PCT_EVERY_US (20LL * 1000 * 1000) /* one per cent every 20 s */
#define DONE_FOR_US (40LL * 1000 * 1000)  /* a finished print shows for 40 s, then the next starts */
#define LAYERS 240

typedef struct
{
    int k;              /* index in DEMO[] */
    pv_state_t state;
    int64_t base_us;    /* when `base_pct` was true */
    int base_pct;
    int64_t done_us;    /* when it reached 100 */
    bool light;
    int active;         /* the tool head (U1) or slot at work */
    pv_unit_t unit[PV_MAX_UNITS];
} demo_ctx_t;

static drv_kind_t demo_support(const account_printer_t *p)
{
    (void)p;
    return DRV_LAN;
}

static void *demo_start(const account_printer_t *p)
{
    demo_ctx_t *c = calloc(1, sizeof *c);
    if (!c)
    {
        return NULL;
    }
    c->k = atoi(p->id + 5) % DEMO_N; /* "demo-N" */
    const demo_printer_t *d = &DEMO[c->k];
    c->state = d->state;
    c->base_pct = d->progress;
    c->base_us = esp_timer_get_time();
    c->light = d->light && (esp_random() & 1);
    c->active = (int)(esp_random() % 4);
    switch (d->unit)
    {
    case U_AMS:
        fill_unit(&c->unit[0], "AMS A", 4, c->active);
        break;
    case U_CFS:
        fill_unit(&c->unit[0], "CFS", 4, c->active);
        break;
    case U_CANVAS:
        fill_unit(&c->unit[0], "Canvas", 4, c->active);
        break;
    case U_ACE:
        fill_unit(&c->unit[0], "ACE", 4, c->active);
        break;
    case U_STATION:
        fill_unit(&c->unit[0], PV_LABEL_EXT, 1, -1);
        fill_unit(&c->unit[1], "Station", 4, c->active);
        break;
    case U_TOOLS:
        fill_unit(&c->unit[0], "E1-E4", 4, c->active);
        break;
    case U_AFC:
        fill_unit(&c->unit[0], "AFC", 4, c->active);
        break;
    default:
        break;
    }
    return c;
}

static void demo_stop(void *ctx)
{
    free(ctx);
}

static int progress_now(demo_ctx_t *c, int64_t now)
{
    if (c->state != PV_PRINTING)
    {
        return c->base_pct;
    }
    const int p = c->base_pct + (int)((now - c->base_us) / PCT_EVERY_US);
    if (p >= 100)
    {
        c->state = PV_DONE; /* finished: shows as done for a while */
        c->base_pct = 100;
        c->done_us = now;
        return 100;
    }
    return p;
}

/* A temperature that sits on its set point the way a real one does: a few tenths
 * either side, slowly. */
static float near(float target, int64_t now, int seed)
{
    return target + 0.6f * sinf((float)(now / 1000) / 2300.0f + (float)seed);
}

static void demo_snapshot(void *ctx, printer_view_t *v)
{
    demo_ctx_t *c = ctx;
    const demo_printer_t *d = &DEMO[c->k];
    const int64_t now = esp_timer_get_time();

    if (c->state == PV_DONE && now - c->done_us > DONE_FOR_US)
    {
        c->state = PV_PRINTING; /* the next print starts */
        c->base_pct = 0;
        c->base_us = now;
    }
    const int pct = progress_now(c, now);
    const bool busy = c->state == PV_PRINTING || c->state == PV_PAUSED;

    v->online = true;
    v->state = c->state;
    v->progress = c->state == PV_IDLE ? 0 : pct;
    strlcpy(v->file, c->state == PV_IDLE ? "" : d->file, sizeof v->file);
    v->total_layer = busy || c->state == PV_DONE ? LAYERS : 0;
    v->layer = v->total_layer ? pct * LAYERS / 100 : 0;
    /* The time left runs down at the clock's pace, at the same rate as the
     * progress, so that the estimated end (now + time left) stays where it is. */
    int64_t left_us = (int64_t)(100 - c->base_pct) * PCT_EVERY_US;
    if (c->state == PV_PRINTING)
    {
        left_us -= now - c->base_us;
    }
    v->left_s = busy && left_us > 0 ? (int)(left_us / 1000000) : 0;

    const float nt = busy ? 220.0f : 0.0f, bt = busy ? 60.0f : 0.0f;
    v->nozzle_target = nt;
    v->nozzle = busy ? near(nt, now, c->k) : 24.0f + (float)(c->k % 3);
    v->bed_target = bt;
    v->bed = busy ? near(bt, now, c->k + 7) : 23.0f + (float)(c->k % 2);
    v->chamber = d->chamber ? (busy ? 34.0f + (float)(c->k % 3) : 25.0f) : 0.0f;
    v->nozzle_count = d->nozzles;
    v->nozzle_active = d->nozzles > 1 ? c->active : -1;
    if (d->nozzles > 1)
    {
        for (int i = 0; i < d->nozzles && i < PV_MAX_NOZZLES; i++)
        {
            const bool at_work = busy && i == c->active;
            v->nozzles[i].target = at_work ? nt : 0.0f;
            v->nozzles[i].temp = at_work ? v->nozzle : 38.0f + (float)i;
            v->nozzles[i].label[0] = '\0';
        }
    }
    v->can_control = true;
    v->can_light = d->light;
    v->light_on = d->light && c->light;
    v->notice[0] = '\0';
    v->notice_error = false;
    memcpy(v->unit, c->unit, sizeof v->unit);
}

/* Commands change only the picture: nothing leaves the device. */
static bool demo_control(void *ctx, pv_control_t cmd)
{
    demo_ctx_t *c = ctx;
    const int64_t now = esp_timer_get_time();
    switch (cmd)
    {
    case PV_CTL_PAUSE:
        if (c->state == PV_PRINTING)
        {
            c->base_pct = progress_now(c, now);
            c->state = PV_PAUSED;
        }
        return true;
    case PV_CTL_RESUME:
        if (c->state == PV_PAUSED)
        {
            c->state = PV_PRINTING;
            c->base_us = now;
        }
        return true;
    case PV_CTL_STOP:
        c->state = PV_IDLE;
        c->base_pct = 0;
        return true;
    case PV_CTL_LIGHT_ON:
    case PV_CTL_LIGHT_OFF:
        c->light = cmd == PV_CTL_LIGHT_ON;
        return true;
    }
    return false;
}

const printer_driver_t DRIVER_DEMO = {
    .brand = "demo",
    .support = demo_support,
    .start = demo_start,
    .stop = demo_stop,
    .snapshot = demo_snapshot,
    .control = demo_control,
};
