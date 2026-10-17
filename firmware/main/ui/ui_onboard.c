/* The first-boot setup and the account screens, after TigerScale V3's
 * (obPrompt, runSignInForm, runAccountPairing), at the screen's 800x480. */

#include "ui_onboard.h"

#include <string.h>

#include "i18n.h"
#include "ui_setup.h"
#include "ui_theme.h"

/* ---- Shared pieces -------------------------------------------------------- */

static void post_cb(lv_event_t *e)
{
    ui_post((ev_type_t)(intptr_t)lv_event_get_user_data(e), 0, NULL, NULL);
}

/* The account silhouette, two discs, as on TigerScale's prompt and account page. */
static lv_obj_t *silhouette(lv_obj_t *parent, int size, lv_color_t col)
{
    lv_obj_t *face = lv_obj_create(parent);
    lv_obj_remove_style_all(face);
    lv_obj_set_size(face, size, size);
    lv_obj_set_scrollable(face, false);
    lv_obj_set_clickable(face, false);
    const int head = size * 2 / 5;
    lv_obj_t *h = lv_obj_create(face);
    lv_obj_remove_style_all(h);
    lv_obj_set_size(h, head, head);
    lv_obj_set_style_radius(h, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(h, col, 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *b = lv_obj_create(face);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, size, size / 2);
    lv_obj_set_style_radius(b, size / 4, 0);
    lv_obj_set_style_bg_color(b, col, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, 0);
    return face;
}

static void prompt_icon(lv_obj_t *col, prompt_kind_t kind, int size)
{
    if (kind == PROMPT_WIFI)
    {
        lv_obj_t *icon = lv_label_create(col);
        lv_label_set_text(icon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(icon, C_GREEN, 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
    }
    else
    {
        silhouette(col, size, C_GREEN);
    }
}

static lv_obj_t *column(lv_obj_t *parent, int width)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, width, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 16, 0);
    lv_obj_set_scrollable(col, false);
    lv_obj_set_clickable(col, false);
    return col;
}

static lv_obj_t *centered_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *s, int width)
{
    lv_obj_t *l = ui_kit_txt(parent, font, color, s, 0, 0);
    lv_obj_set_width(l, width);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

/* ---- Full-screen question ------------------------------------------------- */

void ui_show_prompt(prompt_kind_t kind, const char *title, const char *sub, const char *primary)
{
    ui_side_prompt_close();
    lv_obj_t *scr = ui_kit_page();
    lv_obj_t *col = column(scr, 640);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, -50);
    prompt_icon(col, kind, 64);
    centered_text(col, &font_ui_24, C_TEXT, title, 640);
    if (sub && sub[0])
    {
        centered_text(col, &font_ui_20, C_MUTED, sub, 640);
    }
    ui_kit_button(scr, tr(S_LATER), 120, 350, 270, 64, false, post_cb, (void *)(intptr_t)EV_PROMPT_LATER);
    ui_kit_button(scr, primary, 410, 350, 270, 64, true, post_cb, (void *)(intptr_t)EV_PROMPT_GO);
}

/* ---- The reminder panel --------------------------------------------------- */

/* Lives on LVGL's top layer, above whichever screen is on show, so a reminder
 * never rebuilds the home screen under it. The pointer is cleared before the
 * object is deleted, and never asked about once it may be gone. */
static lv_obj_t *s_side;

static void side_answer(lv_event_t *e)
{
    const ev_type_t ev = (ev_type_t)(intptr_t)lv_event_get_user_data(e);
    if (s_side)
    {
        lv_obj_t *o = s_side;
        s_side = NULL;
        lv_obj_delete_async(o);
    }
    ui_post(ev, 0, NULL, NULL);
}

static void side_slide(void *o, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)o, v);
}

void ui_show_side_prompt(prompt_kind_t kind, const char *title, const char *primary)
{
    ui_side_prompt_close();
    lv_obj_t *back = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, 800, 480);
    lv_obj_set_style_bg_color(back, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_60, 0);
    lv_obj_set_scrollable(back, false);
    lv_obj_set_clickable(back, true); /* a touch outside the panel is "Later" */
    lv_obj_add_event_cb(back, side_answer, LV_EVENT_CLICKED, (void *)(intptr_t)EV_PROMPT_LATER);
    s_side = back;

    /* A notification, not a dialog: full height, from the right, the screen
     * still visible and dimmed on the left. */
    lv_obj_t *panel = lv_obj_create(back);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 380, 480);
    lv_obj_set_style_bg_color(panel, C_CARD, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, C_LINE, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_side(panel, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_clickable(panel, true); /* touches on the panel stay on it */
    lv_obj_set_pos(panel, 800, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, panel);
    lv_anim_set_values(&a, 800, 420);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_exec_cb(&a, side_slide);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    lv_obj_t *col = column(panel, 330);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, -60);
    prompt_icon(col, kind, 56);
    centered_text(col, &font_ui_24, C_TEXT, title, 330);

    lv_obj_t *go = ui_kit_button(panel, primary, 25, 330, 330, 60, true, side_answer,
                                 (void *)(intptr_t)EV_PROMPT_GO);
    (void)go;
    ui_kit_button(panel, tr(S_LATER), 25, 400, 330, 56, false, side_answer, (void *)(intptr_t)EV_PROMPT_LATER);
}

