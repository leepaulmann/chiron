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
