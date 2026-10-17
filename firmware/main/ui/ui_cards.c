#include "ui_cards.h"

#include <stdio.h>
#include <string.h>

#include "brand_logos.h"
#include "esp_heap_caps.h"
#include "i18n.h"
#include "pictos.h"
#include "ui_job.h"
#include "ui_theme.h"
#include "ui_tile.h"


/* Two cards fill the panel (16 px at each edge, 12 between): a third, when there is
 * one, is reached by sliding. 392 high: the row is 400 at y = 64, and 8 px are left
 * under the cards. There is no scroll bar: a thin white line along the bottom while
 * sliding was not wanted. */
#define EDGE 16
#define CARD_GAP 12
#define CARD_W ((800 - 2 * EDGE - CARD_GAP) / 2)
#define CARD_H 392
#define PAD 12
#define INNER_W (CARD_W - 2 * PAD)
#define TILES_Y 294
#define TILE_GAP 6
/* One nozzle: the nozzle, the bed and the chamber side by side on one row,
 * centred in the room under the time left. */
#define TILE_ONE_H 60
#define TILE_ONE_Y 309
/* Several nozzles: the nozzles share a first row, the bed and the chamber a second. */
#define TILE_ROW_H 44
/* The icon of a label, and the gap between it and the text. */
#define LBL_ICO 16
#define LBL_GAP 6

/* The top right corner holds the state (a dot and a word, right-aligned on the
 * first line). The ring is on the left and the printer's own controls are in a
 * row to its right, level with it: the light, then pause / resume, then stop.
 * The light is always there; the pair only while a job runs. */
#define RING 96
#define RING_X (PAD + 4)
#define RING_Y 80
#define BTN_W 74
#define BTN_H 56
#define BTN_GAP 9
#define LIGHT_X (RING_X + RING + 14)
#define BTN_X (LIGHT_X + BTN_W + BTN_GAP)
#define STOP_X (BTN_X + BTN_W + BTN_GAP)
#define BTN_Y (RING_Y + (RING - BTN_H) / 2)
#define STATUS_W 150
#define NAME_X 58
#define NAME_W (CARD_W - PAD - STATUS_W - 8 - NAME_X)

enum
{
    T_NOZZLE0, /* T_NOZZLE0..3: up to four nozzles, as on the full view */
    T_NOZZLE1,
    T_NOZZLE2,
    T_NOZZLE3,
    T_BED,
    T_CHAMBER,
    T_COUNT
};

typedef ui_tile_t tile_t;

typedef struct
{
    lv_obj_t *card;
    lv_obj_t *name;
    lv_obj_t *led;
    lv_obj_t *status;
    lv_obj_t *arc;
    lv_obj_t *pct;
    lv_obj_t *file;
    lv_obj_t *left;
    lv_obj_t *end;
    lv_obj_t *bar;   /* list rows: the progress, a thin bar instead of the ring */
    lv_obj_t *noz;   /* list rows: the nozzle and the bed, one line each */
    lv_obj_t *bed;
    tile_t t[T_COUNT];
    job_buttons_t job;
    int layout; /* nozzles the tiles are placed for, -1 until the first update */
    bool dimmed;
    /* What each label shows now: LVGL redraws a label whenever it is set, even to
     * the same text, and this runs for every card every second. */
    char c_status[40];
    char c_pct[12];
    char c_file[48];
    char c_left[16];
    char c_end[8];
    char c_cap[T_COUNT][20];
    char c_val[T_COUNT][12];
} card_t;

static card_t *s_cards; /* in PSRAM: internal RAM is the scarce one */
static int s_n;
static lv_obj_t *s_scr;
static bool s_list; /* the home is a vertical list of light rows, not cards */

/* The list: one row per printer, 76 high, in TigerScale's scrolling column
 * (ui_kit_scroll_list: the scrollbar always on, the rail of two buttons on the
 * right), as the settings are. A row holds what a glance needs (the state, the
 * progress, the nozzle and the bed) and the printer's three buttons, which are
 * on every printer's card whatever the view. No ring, no tiles, no layer: a
 * silent printer is greyed by its colours, not by an opacity that would draw
 * the row twice. */
