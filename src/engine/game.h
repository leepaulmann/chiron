// terranx.exe 2.0's game data, as the reimplemented modules see it. Layouts
// and addresses follow OpenSMACX (GPL-3.0-or-later) and Thinker (MIT).
//
// The data lives in the game's memory; game_bind() points these at it. A
// unit test may point them at its own tables instead.
#pragma once

#include <stdint.h>

#include "../loader/pe.h"

// ---- Technology --------------------------------------------------------------

enum {
    TECH_COUNT = 89,      // entries in the #TECHNOLOGY table
    TECH_NONE = -1,       // "no prerequisite"
    TECH_DISABLED = -2,   // a tech switched off in alphax.txt
    TECH_TRANSCEND = 88,  // Transcendent Thought, never "had"
    // Techs that exist only in Alien Crossfire.
    TECH_FIRST_SMACX = 78,  // Progenitor Psych
    TECH_LAST_SMACX = 86,   // Temporal Mechanics
};

struct rules_tech {
    uint32_t flags;
    char *name;
    char id[8];  // short name, such as "Biogen"
    int32_t unused;
    int32_t growth, tech, wealth, power;  // the AI's view of what the tech is for
    int32_t preq1, preq2;                 // prerequisites, TECH_NONE or TECH_DISABLED
};
_Static_assert(sizeof(struct rules_tech) == 44, "layout of the game's #TECHNOLOGY entries");

extern struct rules_tech *game_tech;  // [TECH_COUNT]
extern uint8_t *game_tech_achieved;   // [TECH_COUNT], one bit per faction
extern int32_t *game_expansion;       // non-zero when playing Alien Crossfire
extern int32_t *game_turn;            // the current turn

// Point the variables above at a loaded terranx.exe.
void game_bind(struct pe_image *exe);