void ui_side_prompt_close(void)
{
    if (s_side)
    {
        lv_obj_t *o = s_side;
        s_side = NULL;
        lv_obj_delete(o);
    }
}

bool ui_side_prompt_open(void)
{
    return s_side != NULL;
}

/* ---- Header with a back arrow -------------------------------------------- */

/* The shared header (ui_kit_header): the band is the back button, on PRESSED;
 * a large chevron and the title, on the line every screen's top line uses. */
static void header(lv_obj_t *scr, const char *title)
{
    ui_kit_header(scr, title, EV_CANCEL);
}

/* ---- Sign-in page --------------------------------------------------------- */

/* A field that opens the keyboard: a card showing the value, or the label in
 * grey while it is empty. */
static void field(lv_obj_t *scr, int y, const char *label, const char *value, ev_type_t ev)
{
    lv_obj_t *f = lv_obj_create(scr);
    lv_obj_set_pos(f, 100, y);
    lv_obj_set_size(f, 600, 64);
    lv_obj_set_style_bg_color(f, C_CARD, 0);
    lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(f, C_TILE, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(f, C_LINE, 0);
    lv_obj_set_style_border_width(f, 1, 0);
    lv_obj_set_style_radius(f, 12, 0);
    lv_obj_set_style_pad_all(f, 0, 0);
    lv_obj_set_scrollable(f, false);
    lv_obj_set_clickable(f, true);
    lv_obj_add_event_cb(f, post_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ev);
    const bool empty = !value || !value[0];
    lv_obj_t *l = ui_kit_txt(f, &font_ui_20, empty ? C_MUTED : C_TEXT, empty ? label : value, 0, 0);
    lv_obj_set_width(l, 560);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 20, 0);
}

/* Google's mark: the four colours, drawn as four short arcs. Not the logo file,
 * which may not be redistributed; it is what the button needs to be recognised. */
static void google_mark(lv_obj_t *parent)
{
    static const uint32_t COLS[4] = {0x4285F4, 0x34A853, 0xFBBC05, 0xEA4335};
    static const int START[4] = {330, 30, 135, 200};
    static const int END[4] = {30, 135, 200, 300};
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 30, 30);
    lv_obj_set_clickable(box, false);
    lv_obj_align(box, LV_ALIGN_LEFT_MID, 22, 0);
    for (int i = 0; i < 4; i++)
    {
        lv_obj_t *arc = lv_arc_create(box);
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
        lv_obj_set_size(arc, 30, 30);
        lv_arc_set_bg_angles(arc, 0, 0);
        lv_arc_set_angles(arc, START[i], END[i]);
        lv_obj_set_style_arc_width(arc, 6, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, lv_color_hex(COLS[i]), LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_clickable(arc, false);
        lv_obj_center(arc);
    }
    lv_obj_t *bar = lv_obj_create(box);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 13, 6);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLS[0]), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_pos(bar, 15, 12);
}

/* Eight bullets (U+2022), for a password that is set and never shown. */
#define DOTS "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2"

void ui_show_sign_in(const char *email, bool has_password, const char *error)
{
    ui_side_prompt_close();
    lv_obj_t *scr = ui_kit_page();
    header(scr, tr(S_LINK_ACCOUNT));

    lv_obj_t *sub = ui_kit_txt(scr, &font_ui_16, error && error[0] ? C_RED : C_MUTED,
                               error && error[0] ? error : tr(S_SAME_ACCOUNT), 0, 0);
    lv_obj_set_width(sub, 600);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(sub, 100, 80);

    field(scr, 110, tr(S_EMAIL), email, EV_EDIT_EMAIL);
    field(scr, 186, tr(S_PASSWORD), has_password ? DOTS : NULL, EV_EDIT_PASS);
    ui_kit_button(scr, tr(S_SIGN_IN), 100, 266, 600, 64, true, post_cb, (void *)(intptr_t)EV_SUBMIT);

    /* "Continue with Google": white, as Google asks its button to be, with the
     * mark on the left. Leads to the QR pairing. */
    lv_obj_t *g = lv_button_create(scr);
    lv_obj_set_pos(g, 100, 352);
    lv_obj_set_size(g, 600, 64);
    lv_obj_set_style_bg_color(g, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g, 12, 0);
    lv_obj_set_style_border_width(g, 0, 0);
    lv_obj_set_style_shadow_width(g, 0, 0);
    lv_obj_add_event_cb(g, post_cb, LV_EVENT_CLICKED, (void *)(intptr_t)EV_GOOGLE);
    google_mark(g);
    lv_obj_t *gl = lv_label_create(g);
    lv_label_set_text(gl, tr(S_CONTINUE_GOOGLE));
    lv_obj_set_style_text_font(gl, &font_ui_20, 0);
    lv_obj_set_style_text_color(gl, lv_color_hex(0x1f1f1f), 0);
    lv_obj_center(gl);
}

