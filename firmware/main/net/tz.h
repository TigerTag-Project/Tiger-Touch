#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* The time zone of the device.
 *
 * The device is used in every country, so the zone is not written in the code.
 * A person can pick theirs (Settings > Time zone), and that wins. Otherwise it is
 * taken from a country, in this order: the one kept in the TigerSystem account,
 * then the one the access point announces, and UTC when neither is known. Nothing
 * is sent anywhere to find out: no address is geolocated.
 *
 * The zone only matters for the time of day shown on the printer card (the
 * estimated end of the print). The clock that TLS needs is UTC and does not
 * depend on it. */

typedef enum
{
    TZ_NONE,    /* no country known: UTC */
    TZ_ROUTER,  /* the access point's country */
    TZ_ACCOUNT, /* the account's country (or, for Canada, province) */
    TZ_MANUAL,  /* the line the person picked in the settings */
} tz_source_t;

/* From the country the account gave last time (kept in NVS), UTC when there is
 * none. Call once at start, before anything shows a time. */
void tz_init(void);

/* The access point's country, two letters; "" when it announced none. */
tz_source_t tz_from_router(const char *country);

/* What the account holds: `squad` is the country picked for the ranking, `vat`
 * the country of the prices, which can be a Canadian province (CA-QC). Pass
 * empty strings when the account was read and holds neither: the one remembered
 * is then forgotten. Do not call it when the read failed. */
tz_source_t tz_from_account(const char *squad, const char *vat);

tz_source_t tz_source(void);

/* ---- The list to pick from ----
 *
 * One line per rule of the database (tz_table.h), in the order of the clock: a standard
 * offset, the places that follow it, and whether it has a summer time. */
int tz_pick_count(void);
void tz_pick_city(int pick, char *out, size_t n);  /* the first place: "Paris" */
const char *tz_pick_places(int pick);              /* "Paris, Berlin, Rome, Madrid" */
bool tz_pick_summer(int pick);                     /* the zone changes its clock in summer */

/* What a line gives at an instant, daylight time included: minutes east of UTC, and whether
 * it is on the higher of its two offsets (the summer one) at that instant. A line without
 * summer time is never in summer. */
int tz_pick_offset_min(int pick, time_t utc);
bool tz_pick_in_summer(int pick, time_t utc);

/* The line picked by hand: its index, -1 when none (automatic). Setting -1 goes back to
 * the account's country, then the access point's, then UTC. Kept across restarts and
 * updates. False when the index is not a line. */
int tz_manual(void);
bool tz_set_manual(int pick);

/* "UTC+1", "UTC-3:30", "UTC+0" */
void tz_format_offset(int minutes, char *out, size_t n);
