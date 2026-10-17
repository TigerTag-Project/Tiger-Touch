#pragma once

#include <stdbool.h>
#include <stddef.h>

/* TigerTag account: sign-in and the printers it holds.
 *
 * Same Firebase project and Firestore layout as Tiger Studio, TigerScale and
 * TigerSpool. The password is only ever used for one request and is never
 * stored: the refresh token is what is kept. */

#define CLOUD_MAX_PRINTERS 24

typedef enum
{
    CLOUD_OK = 0,
    CLOUD_ERR_NETWORK,     /* no answer / TLS / timeout */
    CLOUD_ERR_CREDENTIALS, /* wrong e-mail or password, or session revoked */
    CLOUD_ERR_NO_PASSWORD, /* account created with Google */
    CLOUD_ERR_RATE,        /* too many attempts */
    CLOUD_ERR_DISABLED,
    CLOUD_ERR_NOT_FOUND,   /* the document does not exist */
    CLOUD_ERR_OTHER,
} cloud_err_t;

typedef struct
{
    char brand[12];    /* creality, bambulab, ... */
    char id[40];       /* the account's document id */
    char name[48];     /* printerName */
    char ip[40];       /* LAN address, empty for cloud printers */
    char model_id[12]; /* TigerTag catalogue id */
    char sn[40];       /* serial number, or the account's id when it has none */
    char cc[24];       /* access / check code, when the brand uses one */
    char user[24];     /* user name, when the brand has one (PrusaLink, Anycubic) */
    char dev_id[40];   /* Anycubic: the 32-hex device id of the broker topics */
    char acu_model[12]; /* Anycubic: its own numeric model id (the other half of the topics) */
    char key[48];       /* API key: OctoPrint (required), Moonraker (optional), PrusaLink (legacy) */
    char proto[8];      /* Elegoo: "sdcp" for a Centauri Carbon 1; empty for MQTT (CC2) */
    char board_id[40];  /* Elegoo SDCP: the MainboardID that comes with discovery */
    bool cloud;        /* reachable only through the maker's cloud */
} account_printer_t;

/* What the account icon says. NONE: no account (never signed in, signed out, or
 * the session was refused). CONNECTING: an account is stored but the last
 * exchange with Firebase failed or has not happened yet. OK: the last exchange
 * succeeded, so the token in hand is valid. */
typedef enum
{
    CLOUD_LINK_NONE = 0,
    CLOUD_LINK_CONNECTING,
    CLOUD_LINK_OK,
} cloud_link_t;

cloud_link_t cloud_link(void);

/* Keeps the session honest while nothing else uses it: renews the token before
 * it expires (every ~45 min) and, after a failure, probes again every 30 s. A
 * revoked session is then noticed within minutes instead of at the next page
 * that happens to need the account. Cheap when there is nothing to do; call it
 * from a task whose stack can take a TLS handshake. */
void cloud_keepalive(void);

const char *cloud_err_text(cloud_err_t e); /* French, for the screen */

bool cloud_has_session(void);
const char *cloud_email(void);
/* The account's display name (users/{uid}.displayName), as TigerScale shows
 * it; "" until read, or when the account has none. Kept across restarts. */
const char *cloud_display_name(void);

cloud_err_t cloud_sign_in(const char *email, const char *password);
cloud_err_t cloud_resume(void);
void cloud_sign_out(void);

/* Reads every brand. On entry `*count` is how many printers `out` already
 * holds (0 for none): a brand that does not answer keeps those of its printers.
 * `*failed_brands` counts brands that did not answer. Returns CLOUD_OK when at
 * least the session was valid. */
cloud_err_t cloud_fetch_printers(account_printer_t *out, int max, int *count, int *failed_brands);

/* Bambu Lab's cloud session, written to the account by Tiger Studio (the
 * device never signs in to Bambu itself). CLOUD_ERR_NOT_FOUND when Studio has
 * not stored one. */
