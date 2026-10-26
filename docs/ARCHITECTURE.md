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
the one a person picked in *Settings > Time zone*, else chosen from a country,
never from a position: the account's, else the access point's, else UTC
(`net/tz.c`).

`net/tz_table.h` maps a country, or a Canadian province, to a POSIX rule. It is
generated by `firmware/tools/make_tz_table.py` from the IANA database, which checks
each rule against the database for three years ahead. The C library of the device
then reads the rule; a negative transition hour (Greenland) is the one thing it
cannot read, so that rule is written differently and declared approximate. Every
rule was run on the board over 800 instants and compared with the database before
it was trusted.

A country with several zones is represented by its capital's or its largest city's:
the United States by New York, Russia by Moscow. That is a choice; a person
elsewhere in such a country sees the end of a print in that zone, unless they pick
theirs.

*Settings > Time zone* is one list: Automatic (the behaviour above), then one line
per distinct rule of the database, 63 of them, in the order of the clock (by the lower of
a line's two offsets: Ireland's winter is UTC+0). Each line shows the offset in force now,
the places that follow it and, when its zone changes its clock, "summer time" or "winter
time", whichever it is on at that moment (`tz_pick_offset_min()` and `tz_pick_in_summer()`
read the rule: the date of each change in the year, in either hemisphere; the C library's
own conversion answers only for the zone in force). Summer is the higher of the two offsets,
never the name the rule gives them. The same calculation was run in Python against the
database for the 63 lines, every half hour for three years, and agreed everywhere. A touch keeps the line (its zone's name, in the
setting `tz_m`) and it wins over the account and the access point; Automatic forgets
it. The lines are in `tz_table.h` (`TZ_PICKS`), written by the same generator and
checked the same way: every rule of the database must have exactly one line, and every
line's rule must reproduce the database for three years. The example places are written by hand in `PICKS`; each was checked against the
database to have the rule of its line, and Vancouver left the Los Angeles line for that
reason: British Columbia no longer changes its clock in this database.

The page holds 64 rows, which would be 64 buttons and 192 labels in internal RAM, so
it builds eight and moves them under the finger as the list scrolls (`tz_fill()` in
`ui_settings.c`), a transparent block giving the scroll its length.

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
