# Chiron

Chiron runs *Sid Meier's Alpha Centauri* and *Alien Crossfire* natively on
Linux, without Wine, using **your own purchased copy** of the game.

Chiron contains no game code or data. It loads the original `terranx.exe`
from your installation, provides its own implementation of the Windows
functions the game uses, and over time replaces the game's own code with
open reimplementations.

> Chiron is an unofficial fan project and is not affiliated with Firaxis
> Games, Electronic Arts or GOG.

## Your copy of the game

Chiron only runs executables it recognises from a purchased release. It
checks the SHA-256 of `terranx.exe` at start-up:

| Release | sha256 of `terranx.exe` |
|---|---|
| Alien Crossfire 2.0, GOG *Planetary Pack* | `01901cbf7196b0c5d0df9540a029520f5df8fd9a6b343deef8b5663872805fcf` |

Point Chiron at the folder containing `terranx.exe` (the GOG install, or a
GOG offline installer unpacked with `innoextract`). Chiron never copies or
distributes game files, and this repository must never contain them.

## Building

The game is 32-bit x86 code, so Chiron is built as a 32-bit Linux program.
The Nix flake provides the toolchain:

```sh
nix develop
cmake -S . -B build -G Ninja
ninja -C build
ctest --test-dir build      # loader tests; no game files needed
```

## Running

```sh
./build/chiron --game "/path/to/Sid Meier's Alpha Centauri Planetary Pack"
./build/chiron --trace      # log every Windows call the game makes
```

## Status

Early development. The game runs from the main menu into a game (tested
with Quick Start), drawn by Chiron's GDI in an SDL window, with sound
effects, music and voice-overs through Chiron's DirectSound, and plays the
game's movies (decoded by the game itself) through Chiron's DirectDraw.
Multiplayer is not implemented yet.

Options: `--screen WxH` sets the screen size the game sees and
`--volume 0-100` scales the sound.

For debugging, `--trace` logs Windows calls, `CHIRON_SNAPSHOT=file.bmp`
saves the screen as it is presented (`tools/bmp2png.py` converts it),
`CHIRON_AUDIO_DUMP=file` records the audio mix as raw 44.1 kHz stereo, and
`CHIRON_SCRIPT="30 click 835 362; 50 close"` drives the game unattended
(`"40 every 1 key Return"` repeats a step). `CHIRON_SOAK=1` reports the turn,
memory and CPU every 30 seconds; `CHIRON_SOAK=ai` also hands your faction to
the AI so a game plays itself:

```sh
CHIRON_SOAK=ai CHIRON_SCRIPT="30 click 835 362; 40 every 1 key Return" ./build/chiron
```

## Licence

Chiron is free software under the GNU General Public License, version 3 or
(at your option) any later version; see `LICENSE`.

Chiron builds on research from two earlier projects:

- [OpenSMACX](https://github.com/b-casey/OpenSMACX) by Brendan Casey (GPL-3.0-or-later)
- [Thinker](https://github.com/induktio/thinker) by the Thinker Mod authors (MIT)

Code adapted from either keeps its original copyright notice, and MIT notices
are preserved alongside Chiron's GPL header.

## How it works

- `src/loader/`: maps PE images, applies relocations, binds imports to
  Chiron's shims, and gives each thread a Windows TEB through `%fs`.
- `src/win32/`: Windows API shims, one directory of functions per DLL, kept
  only as wide as the game needs. The game folder appears as drive `C:` with
  case-insensitive file names.
- `src/install/`: finds and verifies your copy of the game.
- `src/engine/`: (planned) open reimplementations of the game's functions,
  swapped in one at a time and checked against the original.
