# Printer compatibility

For each driver: how it reaches the printer, what it reads, and **what has actually
been seen**. "Seen" means run by this firmware against a real machine. "Compiled"
means written from the maker's protocol and built, never exercised.

## Summary

| Driver | Reached through | Status |
|---|---|---|
| Creality (K2) | WebSocket `:9999`, heartbeat, `boxsInfo` | **Seen**: a full print on a K2; states, progress, layers, times, set points, file, CFS slots. **Also seen idle on an Ender-3 V4** (2026-10-14): state, nozzle, bed, the external spool and the CFS slots with the active one ringed |
| Bambu Lab, cloud | MQTT over TLS to the region broker, session read from the account | **Seen**: a full print on a P2S; AMS, external spool, progress, states, restart in the middle of a print |
| Bambu Lab, LAN | `mqtts://<ip>:8883`, user `bblp`, access code, address from `broker` | **Seen idle on an X1C and an A1** (2026-10-15): state, nozzle, bed, chamber (X1C), light state; both sessions held with the device on a good signal. On a weak one (-76 dBm and below) the reports a LAN printer streams filled the link and the sessions dropped: a LAN Bambu wants good Wi-Fi. No print yet |
| Snapmaker | Moonraker WebSocket `:7125`, subscribe | **Seen idle on a U1** (2026-10-14): state, the four nozzles with the active one ringed, bed; no print yet |
| Elegoo CC2 | MQTT `:1883` | Compiled |
| Elegoo Centauri Carbon 1 | SDCP, UDP `3000` then WebSocket `:3030` | Compiled; the protocol's read-only part was seen by a third party on firmware V1.4.49 |
| FlashForge | HTTP `POST :8898/detail`, every 2 s | **Seen idle on an AD5X** (2026-10-14): state, nozzle and bed temperatures; no print yet |
| Prusa | PrusaLink HTTP with Digest authentication, or `X-Api-Key` when the account holds a legacy `apiKey` | Compiled |
| Klipper | Moonraker over HTTP (port 7125 unless `ip` says otherwise), `X-Api-Key` when the account has one, a poll every 2 s | **Seen against Tiger Studio's Moonraker mock** (2026-10-15): printing, paused, finished, cancelled, error with the printer's message, Klipper not ready; progress, file, layers, time left, temperatures, the light, four AFC lanes with the active one ringed. Not yet on a real printer |
| OctoPrint | its REST API over HTTP (port 80 unless `ip` says otherwise), `X-Api-Key`, a poll every 2 s | **Seen against Tiger Studio's OctoPrint mock** (2026-10-15): printing, paused, finished, error with OctoPrint's text, no printer plugged in; progress, file, time left, temperatures. Not yet on a real printer |
| Anycubic, LAN | `mqtts://<ip>:9883` | Compiled |
| Anycubic, cloud | `mqtts://mqtt-universe.anycubic.com:8883`, one session for every cloud Anycubic of the account, mutual TLS, signed in as the slicer does (as on TigerSpool) | **Seen idle on a Kobra X** (2026-10-14): online, state, the four slots of its box with material and colour; no print yet. Temperatures are not asked for (below) |

A printer whose driver is not "seen" works as far as its maker's documentation says;
a field it does not report is shown as `--`, never as zero.

**Every LAN driver takes `host` or `host:port`.** The account's address (`ip`,
or `broker` for a LAN Bambu) may carry a port: then it is used as it is, and
without one the brand's own port applies - 8883 (Bambu), 9999 (Creality), 1883
(Elegoo CC2), 3030 (Elegoo CC1's WebSocket; its UDP discovery stays on 3000 and
uses the host alone), 8898 (FlashForge), 7125 (Snapmaker, Klipper), 9883
(Anycubic), 80 (Prusa, OctoPrint). A real printer needs no port written; one is
for several printers behind one computer, each on a port of its own, as Tiger
Studio's test printers are. The split is `firmware/main/net/addr.h`; the
account's record has no separate port field. Only Klipper and OctoPrint have
been run on an address with a port (Tiger Studio's mocks); the other brands
have no test printer yet and were checked on their bare address only.

## Several nozzles

The dashboard adapts to the number the printer reports: one, two (left and right)
or up to four, with the active one marked. How each driver learns it:

| Printer | Source of the nozzles | Active one | Status |
|---|---|---|---|
| Bambu Lab, dual head (H2D, H2C, X2D) | `device.extruder.info[]`, id 0 is the right nozzle, id 1 the left | `(extruder.state >> 4) & 0xF` | Compiled |
| Snapmaker U1 | `extruder` to `extruder3` present in the status | `toolhead.extruder`; failing that, the one being heated | **Seen** on a U1, idle: four tiles, the active one ringed |
| FlashForge Creator 5 Pro (tool-changer) | `nozzleTemps[]` with `nozzleTargetTemps[]` | `matlStationInfo.currentSlot` (1-based) | Compiled |
| FlashForge, dual extruder | `nozzleCnt == 2`, `leftTemp`, `rightTemp` | the one with a set point | Compiled |

Not handled: the H2C's rack of interchangeable nozzles (only the two mounted ones
are shown).

