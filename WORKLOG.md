# Worklog

Everything done since the last release, noted the moment it is done. Describe
the end state, not the journey; fold fixes into the entry they fix; let reverts
disappear. `CHANGELOG.md`'s `[Unreleased]` section is written from it, and it is
reset to this header at each release.

### Changed

- The share pictures show the photo-real Panda Touch (`assets/mockups/angle/`) instead of the line drawing: the home card with the printer cards, each brand's card with one of its printers (`scripts/make-og-images.py`, `assets/og/*.jpg`).
- The French speaks like Tiger Studio Manager: tutoiement and its lighter, offbeat voice on the screen (19 strings, none longer than before; `firmware/main/i18n_table.h`), the installer site (40 keys; `installer/i18n.js`) and the nine brand pages (`installer/brand-i18n/fr.json`). Every fact, model, port and limitation is unchanged.
