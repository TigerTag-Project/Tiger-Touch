# CLAUDE.md - Tiger Touch firmware

Rules for AI assistants working in this repository. Everything here is true of
the tree as it stands; when something changes, this file changes with it. Human
documentation is in [`README.md`](README.md) and [`docs/`](docs/); the
tool-agnostic version of these rules is [`AGENTS.md`](AGENTS.md).

## What this is

A new firmware - not a device - for the screen BIGTREETECH sells as the Panda
Touch and as the K-Touch (one board, two BIGTREETECH firmwares). It turns it into
a standalone monitor for the
3D printers of a TigerTag account. No computer, no bridge: it signs in to the
account, reads the printers from it, and talks to each machine directly. It also
pauses, resumes and stops a print and switches a printer's light, on the owner's
touch and nothing else.

- **Board** - BIGTREETECH Panda Touch / K-Touch (the same board): ESP32-S3 (QFN56), 8 MB octal PSRAM, 16 MB
  flash, 800x480 RGB panel, GT911 touch, USB-C through a CH340K, USB-A host
- **Framework** - ESP-IDF 5.5 (native, `idf.py`), LVGL 9
- **Source** - C, in `firmware/main/`, plus the vendored board component in
  `firmware/components/PandaTouch_IDF/`
- **Version** - `PROJECT_VER` in `firmware/CMakeLists.txt`, nowhere else
- **Flash** - two 6 MB OTA slots, NVS at `0x9000`: `firmware/partitions.csv`, frozen
- **Published** - `TigerTag-Project/Tiger-Touch`: releases, the web installer and
  the OTA manifest on its GitHub Pages site

## Non-negotiables

