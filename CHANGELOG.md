# Changelog

All notable changes to the Tiger Touch firmware. The version lives in one place,
`PROJECT_VER` in [`firmware/CMakeLists.txt`](firmware/CMakeLists.txt); the boot
log, the Settings menu, the release and the OTA manifest all follow it.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versions:
[SemVer](https://semver.org/spec/v2.0.0.html) - a fix or a hardening is a patch,
a feature a minor, a break a major.

Entries are written under `[Unreleased]` as the work is done.
`scripts/bump-version.sh X.Y.Z` moves them under the release's heading; a `vX.Y.Z`
tag then builds and publishes it (`.github/workflows/release.yml`). Up to 1.3.2
every commit was a version and nothing was published; those entries are kept as
they were written.

## [Unreleased]

### Added
- **The time on the home screen**: hours and minutes, local time, in the middle
  of its top line; "--:--" until the clock is set.
- **Settings > Demo mode**, for a showroom, a fair or screenshots: the screen
  shows ten made-up printers of every brand, printing - progress that moves,
  temperatures that wobble, prints that finish and start again, filament colours
  drawn from Tiger Studio's palette anew at each start - and looks exactly as
  it does with real printers. Off by default, kept until switched off (the
  switch restarts the screen). While it is on, none of the account's printers
  is reached, nothing is sent to any machine, the owner's choices are not
  overwritten, and the account is told `demo: true` with no printer.
- **Settings > Battery**, a page of its own as on TigerSpool: a *Built-in
  battery* switch, on by default since the Panda Touch and the K-Touch ship with
  one inside, the battery drawn (lit when declared), and why the charge cannot be
  read: the owner's declaration is
  written to the account as `battery_present`, as TigerSpool does. The charge
  and whether it charges stay `null`: this board gives the firmware no way to
  read them.
- **A call to join on the installer page**, for people who are not installing
  today: "You dreamed of it. The TigerSquad built it." and a *Join us now*
  button to tigersystem.io, in the eleven languages.

### Changed
- **Settings shows the account's name, not its e-mail**, as TigerScale V3 does:
  the `displayName` of `users/{uid}`, read with the country the device already
  reads, kept across restarts, forgotten when another account signs in. The
  e-mail shows only when the account has no name.
- **One header on every screen**: 64 px high, no rule under it, and the back
  arrow, the title, the clock, the Wi-Fi and account icons and the buttons all
  centred on the same line (`HEADER_H`, `HEADER_MID`, `ui_kit_on_header()`), on
  the home screen, the settings and their pages, the sign-in pages and a
  printer's full view. Nothing moves from one screen to the next.
- **A print's colour says its state**, the same on the ring, the card and the
  list bar: blue while it prints, orange paused, green finished, red on a
  fault. The dot beside the name still says the connection.
- **Stop asks before it acts**: a touch opens a confirmation ("Stop this
  print? It cannot be resumed."), and only its red *Stop* sends the command;
  *Cancel* or a touch outside closes it. It replaces the 1.5 s hold.
- **The clock is also in the Settings header and in a printer's full view**,
  centred, as on the home screen.
- **The README shows the screen at work**: its top picture turns, in BIGTREETECH's
  45-degree drawing, from the start-up screen to the cards, the list and a
  printer's full view; the gallery shows printers printing (their progress was
  filled in by a bench build, which the caption says). The installer's picture
  turns to the cards printing.
- **The settings row reads *Account*** instead of *Change account*.
- **The installer page uses the width of the screen**: on a computer, the
  pitch and the install steps beside a large picture of the screen, then
  "Before you start" in two columns and the comparison across the page; on a
  tablet one wide column with the screen first; on a phone everything stacked.
  The printer brands are drawn in the text's colour (white on dark, dark on
  light) instead of grey, there and in the README.
- **The installer page opens on the name**: "Tiger Touch" in large type at
  the top, the promise under it, on a computer, a tablet or a phone.
- **The printer brands are shown above the screen**, in the README and on the
  installer page: one strip with Bambu Lab, Creality, Elegoo, Anycubic,
  Snapmaker, FlashForge, Prusa, Klipper and OctoPrint.
