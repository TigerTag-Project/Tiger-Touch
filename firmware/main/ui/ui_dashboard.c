#include "ui_dashboard.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"

#include "brand_logos.h"
#include "pictos.h"
#include "ui_setup.h"
#include "ui_job.h"
#include "ui_theme.h"
#include "ui_tile.h"
#include "ui_wifi.h"

typedef struct
{
    lv_obj_t *dot;
    lv_obj_t *pct;
} slot_widgets_t;

static bool s_ready;
static lv_obj_t *s_chip_dot;
static lv_obj_t *s_chip_name;
static lv_obj_t *s_notice;
static job_buttons_t s_job;
static lv_obj_t *s_card;

static lv_obj_t *s_name;
static lv_obj_t *s_logo;
static char s_logo_brand[12];
static lv_obj_t *s_chip;
static lv_obj_t *s_chip_lbl;
static lv_obj_t *s_chip_led; /* green online, red offline */
static lv_obj_t *s_arc;
static lv_obj_t *s_pct;
static lv_obj_t *s_layer;
static lv_obj_t *s_file;
static lv_obj_t *s_left;
static lv_obj_t *s_end;
static lv_obj_t *s_nozzle;
static lv_obj_t *s_bed;
static lv_obj_t *s_chamber;

/* The temperature tiles: up to four nozzles, the bed, the chamber. They are
 * laid out again whenever the printer's number of nozzles changes; the rest of
 * the card never moves. */
typedef ui_tile_t temp_tile_t;

/* The icon of a caption, and the gap between it and the text. */
#define LBL_ICO 16
#define LBL_GAP 6

static temp_tile_t s_noz[PV_MAX_NOZZLES];
static temp_tile_t s_bed_tile;
static temp_tile_t s_chamber_tile;
static int s_noz_layout = -1; /* how many nozzles the tiles are laid out for */
typedef struct
{
    lv_obj_t *card;
    lv_obj_t *name;
    int small; /* the title's layout: 1 for a small unit, 0 for a box (how it is built) */
    slot_widgets_t slot[4];
} unit_widgets_t;

static unit_widgets_t s_unit[PV_MAX_UNITS];

/* The filament units have a card of their own, beside the printer's: 240 wide, 8 px
 * of room around the units. */
#define SLOTS_X 544
#define SLOTS_W 240
#define UNIT_W 224     /* a CFS / AMS card */
#define UNIT_W_EXT 70  /* the external spool, an AMS HT: one slot; three fit on a row */
/* The title of a small unit is centred over the dot, in what is left of the card
 * once its frame and this much room at each side are taken: "AMS HT" is about 58
 * px wide in 70, and at the box's usual 14 px from the edge it touched the other
 * side. */
#define UNIT_TITLE_PAD 3
#define UNIT_GAP 7     /* between two small cards on a row */
#define UNIT_ROW 98    /* a row of cards, and the gap under it: four rows fit the card */
#define UNITS_X (SLOTS_X + 8)
#define UNITS_Y 72

static lv_obj_t *s_slots;     /* the units' card; shown only when the printer has units */
static int s_slots_shown = -1;

static lv_obj_t *mk_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, C_CARD, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, C_LINE, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 12, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                          int x, int y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

/* One nozzle: the nozzle, the bed and the chamber on one row of three. Two: a
 * row of four, the nozzles with their caption (a pictogram cannot say left from
 * right). Three or four: the nozzles on a first row and the bed and the chamber
 * on a second. Each tile centres its pictogram and its value together. */