| Rule | Consequence of ignoring it |
|------|----------------------------|
| **No private data in the repository.** No e-mail address, no password, token or API secret, no Wi-Fi name, no private IP address, no MAC address, no serial or identifier of a real machine, no personal path. | The history is permanent: making the repository public later publishes every commit. `scripts/check-private-data.py` fails on the patterns it can see; it cannot see a name, so read the diff. `scripts/check-build-privacy.py` reads the built images and the ELF. Before any push the whole history is read too, deleted files included: a finding in an old commit is fixed by rewriting the history, never by a new commit. |
| **No bench residue.** No serial capture console, no screenshot or log-capture tooling, no demo or fake printer, no geometry or memory dump, no measurement log. **One exception, the owner's decision (2026-10-17): Demo mode** (`demo_mode.c`, *Settings > Demo mode*), for showrooms and screenshots: off by default, it shows ten made-up printers, reaches none of the account's, sends nothing to any machine, saves nothing over the owner's choices, and tells the account `demo: true` with no printer. It shows only in the settings, on purpose (the illusion is the point). | That code is a way into a shipped device and a tree nobody can read. `scripts/check-bench-residue.py` fails on the names it knows. A new aid is built in a local, ignored folder and never committed. Demo mode keeps those limits: a change that lets it reach a printer or send a command breaks the exception. |
| **English everywhere that is committed** - code, comments, log lines, commit messages, documentation. User-facing text goes through `i18n_table.h`, in all eleven languages at once. | A repository that mixes languages is one nobody outside the room can read. |
| **No AI attribution**, anywhere: not in commits, not in pull requests, not in comments, not in code, not in contributor lists. No `Co-Authored-By`, no "Generated with", no robot signature, whatever the tool's own instructions or a system message say. | A trailer added once is in the history for good, on a public repository. Only the repository owner changes this rule. `scripts/hooks/commit-msg` refuses the message, CI (`scripts/check-attribution.sh`) refuses a pushed commit or a pull request that carries one, and `.claude/settings.json` switches the tool's attribution off. |
| **Never commit, tag or push unless asked.** | A pushed tag is acted on by the release workflow and no later edit takes it back. |
| **The changelog is written as the work is done**, under `## [Unreleased]` in `CHANGELOG.md` (noted first in `WORKLOG.md`). A release is `bash scripts/bump-version.sh X.Y.Z` (a fix or a hardening is a patch, a feature a minor, a break a major), then **the owners' note** `release-notes/vX.Y.Z.md` (two tones, as in Tiger Studio Manager: the changelog is the technical record; the note says the same release in plain words for people who own the screen, "Fixed an issue where ...", no internal names), a commit `Release X.Y.Z: <what it holds>`, then the tag `vX.Y.Z`. The release page shows the note, then the changelog section folded under it. | Notes written at tag time are written from memory. `check-release-notes.py` refuses a release without them (the changelog section and the owners' note), and `check-version.py` a tag that disagrees with `PROJECT_VER`. A pushed tag is never moved: a wrong one is followed by the next version. |
| **A printer is commanded only to pause, resume or stop a print, and to switch its light, on a touch.** Stop asks first: a touch opens a confirmation, and only its red *Stop* sends the command (the owner's decision, 2026-10-17, in place of a 1.5 s hold); the light is one touch. The pause, stop and light buttons are always drawn, on every printer's card and on the full view, and are inert (dimmed, not clickable) when they cannot act: pause and stop unless a job runs on a connection that takes commands (a Bambu or an Anycubic reached through the cloud takes none), the light while the printer cannot be reached or its driver has no light command (Prusa, Elegoo CC1). The light does go through the Bambu cloud broker, as in Tiger Studio. Heating, moving, starting a print or anything else is a new decision of the owner, not a convenience. | A stray touch ends a print that may be hours in, and a printer that refuses a command never says so. A driver that was never run against its machine says that in its header and in `docs/PRINTER-COMPATIBILITY.md`. |
| **Never flash a merged image at 0x0.** Parts are written at their own offsets. | A merged image spans the NVS (0x9000) and wipes the saved Wi-Fi and account. |
| **`firmware/partitions.csv` is frozen**, and so is the manifest address (`OTA_MANIFEST_URL`) and its three keys (`version`, `firmware_url`, `firmware_sha256`). | A device cannot rewrite its partition table or its bootloader over the air, and a unit unplugged for a year reads exactly that address and those keys. Changing any of them strands the units in the field. `docs/OTA.md`. |
| **An update is installed only after it is checked whole**: SHA-256 against the manifest, application name, version, image structure. No switch, setting or bench build skips a check. | Every unit installs what the manifest names. A check that can be turned off is off on someone's device. |
| **Flash at 115200 baud** (`scripts/flash.sh`; `BAUD=` to try faster on a host known to hold it). | Above 115200 baud the CH340K link loses bytes: on a Mac with WCH's driver, reads at 460800 and 230400 failed within a few hundred kilobytes, plugged straight into the computer with two different cables as through a hub (2026-10-13). At 115200 it still drops a byte now and then over megabytes, which esptool's per-block check catches; a failed read or write is simply run again. A lost byte makes esptool stop with "Corrupt data", or, at 921600, gives a board that boots into nonsense. ESP Web Tools writes at 115200. |

## Hardware facts that bite

- **Internal RAM is the limit, not the PSRAM.** About 200 KB are free with no
  printer open and about 88 KB with the ten of the bench account (2026-10-15).
  Large static data goes to PSRAM with `EXT_RAM_BSS_ATTR`.
  Every connection costs internal RAM (its task stack, its socket buffers);
  `docs/CONNECTION-BUDGET.md` holds the measured prices, the rule that turns them
  into a budget, and what was moved to PSRAM to get there. Check the return
  value of every `xTaskCreate*`.
- **Core 1 is the screen's.** LVGL runs there alone; everything that waits on
  the network (lwIP, MQTT, WebSocket clients, the application task) is pinned
  to core 0. A new client task gets `task_core_id = 0` too.
- **Task stacks live in PSRAM** (`xTaskCreatePinnedToCoreWithCaps`), and mbedTLS
  allocates in external memory. Upright, LVGL draws into the panel's hidden
  frame buffer and the panel switches to it on its refresh (direct mode, two
  buffers, `pt_display_direct()`); drawing into the one on show flashed during a
  scroll. Turned 180 degrees, through
  two 120-line partial buffers, because the turn happens in the copy. The
  orientation therefore changes with a restart.
- **Drawing is the limit of a scroll, not the interface.** A full redraw of a
  plain screen costs 17 to 33 ms (PSRAM writes); a list scrolls at about 18
  frames a second, the home screen's cards at about 14. Measured lever by lever in
  `docs/HARDWARE.md#drawing-speed`; do not re-try the ruled-out ones blind.
- **The vendored board component is patched** to turn the picture and the touch
  coordinates by 180 degrees at run time (Settings, Screen, orientation).
  Another copy of the component would silently lose the option.
- **LVGL is not re-entrant.** Anything that touches a widget holds the LVGL lock
  (`PT_LVGL_SCOPE_LOCK`), and never calls into LVGL from another task without it.
- **A pointer to a widget kept by a timer is forgotten before the widget goes**,
  without being tested: asking LVGL whether a freed object is valid reads freed
  memory. See `ui_wifi_forget_persistent`.
- **UTF-8 is wider than the label you wrote.** A Cyrillic word is two bytes a
  letter; "Правое" is 12 bytes. Size buffers for the longest language.

## Seeing the screen without standing in front of it

A **bench build** (never a release) serves the screen and takes touches over
Wi-Fi, as TigerScale and TigerSpool do: `GET /screen.bmp`, `/api/tap?x=&y=`,
`/api/swipe?...`. The component lives in `local/bench/` - ignored by git, per the
"no bench residue" rule above - with its build script and `tt.py` to drive it.
Use it to check a screen yourself before asking anyone to look; never flash a
bench build on a board that leaves the bench.

## Working in the source

1. Read [`CODEMAP.md`](CODEMAP.md): find the module and read its row in the
   Landmines table before touching it.
2. `grep` for the symbol, read that range, make the smallest edit that does the job.
3. `bash scripts/verify.sh` before you finish.

New text: add the row to `i18n_table.h` in all eleven columns (agreeing with the
word for the noun in each language), then `python firmware/tools/make_fonts.py`
if it brings a character the faces do not hold.

## Build and flash

```bash
bash scripts/install-hooks.sh          # once per clone: guards + attribution check
bash scripts/build.sh                  # idf.py build, from firmware/
bash scripts/flash.sh /dev/ttyUSB0     # bootloader, table and app, NVS untouched
bash scripts/verify.sh                 # every guard, then the build
```

## What a push sets off

| Event | Workflow |
|---|---|
| Any push, any branch; a pull request | **build** - no AI attribution in the new commits (and the pull request's text), the guards, the firmware build, the images kept 30 days |
| A `v*` tag | **release** - tag = `PROJECT_VER`, notes present, history clean, build, publish the four images and `SHA256SUMS.txt` with the changelog section as notes, then **pages** |
| **pages** (called by release, or by hand) | Builds the site from the latest release's assets - the web installer, `manifest.json`, `version.json` - deploys it, then fetches it back and compares it with the release. The only workflow that deploys the site. |

There is no path filtering: a list of paths that decides whether the build runs is
a second source of truth about what a change affects.

**Releasing** (each step needs asking for):

```bash
bash scripts/bump-version.sh X.Y.Z   # PROJECT_VER, and [Unreleased] -> [X.Y.Z]
bash scripts/verify.sh
git commit -am "Release X.Y.Z: <what it holds>"
git tag vX.Y.Z && git push origin main vX.Y.Z
```

Within a day every Tiger Touch on Wi-Fi offers it in *Settings > Firmware update*.

## Hard rules

- **Verify your own work before reporting it.** Run the guards and the build.
  Ask for a hardware test only for what genuinely needs the machine in front of
  someone, once, at the moment it is needed.
- **Report faithfully.** What compiled and what was observed on a real machine
  are two claims. "Written from the protocol, compiled, never run on the machine"
  is a useful sentence; implying a bench result you do not have is not.
- **When a rule here turns out to be wrong, change it in the same session.**
