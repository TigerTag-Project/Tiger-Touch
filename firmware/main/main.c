#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "cloud.h"
#include "demo_mode.h"
#include "esp_attr.h"
#include "i18n.h"
#include "screen.h"
#include "links.h"
#include "ota.h"
#include "esp_mac.h"
#include "ui_onboard.h"
#include "printer_driver.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "pandatouch_display.h"
#include "settings.h"
#include "tz.h"
#include "ui_cards.h"
#include "ui_dashboard.h"
#include "ui_setup.h"
#include "wifi_mgr.h"

static const char *TAG = "monitor";

static QueueHandle_t s_events;
/* In PSRAM: 24 records of a few hundred bytes are about 9 KB of internal RAM,
 * the room of half a printer connection. */
static EXT_RAM_BSS_ATTR account_printer_t s_printers[CLOUD_MAX_PRINTERS];
static int s_printer_count;
static int s_failed_brands;
static volatile bool s_dash_built;       /* the dashboard screen exists (shown or not) */
static volatile uint32_t s_gen = 1;      /* bumped whenever the watched printer changes */
static volatile uint32_t s_ready_gen;    /* the generation the feed has settled */
static bool s_signed_in_now; /* typed credentials this boot, as opposed to a resumed session */
static bool s_printers_unread; /* the last read of the account's printers failed, wholly or for a brand (network) */
/* The start-up in the background (bringup_task): what it found, for the screens
 * to pick up. Flags rather than events alone, because a screen that opens
 * empties the queue: each checks them at least once a second. */
static volatile bool s_bringup_running; /* still connecting, reading the account */
static volatile bool s_sync_printers;   /* the list was read again: rebuild what shows it */
static volatile int s_sync_lang = -1;   /* a language the account asks for (lang_t), to apply here */
static bool s_bg_retries;               /* the background start-up reads the printers again itself */

/* ---- Small helpers -------------------------------------------------------- */

static void flush_events(void)
{
    app_event_t e;
    while (xQueueReceive(s_events, &e, 0) == pdTRUE)
    {
    }
}

static app_event_t wait_event(void)
{
    app_event_t e = {0};
    xQueueReceive(s_events, &e, portMAX_DELAY);
    return e;
}

/* The same, giving up after `ms`: an event of type EV_NONE then. */
static app_event_t wait_event_for(uint32_t ms)
{
    app_event_t e = {0};
    xQueueReceive(s_events, &e, pdMS_TO_TICKS(ms));
    return e;
}

static void busy(const char *title, const char *msg)
{
    PT_LVGL_SCOPE_LOCK()
    {
        ui_show_busy(title, msg);
    }
}

/* ---- Wi-Fi ---------------------------------------------------------------- */

#define SAVED_NETWORK_ROUNDS 3
#define PRINTERS_RETRY_MS (30 * 1000) /* a failed read of the printers, tried again */
#define SAVED_NETWORK_PAUSE_MS 4000

/* Returns true when the device ends up connected. With `allow_cancel`, backing
 * out leaves the previous connection alone and still returns true. */
static bool run_wifi(bool force_pick, bool allow_cancel)
{
    char ssid[33], pass[65];

    if (!force_pick && settings_get("wifi_ssid", ssid, sizeof ssid))
    {
        settings_get("wifi_pass", pass, sizeof pass);
        busy(tr(S_BUSY_WIFI), ssid);
        /* A router can refuse a board that has just been reset for a while: it
         * still holds the old association and answers "refused". Giving it a few
         * quiet seconds is what keeps a restart from ending on the network picker
         * with the right password already saved. */
        for (int round = 0; round < SAVED_NETWORK_ROUNDS; round++)
        {
            if (wifi_mgr_connect(ssid, pass, 15000) == ESP_OK)
            {
                return true;
            }
            if (round + 1 < SAVED_NETWORK_ROUNDS)
            {
                vTaskDelay(pdMS_TO_TICKS(SAVED_NETWORK_PAUSE_MS));
            }
        }
        ESP_LOGW(TAG, "the saved network is not reachable");
    }

    static wifi_net_t nets[24];
    const char *banner = (!force_pick && ssid[0]) ? tr(S_ERR_NET_MISSING) : NULL;

    for (;;)
    {
        busy(tr(S_BUSY_SCAN), NULL);
        int n = wifi_mgr_scan(nets, 24);
        ESP_LOGI(TAG, "scan: %d network(s)", n);
        if (n < 0)
        {
            n = 0;
        }
        flush_events();
        PT_LVGL_SCOPE_LOCK()
        {
            ui_show_wifi_list(nets, n, banner, allow_cancel);
        }
        banner = NULL;

        app_event_t e = wait_event();
        if (e.type == EV_RESCAN)
        {
            continue;
        }
        if (e.type == EV_CANCEL && allow_cancel)
        {
            return wifi_mgr_connected();
        }
        if (e.type != EV_WIFI_PICK || e.index < 0 || e.index >= n)
        {
            continue;
        }

        const wifi_net_t net = nets[e.index];
        pass[0] = '\0';
        if (net.secure)
        {
            const ui_form_t f = {
                .title = tr(S_WIFI_PASSWORD),
                .subtitle = net.ssid,
                .step = "1 / 2",
                .label1 = tr(S_PASSWORD),
                .secret1 = true,
                .allow_cancel = true,
            };
            flush_events();
            PT_LVGL_SCOPE_LOCK()
            {
                ui_show_form(&f);
            }
            app_event_t r = wait_event();
            if (r.type != EV_SUBMIT)
            {
                continue; /* back to the list */
            }
            strlcpy(pass, r.a, sizeof pass);
        }

        busy(tr(S_BUSY_CONNECT_TO), net.ssid);
        if (wifi_mgr_connect(net.ssid, pass, 20000) == ESP_OK)
        {
            settings_set("wifi_ssid", net.ssid);
            settings_set("wifi_pass", pass);
            memset(pass, 0, sizeof pass);
            return true;
        }
        memset(pass, 0, sizeof pass);
        banner = net.secure ? tr(S_ERR_WIFI_PASSWORD) : tr(S_ERR_WIFI_FAIL);
    }
}

/* ---- Account -------------------------------------------------------------- */

/* The name the device gives itself to the pairing page: "tigertouch-" and the
 * last two bytes of its Wi-Fi address, like TigerScale's "tigerscale-XXXX". */
static void device_name(char *out, size_t n)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, n, "tigertouch-%02x%02x", mac[4], mac[5]);
}

/* Google accounts have no password: the device shows a QR, the owner approves
 * it on the phone, and the backend hands the device its session (cloud.c,
 * cloud_pair_*). TigerScale V3's runAccountPairing(), step for step: a code
 * lives 10 minutes, the device gives up after 11, and six failed polls in a
 * row say "Failed" rather than leaving a dead backend looking like a wait. */
#define PAIR_GIVE_UP_MS (11 * 60 * 1000)
#define PAIR_MAX_MISSES 6

static bool run_pairing(void)
{
    flush_events();
    PT_LVGL_SCOPE_LOCK()
    {
        ui_show_pairing();
    }
    char dev[24];
    device_name(dev, sizeof dev);
    static cloud_pair_t pair;
    const bool started = cloud_pair_start(dev, esp_app_get_description()->version, &pair) == CLOUD_OK;
    str_id_t verdict = S_PAIR_FAILED;
    bool linked = false;
    if (started)
    {
        PT_LVGL_SCOPE_LOCK()
        {
            ui_pairing_set(pair.verify_url, pair.code);
        }
        const int64_t give_up = esp_timer_get_time() + (int64_t)PAIR_GIVE_UP_MS * 1000;
        int misses = 0;
        for (;;)
        {
            app_event_t e = wait_event_for((uint32_t)pair.interval_s * 1000);
            if (e.type == EV_CANCEL)
            {
                memset(&pair, 0, sizeof pair);
                return false;
            }
            if (esp_timer_get_time() > give_up)
            {
                verdict = S_PAIR_EXPIRED;
                break;
            }
            const cloud_pair_status_t st = cloud_pair_poll(pair.poll_token);
            if (st == PAIR_PENDING)
            {
                misses = 0;
                continue;
            }
            if (st == PAIR_ERROR && ++misses < PAIR_MAX_MISSES)
            {
                continue;
            }
            linked = st == PAIR_APPROVED;
            verdict = linked ? S_PAIR_DONE : st == PAIR_DENIED ? S_PAIR_DENIED
                                             : st == PAIR_EXPIRED ? S_PAIR_EXPIRED
                                                                  : S_PAIR_FAILED;
            break;
        }
    }
    memset(&pair, 0, sizeof pair);
    PT_LVGL_SCOPE_LOCK()
    {
        ui_pairing_result(tr(verdict), linked, linked ? cloud_email() : NULL);
    }
    vTaskDelay(pdMS_TO_TICKS(linked ? 3000 : 2500));
    if (linked)
    {
        s_signed_in_now = true;
    }
    return linked;
}

