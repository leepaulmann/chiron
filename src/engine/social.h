// Social engineering.
#pragma once

#include <stdint.h>

// The effects [SE_COUNT] of four social models for a faction; `quick`
// leaves out its projects, temple, base values and immunities.
void social_calc(const int32_t *models, int32_t *effect, int faction, int unused, int quick);
// The faction's pending models take effect, with their effects.
void social_upkeep(int faction);
