// Unit test for the reimplemented Planetary Council, on a small made-up
// game: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/council.h"
#include "../src/engine/game.h"

enum { W = 16, H = 12 };

static struct map_tile tiles[W / 2 * H], *tiles_ptr = tiles;
static int32_t half_width = W / 2, base_count = 4, turn = 100, expansion, current = 1, net, pbem, pending, sunspots;
static int32_t governor = -1, candidates[FACTION_COUNT], projects[SP_COUNT], sea_level, sea_council, depth_a = 6, depth_b = 4;
static int32_t rankings[FACTION_COUNT], ranked[2];
static uint32_t rules, more_rules, state;
static uint8_t humans, alive = 0x1e;
static struct base bases[4];
static struct faction_data factions[FACTION_COUNT];
static struct faction_def defs[FACTION_COUNT];
static struct rules_tech techs[TECH_COUNT];
static uint8_t achieved[TECH_COUNT];
static struct rules_chassis chassis[CHASSIS_COUNT];
static struct rules_weapon weapons[WEAPON_COUNT];
static int failures, notices, attack, satan;

#define EXPECT(expr, want)                                                       \
    do {                                                                         \
        int got_ = (expr);                                                       \
        if (got_ != (want)) {                                                    \
            printf("FAIL %s = %d, expected %d\n", #expr, got_, (int)(want));    \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static struct map_tile *at(int x, int y) { return &tiles[x / 2 + y * (W / 2)]; }
static int wants_to_attack_stub(int faction, int other, int odds) { return attack; }
static int great_satan_stub(int faction, int aggressive) { return satan; }
static void __attribute__((thiscall)) netmsg_stub(void *self, char *label, int delay, int unk, char *sound) { notices++; }

// The acts: messages, the popup on the stack, and the session.
static int32_t passes[PROP_COUNT], passed_turn[PROP_COUNT], climate_change, climate_shown[3], victory, control_turn;
static int32_t session, net_call[3];
static struct proposal proposals[PROP_COUNT];
static struct rules_basic rules_basic;
static char battle_text[1024], *script_file = "script";
static int32_t gender, plural;
static int popps, answer, credits_shown, sessions, session_args[3], listed;
static uint32_t agenda_set;
static int popp_stub(char *file, char *label, int flags, char *picture, int unk) { return popps++, answer; }
static int parse_num_stub(int slot, int value) { return 0; }
static int parse_says_stub(int slot, char *text, int gender_, int plural_) { return 0; }
static void __attribute__((thiscall)) console_update_stub(void *console, int unk) { credits_shown++; }
static void set_agenda_stub(int faction, int other, uint32_t agenda, int on) {
    if (on) factions[faction].diplo_agenda[other] |= agenda, agenda_set |= agenda;
}
static void __attribute__((thiscall)) pop_stub(void *pop) {}
static int __attribute__((thiscall)) popup_start_stub(void *pop, char *file, char *label, int a, int b, int c, int d) { return 0; }
static int pcx_stub(char *file, int *w, int *h) { return 1; }
static char *__attribute__((thiscall)) strings_get_stub(void *table, char *s) { return "proposal"; }
static void __attribute__((thiscall)) dialogs_item_stub(void *dialogs, char *text, int id) { listed |= 1 << id; }
static int __attribute__((thiscall)) exec_stub(void *pop, int a, int b) { return answer; }
static int ambience_stub(int sound) { return 0; }
static void council_stub(int faction, int prop, int target) {
    sessions++, session_args[0] = faction, session_args[1] = prop, session_args[2] = target;
}

int main(void) {
    game_map = &tiles_ptr, game_map_half_width = &half_width, game_bases = bases, game_base_count = &base_count;
    game_factions = factions, game_faction_defs = defs, game_turn = &turn, game_expansion = &expansion;
    game_current_faction = &current, game_net_multiplayer = &net, game_pbem = &pbem, game_council_pending = &pending;
    game_sunspot_duration = &sunspots, game_governor_faction = &governor, game_council_candidate = candidates;
    game_secret_projects = projects, game_sea_level = &sea_level, game_sea_level_council = &sea_council;
    game_dome_depth_a = &depth_a, game_dome_depth_b = &depth_b, game_faction_rankings = rankings;
    game_ranked_faction = ranked, game_rules = &rules, game_more_rules = &more_rules, game_state = &state;
    game_human_factions = &humans, game_factions_alive = &alive, game_tech = techs, game_tech_achieved = achieved;
    game_chassis = chassis, game_weapons = weapons, game_wants_to_attack = wants_to_attack_stub;
    game_great_satan = great_satan_stub, game_netmsg_pop = netmsg_stub;
    for (int i = 0; i < SP_COUNT; i++) projects[i] = -1;
    for (int f = 0; f < FACTION_COUNT; f++) {
        rankings[f] = f;
        defs[f].ideology_category = defs[f].anti_ideology_category = -1;
        defs[f].ideology_model = 0, defs[f].anti_ideology_model = 1;
    }
    for (int t = 0; t < TECH_COUNT; t++) techs[t].preq1 = techs[t].preq2 = TECH_NONE;
    chassis[8].preq_tech = 10, weapons[16].preq_tech = 11;
    // Land everywhere but a column of sea; the shore at (6, 2).
    for (int y = 0; y < H; y++)
        for (int x = y & 1; x < W; x += 2) *at(x, y) = (struct map_tile){.climate = (x == 4 ? 1 : 4) << 5, .contour = 50};
    at(6, 2)->climate = 3 << 5, at(6, 2)->contour = 5, at(4, 2)->contour = 0;
    bases[0] = (struct base){.x = 2, .y = 2, .faction = 1, .pop_size = 4};
    bases[1] = (struct base){.x = 8, .y = 2, .faction = 2, .pop_size = 6};
    bases[2] = (struct base){.x = 4, .y = 2, .faction = 3, .pop_size = 2};
    bases[3] = (struct base){.x = 6, .y = 2, .faction = 1, .pop_size = 2};
    factions[1].pop_total = 6;

    // Votes: citizens; half again with the Empath Guild, doubled with
    // Clinical Immortality, times a votes bonus; none for aliens in Alien
    // Crossfire.
    EXPECT(council_votes(1), 6);
    EXPECT(council_votes(2), 6);
    EXPECT(council_votes(4), 0);
    projects[SP_EMPATH_GUILD] = 0;
    EXPECT(council_votes(1), 9);
    projects[SP_CLINICAL_IMMORTALITY] = 3;
    EXPECT(council_votes(1), 18);
    defs[1].bonus_count = 1, defs[1].bonus_id[0] = 11, defs[1].bonus_val1[0] = 3;
    EXPECT(council_votes(1), 54);
    defs[1].bonus_val1[0] = -1;
    EXPECT(council_votes(1), 18);
    defs[1].bonus_count = 0, projects[SP_EMPATH_GUILD] = projects[SP_CLINICAL_IMMORTALITY] = -1;
    defs[3].rule_flags = RULE_FLAG_ALIEN;
    EXPECT(council_votes(3), 2);
    expansion = 1;
    EXPECT(council_votes(3), 0);
    expansion = 0, defs[3].rule_flags = 0;

    // Candidates: fewer than two living factions with more votes.
    EXPECT(eligible(2), 1);
    EXPECT(eligible(3), 0);
    EXPECT(eligible(4), 0);
    alive = 0x1a;  // faction 2 gone
    EXPECT(eligible(3), 1);
    alive = 0x1e;

    // Governor, for itself: 6 with one faction as strong, nearly doubled
    // with no governor; 2 more as governor; 1 more against a governor it
    // has a vendetta with.
    EXPECT(wants_prop(1, PROP_GOVERNOR, 1), 11);
    governor = 1;
    EXPECT(wants_prop(1, PROP_GOVERNOR, 1), 8);
    governor = 2, factions[1].diplo_status[2] = TREATY_VENDETTA;
    EXPECT(wants_prop(1, PROP_GOVERNOR, 1), 7);
    governor = -1, factions[1].diplo_status[2] = 0;
    EXPECT(wants_prop(1, PROP_GOVERNOR, 5), -9);  // dead
    EXPECT(wants_prop(1, PROP_GOVERNOR, 4), -9);  // not eligible
    // For another: an AI it has no feelings for, and one more for being
    // among the strongest in votes.
    factions[2].ai_fight = 1;
    EXPECT(wants_prop(2, PROP_GOVERNOR, 1), 1);
    // The rankings' place, not the faction there, is looked up and
    // compared: with grudges against 2 to 7 the place found is 1.
    for (int k = 2; k < FACTION_COUNT; k++) factions[2].diplo_status[k] = TREATY_VENDETTA;
    factions[2].diplo_status[1] = 0, rankings[1] = 5;
    EXPECT(wants_prop(2, PROP_GOVERNOR, 1), 2);
    for (int k = 2; k < FACTION_COUNT; k++) factions[2].diplo_status[k] = 0;
    rankings[1] = 1;
    // A pact partner: 2, or 1 if either wants to attack the other; 99 if
    // it surrendered.
    factions[2].diplo_status[1] = TREATY_PACT;
    EXPECT(wants_prop(2, PROP_GOVERNOR, 1), 3);
    attack = 1;
    EXPECT(wants_prop(2, PROP_GOVERNOR, 1), 2);
    attack = 0, factions[2].diplo_status[1] |= 0x2000000;
    EXPECT(wants_prop(2, PROP_GOVERNOR, 1), 99);
    // Its pact partner that surrendered voted for faction 1.
    factions[2].diplo_status[1] = 0, factions[2].diplo_status[3] = TREATY_PACT | 0x2000000, candidates[3] = 1;
    EXPECT(wants_prop(2, PROP_GOVERNOR, 1), 999);
    // A candidate who voted for another loses a point and half the rest.
    candidates[1] = 2;
    EXPECT(wants_prop(2, PROP_GOVERNOR, 1), 499);
    factions[2].diplo_status[3] = 0, candidates[3] = candidates[1] = 0;
    // A human candidate: towards a human only a dislike counts, without
    // status bit 0x8000000.
    humans = 1 << 1, ranked[0] = 1, factions[1].diff_level = 1;
    EXPECT(wants_prop(2, PROP_GOVERNOR, 1), 0);
    factions[2].diplo_status[1] = 0x8000000;
    EXPECT(wants_prop(2, PROP_GOVERNOR, 1), 2);
    factions[1].diff_level = 3;  // on the hardest levels it loses one
    EXPECT(wants_prop(2, PROP_GOVERNOR, 1), 0);
    humans = 0, factions[1].diff_level = 0, factions[2].diplo_status[1] = 0;
    // Great Satan.
    satan = 1;
    EXPECT(wants_prop(2, PROP_GOVERNOR, 1), -1);
    satan = 0;

    // Supreme Leader: for itself 5 if first; others oppose, less so a
    // dominant leader; a pact partner backs it under the cooperative
    // victory rule.
    factions[2].ranking = 7;
    EXPECT(wants_prop(2, PROP_SUPREME_LEADER, 2), 5);
    EXPECT(wants_prop(1, PROP_SUPREME_LEADER, 2), -4);
    turn = 250, factions[1].ranking = 3, factions[2].mil_strength = 100, factions[1].mil_strength = 40;
    factions[2].mil_attack = 50, factions[1].mil_attack = 20, factions[2].pop_total = 12;
    EXPECT(wants_prop(1, PROP_SUPREME_LEADER, 2), -2);
    factions[1].diplo_status[2] = TREATY_ATROCITY_VICTIM;
    EXPECT(wants_prop(1, PROP_SUPREME_LEADER, 2), -5);
    factions[1].diplo_status[2] = TREATY_PACT;
    EXPECT(wants_prop(1, PROP_SUPREME_LEADER, 2), 0);
    rules = RULES_VICTORY_COOPERATIVE;
    EXPECT(wants_prop(1, PROP_SUPREME_LEADER, 2), 1);
    factions[2].diplo_status[1] = TREATY_PACT;  // a weaker partner, less populous
    EXPECT(wants_prop(2, PROP_SUPREME_LEADER, 1), -2);
    factions[1].ranking = 7, factions[2].ranking = 7;  // equals: indifferent after turn 200, else against
    EXPECT(wants_prop(2, PROP_SUPREME_LEADER, 1), 0);
    turn = 100;
    EXPECT(wants_prop(2, PROP_SUPREME_LEADER, 1), -1);
    factions[1].ranking = 3, factions[1].diplo_status[2] = factions[2].diplo_status[1] = 0, rules = 0;

    // The Unity core: the power minded and the poor want it, more so when
    // others are richer.
    factions[1].ai_power = 1, factions[1].energy_credits = 50;
    factions[1].energy_surplus_total = 10, factions[1].facility_maint_total = 5;
    EXPECT(wants_prop(1, PROP_UNITY_CORE, 0), 3);
    for (int f = 2; f < FACTION_COUNT; f++) factions[f].energy_credits = 300, factions[f].energy_surplus_total = 20;
    EXPECT(wants_prop(1, PROP_UNITY_CORE, 0), 9);
    factions[1].ai_power = 0;

    // The trade pact: pacts and treaties for, richer commerce against;
    // the repeal is its opposite.
    factions[2].tech_commerce_bonus = 3, factions[1].tech_commerce_bonus = 1;
    factions[1].diplo_status[2] = TREATY_PACT | TREATY_TREATY, factions[1].diplo_status[3] = TREATY_TREATY;
    factions[1].ai_wealth = 1, factions[1].se_pending[SE_ECONOMY] = 2;
    EXPECT(wants_prop(1, PROP_TRADE_PACT, 0), 4);
    EXPECT(wants_prop(1, PROP_REPEAL_TRADE_PACT, 0), -4);
    // Its vote: for, a pact with the proposer adding one; or its promise.
    EXPECT(council_get_vote(1, PROP_TRADE_PACT, 2), -1);
    EXPECT(council_get_vote(1, PROP_REPEAL_TRADE_PACT, 2), -2);
    factions[1].council_pledge[PROP_TRADE_PACT] = -2;
    EXPECT(council_get_vote(1, PROP_TRADE_PACT, 2), -2);
    factions[1].council_pledge[PROP_TRADE_PACT] = 0, factions[1].ai_wealth = 0, factions[1].se_pending[SE_ECONOMY] = 0;
    factions[1].diplo_status[2] = factions[1].diplo_status[3] = 0;

    // The sea. Melting the caps: its own bases count twice, an enemy's
    // against it; a shore base low enough is flooded unless domed.
    factions[1].diplo_status[2] = TREATY_VENDETTA;
    EXPECT(wants_prop(1, PROP_MELT_CAPS, 0), -4);
    bases[3].facilities_built[FAC_PRESSURE_DOME / 8] = 1 << (FAC_PRESSURE_DOME % 8);
    EXPECT(wants_prop(1, PROP_MELT_CAPS, 0), -1);
    EXPECT(wants_prop(1, PROP_SOLAR_SHADE, 0), -1);
    // A growth minded faction wants it unless the council moved the sea
    // that way already.
    factions[1].ai_growth = 1, sea_council = 1;
    EXPECT(wants_prop(1, PROP_SOLAR_SHADE, 0), 3);
    EXPECT(wants_prop(1, PROP_MELT_CAPS, 0), -4);
    factions[1].ai_growth = 0, sea_council = 0, factions[1].diplo_status[2] = 0;

    // The UN charter: against repeal without planet busters when others
    // have them or can build them.
    factions[2].planet_busters = 1;
    EXPECT(wants_prop(1, PROP_REPEAL_CHARTER, 0), -1);
    EXPECT(wants_prop(1, PROP_REINSTATE_CHARTER, 0), 1);
    achieved[11] = 1 << 3;
    EXPECT(wants_prop(1, PROP_REPEAL_CHARTER, 0), -4);
    EXPECT(wants_prop(1, PROP_REINSTATE_CHARTER, 0), 4);
    factions[1].atrocities = 6, factions[1].planet_busters = 1, factions[1].ai_fight = 1, factions[1].ai_power = 1;
    EXPECT(wants_prop(1, PROP_REPEAL_CHARTER, 0), 6);
    factions[1].atrocities = 0, factions[1].planet_busters = 0, factions[1].ai_fight = 0, factions[1].ai_power = 0;
    EXPECT(wants_prop(1, 10, 0), -9);
    EXPECT(wants_prop(1, 11, 0), 0);
    EXPECT(wants_prop(1, -1, 0), 0);

    // A vote for governor: the one it wants most, unless it promised its
    // vote to one it has no quarrel with.
    factions[2].ai_fight = 0;
    EXPECT(council_get_vote(2, PROP_GOVERNOR, 0), 2);
    factions[2].council_pledge[0] = 1, factions[2].council_pledge_to[0] = 3;
    EXPECT(council_get_vote(2, PROP_GOVERNOR, 0), 3);
    factions[2].diplo_status[3] = TREATY_VENDETTA;
    EXPECT(council_get_vote(2, PROP_GOVERNOR, 0), 2);
    factions[2].diplo_status[3] = 0, factions[2].council_pledge[0] = 0;
    // Indifferent: -1 to -3 by the turn.
    factions[2].tech_commerce_bonus = 0;
    EXPECT(council_get_vote(1, PROP_TRADE_PACT, 3), -1 - (turn + 1) % 3);

    // Calling the Council: a commlink with every living faction, or the
    // Empath Guild; a human calling on its own turn is told why not.
    humans = 1 << 1;
    EXPECT(can_call_council(1, 1), 0);
    EXPECT(notices, 1);
    EXPECT(can_call_council(1, 0), 0);
    EXPECT(notices, 1);
    for (int f = 2; f < 5; f++) factions[1].diplo_status[f] = TREATY_COMMLINK;
    EXPECT(can_call_council(1, 1), 1);
    sunspots = 2;
    EXPECT(can_call_council(1, 1), 0);
    EXPECT(notices, 2);
    sunspots = 0, pbem = 1, pending = 1;
    EXPECT(can_call_council(1, 1), 0);
    EXPECT(notices, 3);
    pbem = 0, pending = 0, more_rules = 0x4;
    EXPECT(can_call_council(1, 1), 0);
    state = 0xc0;  // the scenario editor in omniscient view
    EXPECT(can_call_council(1, 1), 1);
    state = 0, more_rules = 0, factions[1].diplo_status[3] = 0;
    EXPECT(can_call_council(1, 0), 0);
    projects[SP_EMPATH_GUILD] = 0;
    EXPECT(can_call_council(1, 0), 1);
    defs[1].rule_flags = RULE_FLAG_ALIEN;
    EXPECT(can_call_council(1, 0), 0);

    defs[1].rule_flags = 0, projects[SP_EMPATH_GUILD] = -1, humans = 0;

    // ---- The acts -------------------------------------------------------
    static uint8_t image[0x300000];  // the game's text, from 0x400000: all empty
    game_test_image = image;
    game_proposal_passes = passes, game_proposal_turn = passed_turn, game_climate_change = &climate_change;
    game_climate_shown = climate_shown, game_victory_type = &victory, game_popp = popp_stub;
    game_parse_num = parse_num_stub, game_parse_says = parse_says_stub, game_console_update_data = console_update_stub;
    game_set_agenda = set_agenda_stub, game_script_file = &script_file, game_parse_gender = &gender;
    game_parse_plural = &plural, game_proposals = proposals, game_rules_basic = &rules_basic;
    game_battle_text = battle_text, game_control_turn = &control_turn, game_council_session = &session;
    game_council_net_call = net_call, game_council = council_stub;
    game_popup_ctor = game_popup_close = game_scroll_close = game_flat_button_dtor = pop_stub;
    game_graphic_win_dtor = game_base_pop_dtor = pop_stub, game_popup_start = popup_start_stub;
    game_pcx_dimensions = pcx_stub, game_strings_get = strings_get_stub, game_dialogs_item = dialogs_item_stub;
    game_base_pop_exec_3 = exec_stub, game_call_council = call_council;
    int32_t ambience = 0, multi_visible = 0, start_year = 2100;
    game_start_year = &start_year;
    game_ambience_flags = &ambience, game_ambience = ambience_stub, game_multi_win_visible = &multi_visible;
    for (int p = 0; p < PROP_COUNT; p++) passed_turn[p] = -9999;

    // A proposal passed is counted with its turn. The Unity core: 500
    // credits to every faction but the aliens.
    for (int f = 1; f < FACTION_COUNT; f++) factions[f].energy_credits = 10;
    defs[3].rule_flags = RULE_FLAG_ALIEN;
    council_action(PROP_UNITY_CORE, 1);
    EXPECT(passes[PROP_UNITY_CORE], 1);
    EXPECT(passed_turn[PROP_UNITY_CORE], turn);
    EXPECT(factions[2].energy_credits, 510);
    EXPECT(factions[3].energy_credits, 10);
    EXPECT(credits_shown, 1);
    defs[3].rule_flags = 0;
    // The solar shade lowers the sea by 3 and the coming change by a step.
    climate_change = 2;
    council_action(PROP_SOLAR_SHADE, 1);
    EXPECT(sea_council, -3);
    EXPECT(climate_change, -2);
    EXPECT(climate_shown[0], 2);
    EXPECT(popps, 1);
    council_action(PROP_MELT_CAPS, 1);
    EXPECT(sea_council, 0);
    sea_council = 0, climate_change = 0;

    // Supreme Leader, the faction playing its turn: it wins unless those
    // it wronged object (under rule 0x2), who declare vendetta while the
    // rest surrender to it.
    current = 2, popps = 0;
    council_action(PROP_SUPREME_LEADER, 2);
    EXPECT(victory, 6);
    EXPECT(state & 1, 1);
    EXPECT((factions[2].flags & 0x20000) != 0, 1);
    state = 0, victory = 0, factions[2].flags = 0;
    rules = 0x2, factions[3].diplo_status[2] = TREATY_ATROCITY_VICTIM;
    council_action(PROP_SUPREME_LEADER, 2);
    EXPECT(victory, 0);
    EXPECT(popps, 1);
    EXPECT((factions[3].diplo_status[2] & TREATY_VENDETTA) != 0, 1);
    EXPECT((factions[1].diplo_status[2] & 0x2000000) != 0, 1);
    EXPECT((factions[3].diplo_status[2] & 0x2000000) != 0, 0);
    for (int f = 1; f < FACTION_COUNT; f++)
        for (int g = 1; g < FACTION_COUNT; g++) factions[f].diplo_status[g] = factions[f].diplo_agenda[g] = 0;
    rules = 0, state = 0;
    // Another's: accepted, the game is lost; refused, the others turn on
    // the player.
    current = 1;
    council_action(PROP_SUPREME_LEADER, 2);
    EXPECT(victory, 0xb);
    state = 0, victory = 0, factions[2].flags = 0, answer = 1;
    council_action(PROP_SUPREME_LEADER, 2);
    EXPECT(victory, 0);
    EXPECT((factions[3].diplo_status[1] & TREATY_VENDETTA) != 0, 1);
    EXPECT((factions[3].diplo_status[2] & TREATY_PACT) != 0, 1);
    EXPECT((factions[2].diplo_status[1] & TREATY_VENDETTA) != 0, 1);
    for (int f = 1; f < FACTION_COUNT; f++)
        for (int g = 1; g < FACTION_COUNT; g++) factions[f].diplo_status[g] = factions[f].diplo_agenda[g] = 0;
    answer = 0, state = 0;
    memset(passes, 0, sizeof passes);

    // Calling the Council. Faction 2 has a commlink with all; no governor.
    rules_basic.min_turns_councils = 20;
    for (int p = 0; p < PROP_COUNT; p++) passed_turn[p] = -9999;
    for (int p = 0; p < PROP_COUNT; p++) proposals[p].preq_tech = p == 10 ? -2 : TECH_NONE;
    for (int f = 1; f < 5; f++)
        if (f != 2) factions[2].diplo_status[f] = TREATY_COMMLINK;
    for (int f = 1; f < FACTION_COUNT; f++) factions[f].council_call_turn = -9999, factions[f].ai_power = 0;
    // The AI wants itself for governor most.
    call_council_ecx(2, 0);
    EXPECT(sessions, 1);
    EXPECT(session_args[1], PROP_GOVERNOR);
    EXPECT(session_args[2], 2);
    // Not the Supreme Leader (no diplomatic victory rule), what needs an
    // earlier proposal passed (repeals, the shade's increase), nor 10.
    EXPECT(listed, 0x1ad);
    // Promised its vote for governor, the AI puts what it wants most
    // otherwise; but with ECX non-zero (the human factions, from
    // faction_upkeep()), nothing but the governor.
    factions[2].council_pledge[0] = 1, factions[2].council_pledge_to[0] = 1;
    factions[2].ai_power = 1, factions[2].energy_credits = 50, sessions = 0;
    call_council_ecx(2, 0);
    EXPECT(sessions, 1);
    EXPECT(session_args[1], PROP_UNITY_CORE);
    sessions = 0;
    call_council_ecx(2, 2);
    EXPECT(sessions, 0);
    factions[2].council_pledge[0] = 0, factions[2].ai_power = 0, sessions = 0;
    // A governor keeps it to one turn in 16, and an AI governor from the
    // election unless it would win half the votes (ECX non-zero: the
    // governor only).
    governor = 1, turn = 101, sessions = 0;
    call_council_ecx(2, 1);
    EXPECT(sessions, 0);
    turn = 92;  // (92 + 4) is a multiple of 16
    call_council_ecx(2, 1);
    EXPECT(sessions, 0);
    humans = 1 << 1;
    call_council_ecx(2, humans);
    EXPECT(sessions, 1);
    EXPECT(session_args[1], PROP_GOVERNOR);
    // A proposal that passed lately can't be put again for 20 turns, nor
    // can the faction call again so soon.
    governor = -1, sessions = 0, passed_turn[PROP_GOVERNOR] = 85;
    call_council_ecx(2, 2);
    EXPECT(sessions, 0);
    // A human chooses; it may call a proposal again after 20 turns.
    humans = 1 << 2, current = 2, answer = PROP_UNITY_CORE, factions[2].council_call_turn = 80;
    call_council_ecx(2, 0);
    EXPECT(sessions, 0);
    EXPECT(notices > 3, 1);
    factions[2].council_call_turn = 72;
    call_council_ecx(2, 0);
    EXPECT(sessions, 1);
    EXPECT(session_args[1], PROP_UNITY_CORE);
    EXPECT(session_args[2], 2);
    humans = 0, answer = 0, turn = 100, governor = -1, current = 1;

    if (failures) return 1;
    printf("ok\n");
    return 0;
}