/* One keyboard page for one field of the sign-in page. Returns false when its
 * back arrow was used (the field keeps what it had). */
static bool edit_field(const char *title, const char *label, bool secret, char *value, size_t n)
{
    const ui_form_t f = {
        .title = title,
        .subtitle = tr(S_SAME_ACCOUNT),
        .label1 = label,
        .prefill1 = secret ? NULL : value,
        .secret1 = secret,
        .allow_cancel = true,
    };
    flush_events();
    PT_LVGL_SCOPE_LOCK()
    {
        ui_show_form(&f);
    }
    for (;;)
    {
        app_event_t e = wait_event();
        if (e.type == EV_CANCEL)
        {
            return false;
        }
        if (e.type == EV_SUBMIT)
        {
            strlcpy(value, e.a, n);
            memset(e.a, 0, sizeof e.a);
            return true;
        }
    }
}

/* The sign-in page, as TigerScale's runSignInForm(): e-mail, password, "Sign
 * in", and "Continue with Google" for accounts that have no password. Returns
 * true once an account is linked; the back arrow returns false and changes
 * nothing. */
static bool run_sign_in(void)
{
    char email[96];
    char pass[72] = {0};
    settings_get("acct_email", email, sizeof email);
    const char *banner = NULL;

    for (;;)
    {
        flush_events();
        PT_LVGL_SCOPE_LOCK()
        {
            ui_show_sign_in(email, pass[0] != '\0', banner);
        }
        banner = NULL;
        const app_event_t e = wait_event();
        switch (e.type)
        {
        case EV_CANCEL:
            memset(pass, 0, sizeof pass);
            return false;
        case EV_EDIT_EMAIL:
            edit_field(tr(S_EMAIL_ADDRESS), tr(S_EMAIL), false, email, sizeof email);
            break;
        case EV_EDIT_PASS:
            edit_field(tr(S_PASSWORD), tr(S_PASSWORD), true, pass, sizeof pass);
            break;
        case EV_GOOGLE:
            memset(pass, 0, sizeof pass);
            if (run_pairing())
            {
                return true;
            }
            break;
        case EV_SUBMIT:
        {
            if (!email[0])
            {
                banner = tr(S_ERR_NEED_EMAIL);
                break;
            }
            if (!pass[0])
            {
                banner = tr(S_ERR_NEED_PASSWORD);
                break;
            }
            busy(tr(S_BUSY_ACCOUNT), email);
            const cloud_err_t r = cloud_sign_in(email, pass);
            memset(pass, 0, sizeof pass);
            if (r == CLOUD_OK)
            {
                s_signed_in_now = true;
                return true;
            }
            banner = cloud_err_text(r);
            break;
        }
        default:
            break;
        }
    }
}

/* A full-screen question with "Later". True for the primary answer. */
static bool ask(prompt_kind_t kind, str_id_t title, str_id_t sub, str_id_t primary)
{
    flush_events();
    PT_LVGL_SCOPE_LOCK()
    {
        ui_show_prompt(kind, tr(title), tr(sub), tr(primary));
    }
    for (;;)
    {
        const app_event_t e = wait_event();
        if (e.type == EV_PROMPT_GO)
        {
            return true;
        }
        if (e.type == EV_PROMPT_LATER)
        {
            return false;
        }
    }
}

/* A stored session is picked up again; a refused one is forgotten (the account
 * icon then says "none" and the home screen offers to sign in again). */
static bool resume_account_ex(bool quiet)
{
    if (!cloud_has_session())
    {
        return false;
    }
    if (!quiet)
    {
        busy(tr(S_BUSY_ACCOUNT), cloud_email());
    }
    const cloud_err_t r = cloud_resume();
    if (r != CLOUD_OK)
    {
        ESP_LOGW(TAG, "resume failed: %d", r);
    }
    /* A network failure keeps the session: it is tried again in the background. */
    return r == CLOUD_OK || r == CLOUD_ERR_NETWORK;
}

static bool resume_account(void)
{
    return resume_account_ex(false);
}

/* ---- Dashboard feed ------------------------------------------------------- */

static char s_selected_id[40];   /* account document id of the printer being shown */
static TaskHandle_t s_feed;

/* Runs the feed pass now instead of at the next second: a change of selection
 * is then on screen as soon as the report is read, not up to a second later. */
static void feed_wake(void)
{
    if (s_feed)
    {
        xTaskNotifyGive(s_feed);
    }
}


/* The printer to show: the one chosen if it is switched on, else the first
 * that is. Callers either are the application task (the only writer of the
 * list) or hold links_lock(). */
static const account_printer_t *current_printer(void)
{
    if (s_selected_id[0])
    {
        for (int i = 0; i < s_printer_count; i++)
        {
            if (strcmp(s_printers[i].id, s_selected_id) == 0 && links_enabled(i))
            {
                return &s_printers[i];
            }
        }
    }
    for (int i = 0; i < s_printer_count; i++)
    {
        if (links_enabled(i))
        {
            return &s_printers[i];
        }
    }
    return NULL;
}

static void feed_task(void *arg)
{
    (void)arg;
    printer_view_t v;
    memset(&v, 0, sizeof v);

    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));

        /* Read before the printer is resolved: a switch that lands in between
         * then makes this pass stale instead of wrongly "ready". */
        const uint32_t gen = __atomic_load_n(&s_gen, __ATOMIC_SEQ_CST);
        links_tick(s_selected_id);

        char name[48];
        strlcpy(name, tr(S_NO_PRINTER), sizeof name);
        bool settled = true;

        links_lock(); /* the list of printers is not rewritten under us */
        const account_printer_t *p = current_printer();
        if (!p)
        {
            memset(&v, 0, sizeof v);
            strlcpy(v.name, name, sizeof v.name);
            strlcpy(v.notice,
                    s_printers_unread ? tr(s_bringup_running ? S_BUSY_PRINTERS : S_PRINTERS_UNREAD_HINT)
                    : s_printer_count > 0 ? tr(S_NO_PRINTER_ON_HINT) : tr(S_ADD_PRINTER_HINT),
                    sizeof v.notice);
        }
        else
        {
            strlcpy(name, p->name, sizeof name);
            const int idx = (int)(p - s_printers);
            if (!links_view(idx, &v, &settled))
            {
                memset(&v, 0, sizeof v);
                strlcpy(v.name, name, sizeof v.name);
                if (links_supported(idx))
                {
                    settled = false; /* switched on, link not open yet */
                }
                else
                {
                    strlcpy(v.notice, tr(S_BRAND_SOON), sizeof v.notice);
                }
            }
            /* Once it is known to be silent the red "Offline" badge says it;
             * a sentence beside it would only repeat it. */
            if (!v.online && !v.notice[0] && !settled)
            {
                strlcpy(v.notice, tr(S_CONNECTING), sizeof v.notice);
            }
        }
        strlcpy(v.brand, p ? p->brand : "", sizeof v.brand);
        links_unlock();

        if (settled)
        {
            __atomic_store_n(&s_ready_gen, gen, __ATOMIC_SEQ_CST);
        }

        if (s_dash_built)
        {
            PT_LVGL_SCOPE_LOCK()
            {
                ui_dashboard_update(&v);
                ui_dashboard_set_printer(name, v.online);
            }
        }

        /* After the picture: a renewal can take a couple of seconds, and the
         * dashboard has already been given this second's data. Not without a
         * network, which would only time out. */
        if (wifi_mgr_rssi() != 0)
        {
            cloud_keepalive();
        }
    }
}

