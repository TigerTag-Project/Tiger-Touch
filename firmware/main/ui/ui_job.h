#pragma once

#include <stdbool.h>

#include "lvgl.h"

/* The two buttons that act on a print: pause or resume, and stop. The printers
 * page puts a pair on each card and the full view puts one under the printer's
 * state; both are built here so that they behave the same.
 *
 *  - pause / resume is one touch, since the button next to it undoes it;
 *  - stop is a touch that only ASKS: a modal says the print ends and cannot be
 *    resumed, and only its red Stop button sends the command. Stopping destroys
 *    a print that may be hours in, and a touch that lands on the wrong card must
 *    not be enough. Cancel, or a touch outside the modal, closes it.
 *
 * They post EV_PRINTER_JOB with `index` and "pause", "resume" or "stop"; the
 * application task sends the command and the printer's own report changes the
 * button. They are drawn on every printer's card all the time, like the light
 * button, and are inert (dimmed, no touch) unless a job runs on a connection that
 * takes commands (see ui_job_set): a Bambu reached through the cloud is never
 * active. */

typedef struct
{
    lv_obj_t *pause;
    lv_obj_t *pause_icon;
    lv_obj_t *stop;
    lv_obj_t *stop_icon;
    int index;      /* the card's row, 0 on the full view */
    bool shown;
    bool paused;
    lv_obj_t *light;      /* the printer's light: always drawn, never hidden */
    lv_obj_t *light_icon;
    int light_can;        /* -1 until set, then 0 / 1 */
    int light_on;
} job_buttons_t;

/* Builds the pair under `parent`, drawn and inert. `w` x `h` each; pause at (px, py) and
 * stop at (sx, sy). The light button, lw x lh, is at (lx, ly): unlike the
 * pair it is always there, because a light is wanted at rest as much as at work;
 * it is only inert (dimmed, not clickable) while the printer cannot be reached or
 * takes no light command. Needs the LVGL lock. */
void ui_job_build(job_buttons_t *b, lv_obj_t *parent, int px, int py, int sx, int sy, int w, int h, int lx, int ly,
                  int lw, int lh, int index);

/* Says whether the light button can act, and whether the printer's light is on. */
void ui_job_set_light(job_buttons_t *b, bool can, bool on);

/* Makes the pair active (clickable, full strength) or inert (dimmed, no touch),
 * and the first one "resume" while paused. The pair is never hidden. */
void ui_job_set(job_buttons_t *b, bool active, bool paused);
