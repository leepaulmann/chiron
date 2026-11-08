// Unit test for the reimplemented scores, rankings and scenario objectives,
// on a small made-up game: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/score.h"

static int failures;
#define EXPECT(expr, want)                                                    \
    do {                                                                      \
        int got_ = (expr);                                                    \
        if (got_ != (want)) {                                                 \
            printf("FAIL %s = %d, expected %d\n", #expr, got_, (int)(want)); \
            failures++;                                                       \
        }                                                                     \
    } while (0)

enum { W = 8, H = 6 };
static struct map_tile tiles[W / 2 * H], *map = tiles;
static int32_t width = W, height = H, half_width = W / 2;
static struct faction_data factions[FACTION_COUNT];
static struct faction_def defs[FACTION_COUNT];
static struct base bases[3];
static struct veh vehs[3];
static struct rules_tech techs[TECH_COUNT];
static uint8_t achieved[TECH_COUNT];
static struct unit_proto protos[PROTO_COUNT];
static struct rules_weapon weapons[4] = {{.offense = 0}, {.offense = 4}, {.offense = 99}, {.offense = -1}};
static int32_t base_count = 3, veh_count = 3, projects[SP_COUNT], turn = 100, start_year = 2000;
static int32_t required = 9000, sudden = 9000, points, victory, scn_facility = -1, native_life = 1;
static int32_t ranking_scores[FACTION_COUNT], faction_rankings[FACTION_COUNT], ranked[2];
static int16_t might[1000][FACTION_COUNT];
static int32_t ascent_points, ascent_pop[FACTION_COUNT], commerce[FACTION_COUNT];
static int32_t current_base = -1, computed_base = 0, base_bits_reporting;
static struct base *current_base_ptr;
static uint32_t rules, state;
static uint8_t alive = 0x0f, humans;

static int satan, climax, treaties, last_treaty[4], rand_value;
static int satan_stub(int f, int aggressive) { return satan; }
static int climax_stub(void) { return climax; }
static int reputation_stub(int f, int other) { return 1; }
static int rand_stub(void) { return rand_value; }
static void set_treaty_stub(int f, int other, uint32_t status, int on) {
    treaties++, last_treaty[0] = f, last_treaty[1] = other, last_treaty[2] = (int)status, last_treaty[3] = on;
}

static void setup(void) {
    game_map = &map, game_map_width = &width, game_map_height = &height, game_map_half_width = &half_width;
    game_factions = factions, game_faction_defs = defs, game_bases = bases, game_base_count = &base_count;
    game_vehs = vehs, game_veh_count = &veh_count, game_tech = techs, game_tech_achieved = achieved;
    game_protos = protos, game_weapons = weapons, game_secret_projects = projects, game_turn = &turn;
    game_current_turn = &turn, game_start_year = &start_year, game_rules = &rules, game_state = &state;
    game_factions_alive = &alive, game_human_factions = &humans;
    game_objectives_required = &required, game_objectives_sudden_death = &sudden, game_objective_points = &points;
    game_victory_points = &victory, game_scn_victory_facility = &scn_facility, game_native_life = &native_life;
    game_ranking_scores = ranking_scores, game_faction_rankings = faction_rankings, game_ranked_faction = ranked;
    game_turn_might = might, game_score_ascent_points = &ascent_points, game_score_ascent_pop = ascent_pop;
    game_base_commerce_import = commerce, game_current_base_id = &current_base, game_current_base = &current_base_ptr;
    game_computed_base_id = &computed_base, game_base_bits_reporting = &base_bits_reporting;
    game_great_satan = satan_stub, game_climactic_battle = climax_stub, game_reputation = reputation_stub;
    game_rand = rand_stub, game_set_treaty = set_treaty_stub;
    for (int i = 0; i < SP_COUNT; i++) projects[i] = -1;
    for (int i = 0; i < W / 2 * H; i++) tiles[i].territory = -1;
    // Bases: 0 of faction 1 (size 3, with headquarters), 1 of faction 2
    // (size 4, headquarters), 2 of faction 3 (size 1).
    static const int at[3][4] = {{2, 0, 1, 3}, {4, 2, 2, 4}, {6, 4, 3, 1}};
    for (int i = 0; i < 3; i++) {
        bases[i].x = (int16_t)at[i][0], bases[i].y = (int16_t)at[i][1];
        bases[i].faction = (uint8_t)at[i][2], bases[i].pop_size = (int8_t)at[i][3];
        tiles[at[i][0] / 2 + at[i][1] * W / 2].bits |= MAP_BASE;
    }
    bases[0].facilities_built[0] = bases[1].facilities_built[0] = 1 << FAC_HEADQUARTERS;
    // Units: an objective unit of faction 1 in base 1, another of faction 1
    // and an objective unit of faction 2.
    vehs[0].faction = 1, vehs[0].flags = 0x20, vehs[0].x = 4, vehs[0].y = 2;
    vehs[1].faction = 1;
    vehs[2].faction = 2, vehs[2].flags = 0x20;
    // Faction 1 owns the first six squares: a road, farm and mine, then a
    // sensor (a river doesn't count).
    for (int i = 0; i < 6; i++) tiles[i].territory = 1;
    tiles[0].bits |= MAP_ROAD | MAP_FARM | MAP_MINE | MAP_RIVER;
    tiles[1].bits |= MAP_SENSOR;
    factions[1].objective_bonus = 2, factions[1].transcendent_techs = 1, factions[1].energy_credits = 50;
    factions[1].pop_total = 5, factions[2].pop_total = 2;
    for (int t = 0; t < 3; t++) achieved[t] = 1 << 1;
    for (int t = 0; t < TECH_COUNT; t++) techs[t].growth = techs[t].tech = techs[t].wealth = techs[t].power = 1;
}

static void year_and_rating(void) {
    EXPECT(game_year(10), 2010);
    char buf[32] = "Year ";
    say_year(buf);
    EXPECT(strcmp(buf, "Year 2100"), 0);
    // 6 tenths of a percent at the lowest level; 1 more a level, and from
    // level 3 (level - 1) / 2 more.
    factions[1].diff_level = 0;
    EXPECT(get_rating(1, 1600), 60);
    factions[1].diff_level = 5;  // 5 + 1 + 1 + 2
    EXPECT(get_rating(1, 160), 15);
    rules = RULES_INTENSE_RIVALRY;
    EXPECT(get_rating(1, 160), 16);
    EXPECT(get_rating(1, -159), -15);  // towards zero
    rules = 0;
}

static void objectives(void) {
    EXPECT(is_objective(1), 0);
    bases[1].event = 0x8000;
    EXPECT(is_objective(1), 1);
    bases[1].event = 0;
    rules = 0x20000000;  // all bases
    EXPECT(is_objective(2), 1);
    rules = 0x40000000, projects[6] = 1;  // secret projects
    EXPECT(is_objective(1), 1);
    EXPECT(is_objective(0), 0);
    rules = 0, projects[6] = -1;
    state = 0x4000000, scn_facility = FAC_HEADQUARTERS;
    EXPECT(is_objective(0), 1);
    EXPECT(is_objective(2), 0);
    scn_facility = 65;  // past the facilities that count
    EXPECT(is_objective(0), 0);
    state = 0, scn_facility = -1;

    // The bonus and the objective unit anywhere.
    EXPECT(num_objectives(1, 0), 3);
    EXPECT(num_objectives(5, 0), 0);  // not alive
    // The unit counts only in an objective base of a cooperative partner.
    rules = 0x8000000;
    EXPECT(num_objectives(1, 0), 2);
    bases[1].event = 0x8000;
    EXPECT(num_objectives(1, 0), 2);
    factions[1].diplo_status[2] = TREATY_PACT, rules |= RULES_VICTORY_COOPERATIVE;
    EXPECT(num_objectives(1, 0), 3);
    bases[1].event = 0;
    rules = 0x10000000 | RULES_VICTORY_COOPERATIVE;  // or in its headquarters
    EXPECT(num_objectives(1, 0), 3);
    // With pacts, a partner's own: faction 2's unit is anywhere-only, so
    // in its headquarters rule it doesn't count, but its objective base does.
    rules = RULES_VICTORY_COOPERATIVE, bases[1].event = 0x8000;
    EXPECT(num_objectives(2, 0), 2);
    EXPECT(num_objectives(1, 1), 5);
    EXPECT(num_objectives(1, 0), 3);
    rules = 0, bases[1].event = 0, factions[1].diplo_status[2] = 0;
    // What the game state counts.
    state = 0x20000000;  // citizens
    EXPECT(num_objectives(1, 0), 6);
    state = 0x40000000;  // techs and transcendent techs
    EXPECT(num_objectives(1, 0), 7);
    state = 0x80000000u;  // energy
    EXPECT(num_objectives(1, 0), 53);
    state = 0x4000000, scn_facility = FAC_HEADQUARTERS;  // bases with the facility
    EXPECT(num_objectives(1, 0), 4);
    state = 0x2000000;  // improvements on its squares
    EXPECT(num_objectives(1, 0), 7);
    state = 0x8000;  // squares
    EXPECT(num_objectives(1, 0), 9);
    state = 0, scn_facility = -1;
    rules = 0x40000000, projects[5] = 0, projects[6] = 1;  // secret projects
    EXPECT(num_objectives(1, 0), 4);
    rules = 0, projects[5] = projects[6] = -1;

    // Most objectives: faction 1 has 3, faction 2 has 1 (its unit), 3 none.
    int who = -1, tied = -1;
    EXPECT(most_objectives(&who, &tied), 3);
    EXPECT(who, 1);
    EXPECT(tied, 0);
    // A tie with faction 2: counted, unless a cooperative pact.
    factions[2].objective_bonus = 2;
    EXPECT(most_objectives(&who, &tied), 3);
    EXPECT(who, 2);
    EXPECT(tied, 1);
    factions[2].diplo_status[1] = TREATY_PACT, rules = RULES_VICTORY_COOPERATIVE;
    EXPECT(most_objectives(NULL, &tied), 3 + 3);  // with pacts, each counts the other's
    factions[2].diplo_status[1] = 0, rules = 0;
    EXPECT(most_objectives(&who, NULL), 3);
    factions[2].objective_bonus = 0;

    // The Voice of Planet, and design 102 first in a queue.
    EXPECT(ascending(1), 0);
    bases[0].queue[0] = FAC_ASCENT;
    EXPECT(transcending(1), 0);
    projects[SP_VOICE_OF_PLANET] = 2;
    EXPECT(ascending(3), 1);
    EXPECT(transcending(1), 1);
    EXPECT(transcending(2), 0);
    bases[0].queue[0] = -FAC_ASCENT;
    EXPECT(transcending(1), 0);
    projects[SP_VOICE_OF_PLANET] = -1;
}

static void ranking(void) {
    // Basic design 0 (unarmed), 64 (weapon 4), 65 (99: not the best
    // weapon), 66 (psi).
    protos[0].preq_tech = TECH_NONE, protos[0].cost = 1;
    protos[64].flags = PROTO_FLAG_ACTIVE | PROTO_FLAG_BUILT, protos[64].weapon = 1, protos[64].cost = 10;
    protos[65].flags = PROTO_FLAG_ACTIVE | PROTO_FLAG_BUILT, protos[65].weapon = 2, protos[65].cost = 5;
    protos[66].flags = PROTO_FLAG_ACTIVE, protos[66].weapon = 3, protos[66].cost = 8;
    projects[5] = 0;
    factions[1].units_active[0] = 4, factions[1].units_active[64] = 3, factions[1].units_active[65] = 251;
    factions[1].units_active[66] = 1, factions[2].units_active[64] = 1;
    humans = 1 << 2;
    // Faction 1: 4 * (5 + 1) + 3 techs * 4 + 10 (project) + 4 / 4 + 30 * (4 / 4)
    // + 8 / 2; the 251 units of design 65 don't count. Faction 2: 4 * 2 + 10.
    rankings(0);
    EXPECT(ranking_scores[1], 81);
    EXPECT(ranking_scores[2], 18);
    EXPECT(ranking_scores[3], 0);
    EXPECT(might[100][1], 81);
    rankings(1);
    EXPECT(factions[1].ranking, 7);
    EXPECT(factions[2].ranking, 6);
    EXPECT(factions[3].ranking, 5);
    EXPECT(factions[7].ranking, 1);
    EXPECT(faction_rankings[7], 1);
    EXPECT(faction_rankings[5], 3);
    EXPECT(faction_rankings[4], 0);  // faction 4 isn't alive
    EXPECT(ranked[0], 2);
    EXPECT(ranked[1], 2);
    EXPECT(ranking_scores[1], -1);
    EXPECT(state & GAME_STATE_RANKED, 0);
    // A scenario won by objectives alone: 10 each.
    required = 5;
    rankings(0);
    EXPECT(ranking_scores[1], 30);
    required = 9000;
    // Near the end, a human on a high level leads; faction 1 at treaty
    // with it, wanted for revenge, rolls under (1 + 4 + 4) * 4; faction 3,
    // at war besides, has no range to roll in.
    climax = 1, factions[2].diff_level = 4, rand_value = 30;
    factions[1].diplo_status[2] = TREATY_TREATY, factions[2].diplo_status[1] = TREATY_WANT_REVENGE;
    factions[3].diplo_status[2] = TREATY_TREATY | TREATY_VENDETTA, factions[3].ai_fight = 3;
    rankings(1);
    EXPECT(state & GAME_STATE_RANKED, GAME_STATE_RANKED);
    EXPECT(treaties, 2);
    EXPECT(last_treaty[0], 3);
    EXPECT(last_treaty[1], 2);
    EXPECT(last_treaty[2], TREATY_SHALL_BETRAY);
    rand_value = 36;  // faction 1 now rolls too high
    rankings(1);
    EXPECT(treaties, 3);
    factions[1].diplo_status[2] = TREATY_PACT | TREATY_TREATY;  // pacts don't plot
    factions[3].diplo_status[2] = 0;
    rankings(1);
    EXPECT(treaties, 3);
    climax = 0, factions[1].diplo_status[2] = factions[2].diplo_status[1] = 0;
    rankings(1);
    EXPECT(state & GAME_STATE_RANKED, 0);
    humans = 0;
}

static void score(void) {
    // Faction 1: its 3 citizens and 4 of faction 2's, surrendered in a pact;
    // commerce from 2 and 3; 3 techs; 10 for its transcendent tech; 25 for
    // its project; 3 objectives.
    factions[2].diplo_status[1] = TREATY_PACT | 0x2000000;
    commerce[2] = 5, commerce[3] = 2;
    struct faction_data *f = &factions[1];
    compute_score(1, NULL, NULL, 0);
    EXPECT(f->score_parts[0], 7);
    EXPECT(f->score_parts[1], 7);
    EXPECT(f->score_parts[2], 3);
    EXPECT(f->score_parts[3], 10);
    EXPECT(f->score_parts[4], 25);
    EXPECT(f->score_parts[6], 3);
    EXPECT(f->score_parts[7], 0);
    EXPECT(f->score_total, 55);
    EXPECT(f->score, 55);
    EXPECT(current_base, 0);
    // Under sanctions no commerce; the Progenitors none either.
    f->sanction_turns = 2;
    compute_score(1, NULL, NULL, 0);
    EXPECT(f->score_parts[1], 0);
    f->sanction_turns = 0, defs[3].rule_flags = RULE_FLAG_ALIEN;
    compute_score(1, NULL, NULL, 0);
    EXPECT(f->score_parts[1], 5);
    defs[3].rule_flags = 0;
    // Native life adds a share, an ironman game doubles the saved score.
    native_life = 3, rules = 0x400;
    compute_score(1, NULL, NULL, 0);
    EXPECT(f->score_parts[5], 27);
    EXPECT(f->score_total, 82);
    EXPECT(f->score, 164);
    native_life = 1, rules = 0;
    // Into the caller's places, partly: nothing saved.
    int total = -1, parts[9];
    f->score = 1;
    compute_score(1, &total, parts, 1);
    EXPECT(total, 55);
    EXPECT(f->score, 1);
    // Once the final score is shown, only partly.
    state = GAME_STATE_FINAL_SCORE_DONE;
    compute_score(1, NULL, NULL, 0);
    EXPECT(f->score, 1);
    state = 0;
    // The Ascent at base 0: 1800 points at turn 100, shared with its pact
    // partner by population, the builder's counted twice.
    projects[SP_ASCENT_TO_TRANSCENDENCE] = 0;
    compute_score(1, NULL, NULL, 0);
    EXPECT(ascent_points, 1800);
    EXPECT(ascent_pop[0], 12);
    EXPECT(ascent_pop[1], 10);
    EXPECT(ascent_pop[2], 2);
    EXPECT(f->score_parts[6], 1500);
    projects[SP_ASCENT_TO_TRANSCENDENCE] = -1;
    // Conquest: 800 at turn 100, by the population held, 7 of 8.
    rules = 0x2, state = 0x2000;  // conquest
    compute_score(1, NULL, NULL, 0);
    EXPECT(f->score_parts[7], 700);
    state = 0;
    compute_score(1, NULL, NULL, 0);
    EXPECT(f->score_parts[7], 0);
    // Its win flags; a cooperative partner's.
    rules = 0, f->flags = 0x100000;
    compute_score(1, NULL, NULL, 0);
    EXPECT(f->score_parts[7], 1800);
    f->flags = 0, factions[2].flags = 0x20000, factions[1].diplo_status[2] = TREATY_PACT;
    rules = RULES_VICTORY_COOPERATIVE;
    compute_score(1, NULL, NULL, 0);
    EXPECT(f->score_parts[7], 500);
    factions[2].flags = 0, factions[1].diplo_status[2] = 0, rules = 0;
    // A scenario won by objectives: the other parts cleared, the victory
    // points for enough objectives.
    required = 3, victory = 400;
    compute_score(1, NULL, NULL, 0);
    EXPECT(f->score_parts[0], 0);
    EXPECT(f->score_parts[6], 3);
    EXPECT(f->score_parts[7], 400);
    EXPECT(f->score_total, 403);
    required = 0;  // as many as the most anyone holds
    compute_score(1, NULL, NULL, 0);
    EXPECT(f->score_parts[7], 400);
    required = 9000, victory = 0;
    // Where the highest score wins, the best gets 1000 after the total
    // (with base 0 alone, so no other base needs working out).
    state = 0x80000, base_count = 1;
    compute_score(1, NULL, NULL, 0);
    EXPECT(f->score_parts[7], 1000);
    EXPECT(f->score_total, 51);
    compute_score(2, NULL, NULL, 0);
    EXPECT(factions[2].score_parts[7], 0);
    EXPECT(factions[2].score_total, 1);
    state = 0, base_count = 3;
}

// ---- The game's end ---------------------------------------------------------------

static char shown[16][32];  // the end's messages and screens, in order
static int shown_n, popp_answer;
static int32_t mission_year, ending_year = 2400, turn_a, turn_b, net, prefs, language, current = 1, victory_type;
static int32_t gender, plural, ascent[2] = {4, 3};
static uint32_t more_rules;
static char battle_text[256], *script = "script";
static void show(const char *what) {
    if (shown_n < 16) snprintf(shown[shown_n++], sizeof shown[0], "%s", what);
}
static int says_stub(int slot, char *text, int g, int p) { return 0; }
static int num_stub(int slot, int value) { return 0; }
static int x_pop_stub(char *label, int unk) { return show(label), 0; }
static int x_pop2_stub(char *file, char *label, int unk) { return show(label), 0; }
static int popp_stub(char *file, char *label, int flags, char *pic, void *fn) { return show(label), popp_answer; }
static void interlude_stub(int id, char *text, int a, int b) { show("interlude"); }
static void movie_stub(char *name) { show(name); }
static void unify_stub(int f, int other) { show("unify"); }
static void credits_stub(void) { show("credits"); }
static void replay_stub(void) { show("replay"); }
static void report_stub(int final) { show("score"); }
static void quayle_stub(int f) { show("quayle"); }
static void fame_stub(int final) { show("fame"); }
static uint32_t __attribute__((stdcall)) upper_stub(char *s) { return 0; }
static uint32_t(__attribute__((stdcall)) * upper_ptr)(char *) = upper_stub;
static int was_shown(const char *what) {
    for (int i = 0; i < shown_n; i++)
        if (!strcmp(shown[i], what)) return 1;
    return 0;
}

static void game_end(void) {
    // Labels and pictures are read from the game's image: the test points
    // game_at() at a made-up, empty one.
    static uint8_t image[0x300000];
    game_test_image = image;
    game_parse_says = says_stub, game_parse_num = num_stub, game_x_pop = x_pop_stub, game_x_pop2 = (void *)x_pop2_stub;
    game_popp = (void *)popp_stub, game_interlude = interlude_stub, game_amovie_project_2 = movie_stub;
    game_mon_winning_unify = unify_stub, game_show_credits = credits_stub, game_show_replay = replay_stub;
    game_report_score = report_stub, game_quayle = quayle_stub, game_hall_of_fame = fame_stub;
    game_char_upper = &upper_ptr, game_script_file = &script, game_battle_text = battle_text;
    game_mission_year = &mission_year, game_ending_year = &ending_year, game_control_turn_a = &turn_a;
    game_control_turn_b = &turn_b, game_net_multiplayer = &net, game_preferences = &prefs, game_language = &language;
    game_current_faction = &current, game_victory_type = &victory_type, game_more_rules = &more_rules;
    game_parse_gender = &gender, game_parse_plural = &plural, game_ascent_numbers = ascent;
    state = 0, rules = 0, sudden = 9000, alive = 0x0f, humans = 0;
    for (int f = 0; f < FACTION_COUNT; f++) factions[f].flags = 0, factions[f].corner_market_turn = 0;

    // Twenty years before the end, the player is warned; the game goes on.
    mission_year = ending_year - 20, shown_n = 0;
    EXPECT(end_of_game(1), 0);
    EXPECT(shown_n, 1);
    EXPECT(strcmp(battle_text, "2400"), 0);
    EXPECT(end_of_game(0), 0);
    EXPECT(shown_n, 1);
    EXPECT((int)state, 0);

    // The time limit: the end is due, and shown with the score, hall of
    // fame and replay; the player leaves.
    mission_year = ending_year, shown_n = 0;
    EXPECT(end_of_game_due(1), 1);
    EXPECT((int)state, 0);
    EXPECT(end_of_game_due(0), 0);
    EXPECT(end_of_game(1), 1);
    EXPECT(victory_type, 8);
    EXPECT((int)state, GAME_STATE_DONE | GAME_STATE_FINAL_SCORE_DONE);
    EXPECT(was_shown("score") && was_shown("quayle") && was_shown("fame") && was_shown("replay"), 1);
    EXPECT(was_shown("credits"), 0);
    EXPECT(turn_a, 1);
    EXPECT(turn_b, 1);
    EXPECT(end_of_game(1), 0);  // shown once

    // The energy market cornered by a pact partner in a cooperative game:
    // its win shared, with the monuments to both; the player plays on.
    state = 0, mission_year = 2200, victory_type = 0, turn_a = turn_b = 0, shown_n = 0, popp_answer = 1;
    rules = RULES_VICTORY_COOPERATIVE;
    factions[2].corner_market_turn = turn, factions[2].corner_market_cost = 500;
    factions[2].diplo_status[1] |= TREATY_PACT;
    EXPECT(end_of_game(1), 0);
    EXPECT(victory_type, 13);
    EXPECT((int)(factions[2].flags & 0x40000), 0x40000);
    EXPECT((int)(state & 0x400009), 0x400009);
    EXPECT(was_shown("unify") && was_shown("interlude") && was_shown("credits"), 1);
    EXPECT(turn_a, 0);
    factions[2].corner_market_turn = 0, factions[2].diplo_status[1] = 0, popp_answer = 0;

    // An alien faction's subspace generators: three of them, at bases of
    // size 4 or more, end the game as a loss for the player.
    state = 0, victory_type = 0, rules = 0, shown_n = 0;
    defs[3].rule_flags |= RULE_FLAG_ALIEN;
    int base2_pop = bases[2].pop_size;
    bases[2].pop_size = 5;
    ascent[1] = 1;
    bases[2].facilities_built[40 / 8] |= 1 << (40 % 8);
    EXPECT(end_of_game_due(1), 1);
    EXPECT(end_of_game(1), 1);
    EXPECT(victory_type, 18);
    EXPECT((int)(factions[3].flags & 0x100000), 0x100000);
    EXPECT((int)(more_rules & 0x80), 0x80);
    EXPECT(was_shown("interlude"), 1);
    bases[2].facilities_built[40 / 8] = 0, bases[2].pop_size = (int8_t)base2_pop, defs[3].rule_flags = 0;
    ascent[1] = 3, state = 0, victory_type = 0, more_rules = 0;
    for (int f = 0; f < FACTION_COUNT; f++) factions[f].flags = 0;
}

int main(void) {
    setup();
    year_and_rating();
    objectives();
    ranking();
    score();
    game_end();
    if (failures) return 1;
    printf("engine_score: all passed\n");
    return 0;
}
