#pragma once

#include <stdbool.h>

#include "cloud.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "wifi_mgr.h"

/* What the screens tell the application. The screens never act on their own:
 * they post one of these and the application task decides. */
typedef enum
{
    EV_NONE = 0,
    EV_WIFI_PICK,       /* index = row in the list shown */
    EV_RESCAN,
    EV_SUBMIT,          /* a = first field, b = second field */
    EV_CANCEL,
    EV_OK,              /* "continue" on an information screen */
    EV_SETTINGS,
    EV_PRINTERS,        /* open the printer list */
    EV_PRINTER_PICK,    /* index = row in the list shown */
    EV_PRINTER_JOB,     /* index = row (0 on the full view), a = "pause" | "resume" | "stop" */
    EV_CHANGE_WIFI,
    EV_CHANGE_ACCOUNT,
    EV_BACK,
    /* The settings pages. */
    EV_OPEN_PRINTERS,    /* the printers page of the settings */
    EV_OPEN_WIFI,
    EV_OPEN_SCREEN,
    EV_OPEN_BATTERY,
    EV_OPEN_DEMO,
    EV_OPEN_LANGUAGE,
    EV_ASK_RESTART,      /* show the confirmation */
    EV_RESTART,          /* confirmed */
    EV_ASK_FACTORY,      /* show the factory-reset confirmation */
    EV_FACTORY,          /* confirmed: erase everything */
    EV_TOGGLE_PRINTER,   /* index = row, a = "1" switched on / "0" off */
    EV_REFRESH_PRINTERS, /* read the account again */
    EV_SET_BRIGHTNESS,   /* index = percent */
    EV_SET_SLEEP,        /* index = seconds, 0 = never */
    EV_SET_ORIENTATION,  /* index = 0 or 180 */
    EV_SET_BATTERY,      /* index = 1 a battery is fitted, 0 none */
    EV_SET_DEMO,         /* index = 1 demo mode on, 0 off: saved, then a restart */
    EV_SET_LANGUAGE,     /* index = lang_t */
    EV_OPEN_UPDATE,      /* the firmware update page */
    /* The first-boot setup and the account pages (ui_onboard.c). */
    EV_PROMPT_GO,        /* "Set up" / "Link now" */
    EV_PROMPT_LATER,     /* "Later", or a touch outside the reminder panel */
    EV_EDIT_EMAIL,       /* the sign-in page's e-mail field */
    EV_EDIT_PASS,        /* its password field */
    EV_GOOGLE,           /* "Continue with Google" */
    EV_UPD_CHECK,        /* read the manifest again */
    EV_UPD_INSTALL,      /* install the version offered */
    EV_ACCOUNT_SYNCED,   /* the background start-up read the account again (main.c) */
    EV_TOGGLE_VIEW,      /* the home: cards <-> list */
} ev_type_t;

typedef struct
{
    ev_type_t type;
    int index;
    char a[72];
    char b[72];
} app_event_t;

void ui_set_event_queue(QueueHandle_t q);
void ui_post(ev_type_t type, int index, const char *a, const char *b);

typedef struct
{
    const char *title;
    const char *subtitle;
    const char *step;       /* "1 / 2", may be NULL */
    const char *label1;
    const char *prefill1;   /* may be NULL */
    bool secret1;
    const char *label2;     /* NULL = single field */
    bool secret2;
    const char *error;      /* may be NULL */
    bool allow_cancel;
} ui_form_t;

/* Every ui_show_* call needs the LVGL lock (PT_LVGL_SCOPE_LOCK) and replaces
 * the whole screen. */
void ui_show_busy(const char *title, const char *message);
void ui_show_wifi_list(const wifi_net_t *nets, int n, const char *banner, bool allow_cancel);
void ui_show_form(const ui_form_t *f);
/* "Account linked" and the e-mail: the confirmation alone. */
void ui_show_linked(const char *email);
void ui_show_dashboard(void);
/* The power-on picture, full screen. */
void ui_show_splash(void);
/* Builds the dashboard screen once, without showing it. */
void ui_prepare_dashboard(void);

/* The printers page. */
typedef enum
{
    CHIP_LOCAL,   /* reachable on the network */
    CHIP_CLOUD,   /* reachable through the maker's cloud */
    CHIP_SOON,    /* no driver for it yet */
    CHIP_ONLINE,  /* the one being watched, and answering */
    CHIP_OFFLINE, /* the one being watched, and silent */
    CHIP_CONNECTING, /* switched on, no answer yet */
} printer_chip_t;

typedef struct
{
    char name[48];
    char meta[64];
    char initial;
    char brand[12]; /* the logo to draw */
    printer_chip_t chip;
    bool selected;
} printer_row_t;