#define ROW_W 690 /* the column's width (724) less its padding, 16 + 18 */
#define ROW_H 76
#define ROW_GAP 8
#define ROW_NAME_W 190
#define ROW_PROG_X 268
#define ROW_PROG_W 150
#define ROW_TEMP_X 432
#define ROW_TEMP_W 48
#define ROW_BTN_W 54
#define ROW_BTN_H 52
#define ROW_BTN_GAP 6
#define ROW_STOP_X (ROW_W - 8 - ROW_BTN_W)
#define ROW_PAUSE_X (ROW_STOP_X - ROW_BTN_GAP - ROW_BTN_W)
#define ROW_LIGHT_X (ROW_PAUSE_X - ROW_BTN_GAP - ROW_BTN_W)
#define ROW_BTN_Y ((ROW_H - ROW_BTN_H) / 2)

static void set_txt(lv_obj_t *l, char *cache, size_t cap, const char *s)
{
    if (strcmp(cache, s) != 0)
    {
        strlcpy(cache, s, cap);
        lv_label_set_text(l, s);
    }
}

/* A label that must not wrap: a fixed height, because LVGL's "dots" mode only
 * truncates against one. */
static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t col, int x, int y, int w,
                          const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_pos(l, x, y);
    if (w > 0)
    {
        lv_obj_set_width(l, w);
        lv_obj_set_height(l, lv_font_get_line_height(font));
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    }
    return l;
}

static lv_obj_t *mk_disc(lv_obj_t *parent, int x, int y, int d, lv_color_t col)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, col, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

/* The room under the time left is filled for the printer's own number of nozzles.
 * One nozzle: the nozzle, the bed and the chamber on one row. Several: the
 * nozzles share a first row (a caption over each value when there are two, a
 * pictogram cannot say left from right) and the bed and the chamber a second. */
static void layout(card_t *c, int nn)
{
    for (int i = 0; i < T_COUNT; i++)
    {
        lv_obj_set_hidden(c->t[i].tile, true);
    }
    for (int i = T_NOZZLE0; i <= T_NOZZLE3; i++)
    {
        c->c_cap[i][0] = '\0';
        lv_label_set_text(c->t[i].cap, "");
    }
    if (nn == 1)
    {
        const int w = (INNER_W - 2 * TILE_GAP) / 3;
        ui_tile_place(&c->t[T_NOZZLE0], PAD, TILE_ONE_Y, w, TILE_ONE_H, &font_ui_20, false);
        ui_tile_place(&c->t[T_BED], PAD + w + TILE_GAP, TILE_ONE_Y, w, TILE_ONE_H, &font_ui_20, false);
        ui_tile_place(&c->t[T_CHAMBER], PAD + 2 * (w + TILE_GAP), TILE_ONE_Y, w, TILE_ONE_H, &font_ui_20, false);
    }
    else
    {
        const int w = (INNER_W - (nn - 1) * TILE_GAP) / nn;
        for (int i = 0; i < nn; i++)
        {
            ui_tile_place(&c->t[T_NOZZLE0 + i], PAD + i * (w + TILE_GAP), TILES_Y, w, TILE_ROW_H, &font_ui_16, false);
        }
        const int wb = (INNER_W - TILE_GAP) / 2;
        const int y = TILES_Y + TILE_ROW_H + TILE_GAP;
        ui_tile_place(&c->t[T_BED], PAD, y, wb, TILE_ROW_H, &font_ui_16, false);
        ui_tile_place(&c->t[T_CHAMBER], PAD + wb + TILE_GAP, y, wb, TILE_ROW_H, &font_ui_16, false);
    }
    c->layout = nn;
}

