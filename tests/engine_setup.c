// Unit test for a faction's escape and end, and the landing message, on a
// small made-up map with a scripted rand() and stand-ins for the game's
// messages and drawing: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/setup.h"

enum { W = 24, H = 16, AREA = W / 2 * H };
static struct map_tile tiles[AREA], *tiles_ptr = tiles;
static int32_t width = W, height = H, half_width = W / 2, area = AREA, area_sqrt = 2, ocean = 1;
static uint32_t flat = 1, state, rules_, more_rules_;
static int32_t near_x[8] = {1, 2, 1, 0, -1, -2, -1, 0}, near_y[8] = {-1, 0, 1, 2, 1, 0, -1, -2};
static int32_t radius_x[RADIUS_SQUARES], radius_y[RADIUS_SQUARES], radius_count[9];
static struct unit_proto protos[PROTO_COUNT];
static struct rules_chassis chassis[3];
static struct rules_weapon weapons[2];
static struct faction_data factions[FACTION_COUNT];
static struct faction_def faction_defs[FACTION_COUNT];
static struct veh vehs[16];
static struct base bases[4];
static struct continent continents[4];
static int32_t veh_count, base_count, net, turn, projects[SP_COUNT], current_faction = 1, find_dist, difficulty = 3;
static int32_t expansion, rankings[8], victory, language;
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

// rand() as the C runtime's.
static uint32_t seed = 1;
static int rand_(void) {
    seed = seed * 214013u + 2531011u;
    return (int)((seed >> 16) & 0x7fff);
}

static int says, say, nums, xpops, popps;
static char last_label[64];
static int parse_says_(int slot, char *text, int gender, int plural) {
    (void)slot, (void)text, (void)gender, (void)plural;
    return says++;
}
static int parse_say_(int slot, int text, int gender, int plural) {
    (void)slot, (void)text, (void)gender, (void)plural;
    return say++;
}
static int parse_num_(int slot, int value) {
    (void)slot, (void)value;
    return nums++;
}
static void x_pop_(char *label, int unk) {
    (void)unk;
    xpops++;
    snprintf(last_label, sizeof last_label, "%s", label);
}
static int popp_(char *file, char *label, int flags, char *picture, int fn) {
    (void)file, (void)label, (void)flags, (void)picture, (void)fn;
    return popps++;
}
static uint32_t __attribute__((stdcall)) upper_(char *s) {
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32);
    return 0;
}
static uint32_t(__attribute__((stdcall)) * upper_ptr)(char *s) = upper_;
static void draw_(int x, int y, int unk) { (void)x, (void)y, (void)unk; }
static void log_say_(const char *s, int a, int b, int c) { (void)s, (void)a, (void)b, (void)c; }
static void __attribute__((thiscall)) win_base_(void *win, int base) { (void)win, (void)base; }
static void __attribute__((thiscall)) win_loc_(void *win, int x, int y, int unk) { (void)win, (void)x, (void)y, (void)unk; }

static void draw_radius_(int x, int y, int r, int unk) { (void)x, (void)y, (void)r, (void)unk; }
static void __attribute__((thiscall)) redraw_(void *win) { (void)win; }
static char text_[] = "Project";

// Flat wet land all over, one region, within reach of no base.
static void land(void) {
    for (int i = 0; i < AREA; i++)
        tiles[i] = (struct map_tile){.climate = 3 << 5 | 1 << 3, .region = 1, .owner_site = 0xf, .territory = -1,
                                     .bits = MAP_BASE_RADIUS};
    veh_count = base_count = 0;
    find_dist = 9999;
    memset(factions, 0, sizeof factions);
    continents[1] = (struct continent){.tile_count = 300, .open_terrain = 300};
}

