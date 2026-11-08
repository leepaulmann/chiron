// Unit test for probe teams' missions, on a small made-up map with a
// scripted rand() and stand-ins for the game's messages, popups and
// drawing: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/probe.h"

enum { W = 24, H = 16, AREA = W / 2 * H, PROBE_PROTO = 10, TANK_PROTO = 11, SP_HSA = 15, PLAN_PROBE = 11 };
static struct map_tile tiles[AREA], *tiles_ptr = tiles;
static int32_t width = W, height = H, half_width = W / 2, area = AREA;
static uint32_t flat = 1, state, rules_;
static int32_t radius_x[RADIUS_SQUARES], radius_y[RADIUS_SQUARES];
static struct unit_proto protos[PROTO_COUNT];
static struct rules_chassis chassis[3];
static struct rules_weapon weapons[2];
static struct rules_armor armor[2];
static struct faction_data factions[FACTION_COUNT];
static struct faction_def faction_defs[FACTION_COUNT];
static struct veh vehs[16];
static struct base bases[4];
static int32_t veh_count, base_count, net, pbem, turn = 50, projects[SP_COUNT], current_faction = 3, find_dist;
static int32_t difficulty = 3, expansion, attack, ranked[2], satan, fear, tribute, charter_a, charter_b;
static uint8_t humans, alive = 0xfe, achieved[TECH_COUNT], monuments[FACTION_COUNT * MONUMENT_SIZE];
static int32_t algo, target, hsa;
static char battle_text[256];
static int failures;

