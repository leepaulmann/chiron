// What units find on the map: supply and unity pods, monoliths, and the
// alien artifacts they bring to a base. The turn's random events, a
// faction's end, and its social choices taking effect.
#pragma once

// A unit opens the pod on its square: returns 1 if an artifact carrying it
// vanished (the unit is gone), else 0.
int goody_box(int veh);
// A unit at a monolith: healed, or its morale raised.
void monolith(int veh);
// An artifact studied at a base or network node: returns 0 if the base had
// already been linked, else 1 (the unit is gone and a tech may be won).
int study_artifact(int veh);
// The turn's chance events (flag 0 once a turn, with the countdowns of
// those under way): at bases, in orbit, on the map and in the markets.
void random_events(int flag);
// A faction with no base left is out of the game: returns 1, or 0 while
// it has a base. `killer` is the faction that put it out, or 0 (or -1).
int eliminate_player(int faction, int killer);
struct faction_def;
// A faction file's rules put back to their defaults.
void clear_faction(struct faction_def *d);
// The social effects the faction's file gives it.
void compute_faction_modifiers(int faction);
// The faction's chosen social models take effect.
void social_set(int faction);
// The social engineering screen for the faction, then its choices.
void social_select(int faction);
