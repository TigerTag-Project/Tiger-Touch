#include "ui_job.h"

#include "i18n.h"
#include "pictos.h"
#include "ui_setup.h"
#include "ui_theme.h"

#define LIGHT_ON lv_color_hex(0xf5b84a)

static void on_pause(lv_event_t *e)
{
    job_buttons_t *b = (job_buttons_t *)lv_event_get_user_data(e);
    ui_post(EV_PRINTER_JOB, b->index, b->paused ? "resume" : "pause", NULL);
}

static void on_light(lv_event_t *e)
{
    job_buttons_t *b = (job_buttons_t *)lv_event_get_user_data(e);
    ui_post(EV_PRINTER_JOB, b->index, b->light_on > 0 ? "light_off" : "light_on", NULL);
}

/* ---- Stop: a touch, then a confirmation -----------------------------------
 *
 * Stopping destroys a print that may be hours in, so a touch only asks: a
 * modal over the screen says what will happen, and only its red button sends
 * the command. A touch outside it, or Cancel, closes it. It belongs to the
 * screen on show and goes with it; it keeps the row it was opened for, not a
 * pointer into the card, which can be rebuilt while it is open. */

static lv_obj_t *s_modal;
static int s_modal_index;

static void close_modal(void)
{
    if (s_modal)
    {
        lv_obj_delete(s_modal);
        s_modal = NULL;
    }
}

static void on_modal_gone(lv_event_t *e)
{
    (void)e;
    s_modal = NULL; /* deleted with its screen */
}

static void on_modal_cancel(lv_event_t *e)
{
    /* The backdrop takes the touches outside the panel; the panel's own do not reach it. */
    if (lv_event_get_target_obj(e) == lv_event_get_current_target_obj(e))
    {
        close_modal();
    }
}

static void on_modal_stop(lv_event_t *e)
{
    (void)e;
    const int index = s_modal_index;
    close_modal();
    ui_post(EV_PRINTER_JOB, index, "stop", NULL);
}

