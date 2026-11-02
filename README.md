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

Early development. The loader maps the game and runs its C runtime start-up
on Chiron's KERNEL32. Windowing, graphics, input and sound are next.

## How it works

- `src/loader/`: maps PE images, applies relocations, binds imports to
  Chiron's shims, and gives each thread a Windows TEB through `%fs`.
- `src/win32/`: Windows API shims, one directory of functions per DLL, kept
  only as wide as the game needs. The game folder appears as drive `C:` with
  case-insensitive file names.
- `src/install/`: finds and verifies your copy of the game.
- `src/engine/`: (planned) open reimplementations of the game's functions,
  swapped in one at a time and checked against the original.
