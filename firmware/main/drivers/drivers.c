#include <string.h>

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

const printer_driver_t DRIVER_CREALITY = {
    .brand = "creality",
    .support = creality_support,
    .start = creality_start,
    .stop = creality_stop,
    .snapshot = creality_snapshot,
    .control = creality_control,
};