static void show_dashboard(void)
{
    PT_LVGL_SCOPE_LOCK()
    {
        ui_show_dashboard();
    }
}

/* Going to the dashboard of a printer. One that is switched on is already
 * connected and its report is in memory, so it is shown at once. Otherwise the
 * card is kept out of sight, behind a loading screen, until the printer has
 * answered (or said why it cannot): without it the card is seen filling in. */
static void enter_dashboard(void)
{
    const account_printer_t *p = current_printer();
    bool warm = false;
    if (p)
    {
        printer_view_t probe;
        bool settled = false;
        warm = links_view((int)(p - s_printers), &probe, &settled) && settled;
    }
    if (!warm)
    {
        if (p)
        {
            char title[80], how[64];
            snprintf(title, sizeof title, tr(S_BUSY_CONNECT_PRINTER), p->name);
            snprintf(how, sizeof how, "%s · %s", cloud_brand_label(p->brand),
                     p->cloud ? tr(S_VIA_CLOUD) : tr(S_LOCAL_NETWORK));
            busy(title, how);
        }
        else
        {
            busy(tr(S_BUSY_LOADING), NULL);
        }
    }
    feed_wake();
    for (uint32_t waited = 0; waited < 9500; waited += 50) /* a link settles within 8 s */
    {
        const bool ready = __atomic_load_n(&s_ready_gen, __ATOMIC_SEQ_CST) == __atomic_load_n(&s_gen, __ATOMIC_SEQ_CST);
        /* A spinner is never a one-frame flash; a warm printer has none. */
        if (ready && (warm || waited >= 400))
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    show_dashboard();
}

/* ---- Application task ----------------------------------------------------- */

static void fetch_and_show_printers(void);
static bool take_background_sync(void);
static void save_printer_cache(void);

/* ---- Settings pages --------------------------------------------------------- */

/* The printers page: one switch per printer, under the load gauge. */
static void run_printers_page(void)
{
    static printer_switch_t rows[CLOUD_MAX_PRINTERS];

    for (;;) /* rebuilt after the account was read again */
    {
        const int n = s_printer_count;
        for (int i = 0; i < n; i++)
        {
            const account_printer_t *p = &s_printers[i];
            printer_switch_t *r = &rows[i];
            memset(r, 0, sizeof *r);
            strlcpy(r->name, p->name, sizeof r->name);
            r->initial = (char)toupper((unsigned char)cloud_brand_label(p->brand)[0]);
            strlcpy(r->brand, p->brand, sizeof r->brand);
            snprintf(r->meta, sizeof r->meta, "%s · %s", cloud_brand_label(p->brand),
                     p->cloud ? tr(S_VIA_CLOUD) : (p->ip[0] ? p->ip : tr(S_ADDRESS_UNKNOWN)));
            r->supported = links_supported(i);
            r->on = links_enabled(i);
        }
        flush_events();
        PT_LVGL_SCOPE_LOCK()
        {
            ui_show_printers_page(rows, n, links_used_pct());
        }

        for (;;)
        {
            app_event_t e = wait_event();
            if (e.type == EV_BACK)
            {
                save_printer_cache(); /* the next start shows what was switched on here */
                return;
            }
            if (e.type == EV_REFRESH_PRINTERS)
            {
                fetch_and_show_printers();
                break;
            }
            if (e.type != EV_TOGGLE_PRINTER || e.index < 0 || e.index >= n)
            {
                continue;
            }
            const bool want = e.a[0] == '1';
            const bool granted = links_set_enabled(e.index, want);
            PT_LVGL_SCOPE_LOCK()
            {
                ui_printers_page_result(e.index, links_enabled(e.index), links_used_pct(), want && !granted);
            }
            if (granted && !want && strcmp(s_printers[e.index].id, s_selected_id) == 0)
            {
                /* The printer on show was switched off: show another one. */
                s_selected_id[0] = '\0';
                settings_erase("sel_printer");
                __atomic_add_fetch(&s_gen, 1, __ATOMIC_SEQ_CST);
                feed_wake();
            }
        }
    }
}

static void run_wifi_page(void)
{
    for (;;)
    {
        wifi_link_info_t li;
        const bool up = wifi_mgr_info(&li);
        wifi_info_t w = {0};
        w.connected = up;
        w.rssi = li.rssi;
        w.channel = li.channel;
        strlcpy(w.ssid, li.ssid, sizeof w.ssid);
        strlcpy(w.ip, li.ip, sizeof w.ip);
        strlcpy(w.mac, li.mac, sizeof w.mac);
        flush_events();
        PT_LVGL_SCOPE_LOCK()
        {
            ui_show_wifi_info(&w);
        }
        app_event_t e = wait_event();
        if (e.type == EV_BACK)
        {
            return;
        }
        if (e.type == EV_CHANGE_WIFI)
        {
            run_wifi(true, true);
        }
    }
}

static void run_screen_page(void)
{
    flush_events();
    PT_LVGL_SCOPE_LOCK()
    {
        ui_show_screen_page(screen_brightness(), screen_sleep_s(), screen_rotation());
    }
    for (;;)
    {
        app_event_t e = wait_event();
        switch (e.type)
        {
        case EV_SET_BRIGHTNESS:
            screen_set_brightness(e.index);
            break;
        case EV_SET_SLEEP:
            screen_set_sleep(e.index);
            break;
        case EV_SET_ORIENTATION:
            if (pt_display_direct() || (e.index == 180) != pt_display_rotation180())
            {
                /* Upright, LVGL draws straight into the panel's frame buffer, which
                 * cannot be turned on the fly (pandatouch_display.c): saved, then
                 * a restart, and the screen comes back the new way up. */
                if ((e.index == 180) != (screen_rotation() == 180))
                {
                    screen_set_rotation(e.index);
                    busy(tr(S_RESTART), NULL);
                    vTaskDelay(pdMS_TO_TICKS(400)); /* let the picture reach the panel */
                    esp_restart();
                }
                break;
            }
            PT_LVGL_SCOPE_LOCK()
            {
                screen_set_rotation(e.index);
            }
            break;
        case EV_BACK:
            return;
        default:
            break;
        }
    }
}

static void run_battery_page(void)
{
    flush_events();
    PT_LVGL_SCOPE_LOCK()
    {
        ui_show_battery_page(screen_battery());
    }
    for (;;)
    {
        app_event_t e = wait_event();
        if (e.type == EV_SET_BATTERY)
        {
            screen_set_battery(e.index != 0); /* the next heartbeat carries it */
        }
        else if (e.type == EV_BACK)
        {
            return;
        }
    }
}

static void run_demo_page(void)
{
    flush_events();
    PT_LVGL_SCOPE_LOCK()
    {
        ui_show_demo_page(demo_mode_on());
    }
    for (;;)
    {
        app_event_t e = wait_event();
        if (e.type == EV_SET_DEMO && (e.index != 0) != demo_mode_on())
        {
            /* Everything that reads the printers was built for one or the other:
             * a restart starts it all again the new way. */
            demo_mode_set(e.index != 0);
            busy(tr(S_RESTART), NULL);
            vTaskDelay(pdMS_TO_TICKS(400)); /* let the picture reach the panel */
            esp_restart();
        }
        else if (e.type == EV_BACK)
        {
            return;
        }
    }
}

static void run_language_page(void)
{
    for (;;)
    {
        flush_events();
        PT_LVGL_SCOPE_LOCK()
        {
            ui_show_language_page((int)i18n_lang(), false);
        }
        app_event_t e = wait_event();
        if (e.type == EV_BACK)
        {
            return;
        }
        if (e.type == EV_SET_LANGUAGE && e.index >= 0 && e.index < LANG_COUNT)
        {
            i18n_set_lang((lang_t)e.index, true);
            PT_LVGL_SCOPE_LOCK()
            {
                ui_language_changed(); /* the dashboard keeps its texts */
            }
            /* Up to the account straight away, as TigerScale does; if that
             * fails it stays pending and goes at the next start. */
            if (cloud_link() == CLOUD_LINK_OK && cloud_push_lang(i18n_code(i18n_lang())) == CLOUD_OK)
            {
                i18n_push_done();
            }
        }
    }
}

static void run_restart_confirm(void)
{
    flush_events();
    PT_LVGL_SCOPE_LOCK()
    {
        ui_show_restart_confirm();
    }
    for (;;)
    {
        app_event_t e = wait_event();
        if (e.type == EV_RESTART)
        {
            busy(tr(S_RESTART), NULL);
            vTaskDelay(pdMS_TO_TICKS(400)); /* let the picture reach the panel */
            esp_restart();
        }
        if (e.type == EV_BACK || e.type == EV_CANCEL)
        {
            return;
        }
    }
}

/* Factory reset: the same question as the restart, with the answer that cannot
 * be taken back. Everything stored goes (Wi-Fi, account, language, choices) and
 * the next start is the first one, as freshly flashed. */
static void run_factory_confirm(void)
{
    flush_events();
    PT_LVGL_SCOPE_LOCK()
    {
        ui_show_factory_confirm();
    }
    for (;;)
    {
        app_event_t e = wait_event();
        if (e.type == EV_FACTORY)
        {
            busy(tr(S_FACTORY), NULL);
            vTaskDelay(pdMS_TO_TICKS(400)); /* let the picture reach the panel */
            ESP_LOGW(TAG, "factory reset from the settings");
            settings_factory_reset();
            esp_restart();
        }
        if (e.type == EV_BACK || e.type == EV_CANCEL)
        {
            return;
        }
    }
}

/* ---- Firmware update ------------------------------------------------------ */

static void on_ota_progress(int percent, void *arg)
{
    (void)arg;
    PT_LVGL_SCOPE_LOCK()
    {
        ui_update_progress(percent);
    }
}

/* The update page, as TigerScale's runOtaMenu(): it checks as it opens, says
 * what it found, and installs on a confirmed touch. The install is the only
 * thing here that changes the device, and only once the image has been
 * checked whole (net/ota.c). */
static void run_update_page(void)
{
    const char *installed = esp_app_get_description()->version;
    ota_offer_t offer;
    for (;;)
    {
        upd_state_t state;
        flush_events();
        PT_LVGL_SCOPE_LOCK()
        {
            ui_show_update_page(installed, UPD_CHECKING, NULL);
        }
        if (!wifi_mgr_connected())
        {
            state = UPD_NO_WIFI;
        }
        else
        {
            const ota_check_t c = ota_check(&offer);
            state = c == OTA_NEWER ? UPD_AVAILABLE : c == OTA_UP_TO_DATE ? UPD_LATEST : UPD_CHECK_FAILED;
        }

        bool again = false;
        while (!again)
        {
            flush_events();
            PT_LVGL_SCOPE_LOCK()
            {
                ui_show_update_page(installed, state, state == UPD_AVAILABLE ? offer.version : NULL);
            }
            const app_event_t e = wait_event();
            if (e.type == EV_BACK || e.type == EV_CANCEL)
            {
                return;
            }
            if (e.type == EV_UPD_CHECK)
            {
                again = true;
                continue;
            }
            if (e.type != EV_UPD_INSTALL || state != UPD_AVAILABLE)
            {
                continue;
            }
            flush_events();
            PT_LVGL_SCOPE_LOCK()
            {
                ui_show_update_confirm();
            }
            app_event_t c;
            do
            {
                c = wait_event();
            } while (c.type != EV_UPD_INSTALL && c.type != EV_CANCEL);
            if (c.type != EV_UPD_INSTALL)
            {
                continue; /* "No": back to the page as it was */
            }
            PT_LVGL_SCOPE_LOCK()
            {
                ui_show_update_progress();
            }
            if (ota_install(&offer, on_ota_progress, NULL) == ESP_OK)
            {
                PT_LVGL_SCOPE_LOCK()
                {
                    ui_update_rebooting();
                }
                vTaskDelay(pdMS_TO_TICKS(2000)); /* let the full ring reach the panel */
                esp_restart();
            }
            state = UPD_FAILED;
        }
    }
}

/* A look at the manifest a little after start-up, then once a day, so that the
 * settings menu can say a new version is out. It only reads; installing is
 * always a touch on the update page. */
#define OTA_FIRST_CHECK_MS (60 * 1000)
#define OTA_CHECK_EVERY_MS (24 * 60 * 60 * 1000)

static void ota_watch_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(OTA_FIRST_CHECK_MS));
    for (;;)
    {
        if (wifi_mgr_connected())
        {
            ota_check(NULL);
        }
        vTaskDelay(pdMS_TO_TICKS(OTA_CHECK_EVERY_MS));
    }
}