static void layout_temps(int n)
{
    s_noz_layout = n;
    for (int i = 0; i < PV_MAX_NOZZLES; i++)
    {
        lv_obj_set_hidden(s_noz[i].tile, true);
    }
    if (n <= 1)
    {
        ui_tile_place(&s_noz[0], 20, 312, 150, 72, &font_ui_20, false);
        ui_tile_place(&s_bed_tile, 185, 312, 150, 72, &font_ui_20, false);
        ui_tile_place(&s_chamber_tile, 350, 312, 150, 72, &font_ui_20, false);
    }
    else if (n == 2)
    {
        ui_tile_place(&s_noz[0], 20, 312, 114, 72, &font_ui_16, true);
        ui_tile_place(&s_noz[1], 142, 312, 114, 72, &font_ui_16, true);
        ui_tile_place(&s_bed_tile, 264, 312, 114, 72, &font_ui_16, false);
        ui_tile_place(&s_chamber_tile, 386, 312, 114, 72, &font_ui_16, false);
    }
    else
    {
        const int w = (480 - 8 * (n - 1)) / n;
        for (int i = 0; i < n; i++)
        {
            ui_tile_place(&s_noz[i], 20 + i * (w + 8), 296, w, 44, &font_ui_16, false);
        }
        ui_tile_place(&s_bed_tile, 20, 346, 236, 44, &font_ui_16, false);
        ui_tile_place(&s_chamber_tile, 264, 346, 236, 44, &font_ui_16, false);
    }
}

void ui_dashboard_invalidate(void)
{
    s_ready = false;
    ui_wifi_forget_persistent(); /* its wave and account icon go with the screen */
    s_chip_dot = s_chip_name = s_notice = NULL;
}

void ui_dashboard_set_printer(const char *name, bool online)
{
    if (!s_ready || !s_chip_name)
    {
        return;
    }
    lv_label_set_text(s_chip_name, name);
    lv_obj_set_style_bg_color(s_chip_dot, online ? C_GREEN : C_MUTED, 0);
}

static void on_back_to_list(lv_event_t *e)
{
    (void)e;
    ui_post(EV_PRINTERS, 0, NULL, NULL);
}