static void open_stop_modal(int index)
{
    close_modal();
    s_modal_index = index;
    lv_obj_t *bg = s_modal = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_60, 0);
    lv_obj_set_clickable(bg, true);
    lv_obj_add_event_cb(bg, on_modal_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(bg, on_modal_gone, LV_EVENT_DELETE, NULL);

    const int w = 520, h = 248;
    lv_obj_t *panel = lv_obj_create(bg);
    lv_obj_set_size(panel, w, h);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, C_CARD, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, C_LINE, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 18, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_clickable(panel, true); /* a touch on it is not a touch outside */

    lv_obj_t *icon = lv_label_create(panel);
    lv_label_set_text(icon, LV_SYMBOL_STOP);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(icon, C_RED, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_t *q = ui_kit_txt(panel, &font_ui_24, C_TEXT, tr(S_STOP_Q), 0, 0);
    lv_obj_align(q, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_t *warn = ui_kit_txt(panel, &font_ui_16, C_MUTED, tr(S_STOP_WARN), 0, 0);
    lv_obj_set_width(warn, w - 48);
    lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(warn, LV_LABEL_LONG_WRAP);
    lv_obj_align(warn, LV_ALIGN_TOP_MID, 0, 110);

    const int bw = (w - 24 * 2 - 16) / 2, bh = 56, by = h - bh - 22;
    lv_obj_t *cancel = ui_kit_button(panel, tr(S_CANCEL), 24, by, bw, bh, false, NULL, NULL);
    lv_obj_add_event_cb(cancel, on_modal_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_t *stop = ui_kit_button(panel, tr(S_STOP), 24 + bw + 16, by, bw, bh, true, NULL, NULL);
    lv_obj_set_style_bg_color(stop, C_RED, 0);
    lv_obj_add_event_cb(stop, on_modal_stop, LV_EVENT_CLICKED, NULL);
}

static void on_stop(lv_event_t *e)
{
    const job_buttons_t *b = (const job_buttons_t *)lv_event_get_user_data(e);
    open_stop_modal(b->index);
}

static lv_obj_t *mk_button(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, C_TILE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 10, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, true);
    return o;
}

/* A button this tall (the printers page's and the full view's) takes a larger symbol. */
#define BIG_BUTTON_H 44

static lv_obj_t *mk_icon(lv_obj_t *parent, const char *symbol, lv_color_t col, int button_h)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, symbol);
    lv_obj_set_style_text_font(l, button_h >= BIG_BUTTON_H ? &lv_font_montserrat_24 : &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_center(l);
    return l;
}

/* The pair is drawn all the time; "active" makes it clickable and full strength,
 * the rest of the time it is dimmed and takes no touch. */
static void set_active(job_buttons_t *b, bool active)
{
    b->shown = active;
    lv_obj_set_clickable(b->pause, active);
    lv_obj_set_clickable(b->stop, active);
    lv_obj_set_style_opa(b->pause, active ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_opa(b->stop, active ? LV_OPA_COVER : LV_OPA_40, 0);
}

void ui_job_build(job_buttons_t *b, lv_obj_t *parent, int px, int py, int sx, int sy, int w, int h, int lx, int ly,
                  int lw, int lh, int index)
{
    b->index = index;
    b->shown = false;
    b->paused = false;
    b->light_can = -1;
    b->light_on = -1;

    b->light = mk_button(parent, lx, ly, lw, lh);
    lv_obj_set_style_bg_color(b->light, C_LINE, LV_STATE_PRESSED);
    b->light_icon = picto_create(b->light, PICTO_BULB, 24, C_MUTED);
    if (lh >= BIG_BUTTON_H)
    {
        lv_image_set_scale(b->light_icon, 341); /* 24 px mask drawn at 32 */
    }
    lv_obj_center(b->light_icon);
    lv_obj_add_event_cb(b->light, on_light, LV_EVENT_CLICKED, b);
    ui_job_set_light(b, false, false);

    b->pause = mk_button(parent, px, py, w, h);
    lv_obj_set_style_bg_color(b->pause, C_LINE, LV_STATE_PRESSED);
    b->pause_icon = mk_icon(b->pause, LV_SYMBOL_PAUSE, C_TEXT, h);
    lv_obj_add_event_cb(b->pause, on_pause, LV_EVENT_CLICKED, b);

    b->stop = mk_button(parent, sx, sy, w, h);
    lv_obj_set_style_border_width(b->stop, 2, 0);
    lv_obj_set_style_border_color(b->stop, C_RED, 0);
    lv_obj_set_style_bg_color(b->stop, C_LINE, LV_STATE_PRESSED);
    b->stop_icon = mk_icon(b->stop, LV_SYMBOL_STOP, C_RED, h);
    lv_obj_add_event_cb(b->stop, on_stop, LV_EVENT_CLICKED, b);

    /* Always drawn, like the light; inert until a job runs on a connection that
     * takes commands (see ui_job_set). */
    set_active(b, false);
}

void ui_job_set_light(job_buttons_t *b, bool can, bool on)
{
    if (!b->light)
    {
        return;
    }
    if ((int)can != b->light_can)
    {
        b->light_can = can;
        lv_obj_set_clickable(b->light, can);
        lv_obj_set_style_opa(b->light, can ? LV_OPA_COVER : LV_OPA_40, 0);
    }
    if ((int)on != b->light_on)
    {
        b->light_on = on;
        lv_obj_set_style_image_recolor(b->light_icon, on ? LIGHT_ON : C_MUTED, 0);
    }
}

void ui_job_set(job_buttons_t *b, bool active, bool paused)
{
    if (!b->pause)
    {
        return;
    }
    if (active != b->shown)
    {
        set_active(b, active);
    }
    if (paused != b->paused)
    {
        b->paused = paused;
        lv_label_set_text(b->pause_icon, paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
    }
}