static void test_escape(void) {
    land();
    turn = 50;
    rules_ = 0;
    alive = 0xfe;
    factions[2].energy_credits = 7;
    factions[2].tech_cost = 99;
    factions[2].units_active[5] = 3;
    faction_defs[2].rule_energy = 5;
    // Faction 2 escapes from faction 3: a colony pod and formers, then
    // another pod and its best defender (none: the scout).
    EXPECT(setup_player(2, 3, 0), 1);
    EXPECT(veh_count, 4);
    EXPECT(vehs[0].proto, 0);
    EXPECT(vehs[1].proto, 1);
    EXPECT(vehs[2].proto, 0);
    EXPECT(vehs[3].proto, 2);
    EXPECT(vehs[0].x == vehs[3].x && vehs[0].y == vehs[3].y, 1);
    EXPECT(vehs[0].y >= 2 && vehs[0].y < H - 2, 1);
    EXPECT(factions[2].energy_credits, 7 + 10 + 5);  // the starting reserve and the file's
    EXPECT(factions[2].tech_cost, -1);
    EXPECT(factions[2].alloc_labs, 5);
    EXPECT(factions[2].last_base_turn, 50);
    EXPECT((at(vehs[0].x, vehs[0].y)->visibility >> 2) & 1, 1);
}

static void test_out(void) {
    land();
    // Too late to escape: out of the game. Faction 1 (the player) is
    // left, and has won by conquest.
    turn = 150;
    alive = (1 << 1) | (1 << 2);
    rankings[7] = 1, rankings[6] = 3;
    current_faction = 1, state = 0, victory = 0;
    EXPECT(setup_player(2, 1, 0), 0);
    EXPECT(alive, 1 << 1);
    EXPECT(veh_count, 0);
    EXPECT(popps, 1);
    EXPECT(victory, 4);
    EXPECT((int)(state & 0x2001), 0x2001);
    // A human is out at once.
    alive = 0xfe, humans = 1 << 4, state = 0, victory = 0;
    EXPECT(setup_player(4, 1, 0), 0);
    EXPECT(alive, 0xfe & ~(1 << 4));
    EXPECT(victory, 7);
    EXPECT((int)(state & 1), 1);
    humans = 0;
    // The player's faction: the message that the game is lost.
    alive = 0xfe, state = 0, current_faction = 5;
    int n = xpops;
    EXPECT(setup_player(5, 0, 0), 0);
    EXPECT(xpops, n + 1);
    EXPECT(victory, 15);
    current_faction = 1;
    // Scenario editing: no landing at all.
    turn = 50, alive = 0xfe;
    EXPECT(setup_player(6, -2, 0), 0);
    EXPECT(alive, 0xfe & ~(1 << 6));
}

static void test_duplicates(void) {
    land();
    turn = 50, alive = 0xfe;
    strcpy(faction_defs[1].leader_name, "Lal");
    strcpy(faction_defs[3].leader_name, "Lal");
    strcpy(faction_defs[3].noun, "Peacekeepers");
    EXPECT(setup_player(3, -2, 0), 0);
    EXPECT(strcmp(faction_defs[3].leader_name, "Lal-2"), 0);
    EXPECT(strcmp(faction_defs[3].noun, "Peacekeepers"), 0);
}

