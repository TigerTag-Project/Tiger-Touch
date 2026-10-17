#pragma once

#include <stdbool.h>

#include "cloud.h"
#include "printer_driver.h"

/* Demo mode (Settings > Demo mode): for a showroom, a fair or screenshots.
 *
 * The owner's decision, and the one deliberate exception to "no fake printer in
 * a shipped build" (CLAUDE.md): while it is on, the printers on the screen are
 * ten made-up ones, printing, and nothing is real. It is off by default, kept
 * across restarts until switched off, and shows nowhere but in the settings,
 * so the screens look exactly as they do with real printers. Its limits keep it
 * from ever touching a machine: no printer of the account is connected, no
 * command leaves the device, and the account is told (`demo` in the presence
 * document) with no printer listed. */

/* Read once at start; switching it restarts the device. */
bool demo_mode_on(void);
void demo_mode_set(bool on);

/* Fills `out` with the made-up printers; returns how many. */
int demo_mode_printers(account_printer_t *out, int max);

/* The driver every printer goes through while demo mode is on. */
extern const printer_driver_t DRIVER_DEMO;
