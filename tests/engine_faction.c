// Unit test for the reimplemented probe and energy market costs and the
// AI's diplomacy, on made-up data: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/faction.h"
#include "../src/engine/game.h"

static int failures;
#define EXPECT(expr, want)                                                    \
    do {                                                                      \
        int got_ = (expr);                                                    \
        if (got_ != (want)) {                                                 \
            printf("FAIL %s = %d, expected %d\n", #expr, got_, (int)(want)); \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static struct faction_data factions[FACTION_COUNT];
static struct base bases[3];
static int32_t base_count = 3, width = 40, half_width = 20, projects[SP_COUNT];
static uint32_t flat = 0;
static uint8_t humans;
static int defenders, encrypted;

static int veh_at_stub(int x, int y) { return 0; }
static int stack_fix_stub(int veh) { return veh; }
static int stack_check_stub(int veh, int type, int c1, int c2, int c3) {
    return type == 6 ? encrypted : c1 == 1 ? defenders : 0;
}

// Diplomacy's stand-ins record what they're asked to do.
static struct faction_def defs[FACTION_COUNT];
static int32_t turn, sunspots, current = 1, multiplayer, pbem, diplo_state[FACTION_COUNT];
static int32_t diplo_with[FACTION_COUNT][FACTION_COUNT], attack_chance, hostility, diplo_friction;
static uint32_t game_state_, rules;
static uint8_t alive;
static int frictions, last_friction[3], treaties, last_treaty[3], trades, encounters, calls, checks, territory_result;
static int sat_attacks, last_sat[4], satan;
static int rand_stub(void) { return 0; }
static int random_stub(int lo, int hi) { return lo; }
static void friction_stub(int f, int i, int v) { frictions++, last_friction[0] = f, last_friction[1] = i, last_friction[2] = v; }
static void treaty_stub(int f, int i, uint32_t s) { treaties++, last_treaty[0] = f, last_treaty[1] = i, last_treaty[2] = (int)s; }
static int reputation_stub(int f, int i) { return 0; }
static void check_stub(int f, int i, int u) { checks++; }
static void communicate_stub(int f, int i, int u) { calls++; }
static void trade_stub(int f, int i) { trades++; }
static void encounter_stub(int f, int i, int x, int y, int t, int n) { encounters++; }
static int territory_stub(int f, int i, int flags, int *base, int *count) { *base = 0, *count = 0; return territory_result; }
static void sat_stub(int f, int i, int kind, int base) {
    sat_attacks++, last_sat[0] = f, last_sat[1] = i, last_sat[2] = kind, last_sat[3] = base;
    factions[f].odp_deployed++;
}
static int satan_stub(int f, int aggressive) { return satan; }
static int ooga_stub(int f, int pact) { return 0; }

static void diplomacy_tests(void) {
    game_faction_defs = defs, game_turn = &turn, game_sunspot_duration = &sunspots, game_factions_alive = &alive;
    game_current_faction = &current, game_net_multiplayer = &multiplayer, game_pbem = &pbem;
    game_diplo_state = diplo_state, game_diplo_state_with = diplo_with, game_diplo_attack_chance = &attack_chance;
    game_diplo_hostility = &hostility, game_diplo_friction = &diplo_friction, game_state = &game_state_;
    game_rules = &rules, game_rand = rand_stub, game_random = random_stub;
    game_cause_friction = friction_stub, game_treaty_on = treaty_stub, game_reputation = reputation_stub;
    game_diplomacy_check = check_stub, game_communicate = communicate_stub, game_enemies_trade_tech = trade_stub;
    game_encounter = encounter_stub, game_territory = territory_stub, game_action_sat_attack = sat_stub;
    game_great_satan = satan_stub, game_aah_ooga = ooga_stub;
    memset(factions, 0, sizeof factions);
    for (int f = 0; f < FACTION_COUNT; f++) factions[f].target_x = -1;
    for (int i = 0; i < SP_COUNT; i++) projects[i] = -1;
    for (int f = 0; f < FACTION_COUNT; f++) defs[f].ideology_category = -1;
    alive = 1 << 1 | 1 << 2;

    // A human's turn moves its agenda flag on and does nothing else.
    humans = 1 << 1;
    factions[1].diplo_agenda[2] = 0x20;
    enemy_diplomacy(1);
    EXPECT(factions[1].diplo_agenda[2], 0x60);
    EXPECT(factions[2].diplo_agenda[1], 0x40);
    EXPECT(frictions + checks, 0);
    // The next turn ends it; the other's half stays.
    enemy_diplomacy(1);
    EXPECT(factions[1].diplo_agenda[2], 0x40);
    EXPECT(factions[2].diplo_agenda[1], 0x40);

    // AI 2 and AI 1 in a pact trade techs every eighth turn (turn + 1 + 2).
    humans = 0, turn = 5;
    factions[2].diplo_status[1] = TREATY_PACT | TREATY_COMMLINK;
    enemy_diplomacy(2);
    EXPECT(trades, 1);
    EXPECT(last_treaty[2], (int)TREATY_UNK_80000000);
    turn = 6;
    enemy_diplomacy(2);
    EXPECT(trades, 1);
    // A betrayal it was set on, in a pact: friction 20, and it wants to talk.
    factions[2].diplo_status[1] = TREATY_PACT | TREATY_SHALL_BETRAY | TREATY_UNK_800;
    enemy_diplomacy(2);
    EXPECT(last_friction[2], 20);
    EXPECT(last_treaty[2], TREATY_WANT_TO_TALK);
    EXPECT(factions[2].diplo_status[1], TREATY_PACT);
    // Outside one, against a faction without a target: war in mind.
    factions[2].diplo_status[1] = TREATY_SHALL_BETRAY;
    factions[1].diplo_spoke[2] = -5;
    enemy_diplomacy(2);
    EXPECT(factions[2].diplo_status[1], TREATY_UNK_800 | TREATY_UNK_40);
    EXPECT(factions[2].diplo_merc[1], 50);
    EXPECT(factions[1].diplo_spoke[2], turn - 8);

    // AI 2 and the player (1) with a commlink, on turn 6 just after
    // speaking: no complaint about territory. Friction (0 + 0 + 3) / 5 = 0,
    // even strength, +1 as the player is far ahead, +1 for its fight
    // priority.
    humans = 1 << 1, frictions = checks = 0;
    factions[2].diplo_status[1] = TREATY_COMMLINK, factions[2].diplo_merc[1] = 0;
    factions[1].diplo_spoke[2] = turn;
    factions[1].mil_strength = factions[2].mil_strength = 10;
    factions[2].ai_fight = 1, satan = 1;
    enemy_diplomacy(2);
    EXPECT(checks, 1);
    EXPECT(frictions, 1);
    EXPECT(last_friction[2], 2);
    // Twice its strength: + 2 * 20 / 30 = 1.
    factions[2].mil_strength = 20;
    enemy_diplomacy(2);
    EXPECT(last_friction[2], 3);
    // Every fourth turn: a player ranked well above it (3 - (3 + 1) / 2 = 1).
    factions[2].mil_strength = 10, turn = 8, factions[1].diplo_spoke[2] = turn;
    factions[1].ranking = 3;
    enemy_diplomacy(2);
    EXPECT(last_friction[2], 2 + 1);
    // At war, easing friction isn't applied.
    factions[2].diplo_status[1] |= TREATY_VENDETTA, factions[2].ai_fight = -9, frictions = 0;
    enemy_diplomacy(2);
    EXPECT(frictions, 0);
    factions[2].diplo_status[1] = TREATY_COMMLINK, factions[2].ai_fight = 0, factions[1].ranking = 0;

    // Spare orbital defense pods attack a faction at war: its mineral
    // satellites, one pod at a time.
    humans = 0, alive |= 1 << 3;
    factions[2].satellites[3] = 3;
    factions[2].diplo_status[3] = TREATY_VENDETTA;
    factions[3].satellites[1] = 1;
    enemy_diplomacy(2);
    EXPECT(sat_attacks, 3);
    EXPECT(last_sat[1], 3);
    EXPECT(last_sat[2], 1);
    EXPECT(last_sat[3], base_count);
    // Not while their busters need them.
    factions[2].odp_deployed = 0, sat_attacks = 0;
    factions[3].planet_busters = 3;
    enemy_diplomacy(2);
    EXPECT(sat_attacks, 0);
}

int main(void) {
    game_factions = factions, game_bases = bases, game_base_count = &base_count;
    game_map_width = &width, game_map_half_width = &half_width, game_map_flat = &flat;
    game_human_factions = &humans, game_secret_projects = projects;
    game_veh_at = veh_at_stub, game_stack_fix = stack_fix_stub, game_stack_check = stack_check_stub;
    for (int i = 0; i < SP_COUNT; i++) projects[i] = -1;

    // Faction 2's headquarters at (36, 4), its base at (2, 10): across the
    // seam 6 squares in x, 6 in y.
    bases[0] = (struct base){.x = 36, .y = 4, .faction = 2, .faction_former = 2};
    bases[0].facilities_built[0] = 1 << 1;
    bases[1] = (struct base){.x = 2, .y = 10, .faction = 2, .faction_former = 2, .pop_size = 4};
    bases[2] = (struct base){.x = 10, .y = 10, .faction = 1, .faction_former = 1, .pop_size = 3};
    factions[2].energy_credits = 300;
    factions[1].mind_control_total = 8;
    int dist = 6;  // rings out on the diagonal grid
    defenders = 2, encrypted = 0;
    // (defenders + past control / 4 + size) * ((reserves + 1200) / (distance + 4))
    EXPECT(mind_control(1, 1, 0), (2 + 2 + 4) * ((300 + 1200) / (dist + 4)));
    // Polymorphic Encryption: defenders count again.
    encrypted = 1;
    EXPECT(mind_control(1, 1, 0), (4 + 2 + 4) * ((1500) / (dist + 4)));
    encrypted = 0;
    // No headquarters: distance 12. Its own headquarters is right there:
    // a probe can't, the market pays as for distance 1.
    bases[0].facilities_built[0] = 0;
    EXPECT(mind_control(1, 1, 0), 8 * (1500 / 16));
    bases[0].facilities_built[0] = 1 << 1;
    EXPECT(mind_control(0, 1, 0), -1);
    EXPECT(mind_control(0, 1, 1), (2 + 2 + 0) * (1500 / 5));
    // A Genejack Factory doubles the distance, a creche halves it.
    bases[1].facilities_built[22 / 8] = 1 << (22 % 8);
    EXPECT(mind_control(1, 1, 0), 8 * (1500 / (dist * 2 + 4)));
    bases[1].facilities_built[22 / 8] = 0;
    // A pact doubles a probe's cost and halves the market's; commerce
    // scales the market's; riots halve, a golden age doubles, a grudge
    // adds half, an atrocity doubles.
    int base_cost = 8 * (1500 / (dist + 4));
    factions[1].diplo_status[2] = 0x1;
    EXPECT(mind_control(1, 1, 0), base_cost * 2);
    factions[2].tech_commerce_bonus = 1;
    EXPECT(mind_control(1, 1, 1), base_cost / 2 * 2 / 1);
    factions[1].diplo_status[2] = 0, factions[2].tech_commerce_bonus = 0;
    bases[1].state = 0x2;
    EXPECT(mind_control(1, 1, 0), base_cost / 2);
    bases[1].state = 0x4;
    EXPECT(mind_control(1, 1, 0), base_cost * 2);
    bases[1].state = 0;
    factions[2].diplo_status[1] = 0x20;
    EXPECT(mind_control(1, 1, 0), base_cost + base_cost / 2);
    factions[2].diplo_status[1] = 0x40020;
    EXPECT(mind_control(1, 1, 0), base_cost * 2);
    factions[2].diplo_status[1] = 0;
    // Against a human on a hard level, the AI pays less.
    humans = 1 << 2, factions[2].diff_level = 5;
    EXPECT(mind_control(1, 1, 0), base_cost * 3 / 5);
    humans = 0;

    // The market: the other factions' bases, at least 1000; none from a
    // faction that has surrendered to it in a pact.
    int sum = mind_control(0, 1, 1) + mind_control(1, 1, 1);
    EXPECT(corner_market(1), sum < 1000 ? 1000 : sum);
    factions[2].diplo_status[1] = 0x2000001;
    EXPECT(corner_market(1), 1000);
    factions[2].diplo_status[1] = 0;
    // Bases of its own don't count.
    bases[0].faction = bases[1].faction = 1;
    EXPECT(corner_market(1), 1000);

    diplomacy_tests();
    if (failures) return 1;
    printf("engine_faction: all tests passed\n");
    return 0;
}
