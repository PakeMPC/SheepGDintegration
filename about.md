# SheepGDintegration

Brings the level request queue of **SheepGDRequest** into Geometry Dash, so you can play, skip and
manage your viewers' requests without leaving the game.

> **This mod needs the SheepGDRequest desktop app.** The app listens to your stream chats and builds
> the queue; this mod is its in-game half.

## How to use it

Press the **sheep button** in the main menu. It opens the queue: the level that's up now on top,
and the ones coming next underneath. From there you can play, view, skip forward or back, remove,
pick one at random, or play any level in the list.

Everything you do is written back to the same file the app uses, so the app and its OBS widgets
update instantly — and new requests show up here on their own.

The **gear** in the top-right corner opens the settings.

## Features

- **Auto-skip.** If you set a timer in SheepGDRequest, the mod moves to the next level without
  leaving gameplay. An optional on-screen timer shows the whole countdown or only the last 10
  seconds.
- **Swap on death.** Optionally wait until you die before switching, instead of cutting the attempt.
- **Q / E** jump to the previous / next level while playing a requested level (one change per
  second at most).
- **When the queue runs out**, choose: stay in the level, go back to the list, stay until you die,
  or load a placeholder level of your choice.
- **Preloading.** The next level and all of its audio — multiple songs and sound effects included —
  are downloaded ahead of time, so jumps are instant and no level starts silent.
- **Sheepy**, an optional cube icon for your gameplay (or only for requested levels). It keeps your
  colours and glow.
- **English, Spanish and Portuguese**, following the language you set in SheepGDRequest.

## Setup

Usually none: the mod finds the queue file on its own. If it can't, it asks you to pick
`queue_data.json` from the SheepGDRequest data folder, and remembers your choice.

## Privacy

Everything stays on your computer. The mod reads and writes `queue_data.json`, and reads
SheepGDRequest's `settings.json` only to know your language. When running through Wine or Proton on
Linux, it looks through the user folders under `/home` to find that file, because Wine doesn't tell
Windows programs where your real home folder is.

Levels and songs are downloaded through Geometry Dash itself. Level thumbnails come from
levelthumbs.prevter.me. No data is collected or sent anywhere.
