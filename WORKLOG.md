# Worklog

Everything done since the last release, noted the moment it is done. Describe
the end state, not the journey; fold fixes into the entry they fix; let reverts
disappear. `CHANGELOG.md`'s `[Unreleased]` section is written from it, and it is
reset to this header at each release.

### Changed

- The share pictures show the photo-real Panda Touch (`assets/mockups/angle/`) instead of the line drawing: the home card with the printer cards, each brand's card with one of its printers (`scripts/make-og-images.py`, `assets/og/*.jpg`).
- The French speaks like Tiger Studio Manager: tutoiement and its lighter, offbeat voice on the screen (19 strings, none longer than before; `firmware/main/i18n_table.h`), the installer site (40 keys; `installer/i18n.js`) and the nine brand pages (`installer/brand-i18n/fr.json`). Every fact, model, port and limitation is unchanged.

### Added

- The print's picture on the full view (`net/thumb.c`, `thumb_fetch` in `printer_driver.h`): one service fetches, reads the PNG line by line with the ROM's tinfl, shrinks it to an 88 px RGB565 square in PSRAM and the dashboard shows it right of the times; fetched when the print starts, kept for the same file, waits while the internal RAM is under 50 KB. The one source so far is a Bambu Lab in cloud mode (its task list, `limit=5`, the Tiger Touch's own client name).
- The picture for a Creality K2 (`creality_thumb_fetch` in `drivers.c`): `http://<ip>/downloads/humbnail/<file without .gcode>.png` on its web server, port 80, the name taken from Tiger Studio Manager's protocol notes. Not tried on a K2.
- The picture for a Klipper and a Snapmaker (`drivers/moonraker_thumb.c`, one `thumb_fetch` for both): `print_stats.filename`, `/server/files/metadata`, the smallest thumbnail at least 88 px, `/server/files/gcodes/<folder>/<relative_path>`; plain HTTP with the `X-Api-Key` when the account has one. The JSON text readers moved from `drv_bambu.c` to `net/json_scan.c`. Not tried on a machine.
