#include "ui_setup.h"
#include "ui_onboard.h"
#include "i18n.h"
#include "cloud.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "boot_splash.h"
#include "brand_logos.h"
#include "ui_cards.h"
#include "ui_dashboard.h"
#include "ui_theme.h"
#include "ui_wifi.h"

/* ---- Event channel -------------------------------------------------------- */

static QueueHandle_t s_q;

void ui_set_event_queue(QueueHandle_t q)
{
    s_q = q;
}

void ui_post(ev_type_t type, int index, const char *a, const char *b)
{
    app_event_t e = {0};
    e.type = type;
    e.index = index;
    if (a)
    {
        strlcpy(e.a, a, sizeof e.a);
    }
    if (b)
    {
        strlcpy(e.b, b, sizeof e.b);
    }
    if (s_q)
    {
        xQueueSend(s_q, &e, 0);
    }
}

/* ---- Small helpers -------------------------------------------------------- */

/* The dashboard is built once and kept alive: rebuilding it on every visit
 * showed its default values (and LVGL's half-filled arc) for a moment. */
static lv_obj_t *s_dash_scr;

static lv_obj_t *new_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);
    return scr;
}

static lv_obj_t *new_page(void)
{
    ui_side_prompt_close(); /* a reminder belongs to the screen it was shown over */
    ui_cards_forget(); /* the old screen's cards go with it */
    ui_wifi_set_transient(NULL); /* the old screen's wave is about to go */
    lv_obj_t *old = lv_screen_active();
    lv_obj_t *scr = new_screen();
    lv_screen_load(scr);
    /* The new screen is up before the old one goes: deleting the one on show
     * is a trap in LVGL. The dashboard is never deleted. */
    if (old && old != s_dash_scr)
    {
        lv_obj_delete(old);
    }
    return scr;
}

static lv_obj_t *txt(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *s, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, int h, bool primary,
                        lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, primary ? C_ACCENT : C_TILE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &font_ui_20, 0);
    lv_obj_set_style_text_color(l, primary ? C_BG : C_TEXT, 0);
    lv_obj_center(l);
    if (cb)
    {
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    }
    return b;
}

/* ---- Busy ----------------------------------------------------------------- */

