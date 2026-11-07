// Unit test for supply pods, monoliths and artifacts, on a small made-up
// map with a scripted rand() and stand-ins for the game's messages and
// drawing: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/base.h"
#include "../src/engine/events.h"
#include "../src/engine/game.h"

enum { W = 16, H = 10, AREA = W / 2 * H };
static struct map_tile tiles[AREA], *tiles_ptr = tiles;
static int32_t width = W, height = H, half_width = W / 2, area = AREA;
static uint32_t flat = 1, state, rules_, more_rules_;
static int32_t near_x[8] = {1, 2, 1, 0, -1, -2, -1, 0}, near_y[8] = {-1, 0, 1, 2, 1, 0, -1, -2};
static int32_t radius_x[RADIUS_SQUARES], radius_y[RADIUS_SQUARES];
static struct unit_proto protos[16];
static struct rules_chassis chassis[3];
static struct rules_weapon weapons[2];
static struct faction_data factions[FACTION_COUNT];
static struct faction_def faction_defs[FACTION_COUNT];
static struct veh vehs[8];
static struct base bases[3];
static struct continent continents[4];
static int32_t veh_count, base_count, net, turn, projects[SP_COUNT], current_faction = 1, find_dist, difficulty = 3;
static int32_t expansion, native_life, more_prefs, map_seed, ranked[2];
static uint8_t humans, alive = 0xfe, achieved[TECH_COUNT];
static char battle_text[256], parse_text[2560];
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

// rand() plays back a script, then 0.
static int script[32], script_len, script_pos;
static void rolls(int n, const int *v) {
    memcpy(script, v, (size_t)n * sizeof *v);
    script_len = n, script_pos = 0;
}
static int rand_(void) { return script_pos < script_len ? script[script_pos++] : 0; }

static int pops, achieves, last_tech, popps;
static const char *last_label;
static void __attribute__((thiscall)) netmsg_pop_(void *netmsg, char *label, int delay, int unk, char *sound) {
    (void)netmsg, (void)delay, (void)unk, (void)sound;
    pops++, last_label = label;
}
static int parse_says_(int slot, char *text, int gender, int plural) {
    (void)slot, (void)text, (void)gender, (void)plural;
    return 0;
}
static int parse_say_(int slot, int text, int gender, int plural) {
    (void)slot, (void)text, (void)gender, (void)plural;
    return 0;
}
static int parse_num_(int slot, int value) {
    (void)slot, (void)value;
    return 0;
}
static int unity_(int x, int y, int faction) {
    (void)x, (void)y, (void)faction;
    return 0;
}
static void __attribute__((thiscall)) fx_(void *fx, int id) { (void)fx, (void)id; }
static void draw_(int x, int y, int unk) { (void)x, (void)y, (void)unk; }
static void draw_map_(int unk) { (void)unk; }
static void achieve_(int faction, int tech, int from, int steal) {
    (void)faction, (void)from, (void)steal;
    achieves++, last_tech = tech;
}
static int popp_(char *file, char *label, int flags, char *picture, int fn) {
    (void)file, (void)label, (void)flags, (void)picture, (void)fn;
    popps++;
    return 0;
}
static void __attribute__((thiscall)) win_base_(void *win, int base) { (void)win, (void)base; }
static void __attribute__((thiscall)) win_loc_(void *win, int x, int y, int unk) { (void)win, (void)x, (void)y, (void)unk; }
static void log_say_(const char *s, int a, int b, int c) { (void)s, (void)a, (void)b, (void)c; }

static char text_[] = "TEXT";

static void land(void) {
    for (int i = 0; i < AREA; i++)
        tiles[i] = (struct map_tile){.climate = 3 << 5, .region = 1, .owner_site = 0xf, .territory = -1};
    veh_count = base_count = 0;
    find_dist = 9999;  // base_find() leaves it as it was when there is no base
    memset(factions, 0, sizeof factions);
}
static void put_unit(int v, int x, int y, int faction, int proto) {
    vehs[v] = (struct veh){.x = (int16_t)x, .y = (int16_t)y, .proto = (int16_t)proto, .faction = (uint8_t)faction,
                           .home_base = -1, .next_in_stack = -1, .prev_in_stack = -1};
    at(x, y)->bits |= MAP_UNIT;
    at(x, y)->owner_site = (uint8_t)faction;
    if (veh_count <= v) veh_count = v + 1;
}
static void put_base(int b, int x, int y, int faction) {
    bases[b] = (struct base){.x = (int16_t)x, .y = (int16_t)y, .faction = (uint8_t)faction};
    strcpy(bases[b].name, "Base");
    at(x, y)->bits |= MAP_BASE;
    at(x, y)->owner_site = (uint8_t)faction;
    if (base_count <= b) base_count = b + 1;
    factions[faction].base_count++;
}

