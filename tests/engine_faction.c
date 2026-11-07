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
static int trades, encounters, calls, checks, territory_result;
static int sat_attacks, last_sat[4];
static int32_t rankings[8], scores[8], objectives = 1000, friction_faction = -1, friction_with = -1, ambience_flags;
static int32_t multi_visible, difficulty = 3, gender, plural;
static int ambience_calls, pops, climactic;
static int rand_stub(void) { return 0; }
static int random_stub(int lo, int hi) { return lo; }
static int ambience_stub(int sound) { return ambience_calls++, sound; }
static void __attribute__((thiscall)) redraw_stub(void *win) {}
static int climactic_stub(void) { return climactic; }
static int says_stub(int slot, char *text, int gender, int plural) { return 0; }
static void x_pop_stub(char *label, int unk) { pops++; }
static void check_stub(int f, int i, int u) { checks++; }
static void communicate_stub(int f, int i, int u) { calls++; }
static void trade_stub(int f, int i) { trades++; }
static void encounter_stub(int f, int i, int x, int y, int t, int n) { encounters++; }
static int territory_stub(int f, int i, int flags, int *base, int *count) { *base = 0, *count = 0; return territory_result; }
static void sat_stub(int f, int i, int kind, int base) {
    sat_attacks++, last_sat[0] = f, last_sat[1] = i, last_sat[2] = kind, last_sat[3] = base;
    factions[f].odp_deployed++;
}