static void build_card(card_t *c, lv_obj_t *row, const printer_row_t *r, int index, lv_event_cb_t on_pick)
{
    lv_obj_t *card = lv_obj_create(row);
    c->card = card;
    c->layout = -1;
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, C_LINE, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_clickable(card, true);
    lv_obj_set_snappable(card, true);
    lv_obj_set_style_bg_color(card, C_TILE, LV_STATE_PRESSED);
    lv_obj_add_event_cb(card, on_pick, LV_EVENT_CLICKED, (void *)(intptr_t)index);

    /* The maker's logo in a disc, then the printer's name. */
    lv_obj_t *dot = mk_disc(card, 10, 8, 40, C_LINE);
    const lv_image_dsc_t *logo = brand_logo(r->brand);
    if (logo)
    {
        lv_obj_t *im = lv_image_create(dot);
        lv_image_set_src(im, logo);
        lv_obj_set_style_image_recolor(im, C_TEXT, 0);
        lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
        lv_obj_center(im);
    }
    else
    {
        char ini[2] = {r->initial ? r->initial : '?', 0};
        lv_obj_t *il = mk_label(dot, &font_ui_20, C_TEXT, 0, 0, 0, ini);
        lv_obj_center(il);
    }
    c->name = mk_label(card, &font_ui_20, C_TEXT, NAME_X, 15, NAME_W, r->name);

    /* A dot and the state, in the top right corner: the dot says whether the
     * printer answers (green, red when it is silent, grey while it is being
     * reached), the word says what it is doing. The two are centred as one block
     * and the block hangs from the right edge, whatever the word's length. */
    lv_obj_t *st = lv_obj_create(card);
    lv_obj_set_size(st, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(st, STATUS_W, 0);
    lv_obj_set_style_bg_opa(st, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(st, 0, 0);
    lv_obj_set_style_pad_all(st, 0, 0);
    lv_obj_set_style_pad_column(st, 6, 0);
    lv_obj_set_scrollable(st, false);
    lv_obj_set_clickable(st, false);
    lv_obj_set_flex_flow(st, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(st, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(st, LV_ALIGN_TOP_RIGHT, -PAD, 15);
    c->led = mk_disc(st, 0, 0, 12, C_MUTED);
    c->status = lv_label_create(st);
    lv_label_set_text(c->status, "");
    lv_obj_set_style_text_font(c->status, &font_ui_16, 0);
    lv_obj_set_style_text_color(c->status, C_MUTED, 0);

    ui_job_build(&c->job, card, BTN_X, BTN_Y, STOP_X, BTN_Y, BTN_W, BTN_H, LIGHT_X, BTN_Y, BTN_W, BTN_H, index);

    c->arc = lv_arc_create(card);
    lv_obj_set_size(c->arc, RING, RING);
    lv_obj_set_pos(c->arc, RING_X, RING_Y);
    lv_arc_set_range(c->arc, 0, 100);
    lv_arc_set_rotation(c->arc, 270);
    lv_arc_set_bg_angles(c->arc, 0, 360);
    lv_obj_remove_style(c->arc, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(c->arc, false);
    lv_obj_set_style_arc_width(c->arc, 9, LV_PART_MAIN);
    lv_obj_set_style_arc_width(c->arc, 9, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(c->arc, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_arc_color(c->arc, C_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(c->arc, true, LV_PART_INDICATOR);
    lv_arc_set_value(c->arc, 0); /* a new arc is drawn half full until it has a value */
    c->pct = mk_label(card, &font_ui_24, C_TEXT, 0, 0, RING, "--");
    lv_obj_set_style_text_align(c->pct, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(c->pct, c->arc, LV_ALIGN_CENTER, 0, 0);

    /* Each caption has the pictogram of what it names in front of it. */
    const int half = CARD_W / 2;
    lv_obj_set_pos(picto_create(card, PICTO_FILE, LBL_ICO, C_MUTED), PAD, 189);
    mk_label(card, &font_ui_14, C_MUTED, PAD + LBL_ICO + LBL_GAP, 188, INNER_W - LBL_ICO - LBL_GAP, tr(S_FILE));
    c->file = mk_label(card, &font_ui_16, C_TEXT, PAD, 206, INNER_W, "--");
    lv_obj_set_pos(picto_create(card, PICTO_CLOCK, LBL_ICO, C_MUTED), PAD, 237);
    mk_label(card, &font_ui_14, C_MUTED, PAD + LBL_ICO + LBL_GAP, 236, half - PAD - 8 - LBL_ICO - LBL_GAP,
             tr(S_TIME_LEFT));
    c->left = mk_label(card, &font_ui_20, C_TEXT, PAD, 254, half - PAD - 8, "--");
    lv_obj_set_pos(picto_create(card, PICTO_FLAG, LBL_ICO, C_MUTED), half, 237);
    mk_label(card, &font_ui_14, C_MUTED, half + LBL_ICO + LBL_GAP, 236, half - PAD - LBL_ICO - LBL_GAP,
             tr(S_END_ESTIMATED));
    c->end = mk_label(card, &font_ui_20, C_TEXT, half, 254, half - PAD, "--:--");

    static const picto_t WHAT[T_COUNT] = {PICTO_NOZZLE, PICTO_NOZZLE, PICTO_NOZZLE, PICTO_NOZZLE, PICTO_BED,
                                          PICTO_CHAMBER};
    for (int i = 0; i < T_COUNT; i++)
    {
        ui_tile_build(&c->t[i], card, WHAT[i], C_TILE, 8);
    }
    layout(c, 1);
}

static void build_row(card_t *c, lv_obj_t *list, const printer_row_t *r, int index, lv_event_cb_t on_pick)
{
    lv_obj_t *row = lv_obj_create(list);
    c->card = row;
    lv_obj_set_size(row, ROW_W, ROW_H);
    lv_obj_set_style_bg_color(row, C_CARD, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_clickable(row, true);
    lv_obj_set_style_bg_color(row, C_TILE, LV_STATE_PRESSED);
    lv_obj_add_event_cb(row, on_pick, LV_EVENT_CLICKED, (void *)(intptr_t)index);

    lv_obj_t *dot = mk_disc(row, 12, (ROW_H - 40) / 2, 40, C_LINE);
    const lv_image_dsc_t *logo = brand_logo(r->brand);
    if (logo)
    {
        lv_obj_t *im = lv_image_create(dot);
        lv_image_set_src(im, logo);
        lv_obj_set_style_image_recolor(im, C_TEXT, 0);
        lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
        lv_obj_center(im);
    }
    else
    {
        char ini[2] = {r->initial ? r->initial : '?', 0};
        lv_obj_center(mk_label(dot, &font_ui_20, C_TEXT, 0, 0, 0, ini));
    }
    c->name = mk_label(row, &font_ui_20, C_TEXT, 64, 10, ROW_NAME_W, r->name);
    c->led = mk_disc(row, 64, 49, 10, C_MUTED);
    c->status = mk_label(row, &font_ui_14, C_MUTED, 80, 44, ROW_NAME_W - 16, "");

    c->pct = mk_label(row, &font_ui_20, C_TEXT, ROW_PROG_X, 8, 72, "--");
    c->left = mk_label(row, &font_ui_14, C_MUTED, ROW_PROG_X + 72, 13, ROW_PROG_W - 72, "");
    lv_obj_set_style_text_align(c->left, LV_TEXT_ALIGN_RIGHT, 0);
    c->bar = lv_bar_create(row);
    lv_obj_set_pos(c->bar, ROW_PROG_X, 38);
    lv_obj_set_size(c->bar, ROW_PROG_W, 6);
    lv_bar_set_range(c->bar, 0, 100);
    lv_obj_set_style_bg_color(c->bar, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(c->bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(c->bar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(c->bar, C_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(c->bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(c->bar, 3, LV_PART_INDICATOR);
    lv_obj_set_clickable(c->bar, false);
    c->file = mk_label(row, &font_ui_14, C_MUTED, ROW_PROG_X, 50, ROW_PROG_W, "");

    lv_obj_set_pos(picto_create(row, PICTO_NOZZLE, LBL_ICO, C_MUTED), ROW_TEMP_X, 15);
    c->noz = mk_label(row, &font_ui_16, C_TEXT, ROW_TEMP_X + LBL_ICO + LBL_GAP, 12, ROW_TEMP_W, "--");
    lv_obj_set_pos(picto_create(row, PICTO_BED, LBL_ICO, C_MUTED), ROW_TEMP_X, 47);
    c->bed = mk_label(row, &font_ui_16, C_TEXT, ROW_TEMP_X + LBL_ICO + LBL_GAP, 44, ROW_TEMP_W, "--");

    ui_job_build(&c->job, row, ROW_PAUSE_X, ROW_BTN_Y, ROW_STOP_X, ROW_BTN_Y, ROW_BTN_W, ROW_BTN_H, ROW_LIGHT_X,
                 ROW_BTN_Y, ROW_BTN_W, ROW_BTN_H, index);
}

static lv_obj_t *create_list(lv_obj_t *parent, const printer_row_t *rows, lv_event_cb_t on_pick)
{
    lv_obj_t *list = ui_kit_scroll_list(parent, 2 * (ROW_H + ROW_GAP));
    lv_obj_set_style_pad_row(list, ROW_GAP, 0);
    for (int i = 0; i < s_n; i++)
    {
        build_row(&s_cards[i], list, &rows[i], i, on_pick);
    }
    ui_kit_scroll_rail(parent, list);
    return list;
}

void ui_cards_set_list(bool list)
{
    s_list = list;
}

bool ui_cards_is_list(void)
{
    return s_list;
}

lv_obj_t *ui_cards_create(lv_obj_t *parent, const printer_row_t *rows, int n, lv_event_cb_t on_pick)
{
    ui_cards_forget();
    s_cards = heap_caps_calloc((size_t)n, sizeof(card_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_n = s_cards ? n : 0;
    s_scr = parent;
    if (s_list)
    {
        return create_list(parent, rows, on_pick);
    }

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_pos(row, 0, 64);
    lv_obj_set_size(row, 800, CARD_H + 8);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_left(row, EDGE, 0);
    lv_obj_set_style_pad_right(row, EDGE, 0);
    lv_obj_set_style_pad_column(row, CARD_GAP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(row, LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < s_n; i++)
    {
        build_card(&s_cards[i], row, &rows[i], i, on_pick);
    }
    return row;
}

static const char *temp_text(char *buf, size_t n, bool live, float v, bool needs_value)
{
    if (!live || (needs_value && v <= 0.0f))
    {
        return "--";
    }
    snprintf(buf, n, "%d\xC2\xB0", (int)(v + 0.5f));
    return buf;
}


/* A list row: the same reading as a card, with less drawn. */
static void update_row(card_t *c, const printer_view_t *v, bool live, bool silent)
{
    char buf[48];
    if (silent != c->dimmed)
    {
        c->dimmed = silent;
        lv_obj_set_style_text_color(c->name, silent ? C_MUTED : C_TEXT, 0);
    }
    const bool jobs = live && v->can_control && (v->state == PV_PRINTING || v->state == PV_PAUSED);
    ui_job_set(&c->job, jobs, live && v->state == PV_PAUSED);
    ui_job_set_light(&c->job, live && v->can_light, v->light_on);

    const int pct = live ? (v->progress < 0 ? 0 : (v->progress > 100 ? 100 : v->progress)) : 0;
    if (lv_bar_get_value(c->bar) != pct)
    {
        lv_bar_set_value(c->bar, pct, LV_ANIM_OFF);
    }
    lv_obj_set_style_bg_color(c->bar, ui_progress_color(v, live), LV_PART_INDICATOR);
    if (live)
    {
        snprintf(buf, sizeof buf, "%d %%", pct);
    }
    else
    {
        snprintf(buf, sizeof buf, "--");
    }
    set_txt(c->pct, c->c_pct, sizeof c->c_pct, buf);
    buf[0] = '\0';
    if (live && v->left_s > 0)
    {
        ui_fmt_left(buf, sizeof buf, v->left_s);
    }
    set_txt(c->left, c->c_left, sizeof c->c_left, buf);
    set_txt(c->file, c->c_file, sizeof c->c_file, (live && v->file[0]) ? v->file : "");

    /* The nozzle at work when the printer has several, else the first. */
    const int nn = v->nozzle_count < 1 ? 1 : (v->nozzle_count > PV_MAX_NOZZLES ? PV_MAX_NOZZLES : v->nozzle_count);
    const int at = (v->nozzle_active >= 0 && v->nozzle_active < nn) ? v->nozzle_active : 0;
    const float noz = nn == 1 ? v->nozzle : v->nozzles[at].temp;
    char tb[12];
    set_txt(c->noz, c->c_val[T_NOZZLE0], sizeof c->c_val[0], temp_text(tb, sizeof tb, live, noz, false));
    set_txt(c->bed, c->c_val[T_BED], sizeof c->c_val[0], temp_text(tb, sizeof tb, live, v->bed, false));
}

void ui_cards_update(int index, const printer_view_t *v, printer_chip_t chip)
{
    if (!s_cards || !v || index < 0 || index >= s_n || s_scr != lv_screen_active())
    {
        return;
    }
    card_t *c = &s_cards[index];
    const bool live = chip == CHIP_ONLINE;
    char buf[48];

    /* The dot says whether the printer answers; the word says what it is doing,
     * or that it is silent or still being reached. */
    lv_color_t col = C_MUTED;
    if (live)
    {
        col = C_GREEN;
        snprintf(buf, sizeof buf, "%s", ui_state_text(v->state));
    }
    else if (chip == CHIP_OFFLINE)
    {
        col = C_RED;
        snprintf(buf, sizeof buf, "%s", tr(S_OFFLINE));
    }
    else
    {
        snprintf(buf, sizeof buf, "%s", chip == CHIP_SOON ? tr(S_SOON) : tr(S_CONNECTING));
    }
    set_txt(c->status, c->c_status, sizeof c->c_status, buf);
    lv_obj_set_style_bg_color(c->led, col, 0);
    lv_obj_set_style_text_color(c->status, live ? C_MUTED : col, 0);

    if (s_list)
    {
        update_row(c, v, live, chip == CHIP_OFFLINE);
        return;
    }

    /* A printer that is silent steps back; one still being reached does not. */
    const bool dim = chip == CHIP_OFFLINE;
    if (dim != c->dimmed)
    {
        c->dimmed = dim;
        lv_obj_set_style_opa(c->card, dim ? LV_OPA_60 : LV_OPA_COVER, 0);
    }

    /* Pause and stop are drawn all the time and act only while a job runs, and
     * only on a connection that takes commands (a Bambu reached through the cloud
     * does not). The light is drawn all the time too, and acts whenever this
     * connection takes the command. */
    const bool jobs = live && v->can_control && (v->state == PV_PRINTING || v->state == PV_PAUSED);
    ui_job_set(&c->job, jobs, live && v->state == PV_PAUSED);
    ui_job_set_light(&c->job, live && v->can_light, v->light_on);

    /* The ring: the progress, coloured by the print's state (ui_progress_color). */
    const int pct = live ? (v->progress < 0 ? 0 : (v->progress > 100 ? 100 : v->progress)) : 0;
    lv_arc_set_value(c->arc, pct);
    lv_obj_set_style_arc_color(c->arc, ui_progress_color(v, live), LV_PART_INDICATOR);
    if (live)
    {
        snprintf(buf, sizeof buf, "%d %%", pct);
    }
    else
    {
        snprintf(buf, sizeof buf, "--");
    }
    set_txt(c->pct, c->c_pct, sizeof c->c_pct, buf);

    set_txt(c->file, c->c_file, sizeof c->c_file, (live && v->file[0]) ? v->file : "--");
    if (live)
    {
        ui_fmt_left(buf, sizeof buf, v->left_s);
    }
    else
    {
        snprintf(buf, sizeof buf, "--");
    }
    set_txt(c->left, c->c_left, sizeof c->c_left, buf);
    if (live)
    {
        ui_fmt_end(buf, sizeof buf, v->left_s);
    }
    else
    {
        snprintf(buf, sizeof buf, "--:--");
    }
    set_txt(c->end, c->c_end, sizeof c->c_end, buf);

    /* Temperatures. */
    const int nn = v->nozzle_count < 1 ? 1 : (v->nozzle_count > PV_MAX_NOZZLES ? PV_MAX_NOZZLES : v->nozzle_count);
    if (nn != c->layout)
    {
        layout(c, nn);
    }
    char tb[12];
    for (int i = 0; i < nn; i++)
    {
        ui_tile_t *z = &c->t[T_NOZZLE0 + i];
        const float temp = nn == 1 ? v->nozzle : v->nozzles[i].temp;
        const bool active = nn > 1 && live && i == v->nozzle_active;
        const bool known = v->nozzle_active >= 0;
        if (nn == 2)
        {
            /* The caption (Left, Right) shows when the printer names its nozzles. */
            char *shown = c->c_cap[T_NOZZLE0 + i];
            const bool had = shown[0] != '\0';
            set_txt(z->cap, shown, sizeof c->c_cap[0], v->nozzles[i].label);
            if (had != (shown[0] != '\0'))
            {
                lv_obj_set_hidden(z->cap, shown[0] == '\0');
            }
        }
        set_txt(z->val, c->c_val[T_NOZZLE0 + i], sizeof c->c_val[0], temp_text(tb, sizeof tb, live, temp, false));
        /* The nozzle at work is ringed; the others step back. */
        lv_obj_set_style_border_width(z->tile, active ? 2 : 0, 0);
        lv_obj_set_style_image_recolor(z->ico, active ? C_ACCENT : C_MUTED, 0);
        lv_obj_set_style_text_color(z->cap, active ? C_ACCENT : C_MUTED, 0);
        lv_obj_set_style_text_color(z->val, (nn == 1 || !live || !known || active) ? C_TEXT : C_MUTED, 0);
    }
    set_txt(c->t[T_BED].val, c->c_val[T_BED], sizeof c->c_val[0], temp_text(tb, sizeof tb, live, v->bed, false));
    /* A printer with no chamber sensor reports 0: that is "not known", never a temperature. */
    set_txt(c->t[T_CHAMBER].val, c->c_val[T_CHAMBER], sizeof c->c_val[0],
            temp_text(tb, sizeof tb, live, v->chamber, true));
}

void ui_cards_forget(void)
{
    if (s_cards)
    {
        heap_caps_free(s_cards);
    }
    s_cards = NULL;
    s_n = 0;
    s_scr = NULL;
}