static void test_energy(void) {
    land();
    put_unit(0, 8, 4, 1, 0);
    turn = 10;
    // Roll 0 of 40 (no base near): energy, 2 x 25 early on.
    rolls(2, (int[]){0, 1});
    EXPECT(goody_box(0), 0);
    EXPECT(factions[1].energy_credits, 50);
    EXPECT(*(int32_t *)((char *)&factions[1] + 0x2040), 1);  // pods opened
    EXPECT(*(int32_t *)((char *)&factions[1] + 0x3c8), 1);   // towards the next tech
    // Later in the game, 50 a unit.
    turn = 60;
    rolls(2, (int[]){0, 2});
    goody_box(0);
    EXPECT(factions[1].energy_credits, 50 + 150);
}

static void test_resource(void) {
    land();
    put_unit(0, 8, 4, 1, 0);
    // Roll 25 of 40: a resource on the square; a third roll of 0 of 3 ends it.
    int n = pops;
    current_faction = 2;  // not the player's: no message
    turn = 10;
    rolls(2, (int[]){25, 0});
    EXPECT(goody_box(0), 0);
    EXPECT((at(8, 4)->bits & 0x400) != 0, 1);
    EXPECT(pops, n);
    // Otherwise another outcome follows: a new roll (energy).
    at(8, 4)->bits &= ~0x400u;
    rolls(6, (int[]){25, 1, 0, 1, 1, 0});
    goody_box(0);
    EXPECT(factions[1].energy_credits, 50);
    current_faction = 1;
}

static void test_fungus(void) {
    land();
    put_unit(0, 8, 4, 1, 0);
    // Roll 7: fungus spreads over the land around, clearing roads; no tower.
    at(10, 4)->bits |= MAP_ROAD;
    rolls(3, (int[]){7, 0, 0});
    goody_box(0);
    EXPECT((at(8, 4)->bits & MAP_FUNGUS) != 0, 1);
    EXPECT((at(10, 4)->bits & (MAP_FUNGUS | MAP_ROAD)), MAP_FUNGUS);
    EXPECT(veh_count, 1);
    // Near a base: a new roll instead (energy).
    land();
    put_unit(0, 8, 4, 1, 0);
    put_base(0, 10, 4, 1);
    find_dist = 0;
    rolls(4, (int[]){7, 0, 1, 0});
    goody_box(0);
    EXPECT((at(8, 4)->bits & MAP_FUNGUS), 0);
    EXPECT(factions[1].energy_credits, 25);
}

static void test_terrain(void) {
    land();
    put_unit(0, 8, 4, 1, 0);
    // Roll 11, choice 1 (farms), the squares around two times in three.
    rolls(4, (int[]){11, 1, 1, 0});
    goody_box(0);
    EXPECT((at(8, 4)->bits & MAP_FARM) != 0, 1);
    EXPECT((at(9, 3)->bits & MAP_FARM) != 0, 1);   // rolled 1 of 3
    EXPECT((at(10, 4)->bits & MAP_FARM) != 0, 0);  // rolled 0 of 3
    // Mines need rolling or rocky land.
    land();
    put_unit(0, 8, 4, 1, 0);
    at(9, 3)->rocky_lock = 0x40;
    rolls(4, (int[]){11, 2, 1, 1});
    goody_box(0);
    EXPECT((at(8, 4)->bits & MAP_MINE), 0);
    EXPECT((at(9, 3)->bits & (MAP_MINE | MAP_ROAD)), MAP_MINE | MAP_ROAD);
}

static void test_build(void) {
    land();
    put_unit(0, 8, 4, 1, 0);
    put_base(0, 14, 4, 1);
    bases[0].queue[0] = -3;  // a facility of 6 rows
    static struct rules_facility facilities[4];
    facilities[3].cost = 6, facilities[3].name = text_;
    game_facilities = facilities;
    // Roll 5: the nearest own base's production is completed (a human's
    // rows are 10 minerals).
    humans = 2;
    rolls(1, (int[]){5});
    goody_box(0);
    int full = cost_factor(1, 1, -1) * 6;
    EXPECT(bases[0].minerals, full);
    EXPECT(full, 60);
    // Not for a colony pod in a base of 1.
    bases[0].minerals = 0, bases[0].queue[0] = 3, bases[0].pop_size = 1, protos[3].plan = PLAN_COLONY;
    rolls(4, (int[]){5, 0, 1, 0});  // then a new roll: energy
    goody_box(0);
    EXPECT(bases[0].minerals, 0);
    EXPECT(factions[1].energy_credits, 25);
    protos[3].plan = 1, humans = 0;
}

static void test_monolith(void) {
    land();
    put_unit(0, 8, 4, 1, 1);  // unarmed
    int n = pops;
    monolith(0);
    EXPECT(vehs[0].state & 0x100, 0x100);
    EXPECT(pops, n + 1);  // what a monolith is, once
    monolith(0);
    EXPECT(pops, n + 1);
    // The player's unit passing by on its way elsewhere is left alone.
    put_unit(1, 8, 4, 1, 1);
    vehs[1].order = 0x18, vehs[1].waypoint_x[0] = 12, vehs[1].waypoint_y[0] = 4;
    monolith(1);
    EXPECT(vehs[1].state & 0x100, 0);
    // Battle ogres too.
    put_unit(2, 8, 4, 2, 16);
    monolith(2);
    EXPECT(vehs[2].state & 0x100, 0);
}

