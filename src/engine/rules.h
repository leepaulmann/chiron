// The readers of the game's text rules (rules.c): alpha(x).txt and the
// faction files.
#pragma once

#include <stdint.h>

#include "game.h"

// A name in the rules turned into an index: a tech's short name (or "None"
// -1, "Disable" -2; an unknown name is reported and taken as -2), a
// chassis', weapon's or armor's name (unknown: reported, 0).
int rules_tech_name(char *name);
int chas_name(char *name);
int weap_name(char *name);
int arm_name(char *name);

// The next line's first item as a number, kept within min and max.
int text_get_number(int min, int max);
// The next line's first item as a tech.
int tech_item(void);
// The next item as a noun's gender (0 male, 1 female, 2 neuter) and
// whether it is plural.
void noun_item(int32_t *gender, int32_t *plural);

// The sections of alpha(x).txt, each returning non-zero when the file or
// section can't be opened.
int read_basic_rules(void);  // #RULES and #WORLDBUILDER
int read_tech(void);         // #TECHNOLOGY
int read_units(void);        // #UNITS: the basic designs
int read_factions(void);     // #BONUSNAMES, the factions playing and their files
// Everything: with all set (a new game), also the basic designs and the
// factions.
int read_rules(int all);

// A faction file into a faction's definition; with toggle 2, only its
// first line (the names).
void read_faction(struct faction_def *def, int toggle);
// A faction's file and art (not for faction 0, the natives).
void read_faction2(int faction);
