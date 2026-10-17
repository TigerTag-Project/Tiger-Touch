/* The settings pages: the menu, Printers (with the load gauge), Wi-Fi, Screen,
 * Language and the restart confirmation. Like every screen they never act on
 * their own: they post an event and the application task decides. */

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"

#include "brand_logos.h"
#include "i18n.h"
#include "ui_setup.h"
#include "ui_theme.h"
#include "ui_wifi.h"


static const int W = 800;

/* ---- Small helpers -------------------------------------------------------- */

static void on_back(lv_event_t *e)
{
    (void)e;
    ui_post(EV_BACK, 0, NULL, NULL);
}

/* The band at the top of every page: the back arrow, then the title. */
/* The band at the top of every page, as TigerScale's lvglAddHeader(): the whole
 * band is the back button, the chevron and the title on it, a hairline under
 * it. PRESSED rather than CLICKED: waiting for the lift reads as the back button
 * hesitating, and LVGL drops the press when its object is deleted, so the
 * release cannot reach the header of the page underneath. */

static void post_header_ev(lv_event_t *e)
{
    ui_post((ev_type_t)(intptr_t)lv_event_get_user_data(e), 0, NULL, NULL);
}

lv_obj_t *ui_kit_header(lv_obj_t *scr, const char *title, ev_type_t back_ev)
{
    lv_obj_t *band = lv_obj_create(scr);
    lv_obj_remove_style_all(band);
    lv_obj_set_size(band, W, HEADER_H);
    lv_obj_set_pos(band, 0, 0);
    lv_obj_set_scrollable(band, false);
    lv_obj_set_clickable(band, true);
    lv_obj_add_event_cb(band, post_header_ev, LV_EVENT_PRESSED, (void *)(intptr_t)back_ev);

    lv_obj_t *chev = lv_label_create(band);
    lv_label_set_text(chev, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(chev, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(chev, C_TEXT, 0);
    lv_obj_align(chev, LV_ALIGN_LEFT_MID, 24, 0);

    lv_obj_t *t = ui_kit_txt(band, &font_ui_24, C_TEXT, title, 0, 0);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 80, 0);
    return band;
}

static void header(lv_obj_t *scr, const char *title)
{
    ui_kit_header(scr, title, EV_BACK);
}

static lv_obj_t *tile_button(lv_obj_t *scr, const char *symbol, int right_margin, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(scr);
    lv_obj_set_size(b, 64, 52); /* centred on the header's line */
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -right_margin, HEADER_MID - 26);
    lv_obj_set_style_bg_color(b, C_TILE, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, symbol);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(l, C_TEXT, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static lv_obj_t *card_box(lv_obj_t *parent, int x, int y, int w, int h)
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

/* ---- The menu ------------------------------------------------------------- */
/* TigerScale's settings list at 800x480: one tall row per setting in a column
 * that scrolls, its current value on the right, and the rail of two buttons
 * beside it. The row height is chosen so the fifth row is cut by the bottom
 * edge: with the scrollbar always on, that is how the list says it scrolls. */

#define ROW_H 84
#define ROW_GAP 10
#define LIST_W 724      /* clears the rail */
#define RAIL_X 732
#define RAIL_W 60
#define ICON_BOX 40

static int s_menu_scroll; /* kept across a visit to a page and back */

static void on_menu_row(lv_event_t *e)
{
    ui_post((ev_type_t)(intptr_t)lv_event_get_user_data(e), 0, NULL, NULL);
}

static void on_menu_scrolled(lv_event_t *e)
{
    s_menu_scroll = lv_obj_get_scroll_y(lv_event_get_target_obj(e));
}

/* Rail: a press scrolls two rows; a button at its end of travel is greyed, so
 * a touch that would do nothing looks like it. */
static void on_rail(lv_event_t *e)
{
    lv_obj_t *list = (lv_obj_t *)lv_event_get_user_data(e);
    const int step = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
    lv_obj_scroll_to_y(list, lv_obj_get_scroll_y(list) + step, LV_ANIM_ON);
}

static void on_rail_end(lv_event_t *e)
{
    lv_obj_t *list = lv_event_get_target_obj(e);
    lv_obj_t *b = (lv_obj_t *)lv_event_get_user_data(e);
    const bool up = (int)(intptr_t)lv_obj_get_user_data(b) < 0;
    const bool end = up ? lv_obj_get_scroll_top(list) <= 0 : lv_obj_get_scroll_bottom(list) <= 0;
    lv_obj_t *ic = lv_obj_get_child(b, 0);
    if (ic)
    {
        lv_obj_set_style_text_color(ic, end ? C_LINE : C_TEXT, 0);
    }
}

static void scroll_rail(lv_obj_t *scr, lv_obj_t *list, int y0, int y1, int step)
{
    const int h = (y1 - y0 - 10) / 2;
    for (int i = 0; i < 2; i++)
    {
        lv_obj_t *b = lv_button_create(scr);
        lv_obj_set_size(b, RAIL_W, h);
        lv_obj_set_pos(b, RAIL_X, i == 0 ? y0 : y0 + h + 10);
        lv_obj_set_style_bg_color(b, C_CARD, 0);
        lv_obj_set_style_border_color(b, C_LINE, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_radius(b, 14, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_user_data(b, (void *)(intptr_t)(i == 0 ? -step : step));
        /* PRESSED: scrolling changes no screen, so waiting for the lift is pure delay. */
        lv_obj_add_event_cb(b, on_rail, LV_EVENT_PRESSED, list);
        lv_obj_t *ic = lv_label_create(b);
        lv_label_set_text(ic, i == 0 ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
        lv_obj_set_style_text_font(ic, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(ic, C_TEXT, 0);
        lv_obj_center(ic);
        lv_obj_add_event_cb(list, on_rail_end, LV_EVENT_SCROLL, b);
        lv_obj_add_event_cb(list, on_rail_end, LV_EVENT_SCROLL_END, b);
    }
    lv_obj_update_layout(list);
    lv_obj_send_event(list, LV_EVENT_SCROLL, NULL);
}

/* ---- Icons, drawn from shapes as on TigerScale, in a 40 px box ---- */

static lv_obj_t *shape(lv_obj_t *parent, int x, int y, int w, int h, int r, lv_color_t col, bool outline)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, r, 0);
    if (outline)
    {
        lv_obj_set_style_border_width(o, 3, 0);
        lv_obj_set_style_border_color(o, col, 0);
    }
    else
    {
        lv_obj_set_style_bg_color(o, col, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    }
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

typedef enum
{
    ICON_GLYPH,
    ICON_USER,
    ICON_GLOBE,
    ICON_SUN,
    ICON_PRINTER,
} icon_kind_t;

static void draw_icon(lv_obj_t *box, icon_kind_t kind, const char *glyph, lv_color_t col)
{
    switch (kind)
    {
    case ICON_GLYPH:
    {
        lv_obj_t *l = lv_label_create(box);
        lv_label_set_text(l, glyph);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(l, col, 0);
        lv_obj_center(l);
        break;
    }
    case ICON_USER: /* head over shoulders; the box clips the shoulders' disc */
        shape(box, 13, 2, 14, 14, LV_RADIUS_CIRCLE, col, false);
        shape(box, 4, 21, 32, 30, LV_RADIUS_CIRCLE, col, false);
        break;
    case ICON_GLOBE: /* TigerScale's globe: a ring, the equator, a meridian */
        shape(box, 3, 3, 34, 34, LV_RADIUS_CIRCLE, col, true);
        shape(box, 3, 19, 34, 3, 0, col, false);
        shape(box, 12, 3, 16, 34, 8, col, true);
        break;
    case ICON_SUN: /* FontAwesome's sun (U+F185), as on TigerScale's Screen row */
    {
        lv_obj_t *l = lv_label_create(box);
        lv_label_set_text(l, "\xEF\x86\x85");
        lv_obj_set_style_text_font(l, &font_icon_32, 0);
        lv_obj_set_style_text_color(l, col, 0);
        lv_obj_center(l);
        break;
    }
    case ICON_PRINTER: /* a frame, the head on its gantry, the bed */
        shape(box, 2, 2, 36, 36, 5, col, true);
        shape(box, 8, 11, 24, 3, 1, col, false);
        shape(box, 16, 11, 8, 10, 2, col, false);
        shape(box, 8, 29, 24, 4, 1, col, false);
        break;
    }
}

static lv_obj_t *settings_row(lv_obj_t *list, icon_kind_t kind, const char *glyph, lv_color_t icon_col,
                              const char *label, lv_color_t label_col, const char *value, lv_color_t value_col,
                              ev_type_t ev)
{
    lv_obj_t *row = lv_button_create(list);
    lv_obj_set_size(row, LV_PCT(100), ROW_H);
    lv_obj_set_style_bg_color(row, C_CARD, 0);
    lv_obj_set_style_bg_color(row, C_TILE, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(row, C_LINE, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_add_event_cb(row, on_menu_row, LV_EVENT_CLICKED, (void *)(intptr_t)ev);

    lv_obj_t *box = lv_obj_create(row);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, ICON_BOX, ICON_BOX);
    lv_obj_align(box, LV_ALIGN_LEFT_MID, 22, 0);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_clickable(box, false);
    draw_icon(box, kind, glyph, icon_col);

    lv_obj_t *l = ui_kit_txt(row, &font_ui_24, label_col, label, 0, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 86, 0);

    /* The big chevron is the "this opens" sign, readable at arm's length. */
    lv_obj_t *c = lv_label_create(row);
    lv_label_set_text(c, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(c, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(c, C_MUTED, 0);
    lv_obj_align(c, LV_ALIGN_RIGHT_MID, -20, 0);

    if (value && value[0])
    {
        lv_obj_t *v = ui_kit_txt(row, &font_ui_20, value_col, value, 0, 0);
        lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
        lv_obj_set_width(v, 280);
        lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(v, LV_ALIGN_RIGHT_MID, -64, 0);
    }
    return row;
}

lv_obj_t *ui_kit_scroll_list(lv_obj_t *scr, int step)
{
    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LIST_W, 480 - HEADER_H);
    lv_obj_set_pos(list, 0, HEADER_H);
    lv_obj_set_style_pad_top(list, 12, 0);
    lv_obj_set_style_pad_bottom(list, 12, 0);
    lv_obj_set_style_pad_left(list, 16, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, ROW_GAP, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    /* The scrollbar is always on, and its track reserved so rows never run under it. */
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ON);
    lv_obj_set_style_bg_color(list, C_MUTED, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list, 8, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(list, 18, LV_PART_MAIN);
    lv_obj_set_user_data(list, (void *)(intptr_t)step);
    return list;
}

void ui_kit_scroll_rail(lv_obj_t *scr, lv_obj_t *list)
{
    scroll_rail(scr, list, HEADER_H + 12, 480 - 12, (int)(intptr_t)lv_obj_get_user_data(list));
}

void ui_show_settings_menu(const settings_menu_t *m)
{
    lv_obj_t *scr = ui_kit_page();
    header(scr, tr(S_SETTINGS));
    ui_kit_clock(scr);
    lv_obj_t *list = ui_kit_scroll_list(scr, 2 * (ROW_H + ROW_GAP));

    char buf[48];
    snprintf(buf, sizeof buf, "%d / %d", m->printers_on, m->printers_total);
    settings_row(list, ICON_PRINTER, NULL, C_TEXT, tr(S_PRINTERS), C_TEXT, m->printers_total ? buf : NULL,
                 C_MUTED, EV_OPEN_PRINTERS);
    settings_row(list, ICON_GLYPH, LV_SYMBOL_WIFI, m->wifi_connected ? C_GREEN : C_RED, tr(S_WIFI), C_TEXT,
                 m->wifi_connected ? m->wifi_ssid : tr(S_NOT_CONNECTED), C_MUTED, EV_OPEN_WIFI);
    settings_row(list, ICON_USER, NULL, m->account_state == 2 ? C_GREEN : m->account_state == 1 ? C_ACCENT : C_RED,
                 tr(S_ACCOUNT), C_TEXT,
                 m->account_state ? m->account_email : tr(S_NO_ACCOUNT), C_MUTED, EV_CHANGE_ACCOUNT);
    settings_row(list, ICON_SUN, NULL, C_TEXT, tr(S_SCREEN), C_TEXT, NULL, C_MUTED, EV_OPEN_SCREEN);
    settings_row(list, ICON_GLYPH, LV_SYMBOL_BATTERY_FULL, m->battery ? C_ACCENT : C_MUTED, tr(S_BATT_TITLE), C_TEXT,
                 tr(m->battery ? S_BATT_FITTED : S_BATT_NONE), C_MUTED, EV_OPEN_BATTERY);
    settings_row(list, ICON_GLOBE, NULL, C_TEXT, tr(S_LANGUAGE), C_TEXT, i18n_native_name(i18n_lang()), C_MUTED,
                 EV_OPEN_LANGUAGE);
    settings_row(list, ICON_GLYPH, LV_SYMBOL_PLAY, m->demo ? C_ACCENT : C_MUTED, tr(S_DEMO), C_TEXT,
                 tr(m->demo ? S_ON : S_OFF), m->demo ? C_ACCENT : C_MUTED, EV_OPEN_DEMO);
    /* With a newer version seen, the row spells the move ("1.3.2 > 2.0.0") in orange. */
    const char *running = esp_app_get_description()->version;
    if (m->update_offered && m->update_offered[0])
    {
        snprintf(buf, sizeof buf, "%s > %s", running, m->update_offered);
    }
    else
    {
        snprintf(buf, sizeof buf, "%s", running);
    }
    const bool offer = m->update_offered && m->update_offered[0];
    settings_row(list, ICON_GLYPH, LV_SYMBOL_DOWNLOAD, offer ? C_ACCENT : C_TEXT, tr(S_UPDATE), C_TEXT, buf,
                 offer ? C_ACCENT : C_MUTED, EV_OPEN_UPDATE);
    /* Amber: interrupts without destroying anything. Red, last: cannot be undone. */
    settings_row(list, ICON_GLYPH, LV_SYMBOL_REFRESH, C_ACCENT, tr(S_RESTART), C_TEXT, NULL, C_MUTED,
                 EV_ASK_RESTART);
    settings_row(list, ICON_GLYPH, LV_SYMBOL_TRASH, C_RED, tr(S_FACTORY), C_RED, NULL, C_MUTED, EV_ASK_FACTORY);

    ui_kit_scroll_rail(scr, list);
    lv_obj_scroll_to_y(list, s_menu_scroll, LV_ANIM_OFF);
    lv_obj_add_event_cb(list, on_menu_scrolled, LV_EVENT_SCROLL_END, NULL);
}

/* Leaving the menu for the home screen starts it from the top next time. */
void ui_settings_menu_reset_scroll(void)
{
    s_menu_scroll = 0;
}

/* ---- Printers ------------------------------------------------------------- */

#define MAX_ROWS 24

static lv_obj_t *s_pp_scr;
static lv_obj_t *s_pp_bar;
static lv_obj_t *s_pp_pct;
static lv_obj_t *s_pp_hint;
static lv_obj_t *s_pp_sw[MAX_ROWS];
static int s_pp_n;
static lv_timer_t *s_pp_timer;

static void gauge_set(int pct, bool refused)
{
    if (pct < 0)
    {
        pct = 0;
    }
    if (pct > 100)
    {
        pct = 100;
    }
    char t[8];
    snprintf(t, sizeof t, "%d %%", pct);
    lv_label_set_text(s_pp_pct, t);
    lv_bar_set_value(s_pp_bar, pct, LV_ANIM_OFF);
    lv_color_t col = pct >= 85 ? C_AMBER : C_GREEN;
    if (refused)
    {
        col = C_RED;
    }
    lv_obj_set_style_bg_color(s_pp_bar, col, LV_PART_INDICATOR);
}

static void on_pp_hint_timer(lv_timer_t *t)
{
    s_pp_timer = NULL;
    lv_timer_delete(t);
    if (s_pp_scr && s_pp_scr == lv_screen_active())
    {
        lv_label_set_text(s_pp_hint, tr(S_LOAD_HINT));
        lv_obj_set_style_text_color(s_pp_hint, C_MUTED, 0);
        lv_obj_set_style_bg_color(s_pp_bar, lv_bar_get_value(s_pp_bar) >= 85 ? C_AMBER : C_GREEN,
                                  LV_PART_INDICATOR);
    }
}

static void on_switch(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    const int row = (int)(intptr_t)lv_event_get_user_data(e);
    ui_post(EV_TOGGLE_PRINTER, row, lv_obj_has_state(sw, LV_STATE_CHECKED) ? "1" : "0", NULL);
}

static void on_refresh(lv_event_t *e)
{
    (void)e;
    ui_post(EV_REFRESH_PRINTERS, 0, NULL, NULL);
}

void ui_show_printers_page(const printer_switch_t *rows, int n, int load_pct)
{
    lv_obj_t *scr = ui_kit_page();
    s_pp_scr = scr;
    s_pp_timer = NULL;
    s_pp_n = n > MAX_ROWS ? MAX_ROWS : n;
    header(scr, tr(S_PRINTERS));
    tile_button(scr, LV_SYMBOL_REFRESH, 16, on_refresh);

    ui_kit_txt(scr, &font_ui_20, C_TEXT, tr(S_LOAD), 20, 84);
    s_pp_pct = ui_kit_txt(scr, &font_ui_20, C_TEXT, "0 %", 0, 84);
    lv_obj_align(s_pp_pct, LV_ALIGN_TOP_RIGHT, -20, 84);

    s_pp_bar = lv_bar_create(scr);
    lv_obj_set_pos(s_pp_bar, 20, 116);
    lv_obj_set_size(s_pp_bar, W - 40, 14);
    lv_bar_set_range(s_pp_bar, 0, 100);
    lv_obj_set_style_bg_color(s_pp_bar, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_pp_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_pp_bar, 7, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_pp_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_pp_bar, 7, LV_PART_INDICATOR);

    s_pp_hint = ui_kit_txt(scr, &font_ui_14, C_MUTED, tr(S_LOAD_HINT), 20, 138);
    lv_obj_set_width(s_pp_hint, W - 40);
    lv_label_set_long_mode(s_pp_hint, LV_LABEL_LONG_DOT);

    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_set_pos(list, 16, 166);
    lv_obj_set_size(list, W - 32, 306);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    for (int i = 0; i < s_pp_n; i++)
    {
        const printer_switch_t *r = &rows[i];
        s_pp_sw[i] = NULL;
        lv_obj_t *row = card_box(list, 0, 0, W - 32, 76);

        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_set_size(dot, 44, 44);
        lv_obj_set_pos(dot, 16, 15);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, C_LINE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_scrollable(dot, false);
        lv_obj_set_clickable(dot, false);
        const lv_image_dsc_t *logo = brand_logo(r->brand);
        if (logo)
        {
            lv_obj_t *im = lv_image_create(dot);
            lv_image_set_src(im, logo);
            lv_obj_set_style_image_recolor(im, r->supported ? C_TEXT : C_MUTED, 0);
            lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
            lv_obj_center(im);
        }
        else
        {
            char ini[2] = {r->initial ? r->initial : '?', 0};
            lv_obj_center(ui_kit_txt(dot, &font_ui_24, C_TEXT, ini, 0, 0));
        }

        lv_obj_t *nm = ui_kit_txt(row, &font_ui_20, r->supported ? C_TEXT : C_MUTED, r->name, 78, 10);
        lv_obj_set_width(nm, 420);
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
        lv_obj_t *mt = ui_kit_txt(row, &font_ui_14, C_MUTED, r->meta, 78, 42);
        lv_obj_set_width(mt, 420);
        lv_label_set_long_mode(mt, LV_LABEL_LONG_DOT);

        if (!r->supported)
        {
            lv_obj_t *chip = lv_obj_create(row);
            lv_obj_set_size(chip, 120, 36);
            lv_obj_align(chip, LV_ALIGN_RIGHT_MID, -18, 0);
            lv_obj_set_style_radius(chip, 18, 0);
            lv_obj_set_style_bg_color(chip, C_LINE, 0);
            lv_obj_set_style_border_width(chip, 0, 0);
            lv_obj_set_style_pad_all(chip, 0, 0);
            lv_obj_set_scrollable(chip, false);
            lv_obj_set_clickable(chip, false);
            lv_obj_center(ui_kit_txt(chip, &font_ui_16, C_MUTED, tr(S_SOON), 0, 0));
            continue;
        }

        /* No share of the load beside the switch: the gauge above says it. */
        lv_obj_t *sw = lv_switch_create(row);
        lv_obj_set_size(sw, 70, 38);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -18, 0);
        lv_obj_set_style_bg_color(sw, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(sw, C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, C_TEXT, LV_PART_KNOB);
        if (r->on)
        {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(sw, on_switch, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
        s_pp_sw[i] = sw;
    }
    gauge_set(load_pct, false);
}

void ui_printers_page_result(int row, bool on, int load_pct, bool refused)
{
    if (!s_pp_scr || s_pp_scr != lv_screen_active() || row < 0 || row >= s_pp_n)
    {
        return;
    }
    lv_obj_t *sw = s_pp_sw[row];
    if (sw)
    {
        if (on)
        {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_remove_state(sw, LV_STATE_CHECKED);
        }
    }
    gauge_set(load_pct, refused);
    if (refused)
    {
        lv_label_set_text(s_pp_hint, tr(S_NOT_ENOUGH_ROOM));
        lv_obj_set_style_text_color(s_pp_hint, C_RED, 0);
        if (s_pp_timer)
        {
            lv_timer_delete(s_pp_timer);
        }
        s_pp_timer = lv_timer_create(on_pp_hint_timer, 2500, NULL);
    }
}

/* ---- Wi-Fi ---------------------------------------------------------------- */

static void on_change_network(lv_event_t *e)
{
    (void)e;
    ui_post(EV_CHANGE_WIFI, 0, NULL, NULL);
}

void ui_show_wifi_info(const wifi_info_t *w)
{
    lv_obj_t *scr = ui_kit_page();
    header(scr, tr(S_WIFI));

    lv_obj_t *top = card_box(scr, 16, 86, W - 32, 100);
    lv_obj_t *wave = ui_wifi_wave(top);
    lv_obj_align(wave, LV_ALIGN_LEFT_MID, 24, 0);
    ui_wifi_set_transient(wave);

    const int level = w->connected ? ui_wifi_level_from_rssi(w->rssi) : 0;
    const char *quality = tr(level >= 3 ? S_Q_EXCELLENT : (level == 2 ? S_Q_GOOD : (level == 1 ? S_Q_FAIR : S_Q_WEAK)));
    lv_color_t qcol = level >= 2 ? C_GREEN : (level == 1 ? C_AMBER : C_RED);

    lv_obj_t *ssid = ui_kit_txt(top, &font_ui_24, C_TEXT, w->connected ? w->ssid : tr(S_NOT_CONNECTED), 90, 16);
    lv_obj_set_width(ssid, 620);
    lv_label_set_long_mode(ssid, LV_LABEL_LONG_DOT);
    if (w->connected)
    {
        lv_obj_t *q = ui_kit_txt(top, &font_ui_20, qcol, quality, 90, 56);
        char dbm[16];
        snprintf(dbm, sizeof dbm, "%d dBm", w->rssi);
        lv_obj_t *d = ui_kit_txt(top, &font_ui_16, C_MUTED, dbm, 0, 0);
        lv_obj_update_layout(top); /* the label's width is needed to sit beside it */
        lv_obj_align_to(d, q, LV_ALIGN_OUT_RIGHT_BOTTOM, 14, -2);
    }

    lv_obj_t *info = card_box(scr, 16, 200, W - 32, 150);
    const char *labels[3] = {tr(S_IP), tr(S_MAC), tr(S_CHANNEL)};
    char chan[8];
    snprintf(chan, sizeof chan, "%d", w->channel);
    const char *values[3] = {w->connected ? w->ip : "-", w->mac[0] ? w->mac : "-", w->connected ? chan : "-"};
    for (int i = 0; i < 3; i++)
    {
        ui_kit_txt(info, &font_ui_20, C_MUTED, labels[i], 20, 16 + i * 42);
        lv_obj_t *v = ui_kit_txt(info, &font_ui_20, C_TEXT, values[i], 0, 0);
        lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -20, 16 + i * 42);
    }

    ui_kit_button(scr, tr(S_CHANGE_NETWORK), 16, 366, W - 32, 64, false, on_change_network, NULL);
}

/* ---- Screen --------------------------------------------------------------- */

typedef struct
{
    lv_obj_t *btn[4];
    int value[4];
    int n;
    ev_type_t ev;
    const char *suffix; /* " %" after a value, or "" */
    lv_obj_t *current;  /* the value shown at the right of the title */
} choice_group_t;

static choice_group_t s_grp[3];

static void style_choice(lv_obj_t *b, bool selected)
{
    lv_obj_set_style_bg_color(b, selected ? C_ACCENT : C_TILE, 0);
    lv_obj_t *l = lv_obj_get_child(b, 0);
    if (l)
    {
        lv_obj_set_style_text_color(l, selected ? C_BG : C_TEXT, 0);
    }
}

static void on_choice(lv_event_t *e)
{
    choice_group_t *g = (choice_group_t *)lv_event_get_user_data(e);
    lv_obj_t *target = lv_event_get_target_obj(e);
    for (int i = 0; i < g->n; i++)
    {
        const bool sel = g->btn[i] == target;
        style_choice(g->btn[i], sel);
        if (sel)
        {
            lv_obj_t *l = lv_obj_get_child(target, 0);
            if (g->current && l)
            {
                lv_label_set_text_fmt(g->current, "%s%s", lv_label_get_text(l), g->suffix);
            }
            ui_post(g->ev, g->value[i], NULL, NULL);
        }
    }
}

static void choice_row(lv_obj_t *scr, choice_group_t *g, int y, const char *title, ev_type_t ev,
                       const char *const *labels, const int *values, int n, int selected_value,
                       const char *suffix)
{
    g->n = n;
    g->ev = ev;
    g->suffix = suffix;
    ui_kit_txt(scr, &font_ui_20, C_TEXT, title, 20, y);
    g->current = ui_kit_txt(scr, &font_ui_20, C_MUTED, "", 0, y);
    lv_obj_align(g->current, LV_ALIGN_TOP_RIGHT, -20, y);
    const int gap = 12;
    const int w = (W - 32 - gap * (n - 1)) / n;
    for (int i = 0; i < n; i++)
    {
        g->value[i] = values[i];
        lv_obj_t *b = ui_kit_button(scr, labels[i], 16 + i * (w + gap), y + 34, w, 56, false, on_choice, g);
        g->btn[i] = b;
        const bool sel = values[i] == selected_value;
        style_choice(b, sel);
        if (sel)
        {
            lv_label_set_text_fmt(g->current, "%s%s", labels[i], suffix);
        }
    }
}

void ui_show_screen_page(int brightness, int sleep_s, int rotation)
{
    lv_obj_t *scr = ui_kit_page();
    header(scr, tr(S_SCREEN));

    static const int BRIGHT_V[4] = {30, 60, 80, 100};
    static const char *const BRIGHT_L[4] = {"30", "60", "80", "100"};
    choice_row(scr, &s_grp[0], 86, tr(S_BRIGHTNESS), EV_SET_BRIGHTNESS, BRIGHT_L, BRIGHT_V, 4, brightness, " %");

    static const int SLEEP_V[4] = {30, 60, 300, 0};
    const char *sleep_l[4] = {tr(S_SLEEP_30S), tr(S_SLEEP_1M), tr(S_SLEEP_5M), tr(S_NEVER)};
    choice_row(scr, &s_grp[1], 196, tr(S_SLEEP_AFTER), EV_SET_SLEEP, sleep_l, SLEEP_V, 4, sleep_s, "");

    static const int ROT_V[2] = {0, 180};
    static const char *const ROT_L[2] = {"0°", "180°"};
    choice_row(scr, &s_grp[2], 306, tr(S_ORIENTATION), EV_SET_ORIENTATION, ROT_L, ROT_V, 2, rotation, "");
}

/* ---- Battery -------------------------------------------------------------- */
/* TigerSpool's page: the switch first, the same place in both states; then the
 * battery drawn, in colour when one is declared; then why it is a declaration
 * and not a measurement. Nothing on the board can read the cell (HARDWARE.md). */

#define BATT_SEGS 4
static lv_obj_t *s_batt_body, *s_batt_cap, *s_batt_seg[BATT_SEGS];

static void paint_battery(bool fitted)
{
    const lv_color_t c = fitted ? C_ACCENT : C_LINE;
    lv_obj_set_style_border_color(s_batt_body, c, 0);
    lv_obj_set_style_bg_color(s_batt_cap, c, 0);
    for (int i = 0; i < BATT_SEGS; i++)
    {
        lv_obj_set_style_bg_color(s_batt_seg[i], c, 0);
    }
}

static void on_battery_switch(lv_event_t *e)
{
    const bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    paint_battery(on);
    ui_post(EV_SET_BATTERY, on ? 1 : 0, NULL, NULL);
}

void ui_show_battery_page(bool fitted)
{
    lv_obj_t *scr = ui_kit_page();
    header(scr, tr(S_BATT_TITLE));

    lv_obj_t *row = card_box(scr, 16, 88, W - 32, 76);
    lv_obj_t *l = ui_kit_txt(row, &font_ui_24, C_TEXT, tr(S_BATTERY), 0, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 22, 0);
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 70, 38);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -18, 0);
    lv_obj_set_style_bg_color(sw, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, C_TEXT, LV_PART_KNOB);
    if (fitted)
    {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, on_battery_switch, LV_EVENT_VALUE_CHANGED, NULL);

    /* The battery, drawn: a body, its cap, four bars. Lit when one is declared. */
    const int bw = 132, bh = 64, bx = (W - bw - 10) / 2, by = 186;
    s_batt_body = lv_obj_create(scr);
    lv_obj_remove_style_all(s_batt_body);
    lv_obj_set_pos(s_batt_body, bx, by);
    lv_obj_set_size(s_batt_body, bw, bh);
    lv_obj_set_style_radius(s_batt_body, 10, 0);
    lv_obj_set_style_border_width(s_batt_body, 5, 0);
    lv_obj_set_scrollable(s_batt_body, false);
    lv_obj_set_clickable(s_batt_body, false);
    s_batt_cap = lv_obj_create(scr);
    lv_obj_remove_style_all(s_batt_cap);
    lv_obj_set_pos(s_batt_cap, bx + bw + 2, by + bh / 2 - 13);
    lv_obj_set_size(s_batt_cap, 8, 26);
    lv_obj_set_style_radius(s_batt_cap, 3, 0);
    lv_obj_set_style_bg_opa(s_batt_cap, LV_OPA_COVER, 0);
    const int seg_w = (bw - 10 - 8 - (BATT_SEGS - 1) * 6) / BATT_SEGS;
    for (int i = 0; i < BATT_SEGS; i++)
    {
        lv_obj_t *s = s_batt_seg[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(s);
        lv_obj_set_pos(s, bx + 9 + i * (seg_w + 6), by + 9);
        lv_obj_set_size(s, seg_w, bh - 18);
        lv_obj_set_style_radius(s, 4, 0);
        lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    }
    paint_battery(fitted);

    /* Then the facts, and why there is no percentage. */
    lv_obj_t *i1 = ui_kit_txt(scr, &font_ui_16, C_TEXT, tr(S_BATT_INFO1), 32, 282);
    lv_obj_set_width(i1, W - 64);
    lv_label_set_long_mode(i1, LV_LABEL_LONG_WRAP);
    lv_obj_t *i2 = ui_kit_txt(scr, &font_ui_16, C_MUTED, tr(S_BATT_INFO2), 32, 0);
    lv_obj_set_width(i2, W - 64);
    lv_label_set_long_mode(i2, LV_LABEL_LONG_WRAP);
    lv_obj_align_to(i2, i1, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 16);
}

/* ---- Demo mode ------------------------------------------------------------ */
/* The same grammar as the battery page: the switch first, then what it does. */

static void on_demo_switch(lv_event_t *e)
{
    const bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    ui_post(EV_SET_DEMO, on ? 1 : 0, NULL, NULL);
}

void ui_show_demo_page(bool on)
{
    lv_obj_t *scr = ui_kit_page();
    header(scr, tr(S_DEMO));

    lv_obj_t *row = card_box(scr, 16, 88, W - 32, 76);
    lv_obj_t *l = ui_kit_txt(row, &font_ui_24, C_TEXT, tr(S_DEMO), 0, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 22, 0);
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 70, 38);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -18, 0);
    lv_obj_set_style_bg_color(sw, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, C_TEXT, LV_PART_KNOB);
    if (on)
    {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, on_demo_switch, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *i1 = ui_kit_txt(scr, &font_ui_20, C_TEXT, tr(S_DEMO_INFO1), 32, 196);
    lv_obj_set_width(i1, W - 64);
    lv_label_set_long_mode(i1, LV_LABEL_LONG_WRAP);
    lv_obj_t *i2 = ui_kit_txt(scr, &font_ui_16, C_MUTED, tr(S_DEMO_INFO2), 32, 0);
    lv_obj_set_width(i2, W - 64);
    lv_label_set_long_mode(i2, LV_LABEL_LONG_WRAP);
    lv_obj_align_to(i2, i1, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 18);
}

/* ---- Language ------------------------------------------------------------- */

static void on_language(lv_event_t *e)
{
    ui_post(EV_SET_LANGUAGE, (int)(intptr_t)lv_event_get_user_data(e), NULL, NULL);
}

void ui_show_language_page(int current_lang, bool first_boot)
{
    lv_obj_t *scr = ui_kit_page();
    if (first_boot)
    {
        /* Nobody has said yet which language they read: the title is the one
         * word everybody recognises, and each choice is written in its own. */
        ui_kit_txt(scr, &font_ui_24, C_ACCENT, "Language", 32, 14);
    }
    else
    {
        header(scr, tr(S_LANGUAGE));
    }
    const int cols = 3, gap = 11, h = 72;
    const int w = (W - 32 - gap * (cols - 1)) / cols;
    for (int i = 0; i < LANG_COUNT; i++)
    {
        const int cx = i % cols, cy = i / cols;
        lv_obj_t *b = ui_kit_button(scr, i18n_native_name((lang_t)i), 16 + cx * (w + gap), 88 + cy * (h + 8), w, h,
                                    i == current_lang, on_language, (void *)(intptr_t)i);
        (void)b;
    }
}

/* ---- Restart -------------------------------------------------------------- */

static void on_restart_yes(lv_event_t *e)
{
    (void)e;
    ui_post(EV_RESTART, 0, NULL, NULL);
}

void ui_show_restart_confirm(void)
{
    lv_obj_t *scr = ui_kit_page();
    lv_obj_t *t = ui_kit_txt(scr, &font_ui_24, C_TEXT, tr(S_RESTART_ASK), 0, 0);
    lv_obj_set_width(t, 700);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_t *m = ui_kit_txt(scr, &font_ui_16, C_MUTED, tr(S_RESTART_WHY), 0, 0);
    lv_obj_set_width(m, 700);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(m, LV_ALIGN_TOP_MID, 0, 176);
    ui_kit_button(scr, tr(S_CANCEL), 100, 300, 280, 64, false, on_back, NULL);
    ui_kit_button(scr, tr(S_RESTART), 420, 300, 280, 64, true, on_restart_yes, NULL);
}

/* ---- Factory reset -------------------------------------------------------- */

static void on_factory_yes(lv_event_t *e)
{
    (void)e;
    ui_post(EV_FACTORY, 0, NULL, NULL);
}

/* The restart confirmation's twin, with the destructive answer in red: a
 * question, what is lost, then two buttons that say what they do. */
void ui_show_factory_confirm(void)
{
    lv_obj_t *scr = ui_kit_page();
    lv_obj_t *t = ui_kit_txt(scr, &font_ui_24, C_RED, tr(S_FACTORY_ASK), 0, 0);
    lv_obj_set_width(t, 700);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_t *m = ui_kit_txt(scr, &font_ui_16, C_MUTED, tr(S_FACTORY_WHY), 0, 0);
    lv_obj_set_width(m, 640);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(m, LV_ALIGN_TOP_MID, 0, 166);
    ui_kit_button(scr, tr(S_CANCEL), 100, 300, 280, 64, false, on_back, NULL);
    lv_obj_t *go = ui_kit_button(scr, tr(S_FACTORY_DO), 420, 300, 280, 64, true, on_factory_yes, NULL);
    lv_obj_set_style_bg_color(go, C_RED, 0);
}

/* ---- Firmware update ------------------------------------------------------ */
/* TigerScale V3's runOtaMenu(), at 800x480: the installed version, then one
 * state below it, then the ring while the image is written. Same colours,
 * same ring, same arrow, so the two devices update the same way. */

#define C_RING_BG lv_color_hex(0x26456F) /* TigerScale's ring track */

static void post_ev(lv_event_t *e)
{
    ui_post((ev_type_t)(intptr_t)lv_event_get_user_data(e), 0, NULL, NULL);
}

static lv_obj_t *upd_row(lv_obj_t *scr, int y, int h)
{
    lv_obj_t *r = card_box(scr, 100, y, 600, h);
    lv_obj_set_style_radius(r, 14, 0);
    lv_obj_set_clickable(r, false);
    return r;
}

/* A round badge with a mark: a check in green, a cross in red. */
static void upd_badge(lv_obj_t *scr, bool ok, const char *msg)
{
    const lv_color_t col = ok ? C_GREEN : C_RED;
    lv_obj_t *b = lv_obj_create(scr);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 76, 76);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 206);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(b, 5, 0);
    lv_obj_set_style_border_color(b, col, 0);
    lv_obj_set_scrollable(b, false);
    lv_obj_t *m = lv_label_create(b);
    lv_label_set_text(m, ok ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(m, col, 0);
    lv_obj_center(m);
    lv_obj_t *t = ui_kit_txt(scr, &font_ui_20, col, msg, 0, 0);
    lv_obj_set_width(t, 600);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 300);
}

void ui_show_update_page(const char *installed, upd_state_t state, const char *offered)
{
    lv_obj_t *scr = ui_kit_page();
    header(scr, tr(S_UPDATE));

    lv_obj_t *r1 = upd_row(scr, 92, 60);
    lv_obj_t *n1 = ui_kit_txt(r1, &font_ui_20, C_MUTED, tr(S_UPD_CURRENT), 0, 0);
    lv_obj_align(n1, LV_ALIGN_LEFT_MID, 22, 0);
    char v[24];
    snprintf(v, sizeof v, "v%s", installed);
    lv_obj_t *v1 = ui_kit_txt(r1, &font_ui_20, C_TEXT, v, 0, 0);
    lv_obj_align(v1, LV_ALIGN_RIGHT_MID, -22, 0);

    switch (state)
    {
    case UPD_CHECKING:
    {
        lv_obj_t *sp = lv_spinner_create(scr);
        lv_obj_set_size(sp, 72, 72);
        lv_obj_align(sp, LV_ALIGN_TOP_MID, 0, 226);
        lv_obj_set_style_arc_color(sp, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_arc_color(sp, C_ACCENT, LV_PART_INDICATOR);
        break;
    }
    case UPD_LATEST:
        upd_badge(scr, true, tr(S_UPD_LATEST));
        break;
    case UPD_CHECK_FAILED:
    case UPD_NO_WIFI:
    case UPD_FAILED:
        upd_badge(scr, false,
                  tr(state == UPD_NO_WIFI ? S_UPD_NO_WIFI : state == UPD_FAILED ? S_UPD_FAILED : S_UPD_CHECK_FAILED));
        ui_kit_button(scr, tr(S_RETRY), 235, 392, 330, 64, false, post_ev, (void *)(intptr_t)EV_UPD_CHECK);
        break;
    case UPD_AVAILABLE:
    {
        /* The banner: amber border, the download glyph, the number in amber. */
        lv_obj_t *r2 = upd_row(scr, 170, 80);
        lv_obj_set_style_border_color(r2, C_AMBER, 0);
        lv_obj_set_style_border_width(r2, 3, 0);
        char label[64];
        snprintf(label, sizeof label, LV_SYMBOL_DOWNLOAD "   %s", tr(S_UPD_NEW));
        lv_obj_t *n2 = ui_kit_txt(r2, &font_ui_20, C_TEXT, label, 0, 0);
        lv_obj_align(n2, LV_ALIGN_LEFT_MID, 22, 0);
        snprintf(v, sizeof v, "v%s", offered ? offered : "?");
        lv_obj_t *v2 = ui_kit_txt(r2, &font_ui_24, C_AMBER, v, 0, 0);
        lv_obj_align(v2, LV_ALIGN_RIGHT_MID, -22, 0);

        ui_kit_button(scr, tr(S_UPD_INSTALL), 190, 336, 420, 76, true, post_ev, (void *)(intptr_t)EV_UPD_INSTALL);
        lv_obj_t *k = ui_kit_txt(scr, &font_ui_16, C_MUTED, tr(S_UPD_KEEPS), 0, 0);
        lv_obj_align(k, LV_ALIGN_BOTTOM_MID, 0, -22);
        break;
    }
    }
}

/* "Confirm update?" with the two-minute warning, Yes / No. */
void ui_show_update_confirm(void)
{
    lv_obj_t *scr = ui_kit_page();
    lv_obj_t *t = ui_kit_txt(scr, &font_ui_24, C_TEXT, tr(S_UPD_CONFIRM), 0, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 130);
    lv_obj_t *m = ui_kit_txt(scr, &font_ui_20, C_MUTED, tr(S_UPD_CONFIRM_WARN), 0, 0);
    lv_obj_set_width(m, 680);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_align(m, LV_ALIGN_TOP_MID, 0, 186);
    ui_kit_button(scr, tr(S_NO), 100, 320, 280, 68, false, post_ev, (void *)(intptr_t)EV_CANCEL);
    ui_kit_button(scr, tr(S_YES), 420, 320, 280, 68, true, post_ev, (void *)(intptr_t)EV_UPD_INSTALL);
}

static lv_obj_t *s_upd_scr;
static lv_obj_t *s_upd_ring;
static lv_obj_t *s_upd_pct;
static lv_obj_t *s_upd_msg;
static lv_obj_t *s_upd_warn;

void ui_show_update_progress(void)
{
    lv_obj_t *scr = ui_kit_page();
    s_upd_scr = scr;

    /* The ring: TigerScale's track and green fill, starting at 12 o'clock.
     * Not a control, an indicator: no knob, not clickable. */
    s_upd_ring = lv_arc_create(scr);
    lv_obj_set_size(s_upd_ring, 220, 220);
    lv_obj_align(s_upd_ring, LV_ALIGN_TOP_MID, 0, 60); /* centre at (400, 170) */
    lv_arc_set_rotation(s_upd_ring, 270);
    lv_arc_set_bg_angles(s_upd_ring, 0, 360);
    lv_arc_set_range(s_upd_ring, 0, 100);
    lv_arc_set_value(s_upd_ring, 0);
    lv_obj_remove_style(s_upd_ring, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(s_upd_ring, false);
    lv_obj_set_style_arc_width(s_upd_ring, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_upd_ring, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_upd_ring, C_RING_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_upd_ring, C_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_upd_ring, true, LV_PART_INDICATOR);

    /* The arrow, drawn with lines as on TigerScale: a line passes exactly
     * through the points given. Ink spans x 0..44, y 12..66, centre (22, 39);
     * placed at (378, 131) that centre lands on the ring's. */
    static const lv_point_precise_t SHAFT[] = {{22, 12}, {22, 66}};
    static const lv_point_precise_t HEAD[] = {{0, 34}, {22, 12}, {44, 34}};
    for (int i = 0; i < 2; i++)
    {
        lv_obj_t *l = lv_line_create(scr);
        lv_line_set_points(l, i == 0 ? SHAFT : HEAD, i == 0 ? 2 : 3);
        lv_obj_set_pos(l, 378, 131);
        lv_obj_set_style_line_color(l, C_GREEN, 0);
        lv_obj_set_style_line_width(l, 12, 0);
        lv_obj_set_style_line_rounded(l, true, 0);
    }

    s_upd_pct = ui_kit_txt(scr, &lv_font_montserrat_48, C_TEXT, "0%", 0, 0);
    lv_obj_align(s_upd_pct, LV_ALIGN_TOP_MID, 0, 296);
    s_upd_msg = ui_kit_txt(scr, &font_ui_20, C_MUTED, tr(S_UPD_INSTALLING), 0, 0);
    lv_obj_align(s_upd_msg, LV_ALIGN_TOP_MID, 0, 362);
    s_upd_warn = ui_kit_txt(scr, &font_ui_20, C_TEXT, tr(S_UPD_DO_NOT_POWER_OFF), 0, 0);
    lv_obj_align(s_upd_warn, LV_ALIGN_TOP_MID, 0, 404);
}

/* The page may have gone. Its pointers are compared, never followed, until it
 * is known to be the one on show: asking LVGL about a freed object reads freed
 * memory (CODEMAP.md, Landmines). */
static bool progress_on_show(void)
{
    return s_upd_scr && s_upd_scr == lv_screen_active();
}

void ui_update_progress(int percent)
{
    if (!progress_on_show())
    {
        return;
    }
    lv_arc_set_value(s_upd_ring, percent);
    lv_label_set_text_fmt(s_upd_pct, "%d%%", percent);
}

/* Full ring, "Rebooting", and the warning goes: nothing is left to interrupt. */
void ui_update_rebooting(void)
{
    if (!progress_on_show())
    {
        return;
    }
    lv_arc_set_value(s_upd_ring, 100);
    lv_label_set_text(s_upd_pct, "100%");
    lv_label_set_text(s_upd_msg, tr(S_UPD_REBOOTING));
    lv_obj_set_hidden(s_upd_warn, true);
}
