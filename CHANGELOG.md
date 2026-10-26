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
- **A time zone setting.** *Settings > Time zone*, right under *Language*, is one list:
  Automatic (the zone of the TigerSystem account's country, as before), then 63 lines
  covering every time zone of the world, in the order of the clock. Each line gives the
  offset in force today (UTC+1), the places that follow it (Paris, Berlin, Rome, Madrid) and,
  when its zone changes its clock, whether it is on summer or winter time now; a touch keeps
  it, and the settings show its first place. A zone picked by hand wins over the account and the access point, and
  stays across restarts and updates. The list is generated from the IANA database, each
  line checked against it for three years, and the page builds eight rows that follow the
  scroll rather than 64.

## [2.4.0] - 2026-10-20


### Added
- **An agent rule: touch only this repository, open issues anywhere**
  (`.agents/rules/this-repository-only.md`, indexed in `CLAUDE.md`).

### Changed
- **The printers' cards show the progress as a line, not a ring.** The light,
  pause and stop buttons now share the card's whole width, right under the
  printer's name, and the progress sits under them: its figure, then a line along
  the rest of the width. Nothing else on the card moved.
- **The full view follows the cards.** The progress ring is a line, the light,
  pause and stop buttons share the card's width under the name, and the state is a
  word in the top bar, in its colour, with no pill.
- **A printer's state is a word in its colour, on the cards and in the list.** The
  dot is gone: a printer that does not answer shows nothing in its card. The word
  (Printing, Paused, Done, Error, Idle, Offline, Connecting) is at the top right of a
  card and under the name in a row, and the progress line takes its colour: blue
  while printing, amber when paused, green when done, red on an error or when the
  printer is silent, white when ready, grey while it is still being reached. The
  maker's logo and the printer's name stay white. A printer in error pulses, its
  name and its logo together, like a heartbeat.

- **The pictures made from the screen captures follow the new screens.** The other five
  captures (Anycubic Kobra S1, Elegoo Centauri Carbon 2, Klipper Voron 2.4, OctoPrint
  Ender-3 S1, Prusa Core One) are taken again in Demo mode too, and the front and
  three-quarter mockups, the cards, list and tour animations are made again from
  them: the line fills, and at 100 % the line turns green and the state word reads Done.
  `scripts/make-mockups.py` draws the line instead of the ring, and `stills` now writes
  the front mockups as well. `docs/MOCKUPS.md` says so. The 45-degree drawings
  (cards, list and printer), the README's animated picture and the installer's device
  picture show the new screens too, set in the drawing by the new
  `scripts/make-drawings.py`, which now also makes the nine brand drawings
  (`assets/devices/brands/`, on the brand pages and the media kit). The ten share pictures
  (`assets/og/`) are made again from them; `scripts/make-og-images.py` takes `--regular`
  beside `--font` to be run with a bold and a regular file (on Windows, Arial Bold and
  Arial). The descriptions of the four brand drawings that spoke of a progress ring (Bambu
  Lab, Creality, Prusa, OctoPrint) say progress bar, in the eleven languages. The screen of
  the PandaTouch Reforged campaign illustration (`assets/social/pandatouch-reforged.png`)
  shows the current full view of the X1 Carbon, set in the glass along its tilt; the rest
  of the picture is as it was.
- **The README's six screen captures show the firmware as it is now**: the cards,
  the list and the full views of the Snapmaker U1, the FlashForge AD5X, the Bambu
  Lab X1 Carbon and the Creality K2 Plus, taken again in Demo mode, with the
  progress line, the three buttons across the card and the state's colour. Their
  descriptions no longer speak of a ring.

- **The pictures show the state written and the logo white.** The eleven captures
  are taken again in Demo mode from the firmware as it is, with a twelfth, the top
  of the list (`assets/screens/home-list-top.png`), for the list animation: its ten
  rows now move as the firmware sorts them (a finished print goes to the bottom,
  then starts again 40 s later). The mockups, the animations, the 45-degree
  drawings, the share pictures (in Helvetica Neue again) and the campaign
  illustration are made again from them.

### Fixed
- **The list view draws the nozzle and bed pictograms** beside the two
  temperatures. They were asked for at 16 px, a size that does not exist (only
  24 px does), so nothing was drawn.

## [2.3.0] - 2026-10-20


### Added
- **The agent rules, one file each**, in `.agents/rules/` (Rule, Why, How),
  indexed one line each in the "Rules" section of `CLAUDE.md`, critical rules
  first; `AGENTS.md` and `llms.txt` point to the index instead of copying it.
  `scripts/check-agent-rules.py` (in `verify.sh` and the pre-commit hook) fails
  when the index and the files disagree, when a rule loses its form, or when a
  path cited by an entry point or a rule does not exist.
- **`scripts/i18n-key.py`** adds or removes a translation key in every language
  at once, in the screen's table or the site's dictionary.
- **The whole history is read for private data** (`scripts/check-history.py`):
  every version of every file and every commit message, with the patterns and
  local terms of `check-private-data.py`; before every push (`scripts/hooks/pre-push`,
  the commits pushed) and in the release workflow (all of it). It replaces a
  personal script that was ignored by git.
- **`scripts/check-i18n.py` catches a shifted column** (Chinese or Cyrillic in
  the wrong column, or missing from its own) and lists the rows no source uses,
  as a warning.

### Changed
- **The list view puts the printers in order of the end of their print**, the one
  that finishes first at the top; a printer with no print running comes after
  them, in the order it had. The rows follow the estimated ends as they move, and
  stay still for 3 s after a touch so that none slides under a finger. The card
  view is unchanged (issue #4).
- **The screen can sleep after up to an hour.** *Settings > Screen > Sleep after*
  now offers 1 min, 10 min, 30 min, 1 h and never, in place of 30 s, 1 min, 5 min
  and never (issue #5). A choice saved by an earlier version (30 s, 5 min) is read
  as the next one up, so the screen never sleeps sooner than it was told to.
- **The documentation says what the code does**: the job buttons are always
  drawn and dimmed when they cannot act, the update task is the exception to the
  core and stack rules, the update is checked when its page opens (*Settings >
  Update*), the drivers' status, the catalogue (more than 400 models), the fonts'
  command, the files the site build writes, and more (`CODEMAP.md`, `README.md`,
  `CONTRIBUTING.md`, `llms.txt`, `docs/`).
- **Eleven translation rows no screen used are removed** from the screen's table.
- **`scripts/bump-version.sh` resets `WORKLOG.md`** to its header.

### Fixed
- **The light button of an H2S, H2D, H2D Pro, H2C or X2D switches both chamber
  lights.** These printers have two LED strips, each its own `ledctrl` node
  (`chamber_light` and `chamber_light2`); the button addressed only the first,
  so one strip stayed as it was (reported on an H2C). The printer is told by the
  catalogue id of its record in the account (7, 8, 9, 11, 12), or by a
  `lights_report` that names `chamber_light2`; the light reads as on when either
  strip is, each report being merged on its own. Every other printer is
  unchanged.
- **The background connection task's creation is checked**, as every other one.
- **`firmware/tools/make_fonts.py` finds the fonts LVGL 9.6 ships** after a first
  build, with no `--sources`.

## [2.2.0] - 2026-10-18


### Added
- **PandaTouch Reforged campaign artwork and an English Reddit launch draft**, showing an unused Panda Touch given a second life with TigerTouch, with the existing device and Demo mode interface as references.
- **One page per printer brand on the site**, for people who search for their
  printer: Bambu Lab, Creality, Elegoo, Anycubic, Snapmaker, FlashForge, Prusa,
  Klipper and OctoPrint, at `/<brand>/`. Each puts its brand first (models,
  connection, what was seen on a real machine, what the screen shows, pause /
  stop / light, three setup steps, its own questions), then the other brands one
  line each with a link, then the project itself. Each has its brand's logo as
  favicon, its own title, description, canonical address and Open Graph card,
  and structured data (SoftwareApplication, BreadcrumbList, FAQPage); the site
  gains `sitemap.xml` and `robots.txt`. The pictures are the device's own screen
  in Demo mode, set in the 45-degree drawing (`assets/devices/brands/`), five of
  them new: Centauri Carbon 2, Kobra S1, Core One, a Voron on Klipper and an
  Ender-3 S1 behind OctoPrint. Built from one template and one data file
  (`scripts/make-brand-pages.py`, `installer/brand-pages.json`) when the site is
  built; never committed.
- **Each brand page shows its printers**: a lineup of the models under the top
  of the page (Tiger Studio Manager's printer pictures, `assets/printers/models/`),
  each named; the header reads "<Brand> x TigerSystem" and the brand's logo
  stands beside the title.
- **Each brand page shows the latest version** ("Latest version v2.1.1,
  released ..., What's new"), read when the page opens from `version.json`, the
  manifest published with every release (`installer/release.js`): no page holds
  a version, so a release changes nothing to edit.
- **The FlashForge page lists the Creator 5 Pro**, in its models and lineup.
- **The site's shared files carry their fingerprint** (`site.css?v=...`):
  a browser that kept the previous stylesheet (Pages lets it for ten minutes)
  drew the new brand strip's logos at full width. Every logo is also sized in
  the page itself.
- **The README has no emoji left**: the feature table and the comparison's
  ticks and crosses use small SVG icons (`assets/icons/`), each with alternative
  text ("Yes", "No", the feature's name) that screen readers and search engines
  read, where an emoji was read as a symbol name. The support links show Buy Me
  a Coffee's, Ko-fi's and PayPal's own logos (Tiger Studio Manager's files), on
  the installer page too.
- **The brand logos stand before each brand** in the README's printer table,
  comparison and protocols table, every brand linked to its page
  (`assets/brands/logos/`, drawn from the brand strip).
- **"Built around makers, not around a brand"**: why the project exists, on
  the installer page (eleven languages), every brand page and the README. Printer
  makers build their software around their own machines; TigerSystem starts from
  the maker, whatever their printers, computer or phone, free and open.
- **The installer's comparison has one row per brand**, each with its logo and
  a link to its page, where Creality, Elegoo, Snapmaker, FlashForge, Prusa and
  OctoPrint shared one row.
- **The TigerSquad card comes right after the install card** on the installer
  page, above the brand logos, instead of at the bottom.
- **A brand page shows the whole brand**, including the models reached
  through another connection, each marked: Elegoo's Centauri Carbon 2, its
  Combo emoji® edition, the Centauri 2 and 2 Combo and the Centauri Carbon, plus
  the OrangeStorm Giga and the Neptune 4, 4 Pro and 4 Max "via Klipper";
  Creality's K2 Plus, K2, K1 Max, K1C, Hi, Ender-3 V4 and V3 KE, plus an Ender-3
  S1 "via OctoPrint". Each page's questions say how. The Elegoo pictures are the
  ones Tiger Studio Manager's catalogue names.
- **"TigerTouch" in two colours, joined, in every title** of the site: the
  installer's (after each translation too) and the brand pages'.
- **A brand's lineup holds up to fourteen printers**: one row of seven, a
  second, centred, only when there are more; one row that scrolls sideways on a
  phone. Bambu Lab shows eleven, Creality thirteen (three of them "via
  OctoPrint"), Elegoo ten, Prusa eight, Klipper nine, OctoPrint eight.
- **After an outside audit**: each brand page has a "What you need" box (the
  screen, sold separately, with the K-Touch said to be untested yet; Chrome or
  Edge and a USB-C cable for the first installation only; Wi-Fi; the account and
  Tiger Studio Manager); every picture of the screen says it is Demo mode; "no
  computer" reads "no computer left on" and "two minutes" "a few minutes"; the
  installer (eleven languages) and every brand page say the project is not
  affiliated with BIGTREETECH or the printer makers.
- **Every page has its own share picture and a full set of tags**: a 1200x630
  Open Graph image per page ("TigerTouch for <Brand> printers", the brand's
  screen and printers; `assets/og/`, made by `scripts/make-og-images.py`), its
  size and alternative text, the Twitter card's title, description and image,
  the locale, and the installer page's canonical address. Each brand page's
  description is written for search results (150 to 160 characters, a whole
  sentence) instead of its first paragraph cut with "...".
- **After a second outside audit**: the installer page's own HTML (what search
  engines, assistants and readers without JavaScript read) still told step 3 to
  leave *Erase device* unticked, where the translations say to tick it when
  replacing the BIGTREETECH firmware; the site build now writes that text from
  the English dictionary, so it cannot drift again. The installer adds, in
  eleven languages, that the screen is sold separately, to add the printers in
  Tiger Studio Manager first, and how to go back to the original firmware, plus
  its structured data. Brand pages add a note for models reached through
  another connection (Elegoo's Neptune 4 and Giga, older Creality behind
  OctoPrint), say the screen's data is made up in Demo mode, link the way back,
  and list the same models as their lineups. `llms.txt` says what TigerSystem
  and TigerTag each are.
- **Share previews pass opengraph.xyz's checks**: a social description of its
  own (125 characters at most, where previews cut; the search description stays
  150 to 160), an "Install it free" button in every share picture, and the
  picture's address carries its fingerprint so networks drop an old copy.
  Page titles hold 60 characters at most ("<Brand> 3D printers on Panda Touch
  & K-Touch | TigerTouch"); headings, share titles and descriptions say "3D
  printers". In the share pictures the button is large, above the printers, and
  the small print sits under the screen's drawing.
- **The README and llms.txt link every brand page**: the brands of the
  printer table and a line under it in the README, a section of its own in
  `llms.txt`, so that search engines and AI assistants find each page.
- **Every logo of the brand strip is a link** to its brand's page, on the
  installer page and on every brand page (the current one in orange). The strip
  is HTML, its colours explicit: as SVG text, a visited logo's name kept
  Chrome's dark link colour and vanished on the dark theme.
- **The rest of TigerSystem at the foot of every page**: Tiger Studio Manager,
  TigerPOD Mini, TigerScale V3 and TigerSpool, a picture and a line each and a
  link to their project, in the eleven languages, on the installer and every
  brand page.

- **A media kit page** (`/press/`, in the eleven languages): the fact sheet,
  a one-sentence and a one-paragraph description with a copy button, and every
  picture to download (front mockups, screen captures, illustrations, share
  pictures, the TigerSystem square tiger head, framed and full, and the
  TigerTag logo), with the whole kit as one ZIP built with the
  site. Linked from the foot of every page, the README and llms.txt.
- **The site's icon is TigerSystem's framed tiger head** (`assets/brand/`), on
  the installer, the media kit and every language's copy, with an Apple touch
  icon; the brand pages keep their brand's logo.
- **Animated views of the screen** (`assets/mockups/animated/`): the home cards,
  the list and an update, each a time-lapse of Demo mode drawn from the real
  captures with the firmware's font, geometry and logic (the clock runs, the
  time left counts down, a finished print turns green and dims pause and stop),
  the screen alone, in the front picture and in perspective on the
  three-quarter photograph, GIF and WebP; and a tour (the start-up screen, then
  the list, then the cards, on one clock). Every screen also as a still on the
  three-quarter photograph (`assets/mockups/angle/`). In the media kit and the
  README. Made by `scripts/make-mockups.py`; the method, for people and agents
  making another view, in `docs/MOCKUPS.md`.
- **The installer page shows the tour in the three-quarter photograph**: the
  start-up screen, then the list, then the cards, transparent on either theme,
  in place of the turning drawing; its description follows the language.
- **Animations in three folders**, `assets/mockups/animated/screen/`, `front/`
  and `angle/`, and three galleries in the media kit.
- **Pause, resume, stop and the light for an Anycubic through the cloud**, sent
  as Tiger Studio sends them: REST orders (`sendOrder`), on a task of their own so
  a touch never waits, never a publish on the shared MQTT session (which the
  broker would close). Signed with the slicer's app id, secret and version that
  Tiger Studio now writes on every cloud printer of the account; without them the
  buttons stay dimmed. Seen on a Kobra X through the cloud (2026-10-18): the light
  on and off, pause, resume and stop during a print. The light shows what was
  asked: the cloud sends no light report.
- **Share this page**, on every page of the site in every language: WhatsApp,
  Telegram, X, Bluesky, Facebook, Reddit, LinkedIn, e-mail, copy the link, and the
  phone's own share sheet where there is one. Each link carries the page's
  address in the language read and its title; nothing is sent until the reader
  picks a site (`installer/share.js`, the marks in `assets/icons/share/`).
### Changed
- **One stylesheet and one theme script for the whole site** (`installer/site.css`,
  `installer/theme.js`), shared by the installer page and the brand pages; the
  installer page gains a favicon.
- **It is a TigerSystem account, not a TigerTag account**: the installer page
  (in its eleven languages), the README, the documents and the screen's own
  texts ("Link a TigerSystem account?", the battery and Live view notes) say
  so. TigerTag stays the name of the chips, the app and the team.
- **The installer page shows the README's picture of the screen**, turning from
  the start-up screen to the cards, the list and a printer's full view (the same
  file, copied by the site build), instead of a picture that flipped on hover.
- **The printer brands run across the whole page** on the installer, under the
  name, the screen and the install card.

- **Every page of the site in eleven languages**: the brand pages speak the
  installer's eleven languages too, with the same language menu in the header
  and the same remembered choice. One script for every page (`installer/lang.js`),
  the shared words in `installer/i18n.js`, each brand's own words in
  `installer/brand-i18n/<lang>.json`, written into its page when the site is
  built. Each language also has its own address (`/fr/`, `/fr/elegoo/`, ...),
  its text written into the HTML, with `hreflang` links between them, its own
  canonical address, title, description and structured data, and every
  address in `sitemap.xml`; the root stays English, and the menu opens the
  chosen language's address. The latest-version line,
  the installer's picture descriptions and the brand strip's label follow the
  language. `check-installer-i18n.py` checks the brand words as it checks the
  installer's.

- **The TigerSquad card sits right under the top of every brand page**, as it
  does on the installer page, instead of near the foot.
- **Elegoo, Prusa and Klipper are seen on real printers** (the owner, 2026-10-18):
  the Centauri Carbon 2 and the Centauri Carbon (reading, pause, resume, stop, and
  the Centauri Carbon 2's light), a Prusa through PrusaLink (reading, pause,
  resume, stop) and a Klipper printer (reading, pause, resume, stop, light). The
  compatibility document, the drivers' headers, the README and the brand pages
  in eleven languages say so.
- **The K-Touch is the same board with another BIGTREETECH firmware**: the site
  and the README no longer call it untested.
### Fixed
- **The site's accessibility**, on the installer and every brand page:
  - the closed language menu no longer takes keyboard stops; open, it shows
    which language has the focus, and the arrow keys, Home, End, Enter, Space,
    Escape and Tab work as in a list box;
  - small orange text and links use a darker orange on the light theme and
    the orange buttons dark text, for a 4.5:1 contrast; the TigerSquad card
    is darker behind its white text;
  - on a phone, the comparison table reads as one card per row, each value
    under its column's name, instead of scrolling sideways;
  - a brand's printers fill one row when they fit and two balanced rows when
    they do not, never a short last row of one or two;
  - the theme and language buttons, the pills and the "Before you start"
    arrow are 44 px touch targets;
  - on a phone the "What you need" notice comes after the title, not above it;
  - a system set to reduce motion gets no animation.

## [2.1.1] - 2026-10-18


### Added
- **A window announces a new version**, as on TigerScale V3 and TigerSpool:
  "New firmware available. Go to the update page?", the move (`2.1.0 > 2.2.0`),
  *Later* and *Yes*, which opens the update page. A Tiger Touch runs day and
  night and its settings are seldom opened, so the orange *Update* row alone
  went unseen. It shows only over the home screens (cards, list, a printer's
  full view), awake, and never over the reminder or the stop confirmation: at
  once for a version not offered yet, and the same version again a day later if
  it is still not installed.

### Changed
- **"LAN mode" is now "Live view"**: the settings row, its page and its
  switch read *Live view* in every language, the name Tiger Studio uses for it
  (`docs/LIVE-VIEW.md`, was `docs/LAN-MODE.md`). The code, the account's
  fields (`lan_live`, `lan_access_code`, `lan_url`) and the API are unchanged.
- **The battery is green when declared**: the *Battery* row's icon, its value
  and the battery drawn on its page turn green with the switch on, and the row
  reads *On* or *Off* instead of *Fitted* / *None*.
- **The manifest is read 20 s after start, again every minute until it is
  read, then every 6 hours**, as TigerScale V3 and TigerSpool do. It was read a
  minute after start, and a screen offline at that minute missed it.

### Fixed
- **An Anycubic printer followed through the cloud stayed "Offline" after a
  restart** while Tiger Studio showed it connected. Its link opens from the
  printer list kept since the last start, about ten seconds before the account
  is read again, and the cloud identity (certificate and token) comes with that
  read; the driver tried once without it, said nothing, and never tried again.
  It now tries every 10 s until the identity is there, and says so in the log.
- **The update check ran every 8 minutes instead of once a day.**
  `pdMS_TO_TICKS()` multiplies before it divides, in 32 bits, and overflows past
  71 minutes at 1 kHz: a day came out as about 8 minutes, and the new 6 hours
  as 125 s (seen on the serial log). Long waits now go in one-minute steps.

## [2.1.0] - 2026-10-18


### Added
- **Settings > LAN mode**, as on TigerScale V3: Tiger Studio Manager, on the
  same Wi-Fi, takes the screen's picture (for an issue or a post), shows it
  live and sends touches and swipes. Every request carries a six-character
  access code (capitals and digits) drawn on the device, kept across updates,
  renewable with one touch, and written to the account with the presence
  (`lan_live`, `lan_access_code`, `lan_url`) where Studio reads it. Ten wrong
  codes lock it for a minute; switched off, nothing is served. Live view is on
  by default. No web page (`docs/LIVE-VIEW.md`, `docs/TELEMETRY.md`).
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
- **The presence document moves to `users/{uid}/tigertouchs/{mac}`** (it was
  `tigertouches`); the account's rules name the new collection only, so a 2.0.0
  screen's heartbeat is refused until it updates.
- **Every picture of the screen**, in the README, `docs/OTA.md`, the device
  drawings and the installer page, taken anew in English in Demo mode, with the
  new header, the clock and the progress colours; the README gallery gains the
  X1 Carbon with its AMS and the K2 Plus with its CFS.
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