#define EXPECT(expr, want)                                                     \
    do {                                                                       \
        int got_ = (expr);                                                     \
        if (got_ != (want)) {                                                  \
            printf("FAIL %s = %d, expected %d\n", #expr, got_, (int)(want));  \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static struct map_tile *at(int x, int y) { return &tiles[x / 2 + y * (W / 2)]; }

// rand() as the C runtime's.
static uint32_t seed = 1;
static int rand_(void) {
    seed = seed * 214013u + 2531011u;
    return (int)((seed >> 16) & 0x7fff);
}
static uint32_t __attribute__((stdcall)) time_(void) { return 0; }

static int messages, popups;
static int parse_says_(int slot, char *text, int gender, int plural) { return (void)slot, (void)text, (void)gender, (void)plural, 0; }
static int parse_say_(int slot, int text, int gender, int plural) { return (void)slot, (void)text, (void)gender, (void)plural, 0; }
static int parse_num_(int slot, int value) { return (void)slot, (void)value, 0; }
static void __attribute__((thiscall)) netmsg_(void *m, char *label, int delay, int unk, char *pic) {
    (void)m, (void)label, (void)delay, (void)unk, (void)pic;
    messages++;
}
static int popp_(char *file, char *label, int flags, char *picture, int fn) {
    (void)file, (void)label, (void)flags, (void)picture, (void)fn;
    return popups++, 0;
}
static void __attribute__((thiscall)) popup_(void *pop) { (void)pop; }
static void draw_(int x, int y, int unk) { (void)x, (void)y, (void)unk; }
static void __attribute__((thiscall)) win_base_(void *win, int base) { (void)win, (void)base; }
static void __attribute__((thiscall)) win_loc_(void *win, int x, int y, int unk) { (void)win, (void)x, (void)y, (void)unk; }
static void __attribute__((thiscall)) console_(void *console, int unk) { (void)console, (void)unk; }
static int climactic_(void) { return 0; }
static void __attribute__((thiscall)) deletion_(void *list, int veh) { (void)list, (void)veh; }
static void log_say_(char *s, int a, int b, int c) { (void)s, (void)a, (void)b, (void)c; }
static void draw_radius_(int x, int y, int r, int unk) { (void)x, (void)y, (void)r, (void)unk; }

// Flat land all over; one sea column at x 2, y 8.
static void world(void) {
    for (int i = 0; i < AREA; i++)
        tiles[i] = (struct map_tile){.climate = 3 << 5 | 1 << 3, .region = 1, .owner_site = 0xf, .territory = -1};
    at(2, 8)->climate = 1 << 5;
    memset(factions, 0, sizeof factions);
    memset(vehs, 0, sizeof vehs);
    memset(bases, 0, sizeof bases);
    for (int i = 0; i < SP_COUNT; i++) projects[i] = -1;
    // Faction 1's probe team at (8, 8), next to faction 2's base at
    // (10, 8), with faction 2's tank at (12, 8); faction 1's base at (4, 8).
    bases[0] = (struct base){.x = 10, .y = 8, .faction = 2, .pop_size = 4};
    bases[1] = (struct base){.x = 4, .y = 8, .faction = 1, .pop_size = 3};
    strcpy(bases[0].name, "Target"), strcpy(bases[1].name, "Home");
    base_count = 2;
    vehs[0] = (struct veh){.x = 8, .y = 8, .proto = PROBE_PROTO, .faction = 1, .home_base = 1, .morale = 2};
    vehs[1] = (struct veh){.x = 12, .y = 8, .proto = TANK_PROTO, .faction = 2, .home_base = 0};
    for (int v = 0; v < 2; v++) vehs[v].next_in_stack = vehs[v].prev_in_stack = -1;
    veh_count = 2;
    humans = 0, attack = 0, current_faction = 3;
    factions[1].energy_credits = 50, factions[2].energy_credits = 100;
}

// A land team can't reach a base from the sea, nor a unit at sea from
// land.
static void test_amphibious(void) {
    world();
    vehs[0].x = 2;
    attack = 2;
    EXPECT(probe(0, 0, -1, 1), 0);
    world();
    vehs[1].x = 2;
    EXPECT(probe(0, -1, 1, 1), 0);
    EXPECT(veh_count, 2);
}

// The Hunter-Seeker Algorithm: a team without the enhancement is stopped
// (asking first, a human's is refused), and dies on a move made.
static void test_hunter_seeker(void) {
    world();
    projects[SP_HSA] = 0;
    humans = 1 << 1;
    EXPECT(probe(0, 0, -1, 0), 0);
    humans = 0;
    attack = 0;
    EXPECT(probe(0, 0, -1, 1), 1);
    EXPECT(veh_count, 2);
    EXPECT(target, 2);
    attack = 2;
    EXPECT(probe(0, 0, -1, 1), 1);
    EXPECT(veh_count, 1);
}

// Subverting a unit, asked only: what the AI can afford, and immunity.
static void test_subvert_cost(void) {
    world();
    // The cost: (100 + 800) / (12 + 2) = 64, times the unit's 4, halved
    // for a combat unit: 128. The AI's own weighing comes to less (its
    // best weapon and armor are worse than the unit's), so the price is
    // what it needs.
    factions[1].energy_credits = 127;
    EXPECT(probe(0, -1, 1, 0), 0);
    factions[1].energy_credits = 128;
    EXPECT(probe(0, -1, 1, 0), 1);
    // Twice the price at PROBE 2; immune at 3.
    factions[2].se[SE_PROBE] = 2;
    factions[1].energy_credits = 255;
    EXPECT(probe(0, -1, 1, 0), 0);
    factions[1].energy_credits = 256;
    EXPECT(probe(0, -1, 1, 0), 1);
    factions[2].se[SE_PROBE] = 3;
    factions[1].energy_credits = 99999;
    EXPECT(probe(0, -1, 1, 0), 0);
    factions[2].se[SE_PROBE] = 0;
    faction_defs[2].rule_flags = 0x800;
    EXPECT(probe(0, -1, 1, 0), 0);
    faction_defs[2].rule_flags = 0;
    // Native life can't be subverted.
    vehs[1].faction = 0;
    EXPECT(probe(0, -1, 1, 0), 0);
    EXPECT(attack, 0);
}

// A move made: the AI, at peace, infiltrates the base's datalinks; the
// team comes home a veteran.
static void test_infiltrate(void) {
    world();
    attack = 2;
    EXPECT(probe(0, 0, -1, 1), 1);
    EXPECT((factions[1].diplo_status[2] & 0x1000) != 0, 1);
    EXPECT(vehs[0].x, 4);
    EXPECT(vehs[0].y, 8);
    EXPECT(vehs[0].morale, 3);
}

// A human's team, replaying its stored choice (draining the reserves),
// isn't asked anything: the energy changes hands.
static void test_drain(void) {
    world();
    humans = 1 << 1;
    attack = 2;
    factions[1].pop_total = 10, factions[2].pop_total = 9;
    factions[2].energy_credits = 1000;
    vehs[0].unk_2c[0] = 3;  // drain energy reserves, framing nobody
    int before = factions[1].energy_credits;
    EXPECT(probe(0, 0, -1, 1), 1);
    int taken = factions[1].energy_credits - before;
    EXPECT(taken > 0, 1);
    EXPECT(factions[2].energy_credits, 1000 - taken);
    EXPECT(popups, 0);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    static uint8_t image[0x300000];  // the game's strings, from 0x400000
    game_test_image = image;
    game_map = &tiles_ptr, game_map_width = &width, game_map_height = &height, game_map_half_width = &half_width;
    game_map_area = &area, game_map_flat = &flat, game_state = &state, game_rules = &rules_;
    game_radius_x = radius_x, game_radius_y = radius_y;
    for (int i = 0; i < RADIUS_SQUARES; i++) radius_x[i] = i < 21 ? (i % 5) - 2 : 4 * W, radius_y[i] = 0;
    game_protos = protos, game_chassis = chassis, game_weapons = weapons, game_armor = armor;
    game_factions = factions, game_faction_defs = faction_defs, game_vehs = vehs, game_veh_count = &veh_count;
    game_bases = bases, game_base_count = &base_count, game_human_factions = &humans, game_net_multiplayer = &net;
    game_pbem = &pbem, game_secret_projects = projects, game_turn = &turn, game_current_faction = &current_faction;
    game_base_find_dist = &find_dist, game_difficulty = &difficulty, game_expansion = &expansion;
    game_factions_alive = &alive, game_tech_achieved = achieved, game_battle_text = battle_text;
    game_veh_attack_flags = &attack, game_ranked_faction = ranked, game_monuments = monuments;
    game_diplo_great_satan = &satan, game_diplo_fear = &fear, game_diplo_tribute = &tribute;
    game_un_charter_a = &charter_a, game_un_charter_b = &charter_b;
    game_probe_algo = &algo, game_probe_target = &target, game_probe_target_hsa = &hsa;
    game_rand = rand_, game_time_get_time = time_, game_climactic_battle = climactic_;
    game_parse_says = parse_says_, game_parse_say = parse_say_, game_parse_num = parse_num_;
    game_netmsg_pop = netmsg_, game_popp = popp_, game_popup_ctor = popup_, game_popup_dtor = popup_;
    game_draw_tile = draw_, game_draw_tiles = draw_, game_console_update_data = console_;
    game_base_win_check_base = win_base_, game_base_win_check_loc = win_loc_;
    static int32_t battle_state;
    game_battle_state = &battle_state;
    game_deletion_add = deletion_, game_log_say = log_say_, game_draw_radius = draw_radius_;
    static int32_t gender, plural;
    game_parse_gender = &gender, game_parse_plural = &plural;
    static char *script_file = "script";
    game_script_file = &script_file;
    static struct rules_basic rules_basic;
    game_rules_basic = &rules_basic;
    static struct rules_tech techs[TECH_COUNT];
    game_tech = techs;
    static int32_t refs[4], current_veh, *net_ref;
    game_veh_ref_map_1 = &refs[0], game_veh_ref_map_2 = &refs[1], game_veh_ref_3 = &refs[2], game_veh_ref_4 = &refs[3];
    game_current_veh = &current_veh, game_net_veh_ref = &net_ref;
    static int32_t lift_x, lift_y, lifted;
    game_veh_lift_x = &lift_x, game_veh_lift_y = &lift_y, game_veh_lifted = &lifted;
    static struct rules_facility facilities[FACILITY_COUNT];
    game_facilities = facilities;
    chassis[0] = (struct rules_chassis){.speed = 1, .triad = TRIAD_LAND};
    weapons[1].offense = 1;
    armor[1].defense = 1;
    protos[PROBE_PROTO] = (struct unit_proto){.chassis = 0, .weapon = 0, .armor = 1, .plan = PLAN_PROBE, .cost = 3};
    protos[TANK_PROTO] = (struct unit_proto){.chassis = 0, .weapon = 1, .armor = 1, .plan = PLAN_COMBAT, .cost = 4};
    for (int i = 0; i < TECH_COUNT; i++) techs[i].preq1 = techs[i].preq2 = TECH_DISABLED;
    test_amphibious();
    test_hunter_seeker();
    test_subvert_cost();
    test_infiltrate();
    test_drain();
    if (failures) return 1;
    printf("engine_probe: ok\n");
    return 0;
}