/* A new image is kept once it has run this long without a reset. A crash, a
 * watchdog or a power cut before then, and the bootloader starts the previous
 * one. The timer runs in the esp_timer task, whose stack is internal: marking
 * the image writes to flash. */
#define OTA_PROBATION_MS (60 * 1000)

static void on_probation_over(void *arg)
{
    (void)arg;
    ota_mark_healthy();
}

static void start_probation_timer(void)
{
    if (!ota_on_probation())
    {
        return;
    }
    const esp_timer_create_args_t args = {.callback = on_probation_over, .name = "ota_ok"};
    esp_timer_handle_t t;
    if (esp_timer_create(&args, &t) == ESP_OK)
    {
        esp_timer_start_once(t, (uint64_t)OTA_PROBATION_MS * 1000);
    }
}

/* The settings menu: reached from a printer's view and from the printer list
 * alike. Returns when its back arrow is touched; what is shown next is up to
 * the caller. */
static void run_settings(void)
{
    for (;;)
    {
        flush_events();
        /* Each row says where its setting stands, as TigerScale's list does. */
        ota_offer_t offered;
        const bool has_offer = ota_cached_offer(&offered);
        wifi_link_info_t link = {0};
        const bool online = wifi_mgr_connected() && wifi_mgr_info(&link);
        /* The name the owner gave the account, as on TigerScale; the e-mail
         * only when the account has none. */
        char email[96];
        const char *dn = cloud_display_name();
        strlcpy(email, dn[0] ? dn : cloud_email(), sizeof email);
        settings_menu_t m = {
            .printers_total = s_printer_count,
            .wifi_connected = online,
            .wifi_ssid = link.ssid,
            .account_state = !cloud_has_session() ? 0 : cloud_link() == CLOUD_LINK_OK ? 2 : 1,
            .account_email = email,
            .update_offered = has_offer ? offered.version : NULL,
            .battery = screen_battery(),
            .demo = demo_mode_on(),
        };
        for (int i = 0; i < s_printer_count; i++)
        {
            m.printers_on += links_enabled(i) ? 1 : 0;
        }
        PT_LVGL_SCOPE_LOCK()
        {
            ui_show_settings_menu(&m);
        }
        app_event_t s = wait_event();
        switch (s.type)
        {
        case EV_OPEN_PRINTERS:
            run_printers_page();
            break;
        case EV_OPEN_WIFI:
            run_wifi_page();
            break;
        case EV_OPEN_SCREEN:
            run_screen_page();
            break;
        case EV_OPEN_BATTERY:
            run_battery_page();
            break;
        case EV_OPEN_DEMO:
            run_demo_page();
            break;
        case EV_OPEN_LANGUAGE:
            run_language_page();
            break;
        case EV_OPEN_UPDATE:
            run_update_page();
            break;
        case EV_ASK_RESTART:
            run_restart_confirm();
            break;
        case EV_ASK_FACTORY:
            run_factory_confirm();
            break;
        case EV_CHANGE_ACCOUNT:
            if (run_sign_in())
            {
                s_selected_id[0] = '\0';
                settings_erase("sel_printer");
                __atomic_add_fetch(&s_gen, 1, __ATOMIC_SEQ_CST);
                fetch_and_show_printers();
            }
            break;
        case EV_BACK:
            ui_settings_menu_reset_scroll();
            return;
        default:
            break;
        }
    }
}

