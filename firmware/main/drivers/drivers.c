#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "addr.h"
#include "k2_client.h"
#include "demo_mode.h"
#include "printer_driver.h"

static const printer_driver_t *const REGISTRY[] = {
    &DRIVER_CREALITY,
    &DRIVER_BAMBU,
    &DRIVER_SNAPMAKER,
    &DRIVER_ELEGOO,
    &DRIVER_ELEGOO_SDCP,
    &DRIVER_FLASHFORGE,
    &DRIVER_PRUSA,
    &DRIVER_ANYCUBIC,
    &DRIVER_KLIPPER,
    &DRIVER_OCTOPRINT,
};

const printer_driver_t *drivers_for(const account_printer_t *p)
{
    if (demo_mode_on())
    {
        return &DRIVER_DEMO; /* nothing real is reached while the demo runs */
    }
    for (size_t i = 0; i < sizeof REGISTRY / sizeof REGISTRY[0]; i++)
    {
        if (strcmp(REGISTRY[i]->brand, p->brand) == 0 && REGISTRY[i]->support(p) != DRV_NONE)
        {
            return REGISTRY[i];
        }
    }
    return NULL;
}

/* ---- Creality: WebSocket :9999, see k2_client.c ----------------------------- */

static drv_kind_t creality_support(const account_printer_t *p)
{
    return (p->ip[0] && !p->cloud) ? DRV_LAN : DRV_NONE;
}

static void *creality_start(const account_printer_t *p)
{
    return k2_client_start(p->ip);
}

static void creality_stop(void *ctx)
{
    k2_client_stop((k2_client_t *)ctx);
}

static bool creality_control(void *ctx, pv_control_t cmd)
{
    return k2_client_control((k2_client_t *)ctx, cmd);
}

static void creality_snapshot(void *ctx, printer_view_t *v)
{
    k2_client_snapshot((k2_client_t *)ctx, v);
}

/* The print's picture. The K2's web server (port 80) serves the slicer's small picture of
 * every file under /downloads/humbnail/ (the firmware's own spelling), and its name is the
 * file's with ".png" for ".gcode": "Wheel logo-Ender-3 V4-PLA_3m28s.gcode" has
 * ".../humbnail/Wheel logo-Ender-3 V4-PLA_3m28s.png" (Tiger Studio Manager's protocol notes,
 * section 12). The printer's file list names it exactly, over the WebSocket, at the price of
 * a second request per print and a list of every file in memory; this does without.
 * `file` is the name without ".gcode", cut at the view's width: a longer one finds nothing
 * and the screen stays as it was. */
static bool creality_thumb_fetch(const account_printer_t *p, const char *file, uint8_t **data, size_t *len)
{
    *data = NULL;
    *len = 0;
    if (!p->ip[0] || p->cloud || !file[0])
    {
        return false;
    }
    char host[ADDR_HOSTPORT_MAX]; /* the web server is on port 80, whatever port the WebSocket is on */
    addr_host_only(host, sizeof host, p->ip);
    char enc[3 * 48 + 1];
    size_t n = 0;
    for (const unsigned char *c = (const unsigned char *)file; *c && n + 4 < sizeof enc; c++)
    {
        if (isalnum(*c) || strchr("-_.~", *c))
        {
            enc[n++] = (char)*c;
        }
        else
        {
            n += (size_t)snprintf(enc + n, 4, "%%%02X", *c);
        }
    }
    enc[n] = '\0';
    char url[ADDR_HOSTPORT_MAX + sizeof enc + 40];
    snprintf(url, sizeof url, "http://%s/downloads/humbnail/%s.png", host, enc);
    return thumb_http_get(url, NULL, false, 256 * 1024, data, len) == 200;
}

const printer_driver_t DRIVER_CREALITY = {
    .brand = "creality",
    .support = creality_support,
    .start = creality_start,
    .stop = creality_stop,
    .snapshot = creality_snapshot,
    .control = creality_control,
    .thumb_fetch = creality_thumb_fetch,
};
