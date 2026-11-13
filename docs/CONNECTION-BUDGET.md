# The connection budget

How many printers one Tiger Touch can hold open at once, how that is decided,
and every measurement it rests on. The same model as
[TigerSpool's](https://github.com/TigerTag-Project/TigerSpool-RFID/blob/main/docs/CONNECTION-BUDGET.md),
so the two devices count, refuse and explain the same way.

The code is `firmware/main/drivers/links.c` (the numbers at its top). If this
document and that file ever disagree, the file is what runs: fix whichever is
wrong, in the same change.

---

## The idea: load slots, like seats on a bus

The limit is not a **number** of printers. It is **internal RAM**: the
ESP32-S3 has a few hundred kilobytes of it beside 8 MB of PSRAM, and what a
printer's connection keeps there depends on how it is reached - a WebSocket or
an MQTT client keeps its task stack there, a TLS session its socket, an HTTP
poller almost nothing.

So every printer takes the **load slots** its connection actually costs, and
the device has a fixed number of them. **One load slot is one kilobyte of
internal RAM.**

> **"Load slot", never plain "slot".** A slot is a place a spool sits - an AMS
> tray, a CFS bay. The two must not be confused in code, in logs or in
> conversation.

A printer is refused at the moment somebody **switches it on**, when they can
see why - not later, as a printer that stays offline with no explanation.

---

## The numbers

### The budget: 144 load slots

| | |
|---|---|
| Internal RAM free with the firmware up and no printer open | **203.5 KB** (2026-10-15, two passes) |
| Survival floor - below it nothing new opens, and the device closes a link to stay alive | **32 KB** |
| Usable for printers | **~171 KB** |
| **Budget** | **144 load slots** |
| Reserve kept for what the model does not count | **~27 KB** |

The reserve covers what the budget does not model: a one-off TLS request (the
account read, a token renewal, the update download, whose task stack is
internal), a screen being built, a JSON document parsed. It was 33 KB (budget
120) until it was measured: with eight printers open, an account read took
the free RAM 10 KB lower than at rest and an update download 19 KB lower (the
worked example below). The budget went to 128, then to 144 when the printer
list moved to PSRAM and the device started with 20 KB more (2026-10-15).

The print's picture is one more one-off request (`net/thumb.c`): the account's session, the
task list and the picture are three TLS requests one after the other, each measured at
9 to 11 KB of internal RAM at its lowest (a P2S in cloud mode, 2026-11-13), and given back.
It never runs while less than 50 KB of internal RAM is free (the floor, that cost and a
margin): the printers come first, and it waits.

**The budget does not stop a crash.** The floor does: if free internal RAM stays
under 32 KB for three seconds, the last link opened is closed and its printer
switched off (`links_tick()`). The budget exists to make the choice visible and
predictable, not to guard the heap.

**And at most ten links**, whatever the budget says (`MAX_LINKS`), as on
TigerSpool.

### Load slots per printer

Each is the **worst cost measured** for that connection over every pass, rounded
up to the next kilobyte. One reading of the same printer varies by 2 to 3 KB
from pass to pass (what else the heap is doing at that moment), which is why
the worst is kept and not the average. Creality and Snapmaker use the same
WebSocket client, so they share its worst reading.

| Printer | Reached through | Measured | Load slots |
|---|---|---|---|
| **First Bambu Lab in cloud mode** | MQTT over TLS, opens the account's session | 21.0 KB | **21** |
| **Each further Bambu Lab in cloud mode** | a subscription on that session | 3.3 KB | **4** |
| **Bambu Lab in LAN mode** | MQTT over TLS, a session of its own: nothing is shared | 20.5 KB (X1C, A1) | **21** |
| **Creality** | WebSocket, the same client as Snapmaker's | 14.9 KB | **15** |
| **Snapmaker** | WebSocket (Moonraker) | 14.9 KB (U1) | **15** |
| **Elegoo** | MQTT (CC2); WebSocket (CC1) | 15.2 KB (CC2) | **16** |
| **FlashForge** | HTTP poller, its stack and buffers in PSRAM | 4.7 KB (AD5X; 1.8 KB on most passes) | **5** |
| **Prusa** | HTTP poller like FlashForge's | *not measured* | **5** |
| **Klipper** | HTTP poller (Moonraker), its stack and buffers in PSRAM | 3.5 KB (against Tiger Studio's mock) | **5** |
| **OctoPrint** | HTTP poller, its stack and buffers in PSRAM | under the noise (against Studio's mock) | **5** |
| **First Anycubic in cloud mode** | MQTT over mutual TLS, opens the account's session | 18.1 KB (Kobra X) | **19** |
| **Each further Anycubic in cloud mode** | a subscription on that session | *not measured* | **4** |
| **Anycubic in LAN mode** | MQTT over TLS | *not measured* | **21** |

**Assumptions left**: a Prusa, a LAN Anycubic and a second cloud Anycubic
have not been on the bench, and Klipper and OctoPrint only against Studio's
mocks; each is charged as the measured connection it resembles. The first
bench with a real one switched on replaces it.

A brand missing from the table costs 0 and is never refused: a new driver is
not finished until it is measured and listed.

### Per connection, not per printer

Every Bambu Lab in cloud mode on one account talks to the same regional broker
with the same credentials, so they share **one** MQTT session
(`drv_bambu.c`, "One cloud session"): the first pays for it, each further one
is a subscription on it. Before it, a second cloud Bambu cost 18.2 KB of its
own. The budget counts the session once, for the first cloud printer in the
account's order. Anycubic in cloud mode is the same (`drv_anycubic.c`, "Cloud
mode"), and there it is not only cheaper but required: the broker accepts one
session per account and client type, so a second one would take the first's
place.

### Worked example: the bench account

Ten printers (2026-10-15): two Bambu Lab in LAN mode, and a Klipper and an
OctoPrint printer served by Tiger Studio's mocks on a computer.

| Printer | Load slots |
|---|---|
| Creality Ender-3 V4 | 15 |
| FlashForge AD5X | 5 |
| Bambu X1C (LAN) | 21 |
| Bambu A1 (LAN) | 21 |
| Bambu P1P (cloud, the only one: it opens the session) | 21 |
| Snapmaker U1 | 15 |
| Elegoo Centauri Carbon 2 | 16 |
| Anycubic Kobra X (cloud) | 19 |
| Klipper (mock) | 5 |
| OctoPrint (mock) | 5 |
| **Total** | **143 of 144 - and ten links, the most there can be** |

Measured with the device next to the router:

| | Free internal RAM | Largest block |
|---|---|---|
| Nothing open | 203.5 KB | 43 KB |
| All ten open, at rest | 88 KB | 31 KB |
| Lowest since start-up, the ten connections opening | 44.8 KB | |
| Eight open (before the list moved to PSRAM), at rest | 68 KB | 31 KB |
| ... while the account is read again (TLS to Firestore) | 58 KB at the lowest | 31 KB |
| ... while an update downloads and is written | 49 KB at the lowest | 31 KB |

Never under the 32 KB floor. The update was downloaded over plain HTTP from a
bench server; a release comes over HTTPS, whose TLS session the account read
measures on its own.

---

## What the user sees

*Settings > Printers*, and the printers page at the end of the first start: a
bar labelled **Load** with a **percentage**, and each printer's own share beside
its switch. Load slots are how the device counts; a person needs to know how
full it is.

- Green while there is room, **orange from 85%**.
- A switch that would overflow the budget or the link count **does not move**;
  the bar turns **red** and the line under it reads **"Not enough room"** for
  two and a half seconds.
- Switching a printer **off** is always allowed.

The serial log says `a printer was refused: <total> of 144 load slots, <n> of
10 links` - counts, never a printer's name (`scripts/check-log-privacy.py`).

---

## Where the internal RAM went, and how it was won back

Before 2.0 the device had **95 KB** free before opening anything, and five
printers left it at **18 KB**, under the floor, with no block over 9 KB: the
next TLS request (an update, a token renewal) failed. Measured, then moved:

| Change | Free internal RAM, nothing open |
|---|---|
| 1.x | 95 KB |
| LVGL's two render buffers (2 x 37 KB) in PSRAM instead of internal RAM (`CONFIG_PT_LVGL_RENDER_PARTIAL_2_PSRAM`) | 164 KB |
| Every allocation from 4 KB up to PSRAM first (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, was 16 KB); Wi-Fi and lwIP code left in flash rather than copied to IRAM, which shares the pool; MQTT's outbox in PSRAM | 185 KB |
| The account's printer list in PSRAM (`EXT_RAM_BSS_ATTR`, `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`) | **203.5 KB** |

And per printer:

| Change | Effect |
|---|---|
| One MQTT session for every cloud Bambu | a further cloud Bambu 18.2 -> 1.7 to 3.3 KB |
| The 4 KB allocation threshold | FlashForge 10.7 -> 1.8 KB (its HTTP buffers) |
| WebSocket task stacks 6 -> 4 KB (at worst 1.6 KB of the 4 left unused, all printers open) | 2 KB less per Creality, Snapmaker, Elegoo CC1; within the pass-to-pass noise, so their prices did not move |

What stays in internal RAM on purpose: the task stacks of the MQTT and WebSocket
clients (their components create them there), the socket buffers, the RGB
panel's DMA bounce buffer, and the Wi-Fi driver's static receive buffers.

## The two cores

| Core | Runs |
|---|---|
| 1 | the screen: the LVGL task (stack in PSRAM), and nothing that waits on the network |
| 0 | the network: Wi-Fi, the TCP/IP task (pinned, `CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0`), the application task, the feed task, every driver's client (MQTT `CONFIG_MQTT_USE_CORE_0`, WebSocket `task_core_id = 0`), the update watch |

Before, the WebSocket clients ran unpinned and were found on core 1, beside the
screen, and the TCP/IP task could run on either: a burst of printer traffic
could take the core that draws. With every printer of the bench account open,
`/api/tasks` now finds `lvgl` alone on core 1 (with the bench's own tasks).

---

## How the numbers were measured

A bench build (never a release; `local/bench/`, see `CLAUDE.md`) has
`/api/memtest`, after TigerSpool's: it closes every link, then opens the
account's printers **one at a time**, and reads the internal RAM after each
has been up for ten settled seconds (the lowest of three reads a second apart).
`/api/tasks` lists every task with its core, where its stack is and how much of
it was never used.

    http://<device>/api/memtest?all=1    every printer of the account
    http://<device>/api/memtest/result   the report
    http://<device>/api/tasks            stacks and cores

A printer that was **offline** during a pass is not a measurement of its
connection: an Elegoo that was off in one pass, and the Anycubic before its
cloud session existed, are left out of the table.

| Pass | Date | Build | What it gave |
|---|---|---|---|
| 1 | 2026-10-14 | 1.x memory layout | 95 KB free at rest; the five reachable printers left 18 KB |
| 2 | 2026-10-14 | render buffers in PSRAM, shared cloud session | 164 KB free at rest (the printers could not be read that pass) |
| 3 | 2026-10-14 | + allocation threshold, IRAM, MQTT on core 0 | 185 KB at rest; first per-printer figures |
| 4, 5 | 2026-10-14 | + WebSocket stacks 4 KB on core 0 | the worst figures above; stack headroom and cores under load |
| 6 | 2026-10-14 | + Anycubic cloud session | 17.9 KB for the Kobra X; all seven printers up, 103 KB free |
| 7 | 2026-10-14 | two Bambu Lab switched to LAN mode, measured alone | 17.6 and 17.8 KB: as much as a cloud session, each |
| 8 | 2026-10-15 | eight printers, the device next to the router (-48 dBm) | all eight open at once: 114 KB taken, 67.5 KB free, largest block 31 KB |
| 9, 10 | 2026-10-15 | + printer list in PSRAM; Klipper and OctoPrint against Studio's mocks | 203.5 KB at rest; all ten open, 88 KB free |

---

## When to revisit

- **A new brand**: measure it with `/api/memtest` and add its worst case here
  and in `links.c`. Do not estimate.
- **A Prusa, a LAN Anycubic or a second cloud Anycubic on the bench**:
  replace its assumed price.
- **ESP-IDF is updated**, or a memory option above changes: run the passes again.
  Every number here depends on where things are allocated.
