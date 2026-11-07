// Unit test for the reimplemented combat and probe team helpers, on a
// small made-up map: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/combat.h"
#include "../src/engine/game.h"

enum { W = 16, H = 12 };

static struct map_tile tiles[W / 2 * H], *tiles_ptr = tiles;
static int32_t width = W, height = H, half_width = W / 2, base_count = 3, veh_count, turn = 100, current = 7;
static int32_t find_dist, net, prefs, gender, plural, algo, target, hsa, lift[4], refs[5], *net_ref, battle;
static uint32_t flat, state;
static uint8_t humans, alive = 0xfe;
static struct base bases[3];
static struct veh veh_slots[1 + VEH_COUNT + 1], *vehs = veh_slots + 1;  // one before, for unit -1
static struct faction_data factions[FACTION_COUNT];
static struct faction_def defs[FACTION_COUNT];
static struct unit_proto protos[PROTO_COUNT];
static struct rules_chassis chassis[CHASSIS_COUNT];
static struct rules_weapon weapons[WEAPON_COUNT];
static int32_t radius_x[RADIUS_SQUARES], radius_y[RADIUS_SQUARES], radius_count[9];
static char battle_text[256];
static int failures;

#define EXPECT(expr, want)                                                       \
    do {                                                                         \
        int got_ = (expr);                                                       \
        if (got_ != (want)) {                                                    \
            printf("FAIL %s = %d, expected %d\n", #expr, got_, (int)(want));    \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static struct map_tile *at(int x, int y) { return &tiles[x / 2 + y * (W / 2)]; }

static char said[10][64];
static int popps, last_popp, crossed, picked = -1, achieved_tech = -2, rand_value, popp_answer;
static int parse_says_stub(int slot, char *text, int g, int p) {
    snprintf(said[slot], sizeof said[slot], "%s", text);
    return 0;
}
static int parse_num_stub(int slot, int value) { return 0; }
static int popp_stub(char *file, char *label, int flags, char *picture, int unk) {
    popps++;
    last_popp = (int)(label - (char *)game_at(0x400000)) + 0x400000;
    return popp_answer;
}
static void double_cross_stub(int f, int o, int u) { crossed++; }
static int32_t ambience_flags;
static int ambience_stub(int sound) { return 0; }
static int tech_pick_stub(int f, int u, int o, char *label) { return picked; }
static void tech_achieve_stub(int f, int t, int from, int steal) { achieved_tech = t; }
static void __attribute__((thiscall)) netmsg_stub(void *self, char *label, int d, int u, char *s) {}
static int rand_stub(void) { return rand_value; }
static void log_say_stub(char *t, int a, int b, int c) {}
static void __attribute__((thiscall)) deletion_stub(void *l, int v) {}
static void __attribute__((thiscall)) check_base_stub(void *w, int b) {}
static void __attribute__((thiscall)) check_loc_stub(void *w, int x, int y, int u) {}

int main(void) {
    static uint8_t image[0x300000];  // strings, from 0x400000; empty but for those set below
    game_test_image = image;
    strcpy((char *)game_at(0x6902b8), "100%");
    strcpy((char *)game_at(0x6902c0), "%");
    strcpy((char *)game_at(0x6902c4), ", ");
    strcpy((char *)game_at(0x6902c8), "%");
    game_map = &tiles_ptr, game_map_width = &width, game_map_height = &height, game_map_half_width = &half_width;
    game_map_flat = &flat, game_bases = bases, game_base_count = &base_count, game_vehs = vehs;
    game_veh_count = &veh_count, game_turn = &turn, game_current_faction = &current, game_net_multiplayer = &net;
    game_preferences = &prefs, game_default_gender = &gender, game_default_plural = &plural;
    game_human_factions = &humans, game_factions_alive = &alive, game_factions = factions, game_faction_defs = defs;
    game_protos = protos, game_chassis = chassis, game_weapons = weapons, game_state = &state;
    game_radius_x = radius_x, game_radius_y = radius_y, game_radius_count = radius_count;
    game_battle_text = battle_text, game_probe_algo = &algo, game_probe_target = &target;
    game_probe_target_hsa = &hsa, game_parse_says = parse_says_stub, game_parse_num = parse_num_stub;
    static char *script = "script";
    game_popp = popp_stub, game_script_file = &script, game_double_cross = double_cross_stub;
    game_ambience_flags = &ambience_flags, game_ambience = ambience_stub, game_tech_pick = tech_pick_stub, game_tech_achieve = tech_achieve_stub;
    game_netmsg_pop = netmsg_stub, game_rand = rand_stub;
    game_veh_lift_x = &lift[0], game_veh_lift_y = &lift[1], game_veh_lifted = &lift[2];
    game_log_say = log_say_stub, game_deletion_add = deletion_stub, game_base_win_check_base = check_base_stub;
    game_base_win_check_loc = check_loc_stub, game_net_veh_ref = &net_ref, game_battle_state = &battle;
    game_veh_ref_map_1 = &refs[0], game_veh_ref_map_2 = &refs[1], game_veh_ref_3 = &refs[2], game_veh_ref_4 = &refs[3];
    game_current_veh = &refs[4], game_base_find_dist = &find_dist;
    // The radius table's first squares: the centre, then the eight around.
    static const int rx[9] = {0, 1, 2, 1, 0, -1, -2, -1, 0}, ry[9] = {0, -1, 0, 1, 2, 1, 0, -1, -2};
    for (int i = 0; i < 9; i++) radius_x[i] = rx[i], radius_y[i] = ry[i];
    for (int i = 9; i < RADIUS_SQUARES; i++) radius_x[i] = 0, radius_y[i] = -40;  // off the map
    radius_count[0] = 1, radius_count[1] = 9;
    for (int y = 0; y < H; y++)
        for (int x = y & 1; x < W; x += 2) *at(x, y) = (struct map_tile){.climate = 4 << 5, .owner_site = 0xf, .territory = -1};
    bases[0] = (struct base){.x = 2, .y = 2, .faction = 1, .pop_size = 4};
    bases[1] = (struct base){.x = 10, .y = 4, .faction = 2, .pop_size = 6};
    bases[2] = (struct base){.x = 6, .y = 8, .faction = 1, .pop_size = 2};
    for (int b = 0; b < 3; b++) at(bases[b].x, bases[b].y)->bits |= MAP_BASE;
    strcpy(bases[0].name, "Alpha"), strcpy(bases[1].name, "Beta"), strcpy(bases[2].name, "Gamma");

    // Distance from headquarters, or 12 without one.
    EXPECT(vulnerable(1, 8, 2), 12);
    bases[2].facilities_built[0] |= 1 << 1;  // headquarters
    EXPECT(vulnerable(1, 6, 8), 0);
    EXPECT(vulnerable(1, 10, 8), 3);
    EXPECT(vulnerable(2, 6, 8), 12);

    // Energy stolen: reserves times the base's size over one more than the
    // faction's population.
    factions[2].energy_credits = 140, factions[2].pop_total = 13;
    EXPECT(steal_energy(1), 60);
    factions[2].energy_credits = 0;
    EXPECT(steal_energy(1), 0);

    // Probe odds: certain success, then success and survival.
    EXPECT(success_rates(0, 3, -1, -1), -1);
    EXPECT(strcmp(said[0], "100%"), 0);
    target = 2;
    EXPECT(success_rates(1, 4, 1, -1), 67);  // 100 - 100 / 3; survival 100 - 200 / 4
    EXPECT(strcmp(said[1], "67%, 50%"), 0);
    factions[2].se[SE_PROBE] = -1;  // weaker defenses
    EXPECT(success_rates(1, 4, 1, -1), 75);
    bases[1].facilities_built[34 / 8] |= 1 << (34 % 8);  // a Covert Ops Center undoes it
    EXPECT(success_rates(1, 4, 1, 1), 67);
    algo = 1;
    EXPECT(success_rates(1, 4, 1, 1), 84);  // losses halved
    hsa = 1;
    EXPECT(success_rates(1, 4, 1, 1), 16);  // halved, and (as the original) halved again
    EXPECT(strcmp(said[1], "33%, 50%"), 0);
    algo = hsa = 0;
    EXPECT(success_rates(2, 0, 0, -1), 100);  // morale at least 1
    EXPECT(strcmp(said[2], "100%, 50%"), 0);

    // An attack breaks a truce, treaty or pact, as the victim holds it.
    EXPECT(act_of_aggression(1, 2), 0);
    EXPECT(factions[1].diplo_status[2] & 0x8010, 0x8010);  // vendetta and bit 0x8000, both ways
    EXPECT(factions[2].diplo_status[1] & 0x8010, 0x8010);
    factions[1].diplo_status[2] = TREATY_TREATY;
    EXPECT(act_of_aggression(1, 2), 0);
    factions[2].diplo_status[1] = 0x4;
    EXPECT(act_of_aggression(1, 2), 1);
    EXPECT(crossed, 1);

    // A tech stolen; 1 when there was none.
    EXPECT(steal_tech(0, 2, 1), 0);
    humans = 1 << 1;  // no rethinking of builds for a human
    EXPECT(steal_tech(1, 2, 1), 1);
    EXPECT(achieved_tech, 9999);
    picked = 12;
    EXPECT(steal_tech(1, 2, 0), 0);
    EXPECT(achieved_tech, 12);
    net = 1, current = 3;
    EXPECT(steal_tech(1, 2, 0), 0);  // a human elsewhere in a network game
    net = 0, humans = 0, current = 7;

    // Orbital defense pods: a hit at the first roll that comes up 50 or more.
    struct faction_data *D = &factions[2];
    EXPECT(shoot(1, 2, 10, 4), 0);  // no pods, no flechettes
    D->satellites[3] = 2;
    rand_value = 75;
    EXPECT(shoot(1, 2, 10, 4), 1);
    EXPECT(D->odp_deployed, 1);
    EXPECT(D->satellites[3], 2);
    rand_value = 10;  // every roll misses: a pod is given up
    EXPECT(shoot(1, 2, 10, 4), 1);
    EXPECT(D->odp_deployed, 1);
    EXPECT(D->satellites[3], 1);
    // A human defender playing here is asked, and may say no.
    humans = 1 << 2, current = 2, popp_answer = 1, popps = 0;
    EXPECT(shoot(1, 2, 10, 4), 0);
    EXPECT(popps, 1);
    EXPECT(D->satellites[3], 1);
    humans = 0, current = 7, popp_answer = 0;
    // Flechette defenses at a base near the target, of anyone but the
    // attacker.
    D->satellites[3] = D->odp_deployed = 0;
    bases[1].facilities_built[39 / 8] |= 1 << (39 % 8);
    rand_value = 60;
    EXPECT(shoot(1, 2, 10, 4), 0);
    rand_value = 20;
    EXPECT(shoot(1, 2, 11, 5), 1);
    EXPECT(shoot(2, 1, 11, 5), 0);  // its own base's

    // Units killed: a stack's chaff is its native units and those with no
    // hit points left; later units move down a slot.
    veh_count = 4;
    protos[1].reactor = 1;
    for (int i = 0; i < 4; i++)
        vehs[i] = (struct veh){.x = 4, .y = 6, .proto = 1, .faction = 1, .next_in_stack = (int16_t)(i < 3 ? i + 1 : -1),
                               .prev_in_stack = (int16_t)(i - 1), .home_base = -1};
    vehs[1].faction = 0, vehs[3].damage = 10;
    vehs[-1].faction = 0;
    factions[1].units_active[1] = 3, factions[0].units_active[1] = 1;
    int killed = 0;
    battle_kill_chaff(2, &killed, NULL, NULL, -1, -1);
    EXPECT(killed, 2);
    EXPECT(veh_count, 2);
    EXPECT(factions[1].units_lost[1], 1);
    EXPECT(factions[0].units_lost[1], 1);
    EXPECT(vehs[0].next_in_stack, 1);
    EXPECT(vehs[1].prev_in_stack, 0);
    // An objective killed counts for the attacker's faction.
    vehs[1].flags = VEH_FLAG_UNK_20;
    EXPECT(battle_kill(1, &killed, NULL, NULL, -1, 3), 1);
    EXPECT(factions[3].objective_bonus, 1);
    battle_kill_stack(0, &killed, NULL, NULL, -1, -1);
    EXPECT(veh_count, 0);
    EXPECT(killed, 4);

    if (failures) return 1;
    printf("engine_combat: ok\n");
    return 0;
}
