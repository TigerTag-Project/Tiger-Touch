#pragma once

#include <stdbool.h>

/* The panel's own settings: brightness, sleep after inactivity, orientation.
 * Kept in the settings store; the brightness and orientation are applied as
 * soon as they are read so the power-on picture already honours them. */

void screen_init(void);

int screen_brightness(void); /* percent */
int screen_sleep_s(void);    /* seconds of inactivity, 0 = never */
int screen_rotation(void);   /* 0 or 180 */

void screen_set_brightness(int percent);
void screen_set_sleep(int seconds);
/* Whether a battery is fitted, as the owner declared it (yes by default: the
 * screen ships with one inside). Read from any task. */
bool screen_battery(void);
void screen_set_battery(bool fitted);

/* Whether the panel sleeps (backlight off), from any task. */
bool screen_asleep(void);
/* Needs the LVGL lock: the screen on show is redrawn. */
void screen_set_rotation(int degrees);

/* Creates the inactivity timer. Needs the LVGL lock. */
void screen_start(void);
