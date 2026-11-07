# Chiron

[![CI](https://github.com/leepaulmann/chiron/actions/workflows/ci.yml/badge.svg)](https://github.com/leepaulmann/chiron/actions/workflows/ci.yml)

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

Chiron never copies or distributes game files, and this repository must
never contain them.

## Setting up

Tell Chiron once where your copy of the game is:

```sh
chiron --setup "/path/to/Sid Meier's Alpha Centauri Planetary Pack"
chiron --setup ~/Games/gog/alpha-centauri      # a folder above it, such as a Wine prefix
chiron --setup setup_sid_meiers_alpha_centauri_2.0.2.23.exe   # GOG offline installer
```

A folder is searched for `terranx.exe`. An installer is unpacked with
[innoextract](https://constexpr.org/innoextract/) into
`~/.local/share/chiron/game`. Either way the game folder is remembered in
`~/.config/chiron/chiron.conf`. Without a setup, Chiron looks in
`~/.local/share/chiron/game` and then in the usual places for GOG games
(`~/Games`, `~/GOG Games`, `~/.wine`, `~/Downloads`) and remembers what it
finds.

Chiron never writes to the game folder. Saves, the game's settings
(`Alpha Centauri.ini`) and its temporary files go to
`~/.local/share/chiron/user`, which the game sees layered over its own
folder: a file there takes the place of the game folder's copy. Saves that
are already in the game folder stay visible to the game.

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
./build/chiron              # the game set up with --setup, or found
./build/chiron --game DIR   # another game folder, for this run only
./build/chiron --user DIR   # another folder for saves and settings
./build/chiron --trace      # log every Windows call the game makes
```

## Status

Early development. The game runs from the main menu into a game (tested
with Quick Start), saves and loads games, and is drawn by Chiron's GDI in an
SDL window, with sound effects, music and voice-overs through Chiron's
DirectSound. It plays the game's movies (decoded by the game itself) through Chiron's DirectDraw.
Multiplayer is not implemented yet.

Options: `--screen WxH` sets the screen size the game sees and
`--volume 0-100` scales the sound.

For debugging, `--trace` logs Windows calls, `CHIRON_SNAPSHOT=file.bmp`
saves the screen as it is presented (`tools/bmp2png.py` converts it),
`CHIRON_AUDIO_DUMP=file` records the audio mix as raw 44.1 kHz stereo, and
`CHIRON_SCRIPT="30 click 835 362; 50 close"` drives the game unattended
(actions are `click X Y`, `dblclick X Y`, `move X Y`, `key NAME` or
`key Ctrl+NAME`, `type TEXT` and `close`; `"40 every 1 key Return"` repeats a
step). `CHIRON_SOAK=1` reports the turn,
memory and CPU every 30 seconds; `CHIRON_SOAK=ai` also hands your faction to
the AI so a game plays itself:

```sh
CHIRON_SOAK=ai CHIRON_SCRIPT="30 click 835 362; 40 every 1 key Return" ./build/chiron
```

## Reimplemented code

Chiron replaces the game's own functions with open reimplementations, one
module at a time. So far:

- technology: whether a faction has or can research a tech, tech levels,
  categories and prerequisites, tech names, research cost, the AI's
  valuation of techs, and the bonuses techs give; which facilities,
  projects and terraforming a faction may build, the projects that make a
  facility redundant, the free tech's message, and the AI's choice of
  research;
- the map: coordinates and distances, what a square holds (altitude,
  region, units and bases, resources, pods, elevation), territory, zones of
  control, sensors, landmarks, where units can reach and how bases, coasts
  and seas connect; the code that changes squares and landmarks; and who
  owns each square, with the counts and sites the AI keeps from it;
- map generation: all of making a new map (continents, hills, erosion,
  shorelines, temperature, rivers, rainfall, fungus, rockiness and all 16
  landmarks), the climate pass that reworks the map when the sea level
  changes, and the altitude setter, with the floods it brings to bases and
  units. Loading and saving maps are still the game's own;
- bases: what each square yields in nutrients, minerals and energy,
  facilities, and a base's nutrient and mineral totals with growth and
  ecological damage; which squares its citizens work and which become
  specialists; unit upkeep and supply convoys; its energy with trade,
  losses to inefficiency and the split into economy, psych and labs; its
  talents and drones; its population goal and the cost factors for
  growth and building; its growth and starvation at the end of a turn,
  with the advice the game gives when stores run low; and its production:
  finishing units, facilities, satellites and secret projects (with their
  effects, the Ascent to Transcendence included), and moving its queue on;
  its research, facility upkeep, energy convoys, golden ages and riots
  (facilities lost, and a human's bases defecting to another faction);
  doctors in riots, disbanding the units it can't support, and the AI
  rushing production with its energy reserves; its ecological damage:
  fungus, native life, global warming and the volcano; the AI's
  terraforming plans; new bases' names, and captured bases renamed;
  finding bases (on a square, the nearest, by faction, region and
  pacts), marking a new base's radius, its first production and a unique
  name, facilities and secret projects held, and minerals lost to
  retooling; founding a base and destroying one (its units rehomed or
  lost, the bases after it renumbered everywhere); the AI's road goals
  between its bases; and
  the base's turn that runs all of these, with its energy and trade
  going into the faction's reserves; and the faction's turn around them:
  loans, the AI's maintenance discount, interest, the first turn's free
  techs and the end of sanctions; the AI's split of energy into economy,
  psych and labs (and the player's warning of falling reserves);
  units' repairs, moves and orders at the start of a faction's turn; and
  the whole of a faction's turn around these, with rushing paid for, the
  AI cornering the energy market, the Planetary Council, a faction with
  nothing left put out of the game, and the autosave;
- social engineering: the effects of a faction's choices, with its
  projects, temples and faction rules, and their taking effect each turn;
  which models a faction may choose and what a change costs; and the AI's
  choice of models, with the advice the social engineering screen gives;
- factions: what it costs to take a base over by mind control, and to
  corner the energy market; and the AI's diplomacy each turn: betrayals,
  meetings and tech trades between AIs, calls to the player, complaints
  about its territory, the friction between them, and orbital attacks on
  satellites; its moods, patience, the reputation a faction has, whether
  it would go to war or unite against a runaway leader, what a base or a
  loan is worth to it, which techs it would trade, and the treaties and
  agendas set and broken between factions;
- the AI's strategy each turn: its census of bases and units (and its
  rivals'), the goals they suggest, disbanding obsolete and stranded units,
  each region's plan of offense, defense or expansion, raising land
  towards other regions, choosing a base to attack from, and every unit's
  orders; and the goals and sites it keeps (adding, finding and removing
  them, and carrying them from turn to turn) with its rating of each
  square as a place for a new base;
- what a base builds next, for the AI and the governor: every unit,
  facility and secret project it can build, scored by the faction's
  priorities and what its bases and units already cover; then setting it,
  with the retooling penalty for humans, the governor's kinds of work each
  claiming an item, its question before a secret project, and the news of
  projects other factions begin;
- units: which designs a faction can build and what they cost, their
  speed, abilities and the AI's rating of their weapons and armor, the
  morale units get from social engineering, bases and secret projects, and
  the defenders a base wants, and the counts the AI makes over a stack of
  units; keeping the stacks of units on each square: lifting, dropping,
  promoting, sorting and clearing units, and their moves left after damage;
  combat strengths of weapons and armor, the nearest unit, and whether a
  unit is ready for orders; a unit's health and moves left, how far air
  drops reach, transports' capacity, the design a faction uses for a plan,
  and which units the AI sends to a monolith; killing units, with the
  units they carry at sea, and renumbering the units after them; loading
  transports and carriers, for the player and the AI; creating units, and
  the AI's choice of new units to send against its target; whether a unit
  stops on spotting another, waking units on board, and whether a faction
  wants a new design; an attacker's and a defender's strength with their
  morale in battle, the terrain's defense, the modifiers a battle report
  lists, and what each move costs; the strengths of both sides in a battle
  with every modifier (terrain, base defenses, altitude, sensors, psi,
  air superiority and the rest), and which unit of a stack defends it;
  the AI's choice of chassis, weapon and armor, what a design costs and
  filling one in, sending a unit somewhere, and the AI's transports
  turning on a nearby enemy base; upgrading designs (the cost, the AI's
  decision, the units and queues switched) and promoting units; which
  abilities a design may have; ports, convoys and sensor sites; the
  garrisons the AI wants; its view of its enemies' best units, and its
  odds of an attack; naming designs, retiring them (their units refitted,
  turned scouts or disbanded), marking superseded ones obsolete, and
  proposing new ones; and the AI's pass that thinks up new designs each
  turn (upgrades to its own, defenders, attackers, artillery, scouts,
  ships, aircraft, missiles and formers);
- spotting: what a unit sees as it moves (squares, bases and units, the
  contacts it makes and the units it wakes), and the squares, bases and
  stacks a faction has seen;
- the path finder: the route search behind every unit's moves (with its
  costs for terrain, roads, fungus, zones of control and borders), a
  unit's next step towards its waypoint, zones of control, the AI's choice
  of sensor sites; and its map work: numbering every continent and sea,
  which seas each continent touches, the coarse map the AI plans on, and
  the land a faction holds around a square;
- scores: the year, objectives, the factions' rankings (with the AI's
  rolls to turn on the leader), the score and its rating by difficulty;
- the Planetary Council: votes, who may stand for governor, whether a
  faction may call the Council, and the AI's stand and vote on each
  proposal.

`--original` runs only the game's own code. `CHIRON_REIMPL=shadow` runs both
on every call, returns the original's answer, and logs any difference; once
a game turn it also checks every function over its whole input range.
Functions that write to memory run on copies, and the original's result is
kept; while they run, the game's message pump and drawing are held off,
and its messages, sounds and movies are held back, compared between the
two runs, and then shown once.
A mismatch names the first byte that differs (`CHIRON_SHADOW_DIFFS=1`
lists every range). It prints a summary of calls, mismatches and sweep
times at exit:

```sh
CHIRON_REIMPL=shadow CHIRON_SOAK=ai CHIRON_SCRIPT="30 click 835 362; 40 every 1 key Return" ./build/chiron
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
  only as wide as the game needs. Drive `C:` is the game folder with the user
  folder layered on top, with case-insensitive file names.
- `src/install/`: finds and verifies your copy of the game.
- `src/engine/`: open reimplementations of the game's functions, swapped in
  one at a time (`hooks.c`) and checked against the original in shadow mode;
  also fixes for bugs in the original (`patches.c`).