/* What a card says about a switched-on printer: answering, silent, or still
 * being reached. */
static printer_chip_t chip_for(bool linked, const printer_view_t *v, bool settled)
{
    if (linked && v->online)
    {
        return CHIP_ONLINE;
    }
    return (linked && settled) ? CHIP_OFFLINE : CHIP_CONNECTING;
}

static printer_chip_t chip_of(int printer_index)
{
    printer_view_t probe;
    bool settled = false;
    const bool linked = links_view(printer_index, &probe, &settled);
    return chip_for(linked, &probe, settled);
}

/* Gives every card its printer's live view. Each view is copied under the
 * links lock and drawn under the LVGL lock, one after the other, never both. */
/* Sends a card's or the full view's job button (pause, resume, stop, light) to its printer. */
static void send_job(int printer_index, const char *what)
{
    pv_control_t cmd = PV_CTL_PAUSE;
    if (strcmp(what, "resume") == 0)
    {
        cmd = PV_CTL_RESUME;
    }
    else if (strcmp(what, "stop") == 0)
    {
        cmd = PV_CTL_STOP;
    }
    else if (strcmp(what, "light_on") == 0)
    {
        cmd = PV_CTL_LIGHT_ON;
    }
    else if (strcmp(what, "light_off") == 0)
    {
        cmd = PV_CTL_LIGHT_OFF;
    }
    const bool sent = links_control(printer_index, cmd);
    ESP_LOGI(TAG, "job command %s: %s", what, sent ? "sent" : "not sent");
}

static void refresh_cards(const int *row_printer, int nrows)
{
    for (int k = 0; k < nrows; k++)
    {
        printer_view_t v;
        bool settled = false;
        const bool linked = links_view(row_printer[k], &v, &settled);
        if (!linked)
        {
            memset(&v, 0, sizeof v);
        }
        const printer_chip_t chip = chip_for(linked, &v, settled);
        PT_LVGL_SCOPE_LOCK()
        {
            ui_cards_update(k, &v, chip);
        }
    }
}

/* `boot`: the page shown right after power-on. Nothing is connected yet, so
 * every card starts as "connecting", and there is no way back: one has to be
 * picked. */
static bool run_printer_menu(bool boot)
{
    static printer_row_t rows[CLOUD_MAX_PRINTERS];

    for (;;) /* rebuilt after the settings, which may have changed the account */
    {
        const account_printer_t *cur = NULL;
        if (boot)
        {
            for (int i = 0; i < s_printer_count; i++)
            {
                if (s_selected_id[0] && strcmp(s_printers[i].id, s_selected_id) == 0 && links_enabled(i))
                {
                    cur = &s_printers[i];
                }
            }
        }
        else
        {
            cur = current_printer();
        }

        /* Only the printers that are switched on, and the printer each row
         * stands for. */
        static int row_printer[CLOUD_MAX_PRINTERS];
        int nrows = 0;
        for (int i = 0; i < s_printer_count; i++)
        {
            if (!links_enabled(i))
            {
                continue;
            }
            const account_printer_t *p = &s_printers[i];
            printer_row_t *r = &rows[nrows];
            row_printer[nrows++] = i;
            memset(r, 0, sizeof *r);
            strlcpy(r->name, p->name, sizeof r->name);
            r->initial = (char)toupper((unsigned char)cloud_brand_label(p->brand)[0]);
            strlcpy(r->brand, p->brand, sizeof r->brand);
            snprintf(r->meta, sizeof r->meta, "%s · %s", cloud_brand_label(p->brand),
                     p->cloud ? tr(S_VIA_CLOUD) : (p->ip[0] ? p->ip : tr(S_ADDRESS_UNKNOWN)));
            r->selected = cur && strcmp(cur->id, p->id) == 0;
            r->chip = chip_of(i);
        }

        flush_events();
        PT_LVGL_SCOPE_LOCK()
        {
            ui_show_printers(rows, nrows); /* no "Retour": touch the printer */
        }
        refresh_cards(row_printer, nrows);
        for (;;)
        {
            app_event_t e;
            if (xQueueReceive(s_events, &e, pdMS_TO_TICKS(1000)) != pdTRUE || e.type == EV_ACCOUNT_SYNCED)
            {
                /* The account was read again in the background: the list is
                 * built anew with what it says. */
                if (take_background_sync())
                {
                    break;
                }
                /* Nothing touched: the cards follow their printers (a link that
                 * settles, a printer switched on or off, a print that moves). */
                refresh_cards(row_printer, nrows);
                continue;
            }
            if (e.type == EV_PRINTER_JOB && e.index >= 0 && e.index < nrows)
            {
                send_job(row_printer[e.index], e.a);
                refresh_cards(row_printer, nrows);
                continue;
            }
            if (e.type == EV_PRINTER_PICK && e.index >= 0 && e.index < nrows)
            {
                const int pi = row_printer[e.index];
                const bool changed = !cur || strcmp(cur->id, s_printers[pi].id) != 0;
                strlcpy(s_selected_id, s_printers[pi].id, sizeof s_selected_id);
                if (!demo_mode_on())
                {
                    settings_set("sel_printer", s_selected_id);
                }
                if (changed)
                {
                    /* After the id: the feed reads the generation first. */
                    __atomic_add_fetch(&s_gen, 1, __ATOMIC_SEQ_CST);
                    feed_wake();
                }
                return changed;
            }
            if (!boot && (e.type == EV_BACK || e.type == EV_CANCEL))
            {
                return false;
            }
            if (e.type == EV_SETTINGS)
            {
                run_settings();
                break; /* rebuild the list */
            }
            if (e.type == EV_TOGGLE_VIEW)
            {
                ui_cards_set_list(!ui_cards_is_list());
                settings_set("home_view", ui_cards_is_list() ? "list" : "cards");
                break; /* the page is built again in the other view */
            }
        }
    }
}

/* ---- The printer list kept on the device ---------------------------------
 *
 * The account's list is read at every start, but that takes the network, the
 * clock and the account first: seconds, on a slow network many. The last list
 * read is kept in NVS so that the home screen shows the printers at once and
 * fills in as they answer: the ones switched on, which are the ones the home
 * screen shows (ten at most), not the whole account. It holds what the drivers need, access codes and
 * keys included, the way the Wi-Fi password and the account's session are
 * kept; it belongs to the account that read it and is ignored under another. */
#define CACHE_MAX 10 /* the printers switched on: no more than MAX_LINKS (links.c) */
#define CACHE_MAGIC 0x54545043u /* "TTPC" */

typedef struct
{
    uint32_t magic;
    uint16_t record_size; /* sizeof(account_printer_t): a firmware that changed it starts afresh */
    uint16_t count;
    char email[96];
    account_printer_t p[CACHE_MAX];
} printer_cache_t;

static void save_printer_cache(void)
{
    if (demo_mode_on())
    {
        return;
    }
    printer_cache_t *c = heap_caps_calloc(1, sizeof *c, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!c)
    {
        return;
    }
    c->magic = CACHE_MAGIC;
    c->record_size = sizeof(account_printer_t);
    links_lock();
    for (int i = 0; i < s_printer_count && c->count < CACHE_MAX; i++)
    {
        if (links_enabled(i))
        {
            c->p[c->count++] = s_printers[i];
        }
    }
    links_unlock();
    strlcpy(c->email, cloud_email(), sizeof c->email);
    settings_set_blob("printers", c, sizeof *c);
    free(c);
}