void ui_dashboard_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, C_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(parent, false);

    /* Top bar. */
    /* Back to the list of printers, as on the screens of the TigerScale. */
    lv_obj_t *back = lv_button_create(parent);
    lv_obj_set_size(back, 72, 60); /* a bare chevron, as on TigerScale; the finger target stays large */
    lv_obj_set_pos(back, 4, 0);
    ui_kit_on_header(back);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(bl, C_TEXT, 0);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, on_back_to_list, LV_EVENT_CLICKED, NULL);

    /* Wi-Fi signal, then the account at the right edge. There is no clock here:
     * the time of day is on the card only as the estimated end of the print. */
    lv_obj_t *wave = ui_wifi_wave(parent);
    lv_obj_align(wave, LV_ALIGN_TOP_RIGHT, -48, 0);
    ui_kit_on_header(wave);
    ui_wifi_set_persistent(wave);
    lv_obj_t *acct = ui_account_icon(parent);
    lv_obj_align(acct, LV_ALIGN_TOP_RIGHT, -16, 0);
    ui_kit_on_header(acct);
    ui_account_set_persistent(acct);

    /* Main printer card. */
    /* Two cards side by side: the printer, and its filament units (see below). */
    lv_obj_t *card = mk_card(parent, 16, HEADER_H, 520, 400);
    s_card = card;
    s_name = mk_label(card, &font_ui_24, C_TEXT, 20, 14, "--");
    lv_obj_set_width(s_name, 270); /* up to the first of the three buttons */
    /* One line: without a height, LONG_DOT wraps a long name over the notice. */
    lv_obj_set_height(s_name, lv_font_get_line_height(&font_ui_24));
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_DOT);
    /* The brand's logo, to the left of the name; painted in the text colour. */
    s_logo = lv_image_create(card);
    lv_obj_set_pos(s_logo, 20, 12);
    lv_obj_set_style_image_recolor(s_logo, C_TEXT, 0);
    lv_obj_set_style_image_recolor_opa(s_logo, LV_OPA_COVER, 0);
    lv_obj_set_hidden(s_logo, true);
    s_logo_brand[0] = '\0';
    s_notice = mk_label(card, &font_ui_16, C_RED, 20, 56, "");
    lv_obj_set_width(s_notice, 480);
    lv_label_set_long_mode(s_notice, LV_LABEL_LONG_DOT);

    /* The state (a dot and a word) is in the top bar, at the right, left of the
     * Wi-Fi wave: the card's top right is for the three buttons. 144 wide:
     * "Imprimiendo" and "A imprimir" must not touch the dot. */
    ui_kit_clock(parent); /* between the back arrow and the state */
    s_chip = lv_obj_create(parent);
    lv_obj_set_size(s_chip, 144, 32);
    lv_obj_align(s_chip, LV_ALIGN_TOP_RIGHT, -92, HEADER_MID - 16);
    lv_obj_set_style_radius(s_chip, 16, 0);
    lv_obj_set_style_border_width(s_chip, 0, 0);
    lv_obj_set_style_pad_all(s_chip, 0, 0);
    lv_obj_set_scrollable(s_chip, false);
    s_chip_lbl = lv_label_create(s_chip);
    lv_obj_set_style_text_font(s_chip_lbl, &font_ui_14, 0);
    lv_obj_align(s_chip_lbl, LV_ALIGN_CENTER, 8, 0);
    s_chip_led = lv_obj_create(s_chip);
    lv_obj_set_size(s_chip_led, 10, 10);
    lv_obj_align(s_chip_led, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_set_style_radius(s_chip_led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_chip_led, 0, 0);
    lv_obj_set_style_pad_all(s_chip_led, 0, 0);
    lv_obj_set_style_bg_color(s_chip_led, C_MUTED, 0);
    lv_obj_set_scrollable(s_chip_led, false);
    lv_obj_set_clickable(s_chip_led, false);

    /* The light, pause / resume and stop, in a row at the top right of the card, as
     * big as the printer's logo on the left. Always drawn; the pair acts only while
     * a job runs on a connection that takes commands. Stop is a hold of 1.5 s. */
    ui_job_build(&s_job, card, 396, 6, 452, 6, 48, 48, 340, 6, 48, 48, 0);

    s_arc = lv_arc_create(card);
    lv_obj_set_size(s_arc, 170, 170);
    lv_obj_set_pos(s_arc, 30, 76);
    lv_arc_set_range(s_arc, 0, 100);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(s_arc, false);
    lv_obj_set_style_arc_width(s_arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, C_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);
    /* A new LVGL arc is drawn half full until it is given a value. */
    lv_arc_set_value(s_arc, 0);

    s_pct = mk_label(card, &lv_font_montserrat_32, C_TEXT, 0, 0, "0 %");
    lv_obj_align_to(s_pct, s_arc, LV_ALIGN_CENTER, 0, 0);

    char layer0[32];
    snprintf(layer0, sizeof layer0, tr(S_LAYER), 0, 0);
    s_layer = mk_label(card, &font_ui_14, C_MUTED, 30, 252, layer0);
    lv_obj_set_width(s_layer, 170);
    lv_obj_set_style_text_align(s_layer, LV_TEXT_ALIGN_CENTER, 0);

    /* Each caption has the pictogram of what it names in front of it. */
    lv_obj_set_pos(picto_create(card, PICTO_FILE, LBL_ICO, C_MUTED), 236, 101);
    mk_label(card, &font_ui_14, C_MUTED, 236 + LBL_ICO + LBL_GAP, 100, tr(S_FILE));
    s_file = mk_label(card, &font_ui_20, C_TEXT, 236, 124, "--");
    lv_obj_set_width(s_file, 264);
    lv_label_set_long_mode(s_file, LV_LABEL_LONG_DOT);

    lv_obj_set_pos(picto_create(card, PICTO_CLOCK, LBL_ICO, C_MUTED), 236, 177);
    mk_label(card, &font_ui_14, C_MUTED, 236 + LBL_ICO + LBL_GAP, 176, tr(S_TIME_LEFT));
    s_left = mk_label(card, &lv_font_montserrat_24, C_TEXT, 236, 200, "--");
    /* x = 380: the German, Portuguese and Polish captions for the estimated end
     * are the longest; with the pictogram they still end before the units. */
    lv_obj_set_pos(picto_create(card, PICTO_FLAG, LBL_ICO, C_MUTED), 380, 177);
    mk_label(card, &font_ui_14, C_MUTED, 380 + LBL_ICO + LBL_GAP, 176, tr(S_END_ESTIMATED));
    s_end = mk_label(card, &lv_font_montserrat_24, C_TEXT, 380, 200, "--:--");

    /* Four nozzle tiles are built, one shown for a single-nozzle printer; the
     * layout follows what the printer reports (see layout_temps). */
    for (int i = 0; i < PV_MAX_NOZZLES; i++)
    {
        ui_tile_build(&s_noz[i], card, PICTO_NOZZLE, C_TILE, 10);
    }
    ui_tile_build(&s_bed_tile, card, PICTO_BED, C_TILE, 10);
    ui_tile_build(&s_chamber_tile, card, PICTO_CHAMBER, C_TILE, 10);
    s_nozzle = s_noz[0].val;
    s_bed = s_bed_tile.val;
    s_chamber = s_chamber_tile.val;
    layout_temps(1);

    /* The units' own card, to the right of the printer's, and on it one small card
     * per CFS / AMS, up to four. Cards that the printer does not have stay hidden,
     * and the units' card with them when there is none. */
    s_slots = mk_card(parent, SLOTS_X, 64, SLOTS_W, 400);
    lv_obj_set_hidden(s_slots, true);
    s_slots_shown = 0;
    for (int u = 0; u < PV_MAX_UNITS; u++)
    {
        unit_widgets_t *w = &s_unit[u];
        w->card = mk_card(parent, UNITS_X, UNITS_Y + u * UNIT_ROW, UNIT_W, 94);
        lv_obj_set_style_bg_color(w->card, C_TILE, 0); /* on the units' card: one step lighter */
        w->name = mk_label(w->card, &font_ui_14, C_MUTED, 14, 7, "--");
        w->small = 0; /* the page is built again on a language change: the title is new, as built */
        for (int i = 0; i < 4; i++)
        {
            const int x = 14 + i * 54;
            w->slot[i].dot = lv_obj_create(w->card);
            lv_obj_set_pos(w->slot[i].dot, x + 6, 30);
            lv_obj_set_size(w->slot[i].dot, 30, 30);
            lv_obj_set_style_radius(w->slot[i].dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(w->slot[i].dot, 0, 0);
            lv_obj_set_style_pad_all(w->slot[i].dot, 0, 0);
            lv_obj_set_scrollable(w->slot[i].dot, false);
            w->slot[i].pct = mk_label(w->card, &font_ui_14, C_MUTED, x - 4, 64, "--");
            lv_obj_set_width(w->slot[i].pct, 52);
            /* One line: without a height, LONG_DOT wraps "PLA High" under the slot. */
            lv_obj_set_height(w->slot[i].pct, lv_font_get_line_height(&font_ui_14));
            lv_label_set_long_mode(w->slot[i].pct, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_align(w->slot[i].pct, LV_TEXT_ALIGN_CENTER, 0);
        }
        lv_obj_set_hidden(w->card, true);
    }

    s_ready = true;
}

const char *ui_state_text(pv_state_t s)
{
    switch (s)
    {
    case PV_PRINTING: return tr(S_ST_PRINTING);
    case PV_PAUSED:   return tr(S_ST_PAUSED);
    case PV_DONE:     return tr(S_ST_DONE);
    case PV_ERROR:    return tr(S_ST_ERROR);
    default:          return tr(S_ST_IDLE);
    }
}

void ui_state_colors(pv_state_t s, uint32_t *bg, uint32_t *fg)
{
    switch (s)
    {
    case PV_PRINTING: *bg = 0x1f3d31; *fg = 0x3ecf8e; break;
    case PV_PAUSED:   *bg = 0x4a3a16; *fg = 0xf5b84a; break;
    case PV_DONE:     *bg = 0x1c3550; *fg = 0x6ab0f0; break;
    case PV_ERROR:    *bg = 0x4a1f1f; *fg = 0xf06a6a; break;
    default:          *bg = 0x2b2f38; *fg = 0x8b909a; break;
    }
}

void ui_fmt_left(char *out, size_t n, int sec)
{
    if (sec <= 0)
    {
        snprintf(out, n, "--");
    }
    else if (sec >= 3600)
    {
        snprintf(out, n, "%d h %02d min", sec / 3600, (sec % 3600) / 60);
    }
    else
    {
        snprintf(out, n, "%d min", (sec + 59) / 60);
    }
}

/* The local time at which the print will end, "--:--" when it is not known. */
lv_color_t ui_progress_color(const printer_view_t *v, bool live)
{
    if (live && v->state == PV_PAUSED)
    {
        return C_ACCENT;
    }
    if (live && v->state == PV_ERROR)
    {
        return C_RED;
    }
    if (live && (v->state == PV_DONE || v->progress >= 100))
    {
        return C_GREEN;
    }
    return C_BLUE;
}

void ui_fmt_end(char *out, size_t n, int left_s)
{
    snprintf(out, n, "--:--");
    const time_t now = time(NULL);
    if (left_s > 0 && now > 1700000000)
    {
        const time_t t = now + left_s;
        struct tm lt;
        localtime_r(&t, &lt);
        strftime(out, n, "%H:%M", &lt);
    }
}

static void set_temp(lv_obj_t *label, float actual, float target, bool show_target)
{
    char buf[24];
    if (show_target)
    {
        snprintf(buf, sizeof buf, "%d / %d" "\xC2\xB0", (int)(actual + 0.5f), (int)(target + 0.5f));
    }
    else
    {
        snprintf(buf, sizeof buf, "%d" "\xC2\xB0", (int)(actual + 0.5f));
    }
    lv_label_set_text(label, buf);
}

static void set_temp_live(lv_obj_t *label, bool live, float actual, float target, bool show_target)
{
    if (live)
    {
        set_temp(label, actual, target, show_target);
    }
    else
    {
        lv_label_set_text(label, "--");
    }
}

void ui_dashboard_update(const printer_view_t *v)
{
    if (!s_ready || !v)
    {
        return;
    }
    char buf[48];

    lv_label_set_text(s_name, v->name);
    if (strcmp(s_logo_brand, v->brand) != 0)
    {
        strlcpy(s_logo_brand, v->brand, sizeof s_logo_brand);
        const lv_image_dsc_t *logo = brand_logo(v->brand);
        if (logo)
        {
            lv_image_set_src(s_logo, logo);
            lv_obj_set_hidden(s_logo, false);
            lv_obj_set_x(s_name, 62);
        }
        else
        {
            lv_obj_set_hidden(s_logo, true);
            lv_obj_set_x(s_name, 20);
        }
    }

    uint32_t bg, fg;
    ui_state_colors(v->state, &bg, &fg);
    lv_label_set_text(s_chip_lbl, v->online ? ui_state_text(v->state) : tr(S_OFFLINE));
    lv_obj_set_style_bg_color(s_chip, lv_color_hex(v->online ? bg : 0x2b2f38), 0);
    lv_obj_set_style_text_color(s_chip_lbl, lv_color_hex(v->online ? fg : 0x8b909a), 0);
    lv_obj_align(s_chip_lbl, LV_ALIGN_CENTER, 8, 0);
    lv_obj_set_style_bg_color(s_chip_led, v->online ? C_GREEN : C_RED, 0);
    ui_job_set(&s_job, v->online && v->can_control && (v->state == PV_PRINTING || v->state == PV_PAUSED),
               v->online && v->state == PV_PAUSED);
    ui_job_set_light(&s_job, v->online && v->can_light, v->light_on);

    /* A printer that is not reached shows "--", as its card does, not the last
     * values it gave. */
    const bool live = v->online;
    lv_arc_set_value(s_arc, live ? v->progress : 0);
    lv_obj_set_style_arc_color(s_arc, ui_progress_color(v, live), LV_PART_INDICATOR);
    snprintf(buf, sizeof buf, "%d %%", v->progress);
    lv_label_set_text(s_pct, live ? buf : "--");
    lv_obj_align_to(s_pct, s_arc, LV_ALIGN_CENTER, 0, 0);

    snprintf(buf, sizeof buf, tr(S_LAYER), v->layer, v->total_layer);
    lv_label_set_text(s_layer, live && v->total_layer > 0 ? buf : ""); /* nothing when the printer does not count them */
    lv_label_set_text(s_file, live && v->file[0] ? v->file : "--");
    lv_label_set_text(s_notice, v->notice);
    lv_obj_set_style_text_color(s_notice, v->notice_error ? C_RED : C_MUTED, 0);

    ui_fmt_left(buf, sizeof buf, live ? v->left_s : 0);
    lv_label_set_text(s_left, buf);

    char end[8];
    ui_fmt_end(end, sizeof end, live ? v->left_s : 0);
    lv_label_set_text(s_end, end);

    /* The tiles follow the printer's own count of nozzles. */
    int nn = v->nozzle_count < 1 ? 1 : (v->nozzle_count > PV_MAX_NOZZLES ? PV_MAX_NOZZLES : v->nozzle_count);
    if (nn != s_noz_layout)
    {
        layout_temps(nn);
    }
    if (nn == 1)
    {
        set_temp_live(s_nozzle, live, v->nozzle, v->nozzle_target, true);
        lv_obj_set_style_border_width(s_noz[0].tile, 0, 0);
        lv_obj_set_style_image_recolor(s_noz[0].ico, C_MUTED, 0);
        lv_obj_set_style_text_color(s_noz[0].val, C_TEXT, 0);
    }
    else
    {
        for (int i = 0; i < nn; i++)
        {
            const pv_nozzle_t *z = &v->nozzles[i];
            const bool active = i == v->nozzle_active;
            const bool known = v->nozzle_active >= 0;
            if (nn == 2)
            {
                /* The caption (Left, Right) shows when the printer names its nozzles. */
                lv_label_set_text(s_noz[i].cap, z->label);
                lv_obj_set_hidden(s_noz[i].cap, z->label[0] == 0);
            }
            set_temp_live(s_noz[i].val, live, z->temp, z->target, true);
            /* The one at work is ringed; the others step back. */
            lv_obj_set_style_border_width(s_noz[i].tile, active ? 2 : 0, 0);
            lv_obj_set_style_image_recolor(s_noz[i].ico, active ? C_ACCENT : C_MUTED, 0);
            lv_obj_set_style_text_color(s_noz[i].cap, active ? C_ACCENT : C_MUTED, 0);
            lv_obj_set_style_text_color(s_noz[i].val, (!known || active) ? C_TEXT : C_MUTED, 0);
        }
    }
    set_temp_live(s_bed, live, v->bed, v->bed_target, true);
    if (live && v->chamber > 0.0f)
    {
        set_temp(s_chamber, v->chamber, 0, false);
    }
    else
    {
        lv_label_set_text(s_chamber, "--"); /* no chamber sensor, as on the card */
    }

    /* The external spool goes first, then the AMS HT (one spool each), which sit
     * to its right on the same row while there is room; the boxes follow, one
     * to a row. The cards are filled in that display order. */
    int order[PV_MAX_UNITS], n_order = 0;
    for (int pass = 0; pass < 3; pass++)
    {
        for (int u = 0; u < PV_MAX_UNITS; u++)
        {
            if (!v->unit[u].present)
            {
                continue;
            }
            const int kind = strcmp(v->unit[u].label, PV_LABEL_EXT) == 0 ? 0
                             : strcmp(v->unit[u].label, PV_LABEL_HT) == 0 ? 1 : 2;
            if (kind == pass)
            {
                order[n_order++] = u;
            }
        }
    }

    const int has_units = n_order > 0;
    if (has_units != s_slots_shown)
    {
        s_slots_shown = has_units;
        lv_obj_set_hidden(s_slots, !has_units);
    }

    int cx = UNITS_X, cy = UNITS_Y;
    for (int k = 0; k < PV_MAX_UNITS; k++)
    {
        unit_widgets_t *w = &s_unit[k];
        if (k >= n_order)
        {
            lv_obj_set_hidden(w->card, true);
            continue;
        }
        const pv_unit_t *pu = &v->unit[order[k]];
        const bool ext = strcmp(pu->label, PV_LABEL_EXT) == 0;
        const bool small = ext || (strcmp(pu->label, PV_LABEL_HT) == 0 && pu->slots <= 1);
        const int uw = small ? UNIT_W_EXT : UNIT_W;
        if (cx != UNITS_X && cx + uw > UNITS_X + UNIT_W)
        {
            cx = UNITS_X;
            cy += UNIT_ROW;
        }
        lv_obj_set_pos(w->card, cx, cy);
        lv_obj_set_width(w->card, uw);
        cx += uw + UNIT_GAP;
        if (!small)
        {
            cx = UNITS_X;
            cy += UNIT_ROW;
        }
        lv_obj_set_hidden(w->card, false);
        if (w->small != small)
        {
            /* Set when the kind of card changes, not every second: a style set
             * redraws the label. */
            w->small = small;
            if (small)
            {
                lv_obj_set_x(w->name, UNIT_TITLE_PAD);
                lv_obj_set_size(w->name, UNIT_W_EXT - 2 * UNIT_TITLE_PAD, lv_font_get_line_height(&font_ui_14));
                lv_label_set_long_mode(w->name, LV_LABEL_LONG_DOT);
                lv_obj_set_style_text_align(w->name, LV_TEXT_ALIGN_CENTER, 0);
            }
            else
            {
                lv_obj_set_x(w->name, 14);
                lv_obj_set_size(w->name, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                lv_label_set_long_mode(w->name, LV_LABEL_LONG_WRAP);
                lv_obj_set_style_text_align(w->name, LV_TEXT_ALIGN_LEFT, 0);
            }
        }
        lv_label_set_text(w->name, ext ? tr(S_EXT) : pu->label);
        for (int i = 0; i < 4; i++)
        {
            const bool exists = i < (pu->slots > 0 ? pu->slots : 4);
            if (!exists)
            {
                lv_obj_set_hidden(w->slot[i].dot, true);
                lv_obj_set_hidden(w->slot[i].pct, true);
                continue;
            }
            lv_obj_set_hidden(w->slot[i].dot, false);
            lv_obj_set_hidden(w->slot[i].pct, false);
            const pv_slot_t *s = &pu->slot[i];
            lv_obj_t *dot = w->slot[i].dot;
            if (s->present)
            {
                lv_obj_set_style_bg_color(dot, lv_color_hex(s->color), 0);
                lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
                /* The material, not the percentage: it is what tells two
                 * similar spools apart at a glance. */
                snprintf(buf, sizeof buf, "%s", s->material[0] ? s->material : "--");
            }
            else
            {
                lv_obj_set_style_bg_color(dot, C_LINE, 0);
                snprintf(buf, sizeof buf, "--");
            }
            lv_obj_set_style_border_width(dot, s->selected ? 3 : 0, 0);
            lv_obj_set_style_border_color(dot, C_ACCENT, 0);
            lv_label_set_text(w->slot[i].pct, buf);
            lv_obj_set_style_text_color(w->slot[i].pct, s->selected ? C_TEXT : C_MUTED, 0);
        }
    }
}
