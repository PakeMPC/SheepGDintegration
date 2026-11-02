# v1.0.0

First release.

## The queue scene
- A sheep button in the main menu opens the SheepGDRequest queue inside the game.
- **Now Playing card** with the current level: difficulty, stars or moons, length, downloads,
  likes, orbs earned, coins, GDDL tier and AREDL rank, thumbnail, who requested it and from which
  platform, and the auto-skip countdown.
- **Next levels** list, scrollable, with the same details for every request.
- Play, view, skip forward or back, remove, pick at random, or play any level straight from the
  list.
- Everything is written back to `queue_data.json`, so SheepGDRequest and its OBS widgets update
  instantly — and new requests appear on their own.
- Settings popup in the scene, so options can change mid-stream without leaving it.
- **Esc** leaves the scene.

## While playing
- **Auto-skip**: when the timer set in SheepGDRequest runs out, the mod moves to the next level
  without leaving gameplay, behind a short fade.
- Optional on-screen timer, with a "last 10 seconds only" mode.
- **Swap on death**: optionally wait until you die before switching levels.
- **Q / E** jump to the previous / next level in levels launched from the queue.
- At most one level change per second, so fast clicking or key mashing can't trip up the queue.
- **When the queue runs out**, choose between staying in the level, going back to the list,
  staying until you die, or loading a placeholder level. The placeholder also shows on the
  Now Playing card.
- Leaving a level takes you back to the queue, with the menu music fading back in.

## Preloading
- The next level is downloaded ahead of time together with all of its audio: multiple songs,
  sound effects and 2.2 music library tracks included.
- If a level's music is still downloading when you enter, the mod waits for it (up to 5 seconds)
  so no level starts silent.

## Sheepy
- Optional **Sheepy** cube icon for gameplay, or only for requested levels. It keeps your colours
  and glow, and shows inside the ship and UFO like any cube.
- Sheepy also appears in the icon kit and the colour picker.
- Two Sheepys hop in the corners of the queue scene when you press your jump keys.

## Setup and languages
- Finds `queue_data.json` on its own, also on Linux through Proton. If it can't, it asks you to
  pick the file once and remembers it.
- English, Spanish and Portuguese, following the language set in SheepGDRequest.