/* Anycubic in cloud mode: the Anycubic account (e-mail, workbench token) and
 * the shared client identity (Anycubic's CA as base64 DER, the client
 * certificate and key as PEM) that Tiger Studio writes on every cloud printer,
 * from the last account read. Copies in PSRAM, to be released with
 * cloud_anycubic_identity_free(); false when any of the five is missing. */
typedef struct
{
    char *email, *token, *ca_der_b64, *cert_pem, *key_pem;
} cloud_acu_identity_t;
bool cloud_anycubic_identity(cloud_acu_identity_t *out);
void cloud_anycubic_identity_free(cloud_acu_identity_t *id);

cloud_err_t cloud_bambu_session(char *user, size_t user_size, char *token, size_t token_size,
                                char *region, size_t region_size);

/* The language preference of the account ("fr", "pt-pt"...), read only. */
cloud_err_t cloud_account_lang(char *out, size_t out_size);

/* The countries of the account: the one picked for the ranking ("FR") and the
 * one of the prices ("FR", "CA-QC", or "CUSTOM"). Read only; either can be empty.
 * CLOUD_ERR_NOT_FOUND when both are. Used for the time zone only. */
cloud_err_t cloud_account_country(char *squad, size_t squad_size, char *vat, size_t vat_size);

/* ---- Pairing: for accounts created with Google -------------------------------
 *
 * pairStart gives a code and the URL to draw as a QR; the owner approves on the
 * phone; pairPoll then returns "approved" with a custom token, which
 * cloud_pair_poll() exchanges for a session on the spot (it is handed out once). */
typedef struct
{
    char verify_url[160];
    char code[16];        /* "ABCD-EFGH", shown under the QR */
    char poll_token[96];
    int interval_s;       /* how often to poll */
} cloud_pair_t;

typedef enum
{
    PAIR_PENDING = 0,
    PAIR_APPROVED, /* the session is open: cloud_email() holds the account */
    PAIR_DENIED,
    PAIR_EXPIRED,
    PAIR_ERROR,    /* no answer, or the token exchange failed */
} cloud_pair_status_t;

cloud_err_t cloud_pair_start(const char *device, const char *fw, cloud_pair_t *out);
cloud_pair_status_t cloud_pair_poll(const char *poll_token);

/* Writes the language to the account ("fr", "pt-pt"...). */
cloud_err_t cloud_push_lang(const char *code);

/* ---- Presence in the account -----------------------------------------------
 * The device says it is there, as TigerScale (users/{uid}/scales/{mac}) and
 * TigerSpool (users/{uid}/tigerspools/{mac}) do, in
 * users/{uid}/tigertouches/{mac}: its version, its Wi-Fi, whether its screen
 * sleeps, and the printers it follows. One Firestore commit, the same contract
 * and field names as TigerSpool's heartbeat (its docs/PRESENCE.md); the server
 * stamps last_heartbeat_at. The first heartbeat creates the document. */
#define CLOUD_PRESENCE_MAX 10 /* the printers switched on: no more than MAX_LINKS */

typedef struct
{
    char mac[13];        /* the Wi-Fi address, 12 lowercase hex digits: the document id */
    char host[24];       /* "tigertouch-xxxx" */
    const char *fw;      /* the running version */
    int rssi;            /* 0 when not known */
    char ip[16];
    bool screen_off;     /* asleep: Studio then waits longer before calling it offline */
    bool battery;        /* fitted, as the owner declared it (Settings > Battery) */
    bool demo;           /* demo mode is on: the screen shows made-up printers */
    int n;               /* printers switched on */
    char ids[CLOUD_PRESENCE_MAX][40];
    bool online[CLOUD_PRESENCE_MAX];
    bool full;           /* identity fields too (at start and after a sign-in) */
} cloud_presence_t;

cloud_err_t cloud_heartbeat(const cloud_presence_t *p);

/* "Bambu Lab" for "bambulab": the maker's name as people write it. */
const char *cloud_brand_label(const char *brand);
