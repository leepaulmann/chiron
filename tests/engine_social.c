// Unit test for the reimplemented social engineering, on made-up tables:
// no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/social.h"

static int failures;
#define EXPECT(expr, want)                                                    \
    do {                                                                      \
        int got_ = (expr);                                                    \
        if (got_ != (want)) {                                                 \
            printf("FAIL %s = %d, expected %d\n", #expr, got_, (int)(want)); \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static uint8_t table[SOCIAL_CATEGORIES * SOCIAL_CATEGORY_SIZE];
static struct faction_data factions[FACTION_COUNT];
static struct faction_def defs[FACTION_COUNT];
static struct base bases[2];
static int32_t projects[SP_COUNT], base_count = 1;
static struct map_tile *no_map;
static int32_t zero;  // no squares: no temple
static int32_t preqs[SOCIAL_CATEGORIES * SOCIAL_PREQ_STRIDE];
static uint32_t state;
static uint8_t humans;
static int32_t ranked[2] = {1, 1};
static int pumps, sets, designs;
static void pump_stub(void) { pumps++; }
static int ooga_stub(int faction, int pact) { return 0; }
static void set_stub(int faction) { sets++; }
static void designs_stub(int faction) { designs++; }

static void set_effect(int cat, int model, int effect, int32_t v) {
    memcpy(table + cat * SOCIAL_CATEGORY_SIZE + model * SOCIAL_MODEL_SIZE + 4 * effect, &v, sizeof v);
}

int main(void) {
    game_social_effects = table, game_factions = factions, game_faction_defs = defs;
    game_secret_projects = projects, game_bases = bases, game_base_count = &base_count, game_map = &no_map;
    game_map_width = game_map_height = &zero;
    for (int i = 0; i < SP_COUNT; i++) projects[i] = -1;
    bases[0].faction = 1;
    // Values: Power (model 1) is MORALE +2, INDUSTRY -2; Future:
    // Cybernetic (model 1) EFFIC +2, PLANET -2.
    set_effect(2, 1, SE_MORALE, 2), set_effect(2, 1, SE_INDUSTRY, -2);
    set_effect(3, 1, SE_EFFIC, 2), set_effect(3, 1, SE_PLANET, -2);
    int32_t models[4] = {0, 0, 1, 1}, e[SE_COUNT];
    social_calc(models, e, 1, 0, 1);
    EXPECT(e[SE_MORALE], 2);
    EXPECT(e[SE_INDUSTRY], -2);
    EXPECT(e[SE_PLANET], -2);
    // The Cloning Vats cancel Power's penalty; the Network Backbone
    // Cybernetic's.
    projects[24] = 0;
    social_calc(models, e, 1, 0, 1);
    EXPECT(e[SE_INDUSTRY], 0);
    EXPECT(e[SE_PLANET], -2);
    projects[24] = -1, projects[21] = 0;
    social_calc(models, e, 1, 0, 1);
    EXPECT(e[SE_PLANET], 0);
    projects[21] = -1;
    // IMPUNITY for Power negates its penalty, PENALTY doubles it.
    defs[1].bonus_count = 1, defs[1].bonus_id[0] = BONUS_IMPUNITY, defs[1].bonus_val1[0] = 2, defs[1].bonus_val2[0] = 1;
    social_calc(models, e, 1, 0, 1);
    EXPECT(e[SE_INDUSTRY], 0);
    defs[1].bonus_id[0] = BONUS_PENALTY;
    social_calc(models, e, 1, 0, 1);
    EXPECT(e[SE_INDUSTRY], -4);
    // In full: base values added, Ascetic Virtues POLICE +1, then ROBUST
    // halves a negative and IMMUNITY clears one.
    factions[1].se_base[SE_POLICE] = 1, factions[1].se_base[SE_INDUSTRY] = -1;
    projects[13] = 0;
    defs[1].bonus_count = 2, defs[1].bonus_id[0] = BONUS_ROBUST, defs[1].bonus_val1[0] = SE_INDUSTRY;
    defs[1].bonus_id[1] = BONUS_IMMUNITY, defs[1].bonus_val1[1] = SE_PLANET;
    social_calc(models, e, 1, 0, 0);
    EXPECT(e[SE_POLICE], 2);
    EXPECT(e[SE_INDUSTRY], -1);  // -2 - 1 = -3, halved towards zero
    EXPECT(e[SE_PLANET], 0);
    // Upkeep: the pending models take effect.
    factions[1].se_models_pending[2] = 1, factions[1].se_upheaval_paid = 7;
    social_upkeep(1);
    EXPECT(factions[1].se_models[2], 1);
    EXPECT(factions[1].se_upheaval_paid, 0);
    EXPECT(factions[1].se[SE_POLICE], 2);
    EXPECT(factions[1].se_2[SE_MORALE], 2);
    // The AI's choice, for faction 2 (no bases): Economics model 2 brings
    // ECONOMY +2, worth (20 + 2 * 2 - 4) / (no threat + 1), doubled for no
    // threat; switching one category costs 2^3 * 3 = 24, a third of it
    // counted. Nothing else scores.
    game_social_preqs = preqs, game_state = &state, game_human_factions = &humans, game_ranked_faction = ranked;
    game_do_all_non_input = pump_stub, game_aah_ooga = ooga_stub, game_social_set = set_stub;
    game_consider_designs = designs_stub;
    for (int i = 0; i < SOCIAL_CATEGORIES * SOCIAL_PREQ_STRIDE; i++) preqs[i] = -1;
    defs[2].ideology_category = defs[2].anti_ideology_category = defs[2].ideology_effect = -1;
    set_effect(1, 2, SE_ECONOMY, 2);
    struct faction_data *f = &factions[2];
    f->unk_7f4 = 20, f->energy_credits = 100;
    EXPECT(social_upheaval(2, (int32_t[]){0, 2, 0, 0}), 24);
    defs[2].rule_flags = RULE_FLAG_ALIEN;
    EXPECT(social_upheaval(2, (int32_t[]){1, 2, 0, 0}), 81 + 40);
    defs[2].rule_flags = 0;
    int32_t advice[4] = {9, 9, 9, 9};
    social_ai(2, 0, 0, 0, 0, advice);
    EXPECT(advice[0], 0);
    EXPECT(advice[1], 2);
    EXPECT(advice[3], 0);
    EXPECT(pumps, 256);
    social_ai(2, 0, 0, 0, 0, NULL);
    EXPECT(f->se_models_pending[1], 2);
    EXPECT(f->energy_credits, 76);
    EXPECT(f->se_upheaval_paid, 24);
    EXPECT(sets, 1);
    EXPECT(designs, 1);
    // Chosen already: nothing to do.
    social_ai(2, 0, 0, 0, 0, NULL);
    EXPECT(sets, 1);
    // Out of reach without its tech (-2 is never known), or ruled out by
    // the leader: the choice stays put.
    f->se_models_pending[1] = 0;
    preqs[1 * SOCIAL_PREQ_STRIDE + 2] = -2;
    EXPECT(society_avail(1, 2, 2), 0);
    social_ai(2, 0, 0, 0, 0, advice);
    EXPECT(advice[1], 0);
    preqs[1 * SOCIAL_PREQ_STRIDE + 2] = -1;
    defs[2].anti_ideology_category = 1, defs[2].anti_ideology_model = 2;
    EXPECT(society_avail(1, 2, 2), 0);
    EXPECT(society_avail(1, 1, 2), 1);
    social_ai(2, 0, 0, 0, 0, advice);
    EXPECT(advice[1], 0);
    defs[2].anti_ideology_category = -1;
    // An effect it is set against rules a model out too.
    defs[2].ideology_effect = SE_ECONOMY;
    set_effect(1, 2, SE_ECONOMY, -2);
    social_ai(2, 0, 0, 0, 0, advice);
    EXPECT(advice[1], 0);
    // Humans choose for themselves, and a negative `growth` asks for
    // nothing.
    humans = 1 << 2;
    social_ai(2, 0, 0, 0, 0, NULL);
    EXPECT(sets, 1);
    advice[1] = 7;
    social_ai(2, -1, 0, 0, 0, advice);
    EXPECT(advice[1], 7);
    if (failures) return 1;
    printf("engine_social: all tests passed\n");
    return 0;
}
