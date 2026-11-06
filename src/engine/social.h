// Social engineering.
#pragma once

#include <stdint.h>

// The effects [SE_COUNT] of four social models for a faction; `quick`
// leaves out its projects, temple, base values and immunities.
void social_calc(const int32_t *models, int32_t *effect, int faction, int unused, int quick);
// The faction's pending models take effect, with their effects.
void social_upkeep(int faction);
// Whether a faction may choose a model.
int society_avail(int category, int model, int faction);
// The energy a switch to `models` [4] would cost.
int social_upheaval(int faction, const int32_t *models);
// The AI's choice of social models: made and paid for (`output` NULL), or
// written to `output` [4] as advice.
void social_ai(int faction, int growth, int tech, int wealth, int power, int32_t *output);
