# Worklog

Everything done since the last release, noted the moment it is done. Describe
the end state, not the journey; fold fixes into the entry they fix; let reverts
disappear. `CHANGELOG.md`'s `[Unreleased]` section is written from it, and it is
reset to this header at each release.

## Fixed
- The light button of an Anycubic Kobra S1 asks for its own chamber lamp (type 2, which its `light` report names over the LAN and which a Kobra S1 Combo of the account is given in the cloud), not the Kobra X's (3), which it refused as its camera light.
