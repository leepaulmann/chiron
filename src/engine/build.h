#pragma once

// What the base builds next: a prototype, or minus a facility. A negative
// base (-1 - base) leaves out secret projects. second and third, if
// given, get the best non-combat and combat choices; score_out, the pick's
// score.
int base_build(int base, int *second, int *third, int *score_out);

// base_compute(), which it calls first; tests put a stand-in here.
extern void (*build_compute)(int update);