- **The README presents the TigerPOD Mini**, the recommended desk reader, with
  its picture and its free MakerWorld file, in place of the original TigerPOD.
- **The TigerPOD Mini's picture is the new one** (the blue stand holding a red
  spool), at the size of the picture it replaces, 420 x 622, so the table does not
  move.
- **The installer page says where to start and where to go**: a "Before you
  start" box with the TigerSystem picture (printers are added with Tiger Studio
  Manager's desktop app; its web version cannot add them), links to the source on GitHub, the TigerSystem wiki, the
  release notes and tigersystem.io, a comparison with the Panda Touch's and the
  K-Touch's own firmwares, the donation links, and a light or dark theme to
  choose (following the system until one is picked). The picture of the screen
  turns from the start-up screen to the printers when the pointer is on it.

### Fixed
- **The title of an AMS HT ran into the edge of its card** on a printer's full
  view (seen on an H2C). The small cards of the external spool and of an AMS HT
  now centre their title over the dot, in a fixed width, with dots if a word were
  ever too long; the boxes keep their title where it was.
- **Demo mode: the estimated end no longer moves.** The time left ran down six
  times faster than the clock, so the end jumped back every 20 s and crept
  forward in between; it now runs at the progress's pace and the end stays put.
  The demo's Ender-3 S1 prints too, instead of sitting idle.

## [2.0.0] - 2026-10-16


### Added
- **The device says it is there, in the account**, as TigerScale and TigerSpool
  do: `users/{uid}/tigertouches/{mac}` with its version, its name, its Wi-Fi,
  whether its screen sleeps and the printers it follows, every 30 s (5 min while
  the screen sleeps), with TigerSpool's field names
  ([docs/ACCOUNT-PAIRING.md](docs/ACCOUNT-PAIRING.md#the-devices-presence)).
  Written and run on a Panda Touch: until the account's rules allow the
  collection, Firestore refuses it (HTTP 403) and nothing else changes.
- **The home screen has a second view, a list**: a button left of the Wi-Fi
  icon switches between the cards and a vertical list of light rows (the
  printer, its state, a progress bar, the nozzle and the bed, and its light,
  pause and stop buttons), in the settings' scrolling column: the scrollbar
  always shown and the up / down rail on the right. The choice is kept across
  restarts.
- **The settings' Screen row has TigerScale's icon**, FontAwesome's sun, drawn
  at 32 px like the row's other icons (a face of its own, `font_icon_32.c`,
  made by `tools/make_fonts.py`). The Language row's globe was TigerScale's
  already.
- **The first start is TigerScale's setup**, step for step
  ([docs/ONBOARDING.md](docs/ONBOARDING.md)): the language first, then
  "Connect to Wi-Fi?" and "Link a TigerTag account?", each with *Set up / Later*.
  What was put off comes back as a panel sliding in from the right on the home
  screen, 10 seconds after it appears and then every 30 minutes.
- **Sign in with Google.** One sign-in page with e-mail, password, *Sign in* and
  *Continue with Google*. Google shows a QR code and its code; the owner scans
  it with the phone and approves, and the device is linked - the same pairing
  as TigerScale and TigerSpool ([docs/ACCOUNT-PAIRING.md](docs/ACCOUNT-PAIRING.md)).
  Accounts created with Google could not sign in at all before. Seen working
  on a Panda Touch.
- **The language follows the account both ways**: picked on the device, it is
  written to the account; changed in the account, the device follows at its
  next start.
- **"Account linked" and the printers are two pages**, as on TigerSpool: the
  confirmation alone, then the printers to switch on.
- **Over-the-air update.** Settings > Firmware update shows the installed
  version, checks for a newer one and installs it with a progress bar; the
  device restarts into it. A check also runs a minute after start-up and then
  once a day, and the menu row shows the new version in orange when there is
  one. Nothing is ever installed without a touch.
- **The settings are TigerScale's list**: a 72 px header that is the back
  button as a whole (a large chevron, the title in white, a hairline), rows
  84 px tall with 40 px icons drawn from shapes and each setting's current
  value on the right (printers on, the Wi-Fi network, the account, the
  language, the version - "1.3.2 > 2.0.0" in orange when one is out), a
  scrollbar always shown, and the up / down rail on the right, greyed at its
  ends. The list keeps its place when a page is opened and closed. Every
  settings page and the account pages take the same header.
- **The rest follows the same grammar**: the Wi-Fi list is TigerScale's (the
  header "Wi-Fi (2.4 GHz)" with its rescan button, tall rows with the signal on
  the left and a padlock on a secured network, the scrollbar and the rail); the
  home's top line is the title in white and bare icons (Wi-Fi, account, a large
  gear); the full view's back button is a bare chevron; the keyboard page's
  chevron is the large one. No grey boxes around icons any more.
