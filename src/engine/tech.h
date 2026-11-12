// Technology queries, reimplemented from terranx.exe 2.0. Behaviour follows
// the originals exactly, quirks included; see tech.c.
#pragma once

#include <stdint.h>

int has_tech(int tech, int faction);
int tech_recurse(int tech, int level);
int tech_category(uint32_t tech);
int tech_avail(uint32_t tech, int faction);
int tech_is_preq(int preq, int parent, uint32_t range);
int tech_mil(int tech);
int tech_tech(int tech);
int tech_infra(int tech);
int tech_colonize(int tech);
int valid_tech_leap(int tech, int faction);
void say_tech(char *out, int tech, int category);
char *tech_name(int tech, int category);
void tech_effects(int faction);
int tech_rate(int faction);
int tech_val(int tech, int faction, int simple);
int redundant(int fac, int faction);
int facility_avail(int fac, int faction, int base, int queue);
int terrain_avail(int terraform, int sea, int faction);
void tech_secret(int tech, int faction);
int tech_ai(int faction);
void tech_achieved(int faction, int tech, int from, int steal);
