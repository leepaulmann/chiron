#pragma once

// What the base builds next: a prototype, or minus a facility. A negative
// base (-1 - base) leaves out secret projects. second and third, if
// given, get the best non-combat and combat choices; score_out, the pick's
// score.
int base_build(int base, int *second, int *third, int *score_out);

// base_compute(), which it calls first; tests put a stand-in here.
extern void (*build_compute)(int update);

// Which class of item a base is making, for the retooling penalty.
int base_making(int item, int base);

// Sets what the base builds (99999 for no change).
void base_change(int base, int item);

// Picks and sets what the base builds next; `governor` lets a human's
// governor share out its kinds of work first.
void base_reset(int base, int governor);

// base_reset() for an AI faction's bases, in one region unless negative,
// and only undefended ones if asked.
void bases_reset(int region, int faction, int undefended);