## Pause, resume and stop

Drawn all the time on every card and on the full view; active only while a job
runs and only on a connection that takes commands, inert (dimmed, no touch) the
rest of the time. Stop asks for a confirmation before anything is sent. The frames are the ones
Tiger Studio sends; none of them has been run from this firmware yet.

| Driver | Command | Status |
|---|---|---|
| Creality (K2) | WebSocket `set` with `pause` 1 / 0 and `stop` 1 | Written from Studio's notes, compiled; to try on the K2 |
| Bambu Lab, LAN | MQTT `print` with `command` `pause` / `resume` / `stop` on `device/<sn>/request` | Compiled; never run (the P2S is used through the cloud) |
| Bambu Lab, cloud | none | **Inert buttons**: the cloud takes no pause, resume or stop from this session, as in Studio |
| Snapmaker | Moonraker `printer.print.pause` / `resume` / `cancel` | Compiled; never run |
| Elegoo CC2 | MQTT methods `1021` pause, `1023` resume, `1022` cancel | Compiled; never run |
| Elegoo CC1 | SDCP commands `129` pause, `131` resume, `130` stop | Compiled; numbers from the OpenCentauri notes, unconfirmed on a CC1 |
| FlashForge | HTTP `POST /control`, `jobCtl_cmd` `pause` / `continue` / `cancel` | Compiled; never run (recent models only) |
| Prusa | `PUT /api/v1/job/<id>/pause` and `/resume`, `DELETE /api/v1/job/<id>`, Digest | Compiled; never run |
| Klipper | `POST /printer/print/pause`, `/resume`, `/cancel` | **Seen against the mock**: pause, resume and stop from the buttons, the state followed |
| OctoPrint | `POST /api/job` `{"command":"pause","action":"pause"}` / `"resume"`, `{"command":"cancel"}` | **Seen against the mock**: pause, resume and stop from the buttons, the state followed |
| Anycubic, LAN | MQTT `print` request, action `pause` / `resume` / `stop`, `taskid` `-1` | Compiled; never run |
| Anycubic, cloud | none for now | **Inert buttons**: the broker closes the whole session on a publish it does not allow, so no command is sent until each one is seen allowed |

A refused command is never reported back: the printer's own report is what
changes the button.

## The light

One button on every card and on the full view. It is always drawn; it is inert
(dimmed, not clickable) while the printer is not reached or has no light command
here. It is lit while the printer says its light is on, and one touch switches it.
The frames are Tiger Studio's; only the K2's (local) and the P2S's (cloud) have been seen to work from this firmware, on and off.

| Driver | Command | State read from | Status |
|---|---|---|---|
| Creality (K2) | WebSocket `set` with `lightSw` 1 / 0 | `lightSw` | **Seen on a K2**: the LED switches on and off from the button |
| Bambu Lab, LAN and cloud | MQTT `system` `ledctrl`, `led_node` `chamber_light` (or what `lights_report` names), `led_mode` on / off, on `device/<sn>/request` | `print.lights_report` | **Seen on a P2S through the cloud**: the light switches on and off from the button and the state follows (the local broker is compiled only) |
| Snapmaker | Moonraker `printer.gcode.script` with `SET_LED LED=cavity_led` (all channels 1 or 0) | `led cavity_led` `color_data` | Compiled; never run |
| Elegoo CC2 | MQTT method `1029`, `power` 1 / 0 | `led.status` | Compiled; never run |
| Anycubic, LAN | MQTT `light` request, action `control`, `type` 3 (the chamber light; 1 is the camera's) | the `light` report | Compiled; never run |
| Anycubic, cloud | none for now (see pause above) | - | Inert |
| FlashForge | HTTP `POST /control`, `lightControl_cmd`, `status` `open` / `close` | `detail.lightStatus` | Compiled; never run (recent models only) |
| Prusa | none: Tiger Studio has none either | - | No light command |
| Klipper | `POST /printer/gcode/script`: `SET_PIN PIN=<name> VALUE=1/0` for an `output_pin` named like a light, `SET_LED LED=<name> RED=1 GREEN=1 BLUE=1 WHITE=1` (or 0) for an LED chain | the pin's `value`, the chain's `color_data` | **Seen against the mock**: off from the button, the state followed |
| OctoPrint | none | - | No light command |
| Elegoo CC1 | none: Tiger Studio's SDCP driver has none | - | No light command |

## Klipper and OctoPrint

Tiger Studio Manager's two printer plugins (2.41): any printer that runs
Klipper with Moonraker, or a Marlin printer behind OctoPrint. Each is a brand of
its own in the account (`klipper`, `octoprint`); what follows is read from
Studio's plugin source, and run against Studio's mocks, not yet a real
printer. How to run them, put one on an account and drive each state: Tiger
Studio Manager's `docs/PRINTER-MOCKS.md` (they live in that repository, never
in this one). "Seen against the mock" is never "works on a real printer".

