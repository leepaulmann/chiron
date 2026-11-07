# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Chiron runs *Sid Meier's Alpha Centauri / Alien Crossfire* (GOG 2.0, `terranx.exe`) natively on 32-bit Linux without Wine: a PE loader maps the game, Chiron's own Win32 shims stand in for the DLLs, and the game's functions are replaced one at a time by GPL reimplementations that shadow mode verifies against the original.

## Hard rules

- Never commit game files or anything derived from them (disassembly, extracted tables, strings, `.idb`). Disassembly and the OpenSMACX/Thinker reference checkouts belong outside the repo (a scratch directory), never in it.
- Game tables are read from game memory through pointers set in `src/engine/game.c`, never copied into the source. Addresses, struct layouts and a function's first few bytes (for hook checks) are fine.
- Keep the purchase check (sha256 allowlist in `src/install/`).
- The developer plays from the same dev copy (`~/.local/share/chiron/game`, saves in `~/.local/share/chiron/user`). Never bulk-delete saves there.
- Test runs open a game window on the developer's screen; avoid triggering error dialogs.

## Commands

All tooling comes from the Nix flake (32-bit gcc, SDL2, mingw for the loader tests):

```sh
nix develop -c cmake -S . -B build -G Ninja
nix develop -c ninja -C build
nix develop -c ctest --test-dir build          # no game files needed
nix develop -c ctest --test-dir build -R engine_mapgen   # one test
./build/tests/engine_map                       # or run a test binary directly
```

Running the game unattended (needs the game set up; see README):

```sh
CHIRON_SOAK=ai CHIRON_SCRIPT="30 click 835 362; 40 every 1 key Return" timeout -s INT 420 ./build/chiron
CHIRON_REIMPL=shadow ...       # check reimplementations against the original; summary printed at exit
./build/chiron --original      # game's own code only
```

- `30 click 835 362` is Quick Start. `30 click 835 287; 40 every 2 key Return` goes through Start Game, which is the path where the game builds a new map with `world_build`.
- `python3 tools/coverage.py` reports how much of the game's code is hooked, by 64 KB block, from your own `terranx.exe` (estimates; the game logic is 0x4e0000–0x5cffff).
- `CHIRON_SNAPSHOT=file.bmp` saves the screen (`tools/bmp2png.py` converts it). `CHIRON_SHADOW_DIFFS=1` lists every differing byte range of a shadow mismatch.
- `CHIRON_SOAK=ai` hands the player's faction to the AI by clearing its human bit. The game still gives the player's console a pass over its units after each round, and would wait at "TURN COMPLETE" for orders for any unit the AI left with moves. `soak.c` skips such units when a turn hasn't moved for 30 seconds. Pace varies a lot between random maps (roughly turns 100–145 in 7 minutes); a whole game ends around turn 275.

## Architecture

