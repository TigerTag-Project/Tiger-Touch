# First start: the setup

What a Tiger Touch shows the first time it is switched on, screen by screen, and
why. It is the same setup as [TigerScale V3](https://github.com/TigerTag-Project/Tiger-Scale-V3)'s,
step for step, so somebody who has set up one TigerTag device recognises the
next. The code is `app_task()` in `firmware/main/main.c`, the screens are
`firmware/main/ui/ui_onboard.c`.

> Before the device can show anything, the printers have to exist in a TigerTag
> account, declared in
> [Tiger Studio Manager](https://github.com/TigerTag-Project/TigerTag-Studio-Manager).
> The device reads them; it never builds the list. See the README's
> [Before you start](../README.md#before-you-start).

## The order

```
 power on
    |
 [1] Language ............ mandatory, the first and only question asked in no language
    |
 [2] "Connect to Wi-Fi?" . Set up  -> network list -> password -> connected
    |                      Later   -> skipped, offered again from the home screen
    |
 [3] "Link a TigerTag account?"     (only once there is a network)
    |                      Link now -> sign-in page (e-mail + password, or Google)
    |                      Later    -> skipped, offered again from the home screen
    |
 [4] "Account linked" ..... the confirmation alone, three seconds or a touch
    |
 [5] Printers ............ one switch per printer of the account, under the load gauge
    |
 [6] Home: one live card per printer that is on
```

A device counts as **new** when it has no language, no saved Wi-Fi and no
account: a fresh board, or one after *Settings > Factory reset*. Only then are
questions 2 and 3 asked at start-up. A device that is set up never waits on
them: its printers page is on screen a second after the start-up picture, with
the printers of the last read, and the Wi-Fi, the account and the printers are
reached behind it ([ARCHITECTURE.md](ARCHITECTURE.md#boot)).

## 1. Language

All eleven languages of the TigerTag ecosystem, each written in its own script,
so a person who cannot read the current language still finds theirs: English,
Français, Deutsch, Español, Italiano, 中文, Português (BR), Português (PT),
Polski, Русский, Nederlands. The title is the one English word everybody
recognises, "Language". There is no back arrow: nothing can be said before this
is answered.

The choice is kept on the device, and written to the account
(`users/{uid}/prefs/app.lang`) as soon as there is one, as TigerScale does: the
same person then sees the same language in Tiger Studio, the mobile app and
every device. Later, a language changed in the account comes back to the
device at its next start. A choice made on the device that has not reached the
account yet is never overwritten by the account's.

## 2. Wi-Fi

A question first, "Connect to Wi-Fi?", with *Set up* and *Later*, then the list
of networks (strongest first, a refresh button), the keyboard for the password,
and the connection. A saved network that does not answer at start-up is tried
three times, a few seconds apart, before the list is offered again: a router
often refuses a board that has just restarted.

## 3. Account

"Link a TigerTag account?", *Link now* or *Later*, then one sign-in page:

| On the page | What it does |
|---|---|
| E-mail, Password | Each field opens the keyboard. The password is used for one request and never stored. |
| **Sign in** | E-mail and password, for an account that has one. |
| **Continue with Google** | For an account created with Google, which has no password: a QR code to scan with the phone. |

The Google route is described in [ACCOUNT-PAIRING.md](ACCOUNT-PAIRING.md). An
account created with Google that tries *Sign in* is told to use the Google
button.

## 4 and 5. Account linked, then the printers

Two pages, not one (as on TigerSpool): the confirmation first, alone, then the
printers. The printers page is *Settings > Printers*: a switch per printer, and
the gauge of the load they put on the device's memory
([CONNECTION-BUDGET.md](CONNECTION-BUDGET.md)). A printer that does not fit is
refused at the moment it is switched on, with the reason, rather than left
silently offline later.

## What was put off: the reminders

What was answered *Later* comes back as a panel sliding in from the right over
the home screen, 10 seconds after the home screen appears and then every 30
minutes, for as long as it is missing: Wi-Fi first (nothing else works without
it), then the account. A touch outside the panel is *Later*. A saved network
that is down is not something to set up again, so it is never reminded.

## When the account cannot be read

A slow network at start-up can make the first read of the printers fail. The
home screen then says so ("Your printers could not be read yet. Trying
again.") instead of inviting to add printers that already exist, and the read is
tried again every 30 seconds until it works.

## Starting again

*Settings > Factory reset* erases everything stored (Wi-Fi, account, language,
choices) after a confirmation, and the next start is the first one again.
*Settings > Wi-Fi > Change network* and *Settings > Account* run the
matching step on its own.

## Related

- [ACCOUNT-PAIRING.md](ACCOUNT-PAIRING.md): e-mail and password, and the QR pairing for Google accounts.
- [INSTALL.md](INSTALL.md): getting the firmware onto the board in the first place.
- [OTA.md](OTA.md): what happens after, when a new version is out.
- [SECURITY.md](../SECURITY.md): what the device stores and sends.