static bool load_printer_cache(void)
{
    printer_cache_t *c = heap_caps_malloc(sizeof *c, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool ok = c && settings_get_blob("printers", c, sizeof *c) && c->magic == CACHE_MAGIC &&
              c->record_size == sizeof(account_printer_t) && c->count <= CACHE_MAX &&
              strcmp(c->email, cloud_email()) == 0;
    if (ok)
    {
        links_lock();
        memcpy(s_printers, c->p, c->count * sizeof c->p[0]);
        s_printer_count = c->count;
        links_unlock();
        ESP_LOGI(TAG, "%d printer(s) from the last read, until the account answers", c->count);
    }
    free(c);
    return ok;
}

/* ---- Presence in the account ----------------------------------------------
 *
 * Tells the account the device is there (cloud_heartbeat: version, Wi-Fi,
 * printers followed), on the TigerSpool's rhythm so that Tiger Studio can read
 * it the same way: every 30 s while the screen is on, every 5 min while it
 * sleeps, at once when the printers switched on or answering change. Full
 * (identity too) at start and after a sign-in. Its own task on core 0: an
 * HTTPS exchange never holds up the screen or the application. */
#define PRESENCE_AWAKE_US (30LL * 1000 * 1000)
#define PRESENCE_ASLEEP_US (300LL * 1000 * 1000)
#define PRESENCE_MIN_BLOCK (24 * 1024) /* below this, a TLS session is not opened for it */

static void collect_presence(cloud_presence_t *p)
{
    int idx[CLOUD_PRESENCE_MAX];
    p->n = 0;
    links_lock();
    for (int i = 0; i < s_printer_count && p->n < CLOUD_PRESENCE_MAX; i++)
    {
        if (links_enabled(i))
        {
            strlcpy(p->ids[p->n], s_printers[i].id, sizeof p->ids[0]);
            idx[p->n++] = i;
        }
    }
    links_unlock();
    for (int k = 0; k < p->n; k++)
    {
        p->online[k] = chip_of(idx[k]) == CHIP_ONLINE;
    }
}

static void presence_task(void *arg)
{
    (void)arg;
    static cloud_presence_t p; /* 500 bytes: not on the stack */
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(p.mac, sizeof p.mac, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    device_name(p.host, sizeof p.host);
    p.fw = esp_app_get_description()->version;

    char account[96] = "";
    bool full = true;
    int fails = 0;
    uint32_t last_sig = 0;
    int64_t next_us = 0;
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (!cloud_has_session() || wifi_mgr_state() != WIFI_STATE_CONNECTED)
        {
            continue;
        }
        if (strcmp(account, cloud_email()) != 0)
        {
            strlcpy(account, cloud_email(), sizeof account); /* a new sign-in: say who we are again */
            full = true;
            next_us = 0;
        }
        collect_presence(&p);
        p.demo = demo_mode_on();
        if (p.demo)
        {
            p.n = 0; /* the account lists no made-up printer */
        }
        p.screen_off = screen_asleep();
        p.battery = screen_battery();
        /* What a person would see change: the printers on, which answer, the screen. */
        uint32_t sig = (uint32_t)p.n * 2654435761u ^ (p.screen_off ? 1u : 0u) ^ (p.battery ? 2u : 0u);
        for (int k = 0; k < p.n; k++)
        {
            sig = sig * 31u + (p.online[k] ? 2u : 1u) + (uint32_t)p.ids[k][0];
        }
        const int64_t now = esp_timer_get_time();
        if (sig == last_sig && now < next_us)
        {
            continue;
        }
        if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < PRESENCE_MIN_BLOCK)
        {
            continue;
        }
        wifi_link_info_t li = {0};
        p.rssi = wifi_mgr_info(&li) ? li.rssi : 0;
        strlcpy(p.ip, li.ip, sizeof p.ip);
        p.full = full;
        last_sig = sig;
        if (cloud_heartbeat(&p) == CLOUD_OK)
        {
            full = false;
            fails = 0;
            next_us = now + (p.screen_off ? PRESENCE_ASLEEP_US : PRESENCE_AWAKE_US);
        }
        else
        {
            /* Three in a row: the account is told less often for a while. */
            next_us = now + (++fails >= 3 ? PRESENCE_ASLEEP_US : PRESENCE_AWAKE_US);
        }
    }
}

/* Reads the account's printers into the list. Returns true when the account
 * answered (the list may still miss a brand that did not). */
static bool fetch_printers(bool show_busy)
{
    if (demo_mode_on())
    {
        s_printers_unread = false; /* the demo's printers stay; the account's are not read */
        return true;
    }
    if (show_busy)
    {
        busy(tr(S_BUSY_PRINTERS), cloud_email());
    }
    /* Read into a copy: the request takes seconds, and the list is locked only
     * to swap it in. Locked for the whole read, it held every printer link and
     * the home screen's cards still that long (2026-10-15). */
    account_printer_t *tmp = heap_caps_malloc(sizeof s_printers, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tmp)
    {
        return false;
    }
    links_lock();
    int n = s_printer_count, failed = 0;
    memcpy(tmp, s_printers, sizeof s_printers); /* what a brand that does not answer keeps */
    links_unlock();
    cloud_err_t r = cloud_fetch_printers(tmp, CLOUD_MAX_PRINTERS, &n, &failed);
    if (r == CLOUD_OK)
    {
        links_lock(); /* the feed reads the list: not while it is being rewritten */
        memcpy(s_printers, tmp, sizeof s_printers);
        s_printer_count = n;
        s_failed_brands = failed;
        links_unlock();
    }
    free(tmp);
    links_reload();
    /* A brand that did not answer is read again too: its printers may be
     * missing (nothing was known of them yet), and an empty list must not
     * read as an empty account. */
    s_printers_unread = r != CLOUD_OK || failed > 0;
    ESP_LOGI(TAG, "account: %d printer(s), %d brand(s) unanswered, result %d", n, failed, r);
    if (r == CLOUD_OK)
    {
        save_printer_cache();
    }
    return r == CLOUD_OK;
}

static void fetch_and_show_printers(void)
{
    fetch_printers(true);
}

/* ---- First-boot setup and what follows an account ------------------------- */

static bool wifi_saved(void)
{
    char ssid[33];
    return settings_get("wifi_ssid", ssid, sizeof ssid);
}

/* The setup's first page, as on TigerScale: the language, before anything is
 * said in one. Mandatory, no way back. */
static void run_language_first(void)
{
    flush_events();
    PT_LVGL_SCOPE_LOCK()
    {
        ui_show_language_page((int)i18n_lang(), true);
    }
    for (;;)
    {
        const app_event_t e = wait_event();
        if (e.type == EV_SET_LANGUAGE && e.index >= 0 && e.index < LANG_COUNT)
        {
            i18n_set_lang((lang_t)e.index, true);
            PT_LVGL_SCOPE_LOCK()
            {
                ui_language_changed();
            }
            return;
        }
    }
}

/* Once the network is up: the clock (TLS needs it) and a first time zone. */
static void network_ready_ex(bool quiet)
{
    if (!quiet)
    {
        busy(tr(S_BUSY_TIME), tr(S_BUSY_TIME_WHY));
    }
    /* Behind the home screen, a short wait: the clock keeps being asked for
     * after it (SNTP runs on), and nothing reached here needs it to the
     * second. On a network that answered late, waiting the full 15 s held
     * every printer back that long (2026-10-15). */
    if (!wifi_mgr_sync_time(quiet ? 2000 : 15000))
    {
        ESP_LOGW(TAG, "clock not set yet, it is asked for in the background");
    }
    /* Local time is only for the end of a print. The access point's country
     * is the first guess, replaced by the account's once that is read. */
    char cc[3];
    tz_from_router(wifi_mgr_country(cc) ? cc : "");
}

static void network_ready(void)
{
    network_ready_ex(false);
}

/* What follows a linked account. The language: one picked on this device goes
 * up to the account, otherwise the account's comes down (TigerScale's
 * pushLangToCloud / syncLangFromCloud). Then the time zone and the printers. */
/* The language and the country, read from the account. From the background the
 * language is not applied there: it is handed to the application task
 * (s_sync_lang), which owns the screens. */
