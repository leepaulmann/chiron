// Factions: probes, the energy market and the AI's diplomacy.
#pragma once

// What it costs `faction` to take `base` over, by its probes or, with
// `market`, when cornering the energy market; -1 for a probe when the
// base is out of reach.
int mind_control(int base, int faction, int market);
// What it costs `faction` to corner the energy market: all other bases'
// mind control costs but those of factions that have surrendered to it in
// a pact, and at least 1000.
int corner_market(int faction);
// An AI faction's diplomacy for the turn (see faction.c); for a human
// faction only its agenda flags move on.
void enemy_diplomacy(int faction);

#include <stdint.h>

// Treaty (TREATY_*) and agenda bits set or cleared between two factions,
// both ways; set_treaty() one way only.
void treaty_on(int faction, int other, uint32_t status);
void treaty_off(int faction, int other, uint32_t status);
void agenda_on(int faction, int other, uint32_t agenda);
void agenda_off(int faction, int other, uint32_t agenda);
void set_treaty(int faction, int other, uint32_t status, int on);
// The pact partners of a faction attacked come to its aid.
void intervention(int victim, int aggressor);
// Whether the player is far enough ahead for the AI to unite against it,
// and (great_satan) whether it is time to.
int great_beelzebub(int faction, int aggressive);
int great_satan(int faction, int aggressive);
// The faction about to win the Council by votes, or 0 (see faction.c).
int aah_ooga(int faction, int pact);
// Whether an AI faction sees the end of the game coming.
int at_climax(int faction);
void cause_friction(int faction, int other, int friction);
int get_mood(int friction);
int reputation(int faction, int other);
int get_patience(int other, int faction);
int energy_value(int energy);
// What `faction` would give for `other`'s base, or -1.
int value_of_base(int base, int faction, int other, int overmatched, int coveted);
// The techs a player and a rival could trade, into game_trade_*.
void tech_analysis(int player, int rival);
// Whether `faction` would attack `other` (with `ally` beside it, if above 0).
int wants_to_attack(int faction, int other, int ally);

// The talks between a player (`faction`) and an AI (`other`).
// diplomacy_check() weighs how the AI stands towards the player, into the
// game_diplo_* variables; with `full`, also whom it might mention and its
// prototypes (scan_prototypes()), which mention_prototypes() brings up.
void diplomacy_check(int faction, int other, int full);
void scan_prototypes(int faction, int other);
void mention_prototypes(int faction, int other);
// Whether the player takes the call (opening the diplomacy window).
int wants_to_speak(int faction, int other);
// Two AIs that have met make peace, step by step: truce, treaty, pact, or
// back down (see faction.c); with `trade` they trade techs first.
void enemies_treaty(int faction, int other, int trade);
// Two AIs go to war (see faction.c), and trade a tech each (in a game with
// tech trading).
void enemies_war(int faction, int other);
void enemies_trade_tech(int faction, int other);
// Units each side has in the other's bases or territory go home when a
// pact ends; returns how many of `faction`'s moved.
int pact_withdraw(int faction, int other);
void pact_ends(int faction, int other);
// Whether the player goes ahead with breaking `status` with `other`.
int break_treaty(int faction, int other, uint32_t status);
// `faction` turns on `victim`, incited by `other` (or -1).
void double_cross(int faction, int victim, int other);
// Two factions meet (`other` 0: native life), at (x, y) or off the map.
void encounter(int faction, int other, int x, int y, int tower, int native);
// territory(faction, other, flags, base, count), how far `other` presses
// into `faction`'s territory, is called through game_territory.
// Atrocities committed by `faction` against `other`.
void atrocity(int faction, int other, int always, int alien_too);
void major_atrocity(int faction, int other);
