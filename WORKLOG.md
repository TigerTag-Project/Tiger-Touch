# Worklog

Everything done since the last release, noted the moment it is done. Describe
the end state, not the journey; fold fixes into the entry they fix; let reverts
disappear. `CHANGELOG.md`'s `[Unreleased]` section is written from it, and it is
reset to this header at each release.

## Added
- Time zone setting: Settings > Time zone, one list (Automatic + 63 lines, TZ_PICKS), tz_set_manual, 4 keys in 11 languages, the offset in force and summer / winter time (tz_pick_offset_min, tz_pick_in_summer), tz_fill recycled rows; make_tz_table.py writes the lines.
