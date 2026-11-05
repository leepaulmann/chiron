// Social engineering. Reimplemented from terranx.exe 2.0's disassembly;
// names follow OpenSMACX (Brendan Casey, GPL-3.0-or-later) and Thinker
// (MIT).
#include <string.h>

#include "game.h"
#include "map.h"
#include "shadow.h"
#include "social.h"

enum {
    CATEGORY_VALUES = 2, CATEGORY_FUTURE = 3, MODEL_POWER = 1, MODEL_CYBERNETIC = 1, MODEL_THOUGHT_CONTROL = 3,
    SP_ASCETIC_VIRTUES_ = 13, SP_NETWORK_BACKBONE_ = 21, SP_LIVING_REFINERY = 23, SP_CLONING_VATS_ = 24,
};

static int holds_project(int project, int faction) {
    int b = game_secret_projects[project];
    return b >= 0 && game_bases[b].faction == faction;
}

static int model_effect(int category, int model, int effect) {
    const uint8_t *p = game_social_effects + category * SOCIAL_CATEGORY_SIZE + model * SOCIAL_MODEL_SIZE;
    int32_t v;
    memcpy(&v, p + 4 * effect, sizeof v);
    return v;
}

// The effects of a faction's four social models, into `effect`
// [SE_COUNT]: each model's, a penalty cancelled by the Network Backbone
// (Cybernetic) or the Cloning Vats (Thought Control and Power), and
// negated or doubled by the faction's IMPUNITY or PENALTY for the model.
// Unless `quick`, also its projects (Ascetic Virtues POLICE +1, the
// Living Refinery SUPPORT +2), a temple (PLANET +1, and RESEARCH +1 for
// the Progenitors), its own base values, and its IMMUNITY (no negative)
// and ROBUST (negative halved). `unused` is passed by the game and not
// read.
void social_calc(const int32_t *models, int32_t *effect, int faction, int unused, int quick) {
    (void)unused;
    memset(effect, 0, SE_COUNT * sizeof *effect);
    const struct faction_def *d = &game_faction_defs[faction];
    for (int cat = 0; cat < SOCIAL_CATEGORIES; cat++) {
        int model = models[cat];
        for (int e = 0; e < SE_COUNT; e++) {
            int v = model_effect(cat, model, e);
            if (v < 0) {
                if (cat == CATEGORY_FUTURE && model == MODEL_CYBERNETIC && holds_project(SP_NETWORK_BACKBONE_, faction))
                    continue;
                if (((cat == CATEGORY_FUTURE && model == MODEL_THOUGHT_CONTROL) ||
                     (cat == CATEGORY_VALUES && model == MODEL_POWER)) &&
                    holds_project(SP_CLONING_VATS_, faction))
                    continue;
                for (int i = 0; i < d->bonus_count; i++) {
                    if (d->bonus_val1[i] != cat || d->bonus_val2[i] != model) continue;
                    if (d->bonus_id[i] == BONUS_IMPUNITY) effect[e] -= v;
                    else if (d->bonus_id[i] == BONUS_PENALTY) effect[e] += v;
                }
            }
            effect[e] += v;
        }
    }
    if (quick) return;
    if (holds_project(SP_ASCETIC_VIRTUES_, faction)) effect[SE_POLICE]++;
    if (holds_project(SP_LIVING_REFINERY, faction)) effect[SE_SUPPORT] += 2;
    if (has_temple(faction)) {
        effect[SE_PLANET]++;
        if (d->rule_flags & RULE_FLAG_ALIEN) effect[SE_RESEARCH]++;
    }
    for (int e = 0; e < SE_COUNT; e++) effect[e] += game_factions[faction].se_base[e];
    for (int i = 0; i < d->bonus_count; i++) {
        int id = d->bonus_id[i];
        if (id != BONUS_IMMUNITY && id != BONUS_ROBUST) continue;
        int32_t *v = &effect[d->bonus_val1[i]];
        if (id == BONUS_IMMUNITY) *v = *v < 0 ? 0 : *v > 999 ? 999 : *v;
        else if (*v < 0) *v /= 2;
    }
}

// The start of a faction's turn: its pending models take effect, and its
// effects are worked out for them three times over (pending, current, and
// a second copy the game keeps); nothing paid yet for changes.
void social_upkeep(int faction) {
    struct faction_data *f = &game_factions[faction];
    memcpy(f->se_models, f->se_models_pending, sizeof f->se_models);
    social_calc(f->se_models_pending, f->se_pending, faction, 0, 0);
    social_calc(f->se_models_pending, f->se, faction, 0, 0);
    social_calc(f->se_models_pending, f->se_2, faction, 1, 0);
    f->se_upheaval_paid = 0;
}

// ---- Hooks and shadow checks ------------------------------------------------

extern struct hook_module social_module;
#define SOCIAL (&social_module)
SHADOW_WRITER(SOCIAL, void, social_calc, (const int32_t *a, int32_t *b, int c, int d, int e), (a, b, c, d, e),
              WRITES(b, SE_COUNT * sizeof(int32_t)), "%d, %d, %d", c, d, e)
SHADOW_WRITER(SOCIAL, void, social_upkeep, (int a), (a), WRITES(&game_factions[a], sizeof(struct faction_data)), "%d", a)

static int slice;

// Each faction's effects for every combination of models, quick and not,
// and its turn's upkeep with made-up pending models.
static void social_sweep(void) {
    slice++;
    if (!*game_map) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    for (int f = 1; f < FACTION_COUNT; f++) {
        int32_t effect[SE_COUNT];
        for (int c = 0; c < 256; c++) {
            int32_t models[SOCIAL_CATEGORIES] = {c & 3, (c >> 2) & 3, (c >> 4) & 3, (c >> 6) & 3};
            check_social_calc(models, effect, f, c & 1, (c + slice) % 3 == 0);
        }
        struct faction_data *fd = &game_factions[f];
        int32_t pending[SOCIAL_CATEGORIES];
        memcpy(pending, fd->se_models_pending, sizeof pending);
        check_social_upkeep(f);
        for (int c = 0; c < SOCIAL_CATEGORIES; c++) fd->se_models_pending[c] = (f + c + slice) & 3;
        check_social_upkeep(f);
        memcpy(fd->se_models_pending, pending, sizeof pending);
    }
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook social_hooks[] = {
    HOOK(0x5b4210, social_calc, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),  // sub esp, 16
    HOOK(0x5b44d0, social_upkeep, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56, 0x57),  // push ebx; push esi; push edi
};
struct hook_module social_module = {"social", social_hooks, HOOK_COUNT(social_hooks), social_sweep, NULL, -1};
