# SheepGDintegration

<img src="logo.png" width="150" alt="SheepGDintegration logo: a white sheep jumping over a spike" />

A [Geode](https://geode-sdk.org) mod that brings the level request queue of **SheepGDRequest** into
Geometry Dash, so a streamer can play, skip and manage viewer requests without leaving the game.

Everything runs locally. There are no servers involved: the mod and the SheepGDRequest desktop app
share a single file, `queue_data.json`, and both can read and write it. Anything you do in-game
shows up instantly in the app and in its OBS widgets, and the other way around.

> **You need SheepGDRequest.** This mod is the in-game half of the SheepGD ecosystem. On its own it
> has no queue to show: the app is what listens to Twitch, YouTube, TikTok, Kick and Discord chats,
> filters the requests and writes the file.

## Features

**The queue, inside the game**
- A sheep button in the main menu opens a dedicated scene: a *Now Playing* card with the current
  level and a scrollable list of the ones coming next.
- Play, view, skip forward or back, remove, pick one at random, or jump straight to any level in
  the list.
- The list refreshes on its own when a new request comes in.

**While you play**
- **Auto-skip.** If the streamer sets a timer in SheepGDRequest, the mod jumps to the next level
  without leaving gameplay, behind a short fade. An optional on-screen timer can show the whole
  countdown or only the last 10 seconds.
- **Swap on death.** Optionally wait until you die before switching levels, instead of cutting the
  attempt short.
- **Q / E** jump to the previous / next level, only in levels launched from the queue. Level changes
  are limited to one per second, so spamming them can't trip up the queue.
- When the queue runs out, choose what happens: stay in the level, go back to the list, stay until
  you die, or load a placeholder level of your choice.
- Leaving a level takes you back to the queue, not to the main menu.

**Preloading**
- The next level is downloaded ahead of time together with **all** of its audio — multiple songs,
  sound effects and 2.2 music library tracks included — so the jump is immediate and no level starts
  silent.

**Extras**
- **Sheepy**, an optional cube icon that replaces yours during gameplay (or only in requested
  levels). It keeps your colours and glow, and it also shows up in the icon kit.
- English, Spanish and Portuguese. The language follows the one set in SheepGDRequest.

All options live in the settings popup (the gear in the top-right corner of the mod's scene), so
they can be changed mid-stream without opening Geode's mod list.

## Requirements

| | |
|---|---|
| Geometry Dash | 2.2081 |
| Geode | 5.9.0 |
| Platform | Windows. Also runs on Linux through Steam's Proton. |
| Dependency | [`geode.node-ids`](https://github.com/geode-sdk/NodeIDs) |
| Companion app | SheepGDRequest |

## Linking the queue file

Most of the time there is nothing to do: the mod finds `queue_data.json` on its own in the
SheepGDRequest data folder.

| System | Folder |
|---|---|
| Windows | `%LOCALAPPDATA%\SheepGDRequest` |
| Linux | `~/.local/share/SheepGDRequest` |

If it can't find it, opening the mod's scene asks you to pick the file by hand, and that choice is
remembered. A hand-picked path always wins over the automatic one — as long as it still exists.

## What the mod reads and where it connects

Since the mod is reviewed by reading its source, here is the full list:

- **`queue_data.json`**, which it reads and rewrites. Writes are atomic (a temporary file renamed
  over the original) so the app never reads a half-written file.
- **`settings.json`** from the same folder, **only** to read the interface language.
- **Under Wine/Proton only**, the list of folders in `Z:/home` and `Z:/var/home`. Wine does not pass
  the real Linux home directory to Windows programs, so the mod looks for
  `<user>/.local/share/SheepGDRequest/queue_data.json` in each one. Nothing else is read there, and
  nothing is stored or sent anywhere.
- **Network:** levels, songs and sound effects are downloaded through Geometry Dash itself, from the
  game's usual servers. Level thumbnails come from
  [levelthumbs.prevter.me](https://levelthumbs.prevter.me) and are cached on disk.

No telemetry, no accounts, no code downloaded at runtime.

## Building

The mod builds like any Geode mod:

```sh
geode build
```

See the [Geode docs](https://docs.geode-sdk.org/getting-started/create-mod#build) for setting up the
SDK and the CLI.

**Pin the SDK to the version in `mod.json`.** `geode sdk install` fetches the latest release, which
may not be the one this mod targets:

```sh
geode sdk update v5.9.0
geode sdk install-binaries
```

<details>
<summary><b>Cross-compiling from Linux</b></summary>

The mod is developed on Linux and cross-compiled to Windows with `clang-cl`. Besides Geode's
cross-tools, the build needs these variables **in the environment your IDE inherits**, not only in
your terminal:

```sh
export GEODE_SDK=$HOME/.local/share/Geode/sdk
export SPLAT_DIR=$HOME/.local/share/Geode/cross-tools/splat
export CMAKE_TOOLCHAIN_FILE=$HOME/.local/share/Geode/cross-tools/clang-msvc-sdk/clang-cl-msvc.cmake
export HOST_ARCH=x86_64
export CC=/usr/lib/llvm-19/bin/clang-cl
export CXX=/usr/lib/llvm-19/bin/clang-cl
export LLVM_PATH=/usr/lib/llvm-19/bin
export PATH="$HOME/.local/bin:$PATH"   # wherever the geode CLI lives
```

Two of these are easy to miss:

- **`LLVM_PATH`.** `CC`/`CXX` give CMake one compiler, but the toolchain file looks up eight LLVM
  tools on its own (`clang-cl`, `lld-link`, `llvm-lib`, `llvm-nm`, `llvm-mt`, `llvm-rc`, `clang`,
  `clang++`), by name. Its list of versioned names stops at 17, so `clang-cl-19` on your `PATH` is
  not enough.
- **`PATH` with the Geode CLI.** `setup_geode_mod` fails with *"Geode CLI was not found"* even if
  `geode` works in your terminal, when the IDE doesn't see the same `PATH`.

Put them in `~/.profile` rather than `~/.bashrc` or `~/.bash_profile`, and log out and back in:
terminals and IDEs don't read the same startup files, so each can end up seeing a different
environment.

</details>

## Credits

Made by **PakeMPC** — [pakempc.com](https://pakempc.com).

If the mod helps your streams, you can support it on [Ko-fi](https://ko-fi.com/pakempc).
