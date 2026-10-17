# Linking the TigerTag account

The device needs the owner's TigerTag account: the printers, their addresses and
access details all come from it. It offers the same two routes as
[TigerScale V3](https://github.com/TigerTag-Project/Tiger-Scale-V3/blob/main/docs/ACCOUNT-PAIRING.md)
and [TigerSpool](https://github.com/TigerTag-Project/TigerSpool-RFID/blob/main/docs/ACCOUNT-PAIRING.md),
on one page (*Link an account*): e-mail and password, or **Continue with
Google**. The code is `net/cloud.c` (requests), `ui/ui_onboard.c` (pages) and
`run_sign_in()` / `run_pairing()` in `main.c` (the flow).

## E-mail and password

One request to Firebase Authentication (`accounts:signInWithPassword`, the
project's public web key). The answer's refresh token is stored; the password is
wiped from memory as soon as the request is sent and is never stored. At every
start the refresh token is exchanged for a fresh ID token
(`securetoken.googleapis.com`), and renewed in the background before it expires.

An account created with Google has no password. When Firebase answers
`MISSING_PASSWORD` the page says to use **Continue with Google**. With e-mail
enumeration protection on, Firebase answers `INVALID_LOGIN_CREDENTIALS` instead,
exactly as for a wrong password, so the page can only say the credentials were
refused; the Google button is on the same page, under the one just used.

## Continue with Google: pairing by QR

A screen has no browser to run Google's sign-in in, and Google's own "device
flow" (RFC 8628) gives no URL that carries the code, so the owner would retype it,
and polling it needs a client secret a firmware cannot keep. TigerTag owns both
ends instead, with four Cloud Functions (`functions/pairing.js` in the private
backend, deployed to `us-central1-tigertag-connect`):

```
 Tiger Touch                     Cloud Functions                 owner's phone
     |-- pairStart -------------------->|                               |
     |<-- code, verify_url, poll_token  |                               |
 [ QR of verify_url + the code ]        |                               |
     |                                  |<-- pairLookup (what is it?) --|  scans the QR
     |                                  |<-- pairApprove (signed in) ---|  approves
     |-- pairPoll (every 5 s) --------->|                               |
     |<-- approved, custom_token, email |                               |
     |-- accounts:signInWithCustomToken -> ID token + refresh token     |
```

| Step | Request | Answer |
|---|---|---|
| Start | `POST .../pairStart` `{"device":"tigertouch-xxxx","model":"Tiger Touch","kind":"display","fw":"2.0.0"}` | `code` (`ABCD-EFGH`), `verify_url` (`https://tigersystem.io/pair?c=…`), `poll_token`, `expires_in` 600, `interval` 5 |
| Wait | `POST .../pairPoll` `{"poll_token":…}` every `interval` | `pending`, `denied`, `expired`, or `approved` with `custom_token` and `email` |
| Finish | `accounts:signInWithCustomToken` `{"token":…,"returnSecureToken":true}` | `idToken`, `refreshToken` |

- **`kind: "display"`** is what the approval page names the device by ("your
  screen"); the device name `tigertouch-xxxx` is the last two bytes of its Wi-Fi
  address, as TigerScale's `tigerscale-xxxx`.
- **The QR holds `verify_url` exactly as the backend sends it**, and the code is
  printed beside it in large type: a QR seen at an angle does not always scan,
  and the code can be typed on the page.
- **The account id is not in `signInWithCustomToken`'s answer.** It is the
  `user_id` claim of the ID token, decoded from the JWT payload
  (`uid_from_jwt()`). TigerScale learned this the hard way: without it the
  account looked linked and every read of it failed until the next restart.
- **The custom token is handed out once.** If its exchange fails, the pairing is
  spent and the page says *Failed*; starting again is one touch.
- **Limits, TigerScale's:** a code lives 10 minutes; the device gives up after
  11; six failed polls in a row show *Failed* rather than leaving a dead backend
  looking like a wait. The back arrow leaves at any moment.

**Seen working on a Panda Touch** (2026-10-14, reported by the owner): QR
scanned, approved on the phone, the device linked to the Google account.

After either route the session is the same: refresh token, account id and
e-mail in NVS, nothing else.

## What is read from the account, and written

| Path | Read | Written |
|---|---|---|
| `users/{uid}/printers/{brand}/devices` (one collection per brand) | at start, and on *Settings > Printers > refresh* | never |
| `users/{uid}/prefs/app` `lang` | at start, when no local choice is waiting | when a language is picked on the device (see [ONBOARDING.md](ONBOARDING.md#1-language)) |
| `users/{uid}` country fields | at start, for the time zone | never |
| The Bambu Lab cloud session stored by Tiger Studio | for a Bambu printer reached through the cloud | never |
| `users/{uid}/tigertouches/{mac}` | its `display_name`, on a full heartbeat | every 30 s, every 5 min while the screen sleeps (below) |

### The device's presence

Like TigerScale (`users/{uid}/scales/{mac}`) and TigerSpool
(`users/{uid}/tigerspools/{mac}`), a Tiger Touch tells the account it is there,
so that the owner sees which devices they have, which version each runs and
whether it is on. One document per device, its id the Wi-Fi address in 12
lowercase hex digits, written with one Firestore `documents:commit` (an update
mask, the server's time in `last_heartbeat_at`); the first write creates it.
The field names are TigerSpool's (its `docs/PRESENCE.md`), so a reader of one
reads the other.

| Field | Written | Value |
|---|---|---|
| `mac`, `fw_version`, `mdns_hostname`, `hardware_revision` | full heartbeat | the document id, the running version, `tigertouch-xxxx`, null |
| `display_name` | full heartbeat, only when the document has none | `TigerTouch-XXXX`; a name given in Tiger Studio is kept |
| `last_heartbeat_at` | every heartbeat | the server's time |
| `wifi_signal_dbm`, `ip_address` | every heartbeat | the link to the access point |
| `power_state` | every heartbeat | `active`, or `screen_off` while the screen sleeps |
| `battery_present` | every heartbeat | what the owner declared in *Settings > Battery*: yes by default, since the Panda Touch and the K-Touch ship with one inside; not a measurement (as on TigerSpool) |
| `power_source`, `battery_percent`, `is_charging` | every heartbeat | null: the board gives the firmware no way to read them ([HARDWARE.md](HARDWARE.md#power-the-switch-the-dock-and-the-battery)); TigerSpool measures its battery on an analog input wired to it, which this board does not have |
| `printers_active`, `printer_ids`, `printers_online` | every heartbeat | how many printers are switched on, their account ids, the ones answering (none in demo mode) |
| `demo` | every heartbeat | `true` while *Settings > Demo mode* is on: the screen then shows made-up printers and lists none here |

A full heartbeat (identity fields too) is sent at start and after a sign-in;
then every 30 s while the screen is on, every 5 minutes while it sleeps, and at
once when the printers switched on or answering change. After three failures in
a row it waits 5 minutes. It runs in a task of its own on core 0, and skips a
beat when internal RAM has no 24 KB block for a TLS session.

**What the account needs.** The project's Firestore rules must let the owner
write `users/{uid}/tigertouches/{deviceId}` (the same rule as `tigerspools`);
without it every heartbeat is refused with HTTP 403 and nothing else changes.
Tiger Studio shows the document once it has a reader for this collection.

## Related

- [ONBOARDING.md](ONBOARDING.md): where this page sits in the first start.
- [SECURITY.md](../SECURITY.md): what is stored, and where it is sent.
- The shared pairing design and why RFC 8628 was rejected:
  [TigerScale V3, ACCOUNT-PAIRING.md](https://github.com/TigerTag-Project/Tiger-Scale-V3/blob/main/docs/ACCOUNT-PAIRING.md).
