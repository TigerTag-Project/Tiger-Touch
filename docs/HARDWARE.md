# Hardware

The screen BIGTREETECH sells as the **Panda Touch** and as the **K-Touch** - one
board, two BIGTREETECH firmwares - as far as Tiger Touch uses it. Everything here
was seen on a Panda Touch.

| | |
|---|---|
| MCU | ESP32-S3 (QFN56), dual core, Wi-Fi 2.4 GHz, 240 MHz |
| Memory | 8 MB octal PSRAM, 16 MB flash |
| Display | 5 inch IPS, 800x480, RGB565, 16-bit parallel, DE mode |
| Touch | GT911 capacitive controller on I2C0 |
| USB-C | power, flashing and serial debug, through a CH340K bridge |
| USB-A | the ESP32-S3's native USB OTG (GPIO19/20), host; not used by this firmware |
| Expansion | one I2C header (I2C1) |
| Audio | **none**: no speaker, no buzzer, no codec |

## Pins used

| Function | GPIO |
|---|---|
| LCD pixel clock, data enable, reset | 5, 38, 46 |
| LCD data | 6 to 18, 39, 47, 48 |
| Backlight (PWM) | 21 |
| Touch SCL, SDA, reset, interrupt | 1, 2, 41, 40 |
| USB-A data | 19, 20 |
| Debug UART | 43, 44 |
| I2C1 header | 3, 4 |

## Power: the switch, the dock and the battery

