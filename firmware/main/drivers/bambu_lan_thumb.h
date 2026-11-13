#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The picture of the print in progress on a Bambu Lab in LAN mode, read from the printer's own file
 * service on port 6000: `<folder of gcode_file>/plate_<N>.png`, a small PNG (not the .3mf, a ZIP of
 * megabytes). `ip` is the account's address, `access_code` the printer's, `gcode_file` the path the
 * printer's report gave. The PNG, in PSRAM, for the caller to free with heap_caps_free. */
bool bambu_lan_thumb_fetch(const char *ip, const char *access_code, const char *gcode_file, uint8_t **data,
                           size_t *len);