/* The printers page: one live card per row, built by ui_cards.c. Touching a
 * card posts EV_PRINTER_PICK with its row. The cards are kept current with
 * ui_cards_update(). There is no way back from it: it is the home page. */
void ui_show_printers(const printer_row_t *rows, int n);

/* ---- Settings pages (ui_settings.c) ----------------------------------------- */

/* The settings menu, as TigerScale's: a scrolling list with each setting's
 * current value, and the up / down rail. */
typedef struct
{
    int printers_on, printers_total;
    bool wifi_connected;
    const char *wifi_ssid;
    int account_state;          /* 0 none, 1 connecting, 2 linked */
    const char *account_email;
    const char *update_offered; /* a newer version seen, or NULL */
    bool battery;               /* declared fitted (Settings > Battery) */
    bool demo;                  /* demo mode is on */
} settings_menu_t;
void ui_show_settings_menu(const settings_menu_t *m);
void ui_settings_menu_reset_scroll(void);

typedef struct
{
    char name[48];
    char meta[64];
    char initial;
    char brand[12]; /* the logo to draw */
    bool supported; /* has a driver: a switch is offered */
    bool on;
} printer_switch_t;

/* One switch per printer of the account, under the load gauge. */
void ui_show_printers_page(const printer_switch_t *rows, int n, int load_pct);
/* The answer to a switch: the state it ends up in, the new load, and whether
 * the budget refused it (the gauge then says so for a moment). */
void ui_printers_page_result(int row, bool on, int load_pct, bool refused);

typedef struct
{
    bool connected;
    char ssid[33];
    int rssi;
    char ip[16];
    char mac[18];
    int channel;
} wifi_info_t;
void ui_show_wifi_info(const wifi_info_t *w);

void ui_show_screen_page(int brightness, int sleep_s, int rotation);
/* The battery page: whether one is fitted, as the owner declares it. */
void ui_show_battery_page(bool fitted);
/* The demo mode page (demo_mode.h). */
void ui_show_demo_page(bool on);
/* `first_boot`: the first page of the setup - its title is "Language" in
 * English whatever is set, and there is no way back from it. */
void ui_show_language_page(int current_lang, bool first_boot);
void ui_show_restart_confirm(void);
void ui_show_factory_confirm(void);

/* The firmware update page, as TigerScale's: the installed version, then one
 * state. Retry posts EV_UPD_CHECK, Install EV_UPD_INSTALL, the back arrow EV_BACK. */
typedef enum
{
    UPD_CHECKING,     /* a spinner while the manifest is read */
    UPD_AVAILABLE,    /* `offered` is newer: the amber banner and Install */
    UPD_LATEST,       /* green badge: up to date */
    UPD_CHECK_FAILED, /* red badge and Retry: the manifest could not be read */
    UPD_NO_WIFI,      /* red badge and Retry */
    UPD_FAILED,       /* red badge and Retry: an install was refused or broke off */
} upd_state_t;
void ui_show_update_page(const char *installed, upd_state_t state, const char *offered);
/* "Confirm update?": Yes posts EV_UPD_INSTALL, No EV_CANCEL. */
void ui_show_update_confirm(void);
/* The install: the green ring, the percentage, "Do not power off". */
void ui_show_update_progress(void);
void ui_update_progress(int percent);
void ui_update_rebooting(void);

/* The language changed: the dashboard keeps its texts, so it is built again. */
void ui_language_changed(void);

/* Shared with ui_settings.c. */
lv_obj_t *ui_kit_page(void);
/* TigerScale's header: a 72 px band that is the back button as a whole
 * (posting `back_ev` on press), a large chevron, the title in white, a hairline. */
lv_obj_t *ui_kit_header(lv_obj_t *scr, const char *title, ev_type_t back_ev);
/* TigerScale's scrolling column under that header, its scrollbar always on;
 * `step` is how far one press of the rail moves it. Rows are added to it, then
 * ui_kit_scroll_rail() puts the up / down buttons beside it. */
lv_obj_t *ui_kit_scroll_list(lv_obj_t *scr, int step);
void ui_kit_scroll_rail(lv_obj_t *scr, lv_obj_t *list);
lv_obj_t *ui_kit_txt(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *s, int x, int y);
/* The time, hours and minutes, centred on the header's line, kept current
 * (the home screen, the settings, a printer's view). Needs the LVGL lock. */
lv_obj_t *ui_kit_clock(lv_obj_t *parent);
/* Puts `o` on the header's line (HEADER_MID, ui_theme.h): its centre there,
 * whatever its height. Set its x and alignment first. */
void ui_kit_on_header(lv_obj_t *o);
lv_obj_t *ui_kit_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h, bool primary,
                        lv_event_cb_t cb, void *user);