/* ---- Pairing page --------------------------------------------------------- */

static lv_obj_t *s_pair_scr;
static lv_obj_t *s_pair_box;
static lv_obj_t *s_pair_code;
static lv_obj_t *s_pair_scan;
static lv_obj_t *s_pair_wait;
static lv_obj_t *s_pair_status;

void ui_show_pairing(void)
{
    ui_side_prompt_close();
    lv_obj_t *scr = ui_kit_page();
    s_pair_scr = scr;
    header(scr, tr(S_LINK_ACCOUNT));

    /* Everything that does not need the network is drawn now, so the page is
     * whole the moment it appears; only the QR square shows it is waiting. */
    s_pair_box = lv_obj_create(scr);
    lv_obj_remove_style_all(s_pair_box);
    lv_obj_set_size(s_pair_box, 320, 320);
    lv_obj_set_pos(s_pair_box, 60, 104);
    lv_obj_set_style_bg_color(s_pair_box, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_pair_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_pair_box, 12, 0);
    lv_obj_set_scrollable(s_pair_box, false);
    lv_obj_t *sp = lv_spinner_create(s_pair_box);
    lv_obj_set_size(sp, 72, 72);
    lv_obj_center(sp);
    lv_obj_set_style_arc_color(sp, lv_color_hex(0xd8d8d8), LV_PART_MAIN);
    lv_obj_set_style_arc_color(sp, C_ACCENT, LV_PART_INDICATOR);

    s_pair_scan = ui_kit_txt(scr, &font_ui_24, C_TEXT, tr(S_PAIR_SCAN), 420, 150);
    lv_obj_set_width(s_pair_scan, 340);
    lv_label_set_long_mode(s_pair_scan, LV_LABEL_LONG_WRAP);
    s_pair_code = ui_kit_txt(scr, &lv_font_montserrat_48, C_TEXT, "", 420, 220);
    s_pair_wait = ui_kit_txt(scr, &font_ui_20, C_MUTED, tr(S_PAIR_WAITING), 420, 300);
    s_pair_status = NULL;
}

/* The pointers are good only while the pairing page is the one on show:
 * compared, never followed, otherwise (CODEMAP.md, Landmines). */
static bool pairing_on_show(void)
{
    return s_pair_scr && s_pair_scr == lv_screen_active();
}

void ui_pairing_set(const char *verify_url, const char *code)
{
    if (!pairing_on_show())
    {
        return;
    }
    lv_obj_clean(s_pair_box);
    lv_label_set_text(s_pair_code, code);
    /* The URL carries the code, so scanning is the whole interaction. The code
     * is shown anyway: a QR seen at an angle does not always scan. */
    lv_obj_t *qr = lv_qrcode_create(s_pair_box);
    lv_qrcode_set_size(qr, 290);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    if (lv_qrcode_update(qr, verify_url, strlen(verify_url)) != LV_RESULT_OK)
    {
        lv_obj_delete(qr); /* the code alone is still a way through */
        return;
    }
    lv_obj_center(qr);
}

void ui_pairing_result(const char *text, bool ok, const char *detail)
{
    if (!pairing_on_show())
    {
        return;
    }
    lv_obj_set_hidden(s_pair_box, true);
    lv_obj_set_hidden(s_pair_scan, true);
    lv_obj_set_hidden(s_pair_code, true);
    lv_obj_set_hidden(s_pair_wait, true);
    if (!s_pair_status)
    {
        lv_obj_t *col = column(s_pair_scr, 700);
        lv_obj_align(col, LV_ALIGN_CENTER, 0, 20);
        if (ok)
        {
            silhouette(col, 64, C_GREEN);
        }
        s_pair_status = centered_text(col, &font_ui_24, ok ? C_GREEN : C_RED, text, 700);
        if (detail && detail[0])
        {
            centered_text(col, &font_ui_20, C_MUTED, detail, 700);
        }
    }
}