static void diplomacy_tests(void) {
    game_faction_defs = defs, game_turn = &turn, game_sunspot_duration = &sunspots, game_factions_alive = &alive;
    game_current_faction = &current, game_net_multiplayer = &multiplayer, game_pbem = &pbem;
    game_diplo_state = diplo_state, game_diplo_state_with = diplo_with, game_diplo_attack_chance = &attack_chance;
    game_diplo_hostility = &hostility, game_diplo_friction = &diplo_friction, game_state = &game_state_;
    game_rules = &rules, game_rand = rand_stub, game_random = random_stub;
    game_faction_rankings = rankings, game_ranking_scores = scores, game_objectives_required = &objectives;
    game_diplo_friction_faction = &friction_faction, game_diplo_friction_with = &friction_with;
    game_ambience_flags = &ambience_flags, game_ambience = ambience_stub, game_multi_win_visible = &multi_visible;
    game_graphic_win_redraw = redraw_stub, game_climactic_battle = climactic_stub, game_difficulty = &difficulty;
    game_parse_says = says_stub, game_x_pop = x_pop_stub, game_parse_gender = &gender, game_parse_plural = &plural;
    game_diplomacy_check = check_stub, game_communicate = communicate_stub, game_enemies_trade_tech = trade_stub;
    game_encounter = encounter_stub, game_territory = territory_stub, game_action_sat_attack = sat_stub;
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
    EXPECT(checks, 0);
    // The next turn ends it; the other's half stays.
    enemy_diplomacy(1);
    EXPECT(factions[1].diplo_agenda[2], 0x40);
    EXPECT(factions[2].diplo_agenda[1], 0x40);

    // AI 2 and AI 1 in a pact trade techs every eighth turn (turn + 1 + 2).
    humans = 0, turn = 5;
    factions[2].diplo_status[1] = TREATY_PACT | TREATY_COMMLINK;
    enemy_diplomacy(2);
    EXPECT(trades, 1);
    EXPECT(factions[2].diplo_status[1] & factions[1].diplo_status[2] & TREATY_UNK_80000000, TREATY_UNK_80000000);
    turn = 6;
    enemy_diplomacy(2);
    EXPECT(trades, 1);
    // A betrayal it was set on, in a pact: friction 20, and it wants to
    // talk, so they meet at once (trading techs) and talking is done.
    factions[2].diplo_status[1] = TREATY_PACT | TREATY_SHALL_BETRAY | TREATY_UNK_800;
    enemy_diplomacy(2);
    EXPECT(factions[2].diplo_friction[1], 20);
    EXPECT(trades, 2);
    EXPECT(factions[2].diplo_status[1], TREATY_PACT | TREATY_UNK_80000000);
    factions[2].diplo_friction[1] = 0;
    // Outside one, against a faction without a target: war in mind.
    factions[2].diplo_status[1] = TREATY_SHALL_BETRAY;
    factions[1].diplo_spoke[2] = -5;
    enemy_diplomacy(2);
    EXPECT(factions[2].diplo_status[1], TREATY_UNK_800 | TREATY_UNK_40);
    EXPECT(factions[2].diplo_merc[1], 50);
    EXPECT(factions[1].diplo_spoke[2], turn - 8);

    // AI 2 and the player (1) with a commlink, on turn 6 just after
    // speaking: no complaint about territory. Friction (0 + 0 + 3) / 5 = 0,
    // even strength, +1 for its fight priority.
    humans = 1 << 1, checks = 0;
    factions[2].diplo_status[1] = TREATY_COMMLINK, factions[2].diplo_merc[1] = 0;
    factions[1].diplo_spoke[2] = turn;
    factions[1].mil_strength = factions[2].mil_strength = 10;
    factions[2].ai_fight = 1;
    enemy_diplomacy(2);
    EXPECT(checks, 1);
    EXPECT(factions[2].diplo_friction[1], 1);
    // Twice its strength: + 2 * 20 / 30 = 1.
    factions[2].mil_strength = 20;
    enemy_diplomacy(2);
    EXPECT(factions[2].diplo_friction[1], 1 + 2);
    // Every fourth turn: a player ranked well above it (3 - (3 + 1) / 2 = 1).
    factions[2].mil_strength = 10, turn = 8, factions[1].diplo_spoke[2] = turn;
    factions[1].ranking = 3;
    enemy_diplomacy(2);
    EXPECT(factions[2].diplo_friction[1], 3 + 2);
    // At war, easing friction isn't applied.
    factions[2].diplo_status[1] |= TREATY_VENDETTA, factions[2].ai_fight = -9;
    enemy_diplomacy(2);
    EXPECT(factions[2].diplo_friction[1], 5);
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

// Treaties, moods and the AI's reckoning.
static void reckoning_tests(void) {
    memset(factions, 0, sizeof factions);
    humans = 0, current = 7, turn = 50, rules = 0;

    // Moods by friction.
    static const int moods[] = {0, 0, 1, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 6, 6, 7, 7, 8, 8};
    for (int x = -1; x <= 21; x++) EXPECT(get_mood(x), moods[x + 1]);
    // Reputation: broken treaties less gifts, 0 to 99.
    factions[1].integrity_blemishes = 5, factions[1].diplo_gifts[2] = 2;
    EXPECT(reputation(1, 2), 3);
    factions[1].diplo_gifts[2] = 9;
    EXPECT(reputation(1, 2), 0);
    factions[1].integrity_blemishes = 200;
    EXPECT(reputation(1, 3), 99);
    // Patience of faction 2 with 1.
    factions[2].diplo_status[1] = TREATY_VENDETTA | TREATY_PACT;
    EXPECT(get_patience(1, 2), 1);
    factions[2].diplo_status[1] = TREATY_PACT;
    EXPECT(get_patience(1, 2), 6);
    factions[2].diplo_status[1] = TREATY_PACT | 0x2000000;
    EXPECT(get_patience(1, 2), 500);
    factions[2].diplo_status[1] = TREATY_TREATY, diplo_friction = 13;
    EXPECT(get_patience(1, 2), 1 - 2 + 3);
    factions[2].diplo_status[1] = 0, diplo_friction = -12;
    EXPECT(get_patience(1, 2), 0 - (-9 / 8) + 3);
    diplo_friction = 0;
    // A loan's goodwill: of 150 / 5 = 30, 10 / 2 + 20 / 3 = 11, then (11 + 4) / 5.
    EXPECT(energy_value(150), 3);
    EXPECT(energy_value(50), 1);
    EXPECT(energy_value(4), 0);
    EXPECT(energy_value(-50), 0);
    EXPECT(energy_value(1000),
           ((10 / 2 + 20 / 3 + 20 / 4 + 20 / 5 + 20 / 6 + 20 / 7 + 20 / 8 + 20 / 9 + 20 / 10 + 20 / 11) + 4) / 5);

    // Friction is kept within 0 to 20; the running total follows its pair.
    cause_friction(3, 4, 15);
    cause_friction(3, 4, 15);
    EXPECT(factions[3].diplo_friction[4], 20);
    friction_faction = 3, friction_with = 4, diplo_friction = 0;
    cause_friction(3, 4, -30);
    EXPECT(factions[3].diplo_friction[4], 0);
    EXPECT(diplo_friction, -30);
    friction_faction = friction_with = -1, diplo_friction = 0;

    // Treaties both ways. A pact brings a treaty and ends a vendetta.
    factions[1].diplo_status[2] = TREATY_VENDETTA | TREATY_UNK_8000 | 0x800000;
    factions[2].diplo_status[1] = TREATY_VENDETTA | TREATY_UNK_40;
    treaty_on(1, 2, TREATY_PACT);
    EXPECT(factions[1].diplo_status[2], TREATY_PACT | TREATY_TREATY);
    EXPECT(factions[2].diplo_status[1], TREATY_PACT | TREATY_TREATY);
    // A vendetta after peace: battles reset, commlink, peace ended; with
    // the player, the war's music.
    factions[1].diplo_combat_count[2] = factions[2].diplo_combat_count[1] = 4;
    current = 2, ambience_calls = 0;
    treaty_on(1, 2, TREATY_VENDETTA);
    EXPECT(factions[1].diplo_status[2], (int)(TREATY_VENDETTA | TREATY_COMMLINK | TREATY_UNK_100 | TREATY_UNK_80000000));
    EXPECT(factions[2].diplo_combat_count[1], 0);
    EXPECT(ambience_flags, 1);
    EXPECT(ambience_calls, 1);
    // A truce with the player ends the music's flag.
    treaty_on(2, 1, 0x4);
    EXPECT(ambience_flags, 0);
    EXPECT(factions[1].diplo_status[2] & (TREATY_VENDETTA | 0x4), 0x4);
    // Off: a treaty takes 0x1000000 and 0x100000 with it; revenge the
    // agenda's 0x400 and 0x800.
    factions[1].diplo_status[2] = factions[2].diplo_status[1] = TREATY_TREATY | 0x1100000 | TREATY_COMMLINK;
    factions[1].diplo_agenda[2] = 0xc00 | 0x1;
    treaty_off(2, 1, TREATY_TREATY | TREATY_WANT_REVENGE);
    EXPECT(factions[1].diplo_status[2], TREATY_COMMLINK);
    EXPECT(factions[2].diplo_status[1], TREATY_COMMLINK);
    EXPECT(factions[1].diplo_agenda[2], 0x1);
    // A vendetta takes 0x8000 and 0x40.
    factions[1].diplo_status[2] = TREATY_VENDETTA | TREATY_UNK_8000 | TREATY_UNK_40 | 0x1;
    treaty_off(1, 2, TREATY_VENDETTA);
    EXPECT(factions[1].diplo_status[2], 0x1);
    // Agendas: 0x20 clears 0x40.
    factions[1].diplo_agenda[2] = factions[2].diplo_agenda[1] = 0x40 | 0x1;
    agenda_on(1, 2, 0x20);
    EXPECT(factions[1].diplo_agenda[2], 0x21);
    EXPECT(factions[2].diplo_agenda[1], 0x21);
    agenda_off(2, 1, 0x1);
    EXPECT(factions[1].diplo_agenda[2], 0x20);
    // One way: 0x40 starts the betrayal's mood.
    set_treaty(3, 4, TREATY_UNK_40 | 0x1, 1);
    EXPECT(factions[3].diplo_status[4], TREATY_UNK_40 | 0x1);
    EXPECT(factions[3].diplo_merc[4], 50);
    EXPECT(factions[4].diplo_status[3], 0);
    set_treaty(3, 4, 0x1, 0);
    EXPECT(factions[3].diplo_status[4], TREATY_UNK_40);

    // Intervention: 3 in a pact with 1 sides with it against the player
    // (2); 4, in a pact with both, doesn't.
    memset(factions, 0, sizeof factions);
    alive = 0xfe, humans = 1 << 2, current = 2, pops = 0;
    factions[3].diplo_status[1] = TREATY_PACT;
    factions[4].diplo_status[1] = TREATY_PACT, factions[4].diplo_status[2] = TREATY_PACT;
    intervention(1, 2);
    EXPECT(factions[3].diplo_status[2] & (TREATY_VENDETTA | TREATY_COMMLINK | TREATY_UNK_40),
           TREATY_VENDETTA | TREATY_COMMLINK | TREATY_UNK_40);
    EXPECT(factions[3].diplo_merc[2], 50);
    EXPECT(factions[2].diplo_spoke[3], turn);
    EXPECT(factions[4].diplo_status[2] & TREATY_VENDETTA, 0);
    EXPECT(pops, 1);
    // Between AIs, it is only told.
    memset(factions, 0, sizeof factions);
    humans = 0, current = 7;
    factions[3].diplo_status[1] = TREATY_PACT;
    intervention(1, 2);
    EXPECT(factions[3].diplo_status[2], 0);

    // The player (1) at the top of the rankings with 6 bases on turn 220:
    // too many on a hard level; on the easiest only with `aggressive`.
    humans = 1 << 1, rankings[7] = 1, rankings[6] = 2, turn = 220;
    factions[1].base_count = 6, factions[1].diff_level = 4;
    EXPECT(great_beelzebub(1, 0), 1);
    factions[1].diff_level = 1;
    EXPECT(great_beelzebub(1, 0), 0);
    EXPECT(great_beelzebub(1, 1), 1);
    factions[1].base_count = 4;
    EXPECT(great_beelzebub(1, 1), 0);
    // great_satan: late enough (100 + 50 per level below 5), and strong
    // enough against the second: two thirds on a hard level.
    factions[1].base_count = 6, factions[1].diff_level = 3;
    scores[1] = 30, scores[2] = 20;
    EXPECT(great_satan(1, 0), 1);
    scores[2] = 21;
    EXPECT(great_satan(1, 0), 0);
    turn = 200, scores[2] = 20;
    EXPECT(great_satan(1, 0), 0);
    // Intense rivalry: from turn 101, at four fifths.
    rules = RULES_INTENSE_RIVALRY, turn = 101, scores[2] = 24;
    EXPECT(great_satan(1, 0), 1);
    scores[2] = 25;
    EXPECT(great_satan(1, 0), 0);
    rules = 0, turn = 50;
    // No diplomatic victory: nobody nears it.
    EXPECT(aah_ooga(1, -1), 0);

    // at_climax: not for humans, on the easiest level or before the player
    // nears the end; then a market cornered by a faction outside a pact.
    memset(factions, 0, sizeof factions);
    humans = 1 << 1, climactic = 1;
    factions[3].corner_market_turn = turn + 5;
    EXPECT(at_climax(1), 0);
    EXPECT(at_climax(2), 1);
    climactic = 0;
    EXPECT(at_climax(2), 0);
    climactic = 1, difficulty = 0;
    EXPECT(at_climax(2), 0);
    difficulty = 3;
    rules = RULES_VICTORY_COOPERATIVE, factions[2].diplo_status[3] = TREATY_PACT;
    factions[3].corner_market_turn = 0, factions[4].corner_market_turn = turn + 1;
    factions[2].diplo_status[4] = TREATY_PACT;
    EXPECT(at_climax(2), 0);
    rules = 0;
    EXPECT(at_climax(2), 1);
    climactic = 0, humans = 0;
}

// wants_to_attack on made-up regions.
static void attack_tests(void) {
    memset(factions, 0, sizeof factions);
    memset(defs, 0, sizeof defs);
    humans = 0, turn = 50, rules = 0, base_count = 0;
    for (int i = 0; i < 8; i++) rankings[i] = i;
    // Revenge, or both alien: yes. A single base: no.
    factions[1].diplo_status[2] = TREATY_WANT_REVENGE;
    EXPECT(wants_to_attack(1, 2, 0), 1);
    factions[1].diplo_status[2] = 0;
    defs[1].rule_flags = defs[2].rule_flags = 0x80;
    EXPECT(wants_to_attack(1, 2, 0), 1);
    defs[1].rule_flags = defs[2].rule_flags = 0;
    factions[1].base_count = 1;
    EXPECT(wants_to_attack(1, 2, 0), 0);
    // A peaceful AI: no, unless against the leader (7).
    factions[1].base_count = 5, factions[1].ai_fight = -1;
    EXPECT(wants_to_attack(1, 2, 0), 0);
    // Against the leader, sharing no region: c = 2 (peaceful) + 1 (no
    // vendetta) + 1 (weapons no better than armor) > 0, so the odds count:
    // 6 * 16 * 0 / (16 * 1) = 0 < c + 6.
    EXPECT(wants_to_attack(1, 7, 0), 0);
    // A fierce AI with better weapons: c = -2 + 1 - 1 = -2: yes.
    factions[1].ai_fight = 1, factions[1].best_weapon_value = 5, factions[2].best_armor_value = 2;
    EXPECT(wants_to_attack(1, 2, 0), 1);
    // In a region they share where 1 has its bases: its strength 30
    // against 2's 40 is too little.
    factions[1].region_force_rating[5] = 30, factions[1].region_total_bases[5] = 3;
    factions[2].region_force_rating[5] = 40, factions[2].region_total_combat_units[5] = 4;
    factions[2].region_total_bases[5] = 2;
    EXPECT(wants_to_attack(1, 2, 0), 0);
    // With 2 units more and an ally's 40 (a quarter), enough. ours =
    // (30 + 20) + (30 + 20), theirs = 1 + 4 + 20; c = -2 + 1 - 1 - 1 (the
    // ally is no threat) = -3: 6 * 16 * 100 / (16 * 25) = 24 >= 3.
    factions[1].region_total_combat_units[5] = 2, factions[3].region_force_rating[5] = 40;
    EXPECT(wants_to_attack(1, 2, 3), 1);
    // Where it has few of its bases (it has 12 in another region), and
    // not its headquarters' region, strength there isn't compared.
    factions[1].region_total_combat_units[5] = 0, factions[3].region_force_rating[5] = 0;
    EXPECT(wants_to_attack(1, 2, 3), 0);
    factions[1].region_total_bases[9] = 12;
    EXPECT(wants_to_attack(1, 2, 3), 1);
    base_count = 3;
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
    reckoning_tests();
    attack_tests();
    if (failures) return 1;
    printf("engine_faction: all tests passed\n");
    return 0;
}
