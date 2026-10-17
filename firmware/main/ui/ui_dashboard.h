#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* One printer, as the dashboard shows it. Every data source (fake feed,
     * Creality WebSocket, ...) fills this struct and nothing else. */

    typedef enum
    {
        PV_IDLE = 0,
        PV_PRINTING,
        PV_PAUSED,
        PV_DONE,
        PV_ERROR,
    } pv_state_t;

    typedef struct
    {
        bool present;     /* a spool is loaded in this slot */
        uint32_t color;   /* 0xRRGGBB */
        int percent;      /* filament left, 0..100, -1 when unknown */
        char material[12]; /* "PLA", "PETG"...: what the dashboard shows */
        bool selected;    /* the slot the printer is feeding from */
    } pv_slot_t;

    /* One unit of filament feeding: a CFS, an AMS, an AMS HT... A printer can
     * carry several; the dashboard stacks up to PV_MAX_UNITS of them. */
#define PV_MAX_UNITS 4
    /* The label of the external spool holder: one slot, shown apart from the boxes. */
#define PV_LABEL_EXT "Ext."
    /* An AMS HT holds one spool: it sits to the right of the external spool, on its row. */
#define PV_LABEL_HT "AMS HT"
    typedef struct
    {
        bool present;
        char label[12];   /* "CFS", "CFS 2", "AMS A", "AMS HT" */
        int slots;        /* 1..4 */
        pv_slot_t slot[4];
    } pv_unit_t;

    /* One nozzle of a printer that has several (a dual-head Bambu, a Snapmaker U1,
     * a Creator 5 Pro). The dashboard lays its tiles out from `nozzle_count`:
     * one tile for a single nozzle, one per nozzle for the others. */
#define PV_MAX_NOZZLES 4
    typedef struct
    {
        char label[20];   /* "Left", "Nozzle 2"...: what the tile's caption says. 20 bytes:
                           * a Cyrillic letter is two bytes of UTF-8, so the Russian word
                           * for "right" alone takes twelve. */
        float temp;
        float target;
    } pv_nozzle_t;

    /* What a card or the full view can ask of a printer. */
    typedef enum
    {
        PV_CTL_PAUSE,
        PV_CTL_RESUME,
        PV_CTL_STOP,
        /* The printer's light. Outside the job commands: a driver tests the
         * range of the job ones with `cmd <= PV_CTL_STOP`. */
        PV_CTL_LIGHT_ON,
        PV_CTL_LIGHT_OFF,
    } pv_control_t;

    typedef struct
    {
        char name[32];
        char brand[12];   /* the account's brand id: its logo sits beside the name */
        pv_state_t state;
        int progress;     /* 0..100 */
        char file[48];
        int layer;
        int total_layer;
        int left_s;       /* seconds left, 0 when unknown */
        float nozzle;
        float nozzle_target;
        /* 0 or 1: one nozzle, read from `nozzle` / `nozzle_target` (the K2, the
         * P2S, every single-nozzle printer). 2..4: the printer says it has that
         * many, and `nozzles[]` carries them; `nozzle` then holds the active
         * one's reading so that anything that only knows one still works. */
        int nozzle_count;
        int nozzle_active; /* index in nozzles[], -1 when the printer does not say */
        pv_nozzle_t nozzles[PV_MAX_NOZZLES];
        float bed;
        float bed_target;
        float chamber;
        bool online;
        bool can_control; /* this connection takes pause / resume / stop */
        bool can_light;   /* this connection takes the light on / off command */
        bool light_on;    /* the printer says its light is on; false when it does not say */
        char notice[160]; /* why nothing is shown, when there is a reason */
        bool notice_error; /* red when it is a fault, muted when it is just waiting */
        pv_unit_t unit[PV_MAX_UNITS];
    } printer_view_t;

    /* Build the widgets under `parent`. Call on the LVGL thread. */
    void ui_dashboard_create(lv_obj_t *parent);

    /* Refresh the labels from `v`. The caller holds the LVGL lock. */
    void ui_dashboard_update(const printer_view_t *v);

    /* The printer selector at the top. Needs the LVGL lock. */
    void ui_dashboard_set_printer(const char *name, bool online);

    /* The words and colours the dashboard uses for a printer's state, and its
     * two time formats, shared with the printer cards. */
    const char *ui_state_text(pv_state_t s);
    void ui_state_colors(pv_state_t s, uint32_t *bg, uint32_t *fg);
    void ui_fmt_left(char *out, size_t n, int sec);
    /* The colour of a print's progress, the same on the ring, the card and the list
 * bar: blue while it prints, orange paused, green finished, red on a fault.
 * The dot beside the name says the connection; this says the print. */
lv_color_t ui_progress_color(const printer_view_t *v, bool live);
void ui_fmt_end(char *out, size_t n, int left_s);

    /* The screen is about to be cleared: forget every widget pointer. */
    void ui_dashboard_invalidate(void);

#ifdef __cplusplus
}
#endif