void ui_show_busy(const char *title, const char *message)
{
    lv_obj_t *scr = new_page();
    lv_obj_t *sp = lv_spinner_create(scr);
    lv_obj_set_size(sp, 72, 72);
    lv_obj_align(sp, LV_ALIGN_CENTER, 0, -60);
    lv_obj_set_style_arc_width(sp, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(sp, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(sp, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_arc_color(sp, C_ACCENT, LV_PART_INDICATOR);

    lv_obj_t *t = txt(scr, &font_ui_24, C_TEXT, title, 0, 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 30);
    if (message && message[0])
    {
        lv_obj_t *m = txt(scr, &font_ui_20, C_MUTED, message, 0, 0);
        lv_obj_set_width(m, 640);
        lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(m, LV_ALIGN_CENTER, 0, 76);
    }
}

/* ---- Wi-Fi list ----------------------------------------------------------- */

static void on_row(lv_event_t *e)
{
    ui_post(EV_WIFI_PICK, (int)(intptr_t)lv_event_get_user_data(e), NULL, NULL);
}

static void on_rescan(lv_event_t *e)
{
    (void)e;
    ui_post(EV_RESCAN, 0, NULL, NULL);
}

/* A padlock from two shapes, for a secured network (TigerScale draws one too). */
static void padlock(lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 24, 28);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_clickable(box, false);
    lv_obj_align(box, LV_ALIGN_RIGHT_MID, -24, 0);
    lv_obj_t *shackle = lv_obj_create(box);
    lv_obj_remove_style_all(shackle);
    lv_obj_set_size(shackle, 16, 18);
    lv_obj_set_pos(shackle, 4, 0);
    lv_obj_set_style_radius(shackle, 8, 0);
    lv_obj_set_style_border_width(shackle, 3, 0);
    lv_obj_set_style_border_color(shackle, C_MUTED, 0);
    lv_obj_t *body = lv_obj_create(box);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, 24, 16);
    lv_obj_set_pos(body, 0, 12);
    lv_obj_set_style_radius(body, 3, 0);
    lv_obj_set_style_bg_color(body, C_MUTED, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
}

/* The network list, as TigerScale's tsPick_network(): the header carries the
 * back arrow and the rescan button, the networks are rows in a scrolling column
 * with the up / down rail, strongest first, the signal on the left and a
 * padlock on the right of a secured one. */
#define NET_ROW_H 76
#define NET_ROW_GAP 10

void ui_show_wifi_list(const wifi_net_t *nets, int n, const char *banner, bool allow_cancel)
{
    lv_obj_t *scr = new_page();
    char title[48];
    snprintf(title, sizeof title, "%s (2.4 GHz)", tr(S_WIFI));
    lv_obj_t *band = ui_kit_header(scr, title, EV_CANCEL);
    if (!allow_cancel)
    {
        /* The first setup has nowhere to go back to: no arrow, no press. */
        lv_obj_set_clickable(band, false);
        lv_obj_set_hidden(lv_obj_get_child(band, 0), true);
    }
    lv_obj_t *rescan = lv_button_create(band);
    lv_obj_set_size(rescan, 72, HEADER_H);
    lv_obj_align(rescan, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_bg_opa(rescan, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rescan, 0, 0);
    lv_obj_set_style_shadow_width(rescan, 0, 0);
    lv_obj_add_event_cb(rescan, on_rescan, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ri = lv_label_create(rescan);
    lv_label_set_text(ri, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(ri, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(ri, C_TEXT, 0);
    lv_obj_center(ri);

    lv_obj_t *list = ui_kit_scroll_list(scr, 4 * (NET_ROW_H + NET_ROW_GAP));
    if (banner && banner[0])
    {
        lv_obj_t *b = txt(list, &font_ui_20, C_RED, banner, 0, 0);
        lv_obj_set_width(b, LV_PCT(100));
        lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    }
    if (n <= 0)
    {
        txt(list, &font_ui_20, C_RED, tr(S_NO_NETWORKS), 0, 0);
    }
    for (int i = 0; i < n; i++)
    {
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), NET_ROW_H);
        lv_obj_set_style_bg_color(row, C_CARD, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, C_TILE, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(row, C_LINE, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 14, 0);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_clickable(row, true);
        lv_obj_add_event_cb(row, on_row, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *wave = ui_wifi_wave(row);
        lv_obj_align(wave, LV_ALIGN_LEFT_MID, 24, 0);
        ui_wifi_wave_set(wave, ui_wifi_level_from_rssi(nets[i].rssi), true);

        lv_obj_t *name = txt(row, &font_ui_24, C_TEXT, nets[i].ssid, 0, 0);
        lv_obj_set_width(name, 520);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 86, 0);

        if (nets[i].secure)
        {
            padlock(row);
        }
    }
    ui_kit_scroll_rail(scr, list);
}

/* ---- Text entry: the TigerScale V3 keyboard ------------------------------- */

/* The same four-row keymaps as the TigerScale V3: every printable ASCII
 * character is reachable across three modes (lower / ABC upper / &123
 * symbols), with cursor arrows around the space bar and a check key that
 * validates. "@" sits in the letter rows because this keyboard types e-mail
 * addresses as much as passwords. LVGL itself handles "abc"/"ABC", the
 * backspace, the arrows and the check key; "&123" is ours and is handled by
 * a callback below. Key numbers matter: the accent keys are picked by index. */
static const char *const KB_LOWER[] = {
    "&123", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", "@", "\n",
    "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
    LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};
static const lv_buttonmatrix_ctrl_t KB_CTRL_LOWER[] = {
    (lv_buttonmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 3), 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    (lv_buttonmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 3),
    (lv_buttonmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 3), 2, 2, 2, 2, 2, 2, 2, 2, 2, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    (lv_buttonmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 3), 10,
    (lv_buttonmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 3),
    (lv_buttonmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 4)};
static const char *const KB_UPPER[] = {
    "&123", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", "@", "\n",
    "_", "-", "Z", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
    LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};
static const char *const KB_SPEC[] = {
    "abc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "~", "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "=", "\n",
    "/", "\\", "{", "}", "[", "]", "<", ">", "?", ";", "\"", "'", "\n",
    "|", "`", "+", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};
static const lv_buttonmatrix_ctrl_t KB_CTRL_SPEC[] = {
    (lv_buttonmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 3), 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    (lv_buttonmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 3),
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, (lv_buttonmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 3), 7,
    (lv_buttonmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 3),
    (lv_buttonmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 3)};

static lv_obj_t *s_ta;

static void on_validate(lv_event_t *e)
{
    (void)e;
    ui_post(EV_SUBMIT, 0, lv_textarea_get_text(s_ta), "");
}

static void on_header_back(lv_event_t *e)
{
    (void)e;
    ui_post(EV_CANCEL, 0, NULL, NULL);
}

static void on_eye(lv_event_t *e)
{
    lv_obj_t *field = (lv_obj_t *)lv_event_get_user_data(e);
    const bool hidden = lv_textarea_get_password_mode(field);
    lv_textarea_set_password_mode(field, !hidden);
    lv_obj_t *icon = lv_obj_get_child(lv_event_get_target_obj(e), 0);
    if (icon)
    {
        lv_label_set_text(icon, hidden ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
    }
}

/* "&123" is not one of the strings the widget special-cases, so the stock
 * handler would type it into the field. */
static void on_kb_symbols(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target_obj(e);
    const uint32_t id = lv_buttonmatrix_get_selected_button(kb);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE)
    {
        return;
    }
    const char *txt = lv_buttonmatrix_get_button_text(kb, id);
    if (txt && strcmp(txt, "&123") == 0)
    {
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_SPECIAL);
        lv_event_stop_processing(e);
    }
}

/* The mode-return key and the check key are drawn in the accent colour: the
 * check is the "important button" of the screen, twinned with the Valider
 * pill. Painted at draw time, not through the shared CHECKED style, which
 * would also light up the other control keys. */
static void on_kb_draw(lv_event_t *e)
{
    lv_draw_task_t *t = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(t);
    if (!base || base->part != LV_PART_ITEMS)
    {
        return;
    }
    lv_obj_t *kb = lv_event_get_target_obj(e);
    const lv_keyboard_mode_t m = lv_keyboard_get_mode(kb);
    const uint32_t hot = (m == LV_KEYBOARD_MODE_TEXT_UPPER) ? 12 : (m == LV_KEYBOARD_MODE_SPECIAL) ? 0 : 0xFFFF;
    const uint32_t ok = (m == LV_KEYBOARD_MODE_SPECIAL) ? 42 : 38;
    if (base->id1 != hot && base->id1 != ok)
    {
        return;
    }
    const lv_draw_task_type_t type = lv_draw_task_get_type(t);
    if (type == LV_DRAW_TASK_TYPE_FILL)
    {
        lv_draw_fill_dsc_t *f = lv_draw_task_get_fill_dsc(t);
        if (f)
        {
            f->color = C_ACCENT;
        }
    }
    else if (type == LV_DRAW_TASK_TYPE_LABEL)
    {
        lv_draw_label_dsc_t *l = lv_draw_task_get_label_dsc(t);
        if (l)
        {
            l->color = C_TEXT;
        }
    }
}

void ui_show_form(const ui_form_t *f)
{
    lv_obj_t *scr = new_page();
    s_ta = NULL;

    /* Header band, as on the TigerScale: back arrow and title on the left,
     * the accent "Valider" pill on the right. The band itself is the back. */
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_size(hdr, 800, 64);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_scrollable(hdr, false);
    lv_obj_set_clickable(hdr, f->allow_cancel);
    if (f->allow_cancel)
    {
        lv_obj_add_event_cb(hdr, on_header_back, LV_EVENT_PRESSED, NULL);
        lv_obj_t *back = lv_label_create(hdr);
        lv_label_set_text(back, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_font(back, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(back, C_TEXT, 0);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 22, 0);
    }
    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, f->title);
    lv_obj_set_style_text_font(title, &font_ui_24, 0);
    lv_obj_set_style_text_color(title, C_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, f->allow_cancel ? 70 : 22, 0);
    lv_obj_t *div = lv_obj_create(hdr);
    lv_obj_remove_style_all(div);
    lv_obj_set_size(div, 800, 1);
    lv_obj_set_pos(div, 0, 63);
    lv_obj_set_style_bg_color(div, C_LINE, 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_clickable(div, false);

    lv_obj_t *ok = lv_button_create(scr);
    lv_obj_set_size(ok, 168, 46);
    lv_obj_align(ok, LV_ALIGN_TOP_RIGHT, -12, 9);
    lv_obj_set_style_bg_color(ok, C_ACCENT, 0);
    lv_obj_set_style_border_width(ok, 0, 0);
    lv_obj_set_style_radius(ok, 23, 0);
    lv_obj_set_style_shadow_width(ok, 0, 0);
    lv_obj_add_event_cb(ok, on_validate, LV_EVENT_CLICKED, NULL);
    lv_obj_t *okl = lv_label_create(ok);
    lv_label_set_text(okl, tr(S_CONFIRM));
    lv_obj_set_style_text_font(okl, &font_ui_20, 0);
    lv_obj_set_style_text_color(okl, C_TEXT, 0);
    lv_obj_center(okl);

    /* One line of context under the band: what this is for, or what went wrong. */
    if (f->subtitle && f->subtitle[0])
    {
        lv_obj_t *sub = txt(scr, &font_ui_16, C_MUTED, f->subtitle, 14, 70);
        lv_obj_set_width(sub, f->error && f->error[0] ? 380 : 770);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
    }
    if (f->error && f->error[0])
    {
        lv_obj_t *er = txt(scr, &font_ui_16, C_RED, f->error, 0, 70);
        lv_obj_set_width(er, 380);
        lv_obj_set_style_text_align(er, LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(er, LV_LABEL_LONG_DOT);
        lv_obj_align(er, LV_ALIGN_TOP_RIGHT, -14, 70);
    }

    s_ta = lv_textarea_create(scr);
    lv_obj_set_size(s_ta, f->secret1 ? 700 : 784, 52);
    lv_obj_set_pos(s_ta, 8, 96);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_max_length(s_ta, 64);
    lv_textarea_set_password_mode(s_ta, f->secret1);
    if (f->secret1)
    {
        lv_textarea_set_password_show_time(s_ta, 900);
    }
    if (f->label1)
    {
        lv_textarea_set_placeholder_text(s_ta, f->label1);
    }
    lv_obj_set_style_bg_color(s_ta, C_CARD, 0);
    lv_obj_set_style_bg_opa(s_ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_ta, C_LINE, 0);
    lv_obj_set_style_border_width(s_ta, 1, 0);
    lv_obj_set_style_radius(s_ta, 10, 0);
    lv_obj_set_style_text_color(s_ta, C_TEXT, 0);
    lv_obj_set_style_text_font(s_ta, &font_ui_20, 0);
    lv_obj_set_style_text_color(s_ta, C_MUTED, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(s_ta, C_ACCENT, LV_PART_CURSOR);
    lv_obj_set_style_border_width(s_ta, 2, LV_PART_CURSOR);
    if (f->prefill1 && f->prefill1[0])
    {
        lv_textarea_set_text(s_ta, f->prefill1);
    }

    if (f->secret1)
    {
        /* The eye: show / hide, like every password field people know. */
        lv_obj_t *eye = lv_button_create(scr);
        lv_obj_set_size(eye, 76, 52);
        lv_obj_set_pos(eye, 716, 96);
        lv_obj_set_style_bg_color(eye, C_CARD, 0);
        lv_obj_set_style_border_color(eye, C_LINE, 0);
        lv_obj_set_style_border_width(eye, 1, 0);
        lv_obj_set_style_radius(eye, 10, 0);
        lv_obj_set_style_shadow_width(eye, 0, 0);
        lv_obj_t *icon = lv_label_create(eye);
        lv_label_set_text(icon, LV_SYMBOL_EYE_CLOSE);
        lv_obj_set_style_text_font(icon, &font_ui_20, 0);
        lv_obj_set_style_text_color(icon, C_MUTED, 0);
        lv_obj_center(icon);
        lv_obj_add_event_cb(eye, on_eye, LV_EVENT_CLICKED, s_ta);
    }

    lv_obj_t *kb = lv_keyboard_create(scr);
    lv_obj_set_size(kb, 788, 322);
    lv_obj_align(kb, LV_ALIGN_TOP_LEFT, 6, 154);
    lv_obj_set_scrollable(kb, false);
    lv_keyboard_set_textarea(kb, s_ta);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, KB_LOWER, KB_CTRL_LOWER);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, KB_UPPER, KB_CTRL_LOWER);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, KB_SPEC, KB_CTRL_SPEC);
    lv_obj_set_style_bg_color(kb, C_BG, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_pad_all(kb, 3, 0);
    lv_obj_set_style_pad_gap(kb, 5, 0);
    lv_obj_set_style_min_height(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(kb, 2, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, C_CARD, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, C_LINE, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, &font_ui_20, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, C_BG, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb, C_MUTED, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(kb, C_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_send_draw_task_events(kb, true);
    lv_obj_add_event_cb(kb, on_kb_draw, LV_EVENT_DRAW_TASK_ADDED, NULL);

    /* Callbacks fire in registration order: take the stock one off, register
     * the "&123" switcher, put the stock one back. */
    lv_obj_remove_event_cb(kb, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(kb, on_kb_symbols, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(kb, lv_keyboard_def_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(kb, on_validate, LV_EVENT_READY, NULL); /* the check key */
}

/* ---- Linked --------------------------------------------------------------- */

static void on_ok(lv_event_t *e)
{
    (void)e;
    ui_post(EV_OK, 0, NULL, NULL);
}

void ui_show_linked(const char *email)
{
    /* Only the confirmation, as on TigerScale and TigerSpool: the printers are
     * the next page, not a list squeezed under the badge. */
    lv_obj_t *scr = new_page();
    lv_obj_t *badge = lv_obj_create(scr);
    lv_obj_set_size(badge, 96, 96);
    lv_obj_align(badge, LV_ALIGN_CENTER, 0, -80);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0x1f3d31), 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_scrollable(badge, false);
    lv_obj_t *ok = lv_label_create(badge);
    lv_label_set_text(ok, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(ok, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ok, C_GREEN, 0);
    lv_obj_center(ok);

    lv_obj_t *t = txt(scr, &font_ui_24, C_TEXT, tr(S_ACCOUNT_LINKED), 0, 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 20);
    lv_obj_t *em = txt(scr, &font_ui_20, C_MUTED, email, 0, 0);
    lv_obj_align(em, LV_ALIGN_CENTER, 0, 60);
    /* A touch anywhere moves on before the few seconds are up. */
    lv_obj_set_clickable(scr, true);
    lv_obj_add_event_cb(scr, on_ok, LV_EVENT_CLICKED, NULL);
}

/* ---- Dashboard ------------------------------------------------------------ */

void ui_show_splash(void)
{
    lv_obj_t *scr = new_page();
    lv_obj_t *img = lv_image_create(scr);
    lv_image_set_src(img, &boot_splash);
    lv_obj_set_pos(img, 0, 0);
}

void ui_prepare_dashboard(void)
{
    if (s_dash_scr)
    {
        return;
    }
    s_dash_scr = new_screen();
    ui_dashboard_create(s_dash_scr);
}

void ui_show_dashboard(void)
{
    ui_prepare_dashboard();
    lv_obj_t *old = lv_screen_active();
    if (old == s_dash_scr)
    {
        return;
    }
    /* The wave of the screen about to go must not be touched by the timer
     * again: new_page() does this for every other screen. */
    ui_wifi_set_transient(NULL);
    lv_screen_load(s_dash_scr);
    if (old)
    {
        lv_obj_delete(old);
    }
}

/* ---- Printers page -------------------------------------------------------- */

static void on_printer_row(lv_event_t *e)
{
    ui_post(EV_PRINTER_PICK, (int)(intptr_t)lv_event_get_user_data(e), NULL, NULL);
}

static void on_list_settings(lv_event_t *e)
{
    (void)e;
    ui_post(EV_SETTINGS, 0, NULL, NULL);
}

static void on_toggle_view(lv_event_t *e)
{
    (void)e;
    ui_post(EV_TOGGLE_VIEW, 0, NULL, NULL);
}

/* A small rounded block, part of the view button's drawing. */
static void view_block(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 2, 0);
    lv_obj_set_style_bg_color(o, C_TEXT, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
}

/* The button that changes the home's view draws the view it leads to: three
 * lines for the list, two cards side by side for the cards. */
static void view_button(lv_obj_t *scr)
{
    lv_obj_t *b = lv_button_create(scr);
    lv_obj_set_size(b, 56, 60);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -152, 0);
    ui_kit_on_header(b);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_add_event_cb(b, on_toggle_view, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ico = lv_obj_create(b);
    lv_obj_set_size(ico, 26, 22);
    lv_obj_set_style_bg_opa(ico, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ico, 0, 0);
    lv_obj_set_style_pad_all(ico, 0, 0);
    lv_obj_set_scrollable(ico, false);
    lv_obj_set_clickable(ico, false);
    lv_obj_center(ico);
    if (ui_cards_is_list())
    {
        view_block(ico, 0, 0, 11, 22);
        view_block(ico, 15, 0, 11, 22);
    }
    else
    {
        for (int i = 0; i < 3; i++)
        {
            view_block(ico, 0, i * 9, 26, 4);
        }
    }
}

/* The clock in the middle of the home's top line: hours and minutes, local
 * time (the time zone follows the country). "--:--" until the clock is set.
 * Written only when the minute changes: a label set to the same text is still
 * redrawn. The timer goes with the label, never after it. */
static void clock_text(char *out, size_t n)
{
    const time_t now = time(NULL);
    if (now < 1700000000)
    {
        snprintf(out, n, "--:--");
        return;
    }
    struct tm lt;
    localtime_r(&now, &lt);
    strftime(out, n, "%H:%M", &lt);
}

static void clock_tick(lv_timer_t *t)
{
    lv_obj_t *l = (lv_obj_t *)lv_timer_get_user_data(t);
    char buf[8];
    clock_text(buf, sizeof buf);
    if (strcmp(lv_label_get_text(l), buf) != 0)
    {
        lv_label_set_text(l, buf);
    }
}

static void clock_gone(lv_event_t *e)
{
    lv_timer_delete((lv_timer_t *)lv_event_get_user_data(e));
}

void ui_kit_on_header(lv_obj_t *o)
{
    lv_obj_update_layout(o);
    lv_obj_set_y(o, HEADER_MID - lv_obj_get_height(o) / 2);
}

lv_obj_t *ui_kit_clock(lv_obj_t *parent)
{
    char buf[8];
    clock_text(buf, sizeof buf);
    lv_obj_t *l = txt(parent, &font_ui_24, C_TEXT, buf, 0, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 0);
    ui_kit_on_header(l);
    lv_timer_t *t = lv_timer_create(clock_tick, 1000, l);
    lv_obj_add_event_cb(l, clock_gone, LV_EVENT_DELETE, t);
    return l;
}

/* The printers page: the header (settings, Wi-Fi, account) and one live card per
 * switched-on printer, which ui_cards.c draws and keeps current. */
void ui_show_printers(const printer_row_t *rows, int n)
{
    lv_obj_t *scr = new_page();
    /* The home's top line, in TigerScale's grammar: the title in white, then the
     * Wi-Fi wave and the account, then the gear as a bare icon - no boxes. */
    lv_obj_t *t = txt(scr, &font_ui_24, C_TEXT, tr(S_PRINTERS), 24, 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 24, 0);
    ui_kit_on_header(t);
    ui_kit_clock(scr);

    lv_obj_t *gear = lv_button_create(scr);
    lv_obj_set_size(gear, 72, 60);
    lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, -4, 0);
    ui_kit_on_header(gear);
    lv_obj_set_style_bg_opa(gear, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gear, 0, 0);
    lv_obj_set_style_shadow_width(gear, 0, 0);
    lv_obj_t *gl = lv_label_create(gear);
    lv_label_set_text(gl, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(gl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(gl, C_TEXT, 0);
    lv_obj_center(gl);
    lv_obj_add_event_cb(gear, on_list_settings, LV_EVENT_CLICKED, NULL);

    lv_obj_t *wave = ui_wifi_wave(scr);
    lv_obj_align(wave, LV_ALIGN_TOP_RIGHT, -120, 0);
    ui_kit_on_header(wave);
    ui_wifi_set_transient(wave);
    lv_obj_t *acct = ui_account_icon(scr);
    lv_obj_align(acct, LV_ALIGN_TOP_RIGHT, -84, 0);
    ui_kit_on_header(acct);
    ui_account_set_transient(acct);
    if (n > 0)
    {
        view_button(scr);
    }

    if (n == 0)
    {
        txt(scr, &font_ui_20, C_MUTED, tr(S_NO_PRINTER_ON), 32, 84);
        txt(scr, &font_ui_16, C_MUTED, tr(S_NO_PRINTER_ON_HINT), 32, 120);
    }
    else
    {
        ui_cards_create(scr, rows, n, on_printer_row);
    }
}

/* ---- Shared with ui_settings.c ---------------------------------------------- */

lv_obj_t *ui_kit_page(void)
{
    return new_page();
}

lv_obj_t *ui_kit_txt(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *s, int x, int y)
{
    return txt(parent, font, color, s, x, y);
}

lv_obj_t *ui_kit_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h, bool primary,
                        lv_event_cb_t cb, void *user)
{
    return button(parent, text, x, y, w, h, primary, cb, user);
}

/* The dashboard is built once and keeps its texts, so a new language needs it
 * built again. Never called while it is the screen on show. */
void ui_language_changed(void)
{
    if (!s_dash_scr || lv_screen_active() == s_dash_scr)
    {
        return;
    }
    ui_dashboard_invalidate();
    lv_obj_delete(s_dash_scr);
    s_dash_scr = NULL;
    ui_prepare_dashboard();
}