static void test_artifact(void) {
    land();
    put_unit(0, 8, 4, 1, 7);
    put_base(0, 8, 4, 1);
    bases[0].state = 0x400;  // linked already: told once
    int n = pops;
    EXPECT(study_artifact(0), 0);
    EXPECT(pops, n + 1);
    EXPECT(bases[0].state, 0xc00);
    EXPECT(study_artifact(0), 0);
    EXPECT(pops, n + 1);
    // Unless it holds the Universal Translator: then the artifact is used.
    projects[20] = 0;
    EXPECT(study_artifact(0), 1);
    EXPECT(veh_count, 0);
    EXPECT(factions[1].flags & 0x10, 0x10);
    EXPECT(popps, 1);
    projects[20] = -1;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    static uint8_t image[0x300000];  // the game's strings, from 0x400000: all empty here
    game_test_image = image;
    game_map = &tiles_ptr, game_map_width = &width, game_map_height = &height, game_map_half_width = &half_width;
    game_map_area = &area, game_map_flat = &flat, game_state = &state, game_rules = &rules_;
    game_more_rules = &more_rules_;
    game_radius_near_x = near_x, game_radius_near_y = near_y, game_radius_x = radius_x, game_radius_y = radius_y;
    for (int i = 0; i < 8; i++) radius_x[1 + i] = near_x[i], radius_y[1 + i] = near_y[i];
    for (int i = 9; i < RADIUS_SQUARES; i++) radius_x[i] = 4 * W, radius_y[i] = 0;
    game_protos = protos, game_chassis = chassis, game_weapons = weapons;
    game_factions = factions, game_faction_defs = faction_defs, game_vehs = vehs, game_veh_count = &veh_count;
    game_bases = bases, game_base_count = &base_count, game_human_factions = &humans, game_net_multiplayer = &net;
    game_secret_projects = projects, game_turn = &turn, game_current_faction = &current_faction;
    game_base_find_dist = &find_dist, game_difficulty = &difficulty, game_expansion = &expansion;
    game_native_life = &native_life, game_more_preferences = &more_prefs, game_map_seed = &map_seed;
    game_ranked_faction = ranked, game_factions_alive = &alive, game_tech_achieved = achieved;
    game_continents = continents, game_battle_text = battle_text, game_parse_text = parse_text;
    game_rand = rand_, game_netmsg_pop = netmsg_pop_, game_parse_says = parse_says_, game_parse_say = parse_say_;
    game_parse_num = parse_num_, game_do_unity_crash = unity_, game_fx_play = fx_, game_fx_fade = fx_;
    game_fx_stop = fx_, game_draw_tile = draw_, game_draw_tiles = draw_, game_draw_map = draw_map_;
    game_tech_achieve = achieve_, game_popp = popp_, game_log_say = (void *)log_say_;
    static char *script_file = "script";
    game_script_file = &script_file;
    static struct rules_basic rules_basic = {.mineral_cost_multi = 10, .nutrient_cost_multi = 10};
    static int32_t map_settings[8] = {2};
    game_rules_basic = &rules_basic, game_map_settings = map_settings;
    static struct rules_tech techs[TECH_COUNT];
    game_tech = techs;
    static int32_t refs[4], current_veh, *net_ref;
    game_veh_ref_map_1 = &refs[0], game_veh_ref_map_2 = &refs[1], game_veh_ref_3 = &refs[2], game_veh_ref_4 = &refs[3];
    game_current_veh = &current_veh, game_net_veh_ref = &net_ref;
    static int32_t lift_x, lift_y, lifted;
    game_veh_lift_x = &lift_x, game_veh_lift_y = &lift_y, game_veh_lifted = &lifted;
    game_base_win_check_base = win_base_, game_base_win_check_loc = win_loc_;
    chassis[0] = (struct rules_chassis){.speed = 1, .triad = TRIAD_LAND};
    weapons[1].offense = 1;
    for (int i = 0; i < 16; i++) protos[i] = (struct unit_proto){.chassis = 0, .weapon = 1, .plan = 1};
    protos[1].weapon = 0, protos[7].weapon = 0, protos[7].plan = 12;
    for (int i = 0; i < SP_COUNT; i++) projects[i] = -1;
    for (int i = 0; i < TECH_COUNT; i++) techs[i].preq1 = techs[i].preq2 = TECH_DISABLED;
    test_energy();
    test_resource();
    test_fungus();
    test_terrain();
    test_monolith();
    test_artifact();
    test_build();
    if (failures) return 1;
    printf("engine_events: ok\n");
    return 0;
}