static void account_settings_sync(bool from_background)
{
    if (i18n_push_pending())
    {
        if (cloud_push_lang(i18n_code(i18n_lang())) == CLOUD_OK)
        {
            i18n_push_done();
        }
    }
    else
    {
        char code[12];
        lang_t al;
        if (cloud_account_lang(code, sizeof code) == CLOUD_OK && i18n_from_code(code, &al) && al != i18n_lang())
        {
            if (from_background)
            {
                s_sync_lang = (int)al;
            }
            else
            {
                i18n_apply_account_lang(al);
                PT_LVGL_SCOPE_LOCK()
                {
                    ui_language_changed();
                }
            }
        }
    }
    {
        /* A failed read says nothing about the account, so it changes nothing. */
        char squad[8];
        char vat[8];
        const cloud_err_t ce = cloud_account_country(squad, sizeof squad, vat, sizeof vat);
        if (ce == CLOUD_OK || ce == CLOUD_ERR_NOT_FOUND)
        {
            tz_from_account(squad, vat);
        }
    }
}

static void after_account(void)
{
    account_settings_sync(false);
    fetch_and_show_printers();
}

/* ---- Start-up in the background ------------------------------------------
 *
 * A device that is set up shows its home screen at once and connects behind
 * it: the Wi-Fi (the saved network, patiently: a router may refuse a board
 * that has just restarted), the clock, the account, its language and country,
 * then the printers - read again every 30 s while a brand has not answered.
 * The icons at the top follow on their own; the screens pick up what changes
 * from the flags above. Printer links open once the network and the account
 * are there (links_set_network_ready). */
static void bringup_task(void *arg)
{
    (void)arg;
    for (int attempt = 0; !wifi_mgr_connected(); attempt++)
    {
        char ssid[33], pass[65];
        if (!settings_get("wifi_ssid", ssid, sizeof ssid))
        {
            vTaskDelay(pdMS_TO_TICKS(5000)); /* forgotten meanwhile: wait for one to be set up */
            continue;
        }
        settings_get("wifi_pass", pass, sizeof pass);
        /* The first rounds are one patient attempt for the icon ("connecting");
         * past them it says the network failed, and animates again while each
         * later attempt runs. */
        wifi_mgr_set_retrying(attempt + 1 < SAVED_NETWORK_ROUNDS);
        if (wifi_mgr_connect(ssid, pass, 15000) == ESP_OK)
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(attempt + 1 < SAVED_NETWORK_ROUNDS ? SAVED_NETWORK_PAUSE_MS : 30000));
    }
    wifi_mgr_set_retrying(false);
    network_ready_ex(true);
    const bool account = resume_account_ex(true);
    links_set_network_ready(true);
    if (account)
    {
        account_settings_sync(true);
        for (;;)
        {
            const bool read = fetch_printers(false);
            if (read)
            {
                s_sync_printers = true;
                ui_post(EV_ACCOUNT_SYNCED, 0, NULL, NULL);
            }
            if (!s_printers_unread)
            {
                break;
            }
            s_bringup_running = false; /* the home screen now says "retrying", not "reading" */
            vTaskDelay(pdMS_TO_TICKS(PRINTERS_RETRY_MS));
        }
    }
    else if (s_sync_lang >= 0)
    {
        ui_post(EV_ACCOUNT_SYNCED, 0, NULL, NULL);
    }
    s_bringup_running = false;
    ESP_LOGI(TAG, "start-up done in the background");
    vTaskSuspend(NULL); /* its stack is in PSRAM; nothing to give back */
}

/* On the application task: what the background start-up found that touches
 * the screens. Returns true when the screen showing the printers must be
 * built again. */
static bool take_background_sync(void)
{
    bool rebuild = false;
    const int lang = s_sync_lang;
    if (lang >= 0)
    {
        s_sync_lang = -1;
        i18n_apply_account_lang((lang_t)lang);
        PT_LVGL_SCOPE_LOCK()
        {
            ui_language_changed();
        }
        rebuild = true;
    }
    if (s_sync_printers)
    {
        s_sync_printers = false;
        rebuild = true;
    }
    return rebuild;
}

/* The confirmation is for someone who just signed in; a monitor that restarts
 * goes straight back to work. Two pages, as on TigerSpool: "Account linked"
 * alone for a moment, then the printers to switch on. */
#define LINKED_SHOW_MS 3000

static void run_printers_page(void);

static void show_linked_if_new(void)
{
    if (!s_signed_in_now)
    {
        return;
    }
    s_signed_in_now = false;
    flush_events();
    PT_LVGL_SCOPE_LOCK()
    {
        char em[96];
        strlcpy(em, cloud_email(), sizeof em);
        ui_show_linked(em);
    }
    wait_event_for(LINKED_SHOW_MS); /* a touch moves on sooner */
    if (s_printer_count > 0)
    {
        run_printers_page();
    }
}

/* The account step of the setup: "Link a TigerTag account?", then sign-in. */
static bool offer_account(void)
{
    if (ask(PROMPT_ACCOUNT, S_OB_ACCOUNT_Q, S_OB_ACCOUNT_SUB, S_LINK_NOW) && run_sign_in())
    {
        after_account();
        return true;
    }
    return false;
}

/* ---- Reminders on the home screen -----------------------------------------
 *
 * What was put off during the setup comes back as a panel from the right, as
 * on TigerScale: 10 s after the home screen appears, then every 30 minutes,
 * for as long as it is still missing. Wi-Fi first (nothing else works without
 * it), then the account. */
#define REMIND_FIRST_MS (10 * 1000)
#define REMIND_EVERY_MS (30 * 60 * 1000)

typedef enum
{
    REMIND_NONE,
    REMIND_WIFI,
    REMIND_ACCOUNT,
} remind_t;

static remind_t s_remind;

static remind_t what_to_remind(void)
{
    if (demo_mode_on())
    {
        return REMIND_NONE; /* a showroom screen asks nothing of anybody */
    }
    if (!wifi_mgr_connected())
    {
        /* A saved network that is down is not something to set up again. */
        return wifi_saved() ? REMIND_NONE : REMIND_WIFI;
    }
    return cloud_has_session() ? REMIND_NONE : REMIND_ACCOUNT;
}

static void show_reminder(void)
{
    s_remind = what_to_remind();
    if (s_remind == REMIND_NONE)
    {
        return;
    }
    PT_LVGL_SCOPE_LOCK()
    {
        if (s_remind == REMIND_WIFI)
        {
            ui_show_side_prompt(PROMPT_WIFI, tr(S_OB_WIFI_Q), tr(S_SETUP));
        }
        else
        {
            ui_show_side_prompt(PROMPT_ACCOUNT, tr(S_ACC_PROMPT_Q), tr(S_SIGN_IN));
        }
    }
}

/* "Set up" / "Sign in" on a reminder. True when something changed and the
 * home screen has to be built again. */
static bool act_on_reminder(void)
{
    bool linked = false;
    if (s_remind == REMIND_WIFI)
    {
        run_wifi(true, true);
        if (!wifi_mgr_connected())
        {
            return true;
        }
        network_ready();
        linked = !cloud_has_session() && offer_account();
    }
    else if (s_remind == REMIND_ACCOUNT && run_sign_in())
    {
        after_account();
        linked = true;
    }
    if (linked)
    {
        show_linked_if_new();
        if (s_printer_count > 0)
        {
            run_printer_menu(true);
        }
    }
    return true;
}