The back of the screen has a three-position switch. BIGTREETECH's wiki
([Panda Touch](https://global.bttwiki.com/PandaTouch.html),
[hardware](https://neo.bttwiki.com/en/docs/panda-series/panda-touch/panda-touch-hardware)):

| Position | What it does |
|---|---|
| **OFF** | The battery charges; the screen stays off. |
| **Battery** | USB power runs the screen and charges the battery; off the dock, the battery runs it for up to about 30 minutes. |
| **DC 5V** | The screen runs straight from the USB input of its **magnetic dock**, and the battery keeps charging; lifted off the dock, it switches off without draining the battery. |

There is nothing for the firmware to switch on: Tiger Touch starts whenever the
board is powered (every start on the bench is a plain power-on reset). If the
screen stays dark in **DC 5V**, the board is not getting enough power in that
position - its USB serial chip does not even show on a computer, which no
firmware can cause. On the bench (2026-10-15) a USB port that could not supply
it left the screen dark in DC 5V while Battery worked; the same screen started
in DC 5V from another USB port. **In DC 5V, power the dock from a port or
charger that delivers enough current.**

**The battery cannot be read.** BIGTREETECH states there is no battery level
indicator, and the board offers no way to measure it:

- every pin of the ESP32-S3 that can read a voltage (GPIO1 to GPIO20) is taken
  by the panel, the touch, the I2C header and the USB-A port;
- the two I2C buses hold nothing else: a scan on the bench found only the GT911
  touch controller (0x5D on I2C0) and nothing on the expansion header (I2C1);
- the three free pins (GPIO0, GPIO42, GPIO45) are each held high by the board,
  and none of them changed while the screen was taken off its dock and put back
  on it, twice, on the bench (2026-10-15): no charger status reaches the
  processor either;
- BIGTREETECH publishes no schematic of the power circuit, and its own firmware
  does not handle the battery either: neither v1.0.7.3 nor v1.0.8.0 holds a
  battery or charging string in any language (its "Voltage" is the Panda PWR
  smart plug's reading, its "system voltage" message one of the Bambu Lab
  printer's error codes).

Reading the battery would take an addition: a fuel-gauge module (a MAX17048,
for instance) on the I2C connector at the back, wired to the battery inside.

So the device cannot tell whether it runs on the dock or on the battery, nor how
much charge is left. What it can say is whether a battery is fitted, because the
owner tells it: *Settings > Battery*, yes by default (both
screens ship with one), written to the account as `battery_present`. The only sign BIGTREETECH documents is a flickering screen
when the battery is nearly empty.

## Drawing speed

What a scroll costs, measured on the board with every printer connected
(2026-10-16; a bench build times each screen refresh during an 800 ms swipe):

| Configuration | Plain screen, full redraw | Home (cards, horizontal) | Settings list (vertical) |
|---|---|---|---|
| 16-line partial buffers in PSRAM (until then) | - | 155 ms, ~6 fps | ~65 ms |
| 120-line partial buffers in PSRAM | 37 ms | 100 ms, ~10 fps | 60 ms |
| Direct, one panel frame buffer | 17 ms | 55 ms, ~16 fps | 41 ms, ~20 fps |
| Full frames, two panel frame buffers (swap on vsync) | 18 ms | 85 ms | 60 ms |
| Direct, two panel frame buffers, swap on vsync | 33 ms | 68-83 ms, ~14 fps | 52 ms, ~18 fps |
| **Direct, two buffers, swap when the panel leaves one (kept)** | - | **80-84 ms, ~13 fps** | - |

The home's list view (rows, scrolled up and down) measures 74-77 ms, ~14 fps,
in the kept configuration: hardly lighter than the cards, because what a frame
costs is mostly the area that moves, not what is drawn on it.

The swap does not wait for the vsync: with bounce buffers the panel copies the
frame buffer ahead and takes the new one when that copy starts a frame, a few
lines before the vsync. A swap given in that gap is taken a frame late, and
waiting for the vsync let LVGL draw into the picture still being sent - a brief
flash now and then. The driver's `on_frame_buf_complete` says when a buffer is
really left; the cache is no longer written back each frame either, since the
copy into the bounce buffers reads through it.

One buffer was the fastest, but a scroll **flashed**: LVGL draws into the
picture on show, and the panel showed it half redrawn - background already
repainted, content not yet. Two buffers, the panel switching to the new one
at its next refresh, never show an unfinished picture; LVGL copies what
changed into the other buffer to keep the two in step, which is the price.

Tried and set aside, each measured:

- **Partial buffers in internal RAM**: 25 % faster than in PSRAM, at 51 KB of
  internal RAM, the room of three printers.
- **LVGL's hot functions in IRAM** (`CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM`):
  no gain on the home screen, 5 % on a list, and 40 KB of internal RAM gone -
  the device fell to 15 KB free.
- **Two drawing threads** (`LV_USE_OS` FreeRTOS, two draw units): 8 %.
- **Code and constants in flash** instead of PSRAM: impossible here - task
  stacks live in PSRAM, which a flash write then cannot reach (the board
  restarts in a loop).
- **A slower panel clock** (16 MHz instead of 23, less PSRAM bandwidth taken by
  the panel): 5 %.
- **The cards as still images during a scroll** (snapshots): nothing - copying
  an image costs what drawing it does.
- **Espressif's SIMD blending** (`esp_lvgl_port`): built for LVGL 9.1 only;
  this firmware uses 9.6.
- **The cards' rings** cost nothing measurable; the direction of a scroll
  matters only through what it moves.

The floor is the PSRAM: every frame of a scroll rewrites the whole moving area
there, about 30 MB/s, and the panel reads it at the same time. What is drawn
adds to it - the home screen's cards about 15 ms more than a list's rows.

Turned 180 degrees, the screen keeps the 120-line buffers (the turn is done in
the copy) and changes orientation with a restart.

## Things that bite

- The USB-C port is **not** the S3's native USB: it is a WCH **CH340K**
  (`1a86:7522`). Above 115200 baud the CH340K link loses bytes: on a Mac with WCH's driver, reads at 460800 and 230400 failed within a few hundred kilobytes, plugged straight into the computer with two different cables as through a hub (2026-10-13). At 115200 it still drops a byte now and then over megabytes, which esptool's per-block check catches; a failed read or write is simply run again. Flash at 115200.
- **macOS has no driver for the CH340K**: its built-in driver knows the CH340
  (`1a86:7523`) only, so the board is on the bus with no port. WCH's
  `CH34xVCPDriver` adds it (`/dev/cu.wchusbserial…`); `docs/INSTALL.md`.
- 16 MB of flash: the bootloader, the NVS at 0x9000, the OTA data, and two 6 MB
  application slots (`docs/OTA.md`). 4 MB are kept free.
- Internal RAM is the scarce resource, not PSRAM: `docs/CONNECTION-BUDGET.md`.
- The reset line is driven by the bridge's RTS; opening the serial port can reset
  the board unless DTR and RTS are held low before the port is opened.
- Settings, Screen offers 0 and 180 degrees. The rotation is done in the patched
  board component (the picture at flush, the touch coordinates on read), so the
  screens above it never know.
