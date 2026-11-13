# Architecture

## Shape

```
              +---------------------------+
   touch ---> |  LVGL task (UI, one lock) | <--- feed task: one view a second
              +-------------+-------------+
                            | events
                  +---------v----------+
                  |  application task  |   language -> Wi-Fi -> time -> account ->
                  |  (the state machine)|   printers -> home -> settings pages
                  +---------+----------+
                            |
                   +--------v---------+        per enabled printer:
                   |      links       | -----> one driver instance, one view
                   +--------+---------+
                            |
        +---------+---------+---------+---------+---------+
     creality   bambu    snapmaker   elegoo   flashforge  prusa  anycubic  klipper  octoprint
```

## Boot

1. NVS, language, screen, display, Wi-Fi are brought up; the backlight stays off.
2. The boot picture is drawn, then the backlight comes on, so the panel never
   flashes white first.
3. **A device that is set up does not wait.** One second after the picture, the
   printers page is on screen with the printers switched on at the last read
   (kept in NVS, `save_printer_cache()`), each card saying it is connecting.
   Behind it, `bringup_task` reaches the saved Wi-Fi (patiently: a router may
   refuse a board that has just restarted), asks for the clock without waiting
   for it (2 s at most; SNTP keeps asking), resumes the account, then opens
   the printer links (`links_set_network_ready`: none before, so that a cloud
   printer has its session) and reads the account: its language, its country,
   its printers, again every 30 s while a brand has not answered. The icons at
   the top follow on their own; the screens pick up what the reads changed
   once a second (`take_background_sync()`).
4. **A new device** runs the setup instead, one question at a time
   ([ONBOARDING.md](ONBOARDING.md)): the language, then "Connect to Wi-Fi?" and
   "Link a TigerSystem account?", each with *Later*; the sign-in page offers e-mail
   and password or Google by QR ([ACCOUNT-PAIRING.md](ACCOUNT-PAIRING.md)).
5. The home loop waits for touches, and on its own: reminders of what was put
   off, a retry of a failed read of the printers.
6. Choosing one printer shows its full view, built once and kept.

Measured on the bench (2026-10-15, ten printers switched on): the printers on
screen 2.5 s after power-on, the first printer connected at 11 s, all ten at
22 s; before, the screen waited about 26 s on "Connecting...", "Reading your
printers" pages and the last printer connected at 46 s.

## The one rule that shapes the rest

A **driver** turns a printer of the account into the same `printer_view_t`, and
nothing else crosses that line. A screen never learns which brand is behind it; a
driver never draws. Adding a brand is one `drv_*.c`, one line in the registry and
one price in `links.c`.

## Threads and who may touch what

| Task | Owns | Touches LVGL |
|---|---|---|
| LVGL task | the widgets | yes, it is the owner |
| application task | the flow, the account, the settings | only under `PT_LVGL_SCOPE_LOCK` |
| feed task | the per-second view, the token keep-alive | only under the lock |
| update watch task | a read of the OTA manifest 20 s after start (each minute until it is read), then every 6 h; a version found is offered by a window on the home screens | never |
| update task (during an install) | the download and the flash writes, stack in **internal** RAM | never; progress goes through the lock |
| `esp_timer` | the end of a new image's 60 s probation | never |
| a driver's own tasks (WebSocket, MQTT, poll) | its report | never |

A driver's report is protected by the driver's own mutex; `links_view()` copies it.

**Cores.** The LVGL task runs alone on core 1. Everything that waits on the
network is on core 0: lwIP's TCP/IP task (`CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0`),
the MQTT clients (`CONFIG_MQTT_USE_CORE_0`), the WebSocket clients
(`task_core_id = 0`), the application, feed and update-watch tasks. A burst of
printer traffic never takes the core that draws. The one exception is the update
task itself (`net/ota.c`): it runs on core 1, with an internal stack because
`esp_ota_write()` turns the flash cache off, while the screen shows only the
update ring.

**Shared cloud sessions.** Every Bambu Lab in cloud mode rides one MQTT session,
and so does every Anycubic in cloud mode (one per brand, opened by the first
printer, closed with the last); messages are routed to their printer by topic.

## Memory

