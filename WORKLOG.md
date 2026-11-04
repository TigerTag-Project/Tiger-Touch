# Worklog

Everything done since the last release, noted the moment it is done. Describe
the end state, not the journey; fold fixes into the entry they fix; let reverts
disappear. `CHANGELOG.md`'s `[Unreleased]` section is written from it, and it is
reset to this header at each release.

## Fixed
- An AFC lane that a Klipper printer publishes twice (`AFC_stepper <lane>` and `AFC_lane <lane>`, as a CANVAS on an OpenCentauri CC1 does) is one slot, not two.
