# Endless

*Play playlist songs endlessly, non-stop - no BS*

This is a fork of TheZipCreator's original [endless-qmod](https://github.com/TheZipCreator/endless-qmod).

This fork keeps the original goal, but makes playlist playback more complete and less restrictive.

## What Changed In This Fork

- Added `Any` for `Difficulty` and `Characteristic` filters.
- Expanded playlist song resolution so more songs are playable instead of being silently dropped.
- Improved fallback behavior for incomplete playlist metadata:
  - Missing or unusable playlist difficulty -> use the song's hardest available difficulty.
  - Missing or unusable playlist characteristic -> use `Standard` when available, otherwise the first valid characteristic.
- Added broader difficulty canonicalization for playlist metadata:
  - Text labels (`Easy`, `Normal`, `Hard`, `Expert`, `Expert+`/`ExpertPlus`).
  - Numeric canonical values (`0..4`).

## What Endless Does

Endless adds an Endless mode tab in Beat Saber's gameplay setup flow.

You start one map, then it keeps launching the next one automatically until you stop, fail (unless configured otherwise), or hit your configured end condition.

## Main Features

- Automatic mode:
  - Source maps from a selected playlist, or `All` playlists/maps.
  - Filter by `Difficulty` and `Characteristic` (both now support `Any`).
  - Filter map requirements for `Noodle Extensions` and `Chroma` as `Allowed`, `Required`, or `Forbidden`.
- Playset mode:
  - Create named playsets.
  - Add/remove specific maps (map + characteristic + difficulty).
  - Run endless using only that curated set.
- Session behavior:
  - `Continue on Fail`.
  - `Sequential` order (or shuffled when disabled).
  - `End After All Songs Finish`.
- In-run controls and HUD:
  - Skip button in pause menu.
  - Optional Endless HUD with total time and running total score.

## Filter Semantics

- `Difficulty = Any`:
  - Do not constrain by one difficulty.
  - Use playlist metadata if valid; otherwise fall back to hardest available.
- `Characteristic = Any`:
  - Do not constrain by one characteristic.
  - Use playlist metadata if valid; otherwise fall back to `Standard`/default valid characteristic.

This allows mixed-difficulty and mixed-characteristic playlists to play continuously instead of being cut down to only one exact combination.

## Compatibility Note

`Replay` is known to conflict and may cause crashes or unstable behavior while Endless is active.

## Build

- `qpm s build`
- `qpm s copy`
- `qpm s qmod`

## Credits

- [zoller27osu](https://github.com/zoller27osu), [Sc2ad](https://github.com/Sc2ad), and [jakibaki](https://github.com/jakibaki) - [beatsaber-hook](https://github.com/sc2ad/beatsaber-hook)
- [raftario](https://github.com/raftario)
- [Lauriethefish](https://github.com/Lauriethefish), [danrouse](https://github.com/danrouse), and [Bobby Shmurner](https://github.com/BobbyShmurner) for [this template](https://github.com/Lauriethefish/quest-mod-template)