- **The address is `ip`, as `host` or `host:port`.** Klipper's port is 7125
  unless written, OctoPrint's 80 (an OctoPi). A name such as `voron.local` is
  not resolved: the device has no mDNS. Write the address.
- **The API key is `apiKey`.** OctoPrint needs one; Moonraker only when it is
  set to ask (most home installs trust the local network). An empty key is no
  key.
- **Klipper: what the printer has decides what is asked.** Its objects are
  listed once: up to four extruders, a chamber sensor (`heater_generic
  chamber`, `temperature_sensor chamber`, or any of those named with
  "chamber"), a light (an LED chain, or an `output_pin` named like a light), a
  Happy Hare MMU (`mmu`) or AFC lanes. Gate and lane colours are read when
  hexadecimal; a colour name is shown as unknown, as in Studio.
- **Klipper's time left** is the file's `estimated_time` less the time printed;
  Moonraker has no live figure.
- **OctoPrint with no printer plugged in** answers, but `/api/printer` says
  409: the card says so instead of showing the printer offline.
- **Snapmaker stays separate.** A U1 is Moonraker too, but keeps its own driver
  for its proprietary filament data.

## Bambu Lab in LAN mode

- **The address is `broker`** in the account (where Tiger Studio writes it, and
  TigerSpool reads it); without it the printer shows "unknown address".
- **Each LAN printer is a session of its own** with its printer: about 18 KB of
  internal RAM each, nothing shared ([CONNECTION-BUDGET.md](CONNECTION-BUDGET.md)).
- **It wants good Wi-Fi.** A LAN printer pushes its full reports continuously;
  at -76 dBm two of them took the device's link from 28 ms to 240 ms and one
  session dropped every minute or so ("No PING_RESP"). Next to the router
  (-48 dBm) both held, at 16 ms.
- **The printer allows few local clients.** A TigerSpool following the same
  printers in LAN mode is one of them.
- **No chamber sensor on the A1, A1 mini, P1P, P1S**, which still report a
  chamber temperature: it is shown as `--`, told by the model code that starts
  the serial (`039`, `030`, `01S`, `01P`). Seen on an A1.

## Anycubic through the cloud

What the bench taught, each point a session lost before it was understood
(`drivers/drv_anycubic.c`, "Cloud mode"; TigerSpool's `anycubic_cloud.cpp` is
the reference):

- **The handshake needs RSA with SHA-1 among the offered signature
  algorithms.** The broker's certificate is signed with SHA-1 by Anycubic's own
  CA; a client hello without `rsa_pkcs1_sha1` is answered with alert 71,
  "insufficient security" (seen with `openssl s_client`, and as
  `-0x7780` from mbedTLS). mbedTLS 3 does not offer it by default. The
  connection adds it, and verifies the server against that CA only, under a
  profile that accepts SHA-1; every other connection keeps the defaults.
- **One subscription per report family**, not the whole subtree: `print`,
  `tempature`, `status`, `light`, `lastWill`, `multiColorBox`, `extfilbox`.
- **Only the two `getInfo` TigerSpool sends** (`multiColorBox`, `extfilbox`).
  The `tempature` and `light` queries the LAN driver sends at rest made the
  broker close the session within a second. So, at rest, the temperatures read
  0 until the printer reports them on its own while printing.
- **A type of its own.** The broker keeps one session per account and per
  client type, and the newest takes the place of the older. Signed in as
  "web", the device and a TigerSpool following the same printer took the
  session from each other every few seconds. The device signs in as "android";
  three sessions in a row lasting under ten seconds make it move to the next
  type ("ios", "web", "pcf").
- **What the account must hold**, written by Tiger Studio on the cloud
  printer: `key` and `machineType` (the two halves of the topics), `cloudEmail`
  and `cloudToken` (the Anycubic account), `acuCloudCaDerB64`,
  `acuCloudClientCertPem` and `acuCloudClientKeyPem` (the client identity).
  The device logs which are present, never their values, and keeps them in
  memory only. A missing one shows *"Its cloud sign-in is missing"*; a token
  the broker refuses, *"Anycubic refused the sign-in"*: both are fixed by
  refreshing the printer in Tiger Studio.

## What a driver must say about itself

A driver that has not run against a real machine carries that sentence in the header
of its source file. The day it runs, this table, the header and the load price in
`docs/CONNECTION-BUDGET.md` change together.