static void app_task(void *arg)
{
    (void)arg;
    links_init(s_printers, &s_printer_count);
    s_events = xQueueCreate(4, sizeof(app_event_t));
    ui_set_event_queue(s_events);
    settings_get("sel_printer", s_selected_id, sizeof s_selected_id);
    char view[8] = "";
    settings_get("home_view", view, sizeof view);
    ui_cards_set_list(strcmp(view, "list") == 0);
    if (demo_mode_on())
    {
        /* The made-up printers, reached by nothing: no network to wait for. */
        s_printer_count = demo_mode_printers(s_printers, CLOUD_MAX_PRINTERS);
        links_reload();
        links_set_network_ready(true);
        ESP_LOGW(TAG, "demo mode: %d made-up printers, nothing real is reached", s_printer_count);
    }
    if (xTaskCreatePinnedToCoreWithCaps(presence_task, "presence", 12 * 1024, NULL, 2, NULL, 0,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS)
    {
        ESP_LOGW(TAG, "presence task not started: the account will not see this device");
    }

    /* Power-on picture, a few seconds. The backlight comes up only after it is
     * drawn, so the panel never flashes white first. */
    PT_LVGL_SCOPE_LOCK()
    {
        ui_show_splash();
    }
    vTaskDelay(pdMS_TO_TICKS(150)); /* let LVGL flush it */
    pt_backlight_set((uint32_t)screen_brightness());
    PT_LVGL_SCOPE_LOCK()
    {
        screen_start();
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); /* the picture is seen; the work starts behind it */
    start_probation_timer();

    /* The setup, as on TigerScale V3: the language first on a fresh device,
     * then "Connect to Wi-Fi?" and "Link a TigerTag account?", each with
     * "Later". What is put off is offered again from the home screen. A device
     * already set up goes straight to work with what it has saved. */
    const bool first = !i18n_is_chosen() && !wifi_saved() && !cloud_has_session();
    if (!i18n_is_chosen())
    {
        run_language_first();
    }

    /* A device that is set up does not wait: the home screen comes at once,
     * with the printers of the last read, and the network, the account and the
     * printers are reached behind it (bringup_task). Only the first start is a
     * conversation, step by step. */
    const bool quick = !first && wifi_saved();
    if (quick)
    {
        if (cloud_has_session() && !demo_mode_on())
        {
            load_printer_cache();
            s_printers_unread = true; /* until the account has answered */
            s_bg_retries = true;
        }
        links_reload();
        s_bringup_running = true;
        wifi_mgr_set_retrying(true); /* the icon says "connecting" from the first frame */
        if (xTaskCreatePinnedToCoreWithCaps(bringup_task, "bringup", 16 * 1024, NULL, 4, NULL, 0,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS)
        {
            ESP_LOGE(TAG, "background start-up not started");
            s_bringup_running = false;
        }
    }
    else
    {
        if (wifi_saved())
        {
            run_wifi(false, true);
        }
        else if (first && ask(PROMPT_WIFI, S_OB_WIFI_Q, S_OB_WIFI_SUB, S_SETUP))
        {
            run_wifi(true, true);
        }

        if (wifi_mgr_connected())
        {
            network_ready();
            if (resume_account())
            {
                after_account();
            }
            else if (first)
            {
                offer_account();
            }
        }
        links_set_network_ready(true);
    }

    /* From here the switched-on printers are connected in the background, so
     * that by the time one is touched in the list its report is already in. */
    xTaskCreatePinnedToCoreWithCaps(feed_task, "feed", 12 * 1024, NULL, 4, &s_feed, 0,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (xTaskCreatePinnedToCoreWithCaps(ota_watch_task, "ota_watch", 8 * 1024, NULL, 2, NULL, 0,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS)
    {
        ESP_LOGW(TAG, "no update check in the background: not enough memory");
    }

    show_linked_if_new();

    /* Power-on lands on the printer list: nothing is watched until one is
     * chosen. With an empty account there is nothing to choose. */
    if (s_printer_count > 0)
    {
        run_printer_menu(true);
    }

    /* Built once and kept: switching screens never rebuilds it. */
    PT_LVGL_SCOPE_LOCK()
    {
        ui_prepare_dashboard();
    }
    s_dash_built = true;
    enter_dashboard();

    int64_t remind_at = esp_timer_get_time() + (int64_t)REMIND_FIRST_MS * 1000;
    int shown_count = s_printer_count;
    for (;;)
    {
        const int64_t now = esp_timer_get_time();
        const int64_t left_ms = (remind_at - now) / 1000;
        /* At least once a second: what the background start-up found. */
        app_event_t e = wait_event_for(left_ms > 1000 ? 1000 : (left_ms > 0 ? (uint32_t)left_ms : 1));
        if ((e.type == EV_NONE || e.type == EV_ACCOUNT_SYNCED) && take_background_sync())
        {
            /* The printers page once, when the first printers appear; then the
             * view, built again with the list (and the language) just read. */
            if (shown_count == 0 && s_printer_count > 0)
            {
                run_printer_menu(true);
            }
            enter_dashboard();
            shown_count = s_printer_count;
            continue;
        }
        if (e.type == EV_ACCOUNT_SYNCED || (e.type == EV_NONE && esp_timer_get_time() < remind_at))
        {
            continue;
        }
        if (e.type == EV_NONE)
        {
            /* The printers could not be read (a slow network at start-up): try
             * again quietly rather than leave an empty home until a restart.
             * The background start-up does it itself when it runs. */
            if (s_printers_unread && !s_bg_retries && cloud_has_session() && wifi_mgr_connected())
            {
                const int had = s_printer_count;
                if (fetch_printers(false))
                {
                    ESP_LOGI(TAG, "printers read on retry: %d", s_printer_count);
                    /* The printers page once, when the first printers appear;
                     * a retry for a brand that is still silent stays quiet. */
                    if (had == 0 && s_printer_count > 0)
                    {
                        run_printer_menu(true);
                        enter_dashboard();
                    }
                    else if (s_printer_count != had)
                    {
                        enter_dashboard();
                    }
                }
                remind_at = esp_timer_get_time() + (int64_t)PRINTERS_RETRY_MS * 1000;
                continue;
            }
            show_reminder();
            remind_at = esp_timer_get_time() + (int64_t)REMIND_EVERY_MS * 1000;
            continue;
        }
        if (e.type == EV_PROMPT_LATER)
        {
            continue;
        }
        if (e.type == EV_PROMPT_GO)
        {
            act_on_reminder();
            enter_dashboard();
            continue;
        }
        if (e.type == EV_PRINTERS)
        {
            PT_LVGL_SCOPE_LOCK()
            {
                ui_side_prompt_close();
            }
            const bool changed = run_printer_menu(false);
            const bool settled = __atomic_load_n(&s_ready_gen, __ATOMIC_SEQ_CST) ==
                                 __atomic_load_n(&s_gen, __ATOMIC_SEQ_CST);
            if (changed || !settled)
            {
                enter_dashboard();
            }
            else
            {
                show_dashboard();
            }
            continue;
        }
        if (e.type == EV_PRINTER_JOB)
        {
            links_lock(); /* the list of printers is not rewritten under us */
            const account_printer_t *p = current_printer();
            const int idx = p ? (int)(p - s_printers) : -1;
            links_unlock();
            if (idx >= 0)
            {
                send_job(idx, e.a);
            }
            continue;
        }
        if (e.type != EV_SETTINGS)
        {
            continue;
        }
        PT_LVGL_SCOPE_LOCK()
        {
            ui_side_prompt_close();
        }
        run_settings();
        flush_events();
        if (__atomic_load_n(&s_ready_gen, __ATOMIC_SEQ_CST) != __atomic_load_n(&s_gen, __ATOMIC_SEQ_CST))
        {
            enter_dashboard();
        }
        else
        {
            show_dashboard();
        }
    }
}

void app_main(void)
{
    settings_init();
    ota_boot();
    tz_init();
    i18n_init();
    screen_init();

    if (pt_display_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "pt_display_init failed");
        return;
    }
    pt_backlight_set(0); /* switched on once the power-on picture is drawn */
    wifi_mgr_init();

    /* TLS handshakes and JSON parsing want a deep stack. It lives in PSRAM, as
     * the LVGL task's does: the internal RAM is what Wi-Fi and TLS need. */
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(app_task, "app", 24 * 1024, NULL, 5, NULL, 0,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS)
    {
        ESP_LOGE(TAG, "could not start the application task");
        return;
    }
    ESP_LOGI(TAG, "started");
}
