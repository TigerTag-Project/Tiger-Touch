#pragma once

#include "thumb.h"

/* The picture of the print in progress on a printer that runs Klipper's Moonraker (Klipper, Snapmaker):
 * a `thumb_fetch_fn` for the two drivers. */
bool moonraker_thumb_fetch(const account_printer_t *p, const char *file, uint8_t **data, size_t *len);