- **The update page is TigerScale's**: it checks as it opens, a green badge
  when up to date, an amber banner and *Install* when not, a confirmation, then
  the same progress ring (navy track, green fill, green arrow, the percentage,
  "Do not power off").
- **A version that does not start is not offered again.** After the roll-back,
  the slot it was written to stays marked invalid; its version is read at every
  start and skipped, even while the manifest still names it.
- **The update is checked before anything is switched.** The manifest and the
  image are fetched over TLS verified against the certificate bundle; the image
  must hash to the manifest's SHA-256, carry this firmware's name (another
  product's image is refused) and the version announced. A new version boots on
  probation: if it restarts within its first minute, the previous one comes back
  by itself.
- **Web installer** at <https://tigertag-project.github.io/Tiger-Touch/>, in the
  eleven languages of the device (Chrome or Edge on a computer). It keeps the
  saved Wi-Fi and account unless "Erase device" is ticked.
- **Releases.** A `vX.Y.Z` tag builds the firmware, publishes the images with
  their SHA-256 sums and the changelog section as notes, then deploys the
  installer and the OTA manifest, and reads the published site back to prove it
  serves the release.
- **`docs/INSTALL.md`**: replacing the BigTreeTech firmware step by step - the
  CH340K driver macOS lacks and how to allow it, a copy of the BigTreeTech
  firmware first, "Erase device" on that first install, building with Docker or
  ESP-IDF on a Mac, and going back. `scripts/backup-flash.sh` takes that copy in
  checked 128 KB pieces, because a single 16 MB read over the CH340K seldom
  reaches the end.
- `LICENSE` (MIT), `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, issue and pull
  request templates, `llms.txt`, and `docs/OTA.md`.
- **Anycubic printers in cloud mode**, which were listed as not supported:
  one session for every cloud Anycubic of the account, signed in the way the
  slicer does with what Tiger Studio writes on the printer, as TigerSpool
  does. Seen idle on a Kobra X with its box. No pause, stop or light over the
  cloud yet, and no temperature at rest: the broker closes the session on a
  request it does not allow, so each will be added once seen allowed
  ([docs/PRINTER-COMPATIBILITY.md](docs/PRINTER-COMPATIBILITY.md#anycubic-through-the-cloud)).

- **Klipper and OctoPrint printers**, Tiger Studio Manager's two new printer
  plugins: any Klipper printer through Moonraker (state, progress, layers,
  temperatures up to four extruders and a chamber, its light, Happy Hare or AFC
  lanes) and any printer behind OctoPrint (state, progress, temperatures), with
  pause, resume and stop, and the printer's own message on an error. Seen
  against Studio's mocks in every state, not yet on a real printer.
  Their logos are Studio's.
- A Prusa whose account holds a legacy `apiKey` is reached with it, as in
  Studio, and the printer's own message is shown when it says something is
  wrong.

### Changed
- **Every LAN printer takes an address with a port** (`host:port` in the
  account's `ip`), as Klipper and OctoPrint already did: Bambu Lab, Creality,
  Elegoo, FlashForge, Snapmaker, Anycubic and Prusa used to add their own port
  to it and build an address that could not work. A bare address keeps the
  brand's port. One helper, `net/addr.h`, does the split for all of them.
- **The README shows the printers and compares the firmwares**: a line-up of
  popular printers, the brands' marks in one SVG, a table of Tiger Touch against
  the Panda Touch's and the K-Touch's own firmwares (brands, protocols, how many
  printers, what is sent), and the TigerSystem products with their pictures.
- **The README is rewritten** for people who own the screen: the Panda Touch and
  the K-Touch, the brands, the clouds, screenshots of the cards, the list and the
  full views, a picture of the screen running Tiger Touch, and the questions
  people ask. The screen is a 5" panel, not a 7" one as the documents said.
- **The settings' printers page no longer shows each printer's share of the
  load** beside its switch: the gauge above it says the total.
- **The device starts in seconds**: a device that is set up shows its printers
  a second after the start-up picture, with the printers of the last read, and
  connects the Wi-Fi, the account and the printers behind them instead of
  waiting on "Connecting...", "Reading your printers" pages. On the bench, ten
  printers: on screen at 2.5 s instead of about 26, all connected at 22 s
  instead of 46. The start-up picture lasts one second instead of three and a
  half, the clock is no longer waited for, and reading the account no longer
  holds the printers back. The first start keeps its step-by-step setup.
- **Scrolling is about two and a half times smoother**: LVGL draws into the
  panel's hidden frame buffer and the panel switches to it on its refresh,
  instead of drawing in 16-line strips and copying them. The home screen's
  cards scroll at about 14 frames a second instead of 6, the lists at about 18,
  and the panel never shows a picture half drawn. Changing the screen's orientation now restarts the device.
  The swap waits for the panel to really leave a buffer, not for its vsync:
  with bounce buffers the panel takes the new picture a few lines before the
  vsync, and a swap that fell in that gap let LVGL draw into the picture still
  being sent, a brief flash now and then while scrolling.
- **The Wi-Fi icon shows a connection in progress** the way macOS does: its
  arcs light up one after the other from the bottom, in white, while the device
  connects or re-establishes a lost link. It turns red only on a failure (the
  attempts failed, or a lost link stayed down for a minute), no longer for
  every second spent connecting.
- **Tiger Touch presents itself as what it is: a new firmware**, for the screen
  BIGTREETECH sells as the Panda Touch and as the K-Touch (one board, two
  BIGTREETECH firmwares), in the README, the documentation and the web
  installer, in every language. Its Bambu Lab sessions identify as
  `tigertouch-…` (they said `tigerpanda-…`).
- **The printer load budget works as TigerSpool's**
  ([docs/CONNECTION-BUDGET.md](docs/CONNECTION-BUDGET.md)): 144 load slots,
  1 slot = 1 KB of internal RAM, and each printer priced at the worst its
  connection was measured to cost. Every Bambu Lab in cloud mode shares one
  session, so a second one costs 4 slots instead of 18. At most ten printers
  at once; if the internal RAM stays under 32 KB for three seconds, the last
  printer switched on is switched off rather than the device running out.
- **Twice the internal RAM**: about 200 KB free with no printer open instead
  of 95 KB, and 88 KB with ten printers open where five used to leave 18 KB. The screen's two render buffers, the larger network buffers, MQTT's
  outbox, the Wi-Fi and lwIP code and the account's printer list moved out of
  internal RAM.
- **The screen has a core of its own**: everything that waits on the network
  runs on the other one, so printer traffic no longer slows drawing.
- **The device joins the strongest access point of the network**, not the
  first one it hears: on a mesh it sat at -79 dBm on a far access point while
  next to another at -48 dBm. Every reconnection looks again.
- **The network answers about 25 times faster**: Wi-Fi power saving is off
  (the device is on mains power). Asleep between the router's beacons, every
  packet waited: on the bench, pings to the device averaged 740 ms with peaks
  of 2 s, and 28 ms with peaks under 100 ms once it stayed awake. The account,
  which needed several tries and minutes on a weak signal, reads in seconds.
- **A printer brand that does not answer keeps its printers** on the list
  instead of making them disappear (and their switches with them) until the
  next read, and it is asked again every 30 seconds until it answers. A read
  where every brand failed no longer says "Add a printer in Tiger Studio".
- **The same printer written twice in the account is shown once**, from the
  document updated last, as on TigerSpool: an A1 had a LAN and an older
  document at the same address.
- Up to 20 network sockets instead of 10: with nine printers the device ran
  out of them.
- A Bambu Lab printer in LAN mode is found at the address Tiger Studio writes
  for it (`broker`); such printers showed "unknown address". Seen idle on an
  X1C and an A1.
- An A1, A1 mini, P1P or P1S shows "--" for the chamber, which it has no
  sensor for (an A1 showed 5 degrees).
- **The full view of a printer that is not reached shows "--"**, as its card
  does, instead of the last file, progress and temperatures it gave; its ring
  takes the card's colours (amber paused, red on a fault); a long name stays on
  one line instead of covering the message under it; "Layer 0 / 0" is no
  longer shown for a printer that does not count layers.
- A material name too long for its slot ends in "..." on one line instead of
  running under the slot, and a printer with no chamber sensor shows "--" for
  it on the full view as on its card.
- **The home screen tells the truth when the account cannot be read** at
  start-up (a slow network): "Your printers could not be read yet. Trying
  again." instead of "Add a printer in Tiger Studio", and the read is tried
  again every 30 seconds until it works.
- The brand is written as people write it on the linked-account and printers
  pages ("Bambu Lab", not "bambulab").
- `docs/PRINTER-COMPATIBILITY.md`: a Snapmaker U1 (four nozzles), a
  FlashForge AD5X and a Creality Ender-3 V4 with its CFS were seen idle on this
  firmware.
- **The flash holds two 6 MB application slots** instead of one 4 MB slot
  (`firmware/partitions.csv`). The NVS keeps its place and size, so a board
  moved to this version over USB keeps its Wi-Fi and account; from here on the
  table is frozen.
- The settings menu has eight rows, a little shorter each.
- **`scripts/flash.sh` writes at 115200 baud** instead of 460800 (`BAUD=` to
  change it): through the screen's CH340K, 230400 and 460800 lost bytes on
  a Mac with three different cables.
- Versioning works as in the sibling projects: notes accumulate under
  `[Unreleased]`, `bump-version.sh` dates them, a tag publishes them. The
  pre-commit check that every commit moves the version is gone.
- The web installer names the serial port both ways the screen can show it
  (`USB-SERIAL CH340`, or the ESP32-S3's own `USB JTAG/serial debug unit`).


## [1.3.2] - 2026-10-13

### Added

### Changed
- The full view is two cards side by side: the printer's, and a card of its own for
  its filament units (the external spool, the AMS HT and the boxes), instead of
  units standing on the printer's background. The units' card is shown only when
  the printer has units. Three small units (the external spool and two AMS HT)
  fit on a row; four rows of boxes fit the card.

### Fixed

## [1.3.1] - 2026-10-13

### Added

### Changed

### Fixed
- The light of a Bambu Lab (seen on a P2S, through the cloud) was shown off while
  it was on, so the button kept sending "on" to a lit lamp and nothing happened.
  The printer's `lights_report` lists its lights as `{node, mode}` in either key
  order; the reader took the `mode` that followed the node, which was the next
  light's when the keys came the other way round. Each element is now read on its
  own. The command also goes out at QoS 1, as Tiger Studio publishes to the cloud
  broker, with a `sequence_id` that changes at every command (it was always the
  same), and the log says what was published and what the printer reports
  (`light report`). Seen working on a P2S through the cloud.

## [1.3.0] - 2026-10-13

### Added
- Pause / resume and stop, on every printer card and on the full view. Pause is a
  touch (the button turns into resume while paused); stop is a hold of 1.5 s, with
  a bar that fills while it is held, and letting go cancels it. They are drawn all
  the time and act only while a job runs on a connection that takes commands (dimmed
  and inert otherwise): a Bambu reached through the cloud takes none, as in Tiger
  Studio; in LAN mode it does.
- A command for every family of drivers, from Tiger Studio's protocol notes:
  Creality, Bambu (LAN), Snapmaker, Elegoo CC2 and CC1, FlashForge, Prusa and
  Anycubic (LAN). None has been run from this firmware yet; see
  `docs/PRINTER-COMPATIBILITY.md`.

- A light button on every card and on the full view, always drawn (a light is
  wanted at rest as much as at work), lit amber while the printer says its light is
  on. One touch switches it. It is inert, dimmed and not clickable, while the
  printer is not reached, and for the printers whose driver has no light command
  (Prusa, Elegoo Centauri Carbon 1: Tiger Studio has none for them either). The
  commands are Studio's: Creality `lightSw`, Bambu `ledctrl` (LAN and cloud),
  Anycubic `light` `control` type 3, FlashForge `lightControl_cmd`, Elegoo CC2
  method 1029, Snapmaker `SET_LED cavity_led`. Seen working on a K2 (the light
  switches on and off); the others have not been run from this firmware yet, see
  `docs/PRINTER-COMPATIBILITY.md`.

### Changed
- **The firmware now sends commands to a printer** (pause, resume, stop, and the
  light, on a touch, and nothing else). The rule that it never did is rewritten in
  `CLAUDE.md`.
- The progress ring turns green when the print reaches 100 %, on the cards and on
  the full view.
- The thin separating line above the temperature tiles is gone, on the cards and on
  the full view.
- On the full view the light, pause / resume and stop are one row of three at the
  card's top right, as big as the printer's logo; the state (a dot and a word)
  moved to the top bar, left of the Wi-Fi wave.
- On the printers page the state (a dot and a word) hangs from the top right corner
  of each card; the pause and stop buttons and the light sit under it.
- The cards are two across and fill the panel; a third is reached by sliding. The
  thin white scroll bar along the bottom is gone.
- A card says what the printer is doing ("Printing", "Paused"...); the green, red
  or grey dot already says whether it answers, so the word "Online" is gone.
- Nozzle, bed and chamber are pictograms, not words (the ones of Tiger Studio's
  printer cards), with the value centred beside them; one nozzle puts the three
  on one row, several share a row of nozzles over the bed and the chamber (a
  caption, Left / Right, stays over each value for two nozzles). The file, the
  time left and the estimated end have a pictogram in front of their caption. The
  full view's values carry their unit sign now that "(°C)" is gone from the captions.
- The full view's card is one background behind the whole page: the external
  spool and the AMS / CFS units sit on it, and an AMS HT takes the place to the
  right of the external spool, on its row, while there is room.

### Fixed
- A paused printer now shows as paused: the button turns into "resume" and the card
  says "Paused". Creality (every model: the driver is the brand's) reports a pause
  as `state` 5, which was read as printing (seen on a K2: the print paused but the
  card kept saying "Printing"; resume then worked from the new button); FlashForge
  had no paused state at all.

## [1.2.0] - 2026-10-13

### Added
- The printers page shows one live card per switched-on printer, in a row that
  slides sideways: its name and maker's logo, whether it answers and what it is
  doing, the progress, the file, the time left and the estimated end, and its
  temperatures. A printer with several nozzles shows them as the full view does
  (left and right for two, one tile each for up to four, the active one ringed),
  then the bed and the chamber. The filament units are not on the card; they are
  on the full view.
- Three short captions (nozzle, bed, chamber), in the eleven languages, taken
  from the existing ones without their unit.

### Changed
- The printers page was a list; it is now these cards. Touching a card opens the
  full view of the printer, exactly as touching a row did. The cards never
  command a printer.
- A chamber that the printer does not report is shown as "--" on the cards, not
  as 0.
- The dashboard's state words and time formats are shared with the cards.

## [1.1.2] - 2026-10-12

### Changed
- The main component is built with the warnings of the CI (`-Wall -Werror=all`,
  `-Wextra`), written in its `CMakeLists.txt`. They came from ESP-IDF's project
  setup, which differs between machines: a local build said "no warnings" while the
  CI build failed. Now both build it the same way.

### Fixed
- The Elegoo driver's register-response topic could be cut short: at the longest
  (a 39-character serial, a 15-character client id) it is 85 bytes for a buffer of
  80, and the reply would never be matched. The buffer is 96 bytes. Found by the
  first CI run of the firmware build, which failed on it.

## [1.1.1] - 2026-10-12

Nothing changes on the device.

### Removed
- The web installer (`installer/`, its workflow, its manifest script and its
  translation check). The firmware is built and flashed with ESP-IDF and
  `scripts/flash.sh`; the images of every push are kept as a build artifact. The
  installer remains in the history of the earlier commits.

### Changed
- `docs/ROADMAP.md` and `THIRD_PARTY_LICENSES.md` say that the two licence questions
  (the board component's and the project's) are not being handled: the repository is
  private, and they matter only the day it is made public.

### Fixed
- The README no longer claims version 1.0.0 as its status.

## [1.1.0] - 2026-10-12

### Added
- The time zone follows the country, in any country. It is taken from the TigerTag
  account (the country picked for the ranking, or the country of the prices, which
  can be a Canadian province), then from the country the access point announces,
  and is UTC when neither is known. Nothing is sent anywhere to find out. The table
  is generated from the IANA time zone database (`firmware/tools/make_tz_table.py`,
  260 countries and provinces, checked against the database for three years ahead)
  and every rule was run on the board and compared.
- `docs/ROADMAP.md` records the two next updates the owner wants: over-the-air
  update and the Anycubic cloud.

### Changed
- The printer screen no longer shows a clock in the corner. The estimated end of the
  print stays, and is in the local time of the country. The Wi-Fi signal and the
  account icon moved to the right edge.

### Removed
- The Central European time zone that was written in the code.

### Fixed
- Morocco, Western Sahara and Greenland are approximations: the Ramadan period of
  the first two and a negative transition hour in the third cannot be carried by
  the rule the device reads. Greenland's daylight time begins one hour late; Morocco
  is one hour off for about a month a year. Listed in the generator, not hidden.

## [1.0.1] - 2026-10-12

Nothing changes on the device; what changes is what the build and the history can
say about the machine that made them.

### Added
- `scripts/check-build-privacy.py`: fails when a built image or the ELF carries a
  home-directory path or a local private term. Part of `verify.sh` after a build.
- `scripts/check-version-bump.py`, run by the pre-commit hook: refuses a commit
  that does not move `PROJECT_VER`.

### Changed
- Reproducible build (`CONFIG_APP_REPRODUCIBLE_BUILD`): the images and the debug
  file no longer carry the absolute path of the machine that built them, nor the
  build date.
- `scripts/bump-version.sh` dates the entry and finds a working Python on Windows
  (`python3` there can be a Store shortcut that does nothing).

## [1.0.0] - 2026-10-12

First version of the firmware that is published as a project of its own.

### Added
- A printer monitor for the BigTreeTech Panda Touch (ESP32-S3, 800x480 RGB panel,
  GT911 touch), standalone: no computer, no bridge.
- Sign-in with a TigerTag account; the printers, and their access details, are read
  from the account. Nothing is configured on the device beyond Wi-Fi and the account.
- Several printers watched at once, within a memory budget measured in load slots
  (`docs/CONNECTION-BUDGET.md`); the printer list shows who answers.
- Drivers: Creality (K2), Bambu Lab (cloud and LAN), Snapmaker, Elegoo (CC2 over MQTT,
  Centauri Carbon 1 over SDCP), FlashForge, Prusa, Anycubic (LAN). Each is documented
  with what has and has not been seen on a real machine in
  `docs/PRINTER-COMPATIBILITY.md`.
- A dashboard that adapts to the printer: one nozzle, two (left and right), or up to
  four tool heads, with the active one marked; filament units (CFS, AMS, external spool).
- Account status beside the Wi-Fi signal (green, orange while reaching it, red), kept
  honest by a background token renewal.
- Settings: printers on or off, Wi-Fi, screen (brightness, sleep, orientation),
  language, change account, restart, factory reset. The firmware version is shown
  at the top right of the menu.
- Eleven languages (en, fr, de, es, it, zh, pt, pt-PT, pl, ru, nl). English until the
  device or the account says otherwise.
- A browser installer (`installer/`), in the eleven languages of the device. The window
  ESP Web Tools opens after the button is English only, so the page quotes its labels.

### Known limits
- Only the Creality K2 and the Bambu Lab P2S have been run against real machines. The
  other drivers are written from each maker's protocol and compiled, not exercised.
- No over-the-air update yet: the installer is the update path.