- `src/loader/`: maps the PE image, applies relocations, binds imports to the shims, and gives each thread a Windows TEB through `%fs`.
- `src/win32/`: one shim file (or group) per DLL, kept only as wide as the game needs. Drive `C:` is the game folder with the user folder layered over it, and file names are case-insensitive. Every shim must pop the same argument bytes as the real stdcall function; the `stdcall_sizes` test checks this.
- `src/install/`: finds and verifies the user's copy of the game.
- `src/main.c`: runs the game on its own thread, then `patches_apply` (byte fixes for bugs in the original, `src/engine/patches.c`) and `hooks_apply`.
- `src/engine/`: the reimplementations.
  - `game.h`/`game.c`: the game's struct layouts (with `GAME_AT` offset asserts) and pointers to its globals and functions, bound from the image base.
  - `tech.c`, `map.c`, `mapgen.c`, `base.c`, `production.c`, `upkeep.c`, `social.c`, `faction.c`, `strategy.c`, `build.c`, `veh.c`, `spot.c`, `path.c`, `score.c`, `council.c`: modules; `quiet.c` holds the shadow-only quiet hooks. Each ends with a hook table and a `struct hook_module` listed in `hooks.c`.
  - `hooks.c`: hook machinery. A hook writes a JMP over the original's start after checking its expected first bytes (skipped, with a warning, if they differ). A trampoline keeps the original callable through `orig_<name>`. Modes are on (default), off (`--original`) and shadow.
  - `shadow.h`: shadow-mode wrappers. `SHADOWn` is for pure functions. `SHADOW_WRITER` is for functions that write: it saves the named memory spans, runs the original, rewinds, runs Chiron's version, compares, and keeps the original's result. Map generation checks cover all of the game's writable memory (0x682000–0x9ca000), the map and `rand()`'s per-thread state, with `GetTickCount` held still.
  - Calls made inside a check or a sweep go straight to Chiron's code (`shadow_bypass`). Shadow-only "quiet" hooks (`quiet.c`) keep the game's message pump and drawing idle while state is protected: the heap isn't rewound, so drawing in a check produces spurious differences. Messages, sounds and movies (`popb`, `popp`, `parse_say` and the like) are held instead (`hooks_ui_hold`): a check compares the two runs' calls, and the original's are made after the outermost check, so a player sees each message once; a sweep's are dropped. A held call returns 0, so a check of a function that uses a popup's answer (the player's choice) is only valid for AI factions.
  - Nothing that allocates from the game's heap may run inside a check: the C runtime's heap keeps its bookkeeping in the writable memory a check puts back, but not the memory it hands out, so the heap is corrupted (crashes in the CRT around 0x647ed1). Windows (the Monuments, New Tech and Council windows, the tutorial's notes) and dialog pictures allocate; that is why the monument calls (`mon_*`), `tech_advance`, `tech_research`, `tech_achieved` and `call_council` are held and pictures are reported missing. quiet.c's heap guard hooks the game's malloc and free and counts any use while state is protected as a mismatch, with the caller's stack.
  - Some originals read stack slots they never set (drone_riot's revolt chances): their reimplementations get the original's frame from an assembly entry, and `SHADOW_WRITER_STALE` gives both runs of a check the same bytes below the call (`hooks_stale_call`). Anything that waits on the clock (`clock_wait`) must be held: a check holds the clock still (from before it saves state), and no Win32 or multimedia timer comes due and PeekMessage returns nothing meanwhile (the game's timer objects set a flag when theirs fire; multimedia timers' callbacks run on their own threads, so they wait on any thread's hold, and a hold waits for a callback already running). Heap memory a check can't put back can also carry state between its two runs: base_terraform's check saves the path finder's tables, and name_base (which opens files) is checked unprotected, keeping only its outputs apart. A check of a caller runs the functions it calls unchecked, so a module whose functions the game calls only from inside another's check (production, from base_upkeep) has its sweep chained to that module's (`hook_module.also`); and the caller's check must skip whatever its callees' checks skip (production_phase and base_upkeep run the original for human factions and bases about to be renamed; base_upkeep, which the game calls only inside production_phase's check, is checked on its own in the upkeep sweep). faction_upkeep's check also runs the original unchecked while any faction is human, since the AI's diplomacy and the Council would ask the player questions a check leaves unanswered; its phases are then checked on their own. enemy_diplomacy, likewise unchecked while any faction is human, is swept with another faction made the player in a hotseat game, so the paths towards a human are checked without a call being put to anyone.
  - Each module's sweep runs once a game turn in shadow mode and checks its functions over a range of inputs on a protected copy of the game.

## Reimplementing a function

- Work from the disassembly of `terranx.exe`. Disassemble from the function's own address with `i686-w64-mingw32-objdump -d -M intel --start-address=...`; a linear pass over the whole file loses sync. Behaviour must match the original exactly, quirks included.
- Names come from OpenSMACX and Thinker. Their signatures and logic are often wrong in details, and OpenSMACX's map generation is marked untested, so trust the disassembly and shadow mode over the reference.
- Reimplementations call each other directly, and call game code through the `game_*` pointers. Code adapted from either reference keeps its copyright notice.
- Add a unit test in `tests/engine_<module>.c`. These run on a small made-up map with stand-ins for the game functions.
- Verify with a shadow soak (0 mismatches) and a normal-mode soak. A planted bug that shadow mode catches is a good check that the sweep reaches the code.
- Write comments in plain prose describing behaviour, and note where the original differs from OpenSMACX.

## Commits

End commit messages with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. The repo is public at github.com/leepaulmann/chiron, and commits use the GitHub noreply email.
