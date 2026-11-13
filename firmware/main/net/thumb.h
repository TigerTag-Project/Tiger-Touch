#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cloud.h"
#include "lvgl.h"

/* The picture of the print in progress, on a printer's full view.
 *
 * One service for every brand: a driver only says where the picture is (its
 * `thumb_fetch`), and this module fetches it on a task of its own, decodes it,
 * shrinks it to a THUMB_SIZE square and hands the screen a ready RGB565 image.
 * Everything big (the fetched file, the decoded picture, the task's stack) is in
 * PSRAM; the one internal-RAM cost is the HTTPS request itself, a one-off. */

#define THUMB_SIZE 88 /* pixels, square */

typedef enum
{
    THUMB_NONE = 0, /* nothing to show: no print, no source, or not found (yet) */
    THUMB_LOADING,  /* the first attempt is in progress */
    THUMB_READY,
} thumb_state_t;

/* A driver's part: the picture of `file` for printer `p`, as the maker serves
 * it (a PNG), in PSRAM. false when there is none. Runs on the service's task, so
 * it may take seconds; it must not touch the driver's connection. */
typedef bool (*thumb_fetch_fn)(const account_printer_t *p, const char *file, uint8_t **data, size_t *len);

/* The print that is worth a picture: call every second from a task other than LVGL's.
 * A new printer or file starts a fetch, and the picture stays while it is the same one,
 * whichever view is on show; no printer, no source or no file (`p` or `fetch` NULL, `file`
 * empty) drops the picture and its memory. The fetch waits while the internal RAM is
 * near its floor. */
void thumb_want(const account_printer_t *p, thumb_fetch_fn fetch, const char *file);

/* A picture a driver received on its own connection (an Elegoo's MQTT answer) and hands to the service: kept
 * only when it is for the print now wanted (`sn` the printer's serial, `file` the name the view shows), and
 * read on the service's task. The driver's own `thumb_fetch` asks the driver for it and gives nothing back. */
void thumb_provide(const char *sn, const char *file, const uint8_t *data, size_t len);

/* For the screen, under the LVGL lock. `*img` is valid while the state is READY;
 * `*version` changes when it is a new picture. */
thumb_state_t thumb_get(const lv_image_dsc_t **img, uint32_t *version);

/* The screen is about to be cleared: the picture and its memory go. Under the LVGL lock. */
void thumb_forget(void);

/* For the drivers' fetch functions: one blocking GET into PSRAM, growing up to
 * `cap` bytes. Returns the HTTP status, or -1 when the request failed. With
 * `bbl_identity` the request carries Tiger Touch's own client name in the headers
 * Bambu's edge reads: it refuses the borrowed OrcaSlicer name (403), and asks
 * third parties to send their own. The caller frees `*out` with heap_caps_free. */
int thumb_http_get(const char *url, const char *bearer, bool bbl_identity, size_t cap, uint8_t **out, size_t *len);

/* The same for a printer on the local network that wants an `X-Api-Key` (OctoPrint, Moonraker) when
 * `api_key` is not empty. */
int thumb_http_get_key(const char *url, const char *api_key, size_t cap, uint8_t **out, size_t *len);

/* The same for a printer that answers a Digest challenge (PrusaLink): with `user` not empty the request is
 * authenticated with `user` and `pass`; `api_key`, when not empty, is sent as X-Api-Key as well. */
int thumb_http_get_auth(const char *url, const char *user, const char *pass, const char *api_key, size_t cap, uint8_t **out,
                        size_t *len);

/* The same with headers of the caller's (a maker's signed API): `nhdr` name/value pairs. */
typedef struct
{
    const char *name;
    const char *value;
} thumb_hdr_t;
int thumb_http_get_headers(const char *url, const thumb_hdr_t *hdrs, int nhdr, size_t cap, uint8_t **out, size_t *len);

/* Appends `s` percent-encoded to `out` at `n` (everything but the unreserved characters, and the '/' when
 * `keep_slash`), within `cap` bytes; the new length. For the paths of the drivers' picture requests. */
size_t thumb_url_append(char *out, size_t n, size_t cap, const char *s, bool keep_slash);
