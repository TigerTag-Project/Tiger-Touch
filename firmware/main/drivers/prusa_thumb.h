#pragma once

#include "thumb.h"

/* The picture of the print in progress on a PrusaLink printer: a `thumb_fetch_fn` for its driver. */
bool prusa_thumb_fetch(const account_printer_t *p, const char *file, uint8_t **data, size_t *len);