static void test_planetfall(void) {
    strcpy(faction_defs[2].filename, "gaians");
    turn = 0, says = 0;
    planetfall(2);
    EXPECT(strcmp(last_label, "PF"), 0);
    EXPECT(says, 4);
    // Later on: the turn and the faction's secret projects.
    land();
    bases[0] = (struct base){.faction = 2};
    base_count = 1;
    projects[3] = 0;
    turn = 120;
    planetfall(2);
    EXPECT(strcmp(last_label, "PF2"), 0);
    EXPECT(nums, 1);
    EXPECT(say, 1);
    projects[3] = -1;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    static uint8_t image[0x300000];  // the game's strings, from 0x400000
    game_test_image = image;
    strcpy((char *)image + 0x691500 - 0x400000, "-");
    strcpy((char *)image + 0x68f56c - 0x400000, "PF");
    strcpy((char *)image + 0x68f578 - 0x400000, "2");
    game_map = &tiles_ptr, game_map_width = &width, game_map_height = &height, game_map_half_width = &half_width;
    game_map_area = &area, game_map_area_sqrt = &area_sqrt, game_ocean_coverage = &ocean, game_map_flat = &flat;
    game_state = &state, game_rules = &rules_, game_more_rules = &more_rules_;
    game_radius_near_x = near_x, game_radius_near_y = near_y, game_radius_x = radius_x, game_radius_y = radius_y;
    game_radius_count = radius_count;
    for (int i = 0; i < 8; i++) radius_x[1 + i] = near_x[i], radius_y[1 + i] = near_y[i];
    for (int i = 9; i < RADIUS_SQUARES; i++) radius_x[i] = 4 * W, radius_y[i] = 0;
    radius_count[0] = 1;
    for (int i = 1; i < 9; i++) radius_count[i] = 9;
    game_protos = protos, game_chassis = chassis, game_weapons = weapons;
    game_factions = factions, game_faction_defs = faction_defs, game_vehs = vehs, game_veh_count = &veh_count;
    game_bases = bases, game_base_count = &base_count, game_human_factions = &humans, game_net_multiplayer = &net;
    game_secret_projects = projects, game_turn = &turn, game_current_faction = &current_faction;
    game_base_find_dist = &find_dist, game_difficulty = &difficulty, game_expansion = &expansion;
    game_factions_alive = &alive, game_tech_achieved = achieved, game_continents = continents;
    game_battle_text = battle_text, game_parse_text = parse_text, game_faction_rankings = rankings;
    game_victory_type = &victory, game_language = &language;
    game_rand = rand_, game_parse_says = parse_says_, game_parse_say = parse_say_, game_parse_num = parse_num_;
    game_x_pop = x_pop_, game_popp = popp_, game_char_upper = &upper_ptr;
    game_draw_tile = draw_, game_log_say = (void *)log_say_;
    game_draw_radius = draw_radius_, game_graphic_win_redraw = redraw_;
    static int32_t more_prefs;
    game_more_preferences = &more_prefs;
    static int32_t gender, plural;
    game_parse_gender = &gender, game_parse_plural = &plural;
    static char *script_file = "script";
    game_script_file = &script_file;
    static struct rules_basic rules_basic = {.mineral_cost_multi = 10, .nutrient_cost_multi = 10};
    *(int32_t *)((char *)&rules_basic + 0x7c) = 10;  // the starting energy reserve
    game_rules_basic = &rules_basic;
    static int32_t map_settings[8] = {2};
    game_map_settings = map_settings;
    static struct rules_tech techs[TECH_COUNT];
    game_tech = techs;
    static int32_t tectonic[FACTION_COUNT];
    game_tectonic_detonations = tectonic;
    static void *wins[8];
    game_map_wins = wins;
    static int32_t refs[4], current_veh, *net_ref;
    game_veh_ref_map_1 = &refs[0], game_veh_ref_map_2 = &refs[1], game_veh_ref_3 = &refs[2], game_veh_ref_4 = &refs[3];
    game_current_veh = &current_veh, game_net_veh_ref = &net_ref;
    static int32_t lift_x, lift_y, lifted;
    game_veh_lift_x = &lift_x, game_veh_lift_y = &lift_y, game_veh_lifted = &lifted;
    game_base_win_check_base = win_base_, game_base_win_check_loc = win_loc_;
    static struct rules_facility facilities[FACILITY_COUNT];
    for (int i = 0; i < FACILITY_COUNT; i++) facilities[i].name = text_, facilities[i].preq_tech = TECH_DISABLED;
    game_facilities = facilities;
    chassis[0] = (struct rules_chassis){.speed = 1, .triad = TRIAD_LAND};
    weapons[1].offense = 1;
    for (int i = 0; i < 16; i++) protos[i] = (struct unit_proto){.chassis = 0, .weapon = 1, .plan = 1};
    protos[0].plan = PLAN_COLONY, protos[0].weapon = 0;
    for (int i = 0; i < SP_COUNT; i++) projects[i] = -1;
    for (int i = 0; i < TECH_COUNT; i++) techs[i].preq1 = techs[i].preq2 = TECH_DISABLED;
    test_escape();
    test_out();
    test_duplicates();
    test_planetfall();
    if (failures) return 1;
    printf("engine_setup: ok\n");
    return 0;
}
