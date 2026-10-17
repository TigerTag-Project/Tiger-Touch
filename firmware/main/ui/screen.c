#include "screen.h"

#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"
#include "lvgl.h"
#include "pandatouch_display.h"
#include "pandatouch_touch.h"
#include "settings.h"

static const char *TAG = "screen";

#define DEFAULT_BRIGHTNESS 80

static int s_brightness = DEFAULT_BRIGHTNESS;
static int s_sleep_s = 0;
static int s_rotation = 0;
/* Whether the owner says a battery is fitted. The Panda Touch and the K-Touch
 * ship with one inside, so yes unless switched off; the board cannot detect it
 * (docs/HARDWARE.md), the way TigerSpool asks rather than guesses. */
static volatile int s_battery = 1;

typedef enum
{
    AWAKE,
    ASLEEP,
    WAKING, /* touched: lit again, waiting for the finger to lift */
} power_t;
static volatile power_t s_power = AWAKE;

static int get_int(const char *key, int def)
{
    char buf[12];
    return settings_get(key, buf, sizeof buf) ? atoi(buf) : def;
}

static void put_int(const char *key, int v)
{
    char buf[12];
    snprintf(buf, sizeof buf, "%d", v);
    settings_set(key, buf);
}

void screen_init(void)
{
    s_brightness = get_int("bright", DEFAULT_BRIGHTNESS);
    if (s_brightness < 10 || s_brightness > 100)
    {
        s_brightness = DEFAULT_BRIGHTNESS;
    }
    s_sleep_s = get_int("sleep", 0);
    s_rotation = get_int("rot", 0) == 180 ? 180 : 0;
    s_battery = get_int("battery", 1) ? 1 : 0;
    pt_display_set_rotation180(s_rotation == 180);
}

bool screen_battery(void)
{
    return s_battery != 0;
}

void screen_set_battery(bool fitted)
{
    s_battery = fitted ? 1 : 0;
    put_int("battery", s_battery);
}

int screen_brightness(void)
{
    return s_brightness;
}

int screen_sleep_s(void)
{
    return s_sleep_s;
}

int screen_rotation(void)
{
    return s_rotation;
}

void screen_set_brightness(int percent)
{
    s_brightness = percent;
    put_int("bright", percent);
    if (s_power == AWAKE)
    {
        pt_backlight_set((uint32_t)percent);
    }
}

void screen_set_sleep(int seconds)
{
    s_sleep_s = seconds;
    put_int("sleep", seconds);
}

void screen_set_rotation(int degrees)
{
    s_rotation = degrees == 180 ? 180 : 0;
    put_int("rot", s_rotation);
    pt_display_set_rotation180(s_rotation == 180);
    lv_obj_invalidate(lv_screen_active());
}

bool screen_asleep(void)
{
    return s_power != AWAKE; /* one word, read without the LVGL lock */
}

static void set_indev_enabled(bool on)
{
    for (lv_indev_t *d = lv_indev_get_next(NULL); d; d = lv_indev_get_next(d))
    {
        lv_indev_enable(d, on);
    }
}

static void power_tick(lv_timer_t *t)
{
    (void)t;
    switch (s_power)
    {
    case AWAKE:
        if (s_sleep_s > 0 && lv_display_get_inactive_time(NULL) > (uint32_t)s_sleep_s * 1000u)
        {
            s_power = ASLEEP;
            ESP_LOGI(TAG, "asleep after %d s without a touch", s_sleep_s);
            pt_backlight_set(0);
            /* While it sleeps the touch is read here, so that the touch that
             * wakes it is not also a press on whatever is under the finger. */
            set_indev_enabled(false);
        }
        break;
    case ASLEEP:
    {
        pt_touch_event_t ev;
        if (pt_touch_get_touch(&ev) && ev.number > 0)
        {
            ESP_LOGI(TAG, "touched: waking");
            pt_backlight_set((uint32_t)s_brightness);
            s_power = WAKING;
        }
        break;
    }
    case WAKING:
    {
        pt_touch_event_t ev;
        if (!pt_touch_get_touch(&ev) || ev.number == 0)
        {
            set_indev_enabled(true);
            lv_display_trigger_activity(NULL);
            s_power = AWAKE;
        }
        break;
    }
    }
}

void screen_start(void)
{
    lv_timer_create(power_tick, 200, NULL);
}