Task stacks, reports and TLS buffers are in PSRAM. What is left to budget is the
internal RAM a connection costs: `docs/CONNECTION-BUDGET.md`.

## Time zone

The clock the device keeps is UTC (TLS needs it, and SNTP gives it). Local time is
used in one place: the estimated end of a print on the printer card. Its zone is
chosen from a country, never from a position: the account's, else the access
point's, else UTC (`net/tz.c`).

`net/tz_table.h` maps a country, or a Canadian province, to a POSIX rule. It is
generated by `firmware/tools/make_tz_table.py` from the IANA database, which checks
each rule against the database for three years ahead. The C library of the device
then reads the rule; a negative transition hour (Greenland) is the one thing it
cannot read, so that rule is written differently and declared approximate. Every
rule was run on the board over 800 instants and compared with the database before
it was trusted.

A country with several zones is represented by its capital's or its largest city's:
the United States by New York, Russia by Moscow. That is a choice; a person
elsewhere in such a country sees the end of a print in that zone. The device has no
setting for it yet.

## The print's picture

The full view shows the picture of the print in progress, right of the times
(`ui_dashboard.c`). One service does the work for every brand (`net/thumb.c`); a
driver only says where the picture is, with an optional `thumb_fetch` in
`printer_driver.h`. The feed task tells the service once a second which print is
worth a picture (the printer shown, printing, paused or done, and a driver with a
`thumb_fetch`); the service's own task, in PSRAM, does the rest:

1. **Fetch**: the driver's function returns the picture as the maker serves it (a PNG).
2. **Read line by line**: the ROM's `tinfl` inflates it in 32 KB pieces, each line is
   unfiltered and added to the output pixel it falls in (a box average, the
   transparency laid over the tile's colour), so about 80 KB of PSRAM in small blocks
   is all that is held. Decoding a 512 x 512 picture whole asks for two blocks of a
   megabyte and failed on the second try, when the PSRAM was too fragmented to give
   them (2026-11-13); the whole decode stays as the fallback for 16-bit and
   interlaced PNGs.
3. **Show**: an 88 x 88 RGB565 image, double-buffered; the dashboard reads it under the
   LVGL lock and a drop of the old one is done under that same lock.

It is fetched as soon as the print starts, whichever view is on show, and kept while
it is the same printer and file, so going back and forth costs nothing. It waits while
the internal RAM is under 50 KB, so the printers come first. Without a picture the
screen is as it was.

A Creality K2 serves the slicer's picture of each file on its web server (port 80,
`/downloads/humbnail/<file>.png`, the firmware's own spelling); the print's picture is the one
named like its file, so no request goes to the WebSocket. It is written from Tiger Studio
Manager's notes and was not tried on a K2.

A Klipper or a Snapmaker is asked through Moonraker (`drivers/moonraker_thumb.c`, one function for
both): the file that is printing (`print_stats.filename`, with its folder: the view's name has
lost both the folder and the extension), its metadata, and the smallest listed picture that is at
least as big as the screen's, from the printer's file server. Plain HTTP on the local network, so
almost nothing in internal RAM. Written from Tiger Studio Manager's code, not tried on a machine.

For a Bambu Lab in cloud mode the source is the account's task list: the five latest
tasks of the printer, the one whose title starts like the file name the printer
reports, and its `cover`, a PNG on the maker's storage that needs no token. The
request carries Tiger Touch's own client name: Bambu's edge answers the borrowed
OrcaSlicer name with 403 (Cloudflare, since 2026-11-12) and asks third parties to send
their own. The task list is read as text, not as a cJSON tree, which is hundreds of
small blocks in internal RAM.

## Account

Sign-in is an e-mail and password, or Google through a QR approved on the phone
([ACCOUNT-PAIRING.md](ACCOUNT-PAIRING.md)); the refresh token is kept, a password
never is. The printers, with their addresses and access details, are read from
the account at every start and held in RAM only. The language is written to the
account when it is picked on the device, and read from it otherwise.

## Update

Two application slots, a manifest on GitHub Pages, every image checked before it
is switched to, and a 60 s probation after which the bootloader would go back:
[OTA.md](OTA.md).
