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

    if (failures) return 1;
    printf("ok\n");
    return 0;
}
