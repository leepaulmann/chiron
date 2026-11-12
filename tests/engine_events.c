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
static struct base bases[4];
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

// ---- The turn's random events, a faction's end ----

// game_random() plays back a script of picks, then 0.
static int picks[16], picks_len, picks_pos;
static void pick(int n, const int *v) {
    memcpy(picks, v, (size_t)n * sizeof *v);
    picks_len = n, picks_pos = 0;
}
static int random_(int lo, int hi) {
    (void)hi;
    return lo + (picks_pos < picks_len ? picks[picks_pos++] : 0);
}
static void reseed_(int seed) { (void)seed; }
static int pop2s, last_pop2_base;
static int pop2_(char *label, char *picture, int base) {
    (void)label, (void)picture;
    pop2s++, last_pop2_base = base;
    return 0;
}
static int __attribute__((thiscall)) focus_(void *console, int x, int y, int faction) {
    (void)console, (void)x, (void)y, (void)faction;
    return 1;
}
static int16_t __attribute__((stdcall)) key_(int key) {
    (void)key;
    return (int16_t)0x8000;  // shift held: no pauses
}
static void wait_(int ms) { (void)ms; }
static void __attribute__((thiscall)) update_(void *console, int unk) { (void)console, (void)unk; }
static void draw_radius_(int x, int y, int r, int unk) { (void)x, (void)y, (void)r, (void)unk; }
static int32_t rankings[8], pbem, flares, sunspots, dust, start_year = 2100;
static int setups;
static int setup_(int faction, int killer, int probe) {
    (void)faction, (void)killer, (void)probe;
    setups++;
    return 0;
}
static char his_[] = "his";
static char *his_her_(int faction, int unk) {
    (void)faction, (void)unk;
    return his_;
}
static int monuments;
static void mon_(int faction, int other) { (void)faction, (void)other, monuments++; }

static void events_base(int faction, int pop) {
    land();
    put_base(0, 8, 4, faction);
    put_base(1, 2, 2, faction);
    bases[0].pop_size = (int8_t)pop;
    turn = 120, net = 0, pbem = 0;
}

static void test_base_events(void) {
    events_base(1, 5);
    current_faction = 1;
    // Base 0 of 100, event 1 (famine): the faction is ranked too high.
    factions[1].ranking = 2;
    pick(2, (int[]){0, 1});
    random_events(1);
    EXPECT(bases[0].event, 0);
    // Ranked low, it falls on the base for ten turns, and the player is told.
    factions[1].ranking = 6;
    int n = pop2s;
    pick(2, (int[]){0, 1});
    random_events(1);
    EXPECT(bases[0].event, 0x400);
    EXPECT(bases[0].random_event_turns, 10);
    EXPECT(pop2s, n + 1);
    // No more events at the base while one is under way.
    pick(2, (int[]){0, 3});
    random_events(1);
    EXPECT(bases[0].event, 0x400);
    // A bumper crop passes the faction in first place by, but one time in
    // four.
    bases[0].event = 0, rankings[7] = 1;
    pick(3, (int[]){0, 0, 2});
    random_events(1);
    EXPECT(bases[0].event, 0);
    pick(3, (int[]){0, 0, 0});
    random_events(1);
    EXPECT(bases[0].event, 0x200);
    // Bases of three or fewer are left alone.
    events_base(1, 3);
    factions[1].ranking = 7;
    pick(2, (int[]){0, 1});
    random_events(1);
    EXPECT(bases[0].event, 0);
    rankings[7] = 0;
}

static void test_market(void) {
    events_base(1, 6);
    // Event 12: the AI with 1500 keeps it; with 2400 a quarter is left.
    factions[1].energy_credits = 1500;
    pick(2, (int[]){0, 12});
    random_events(1);
    EXPECT(factions[1].energy_credits, 1500);
    factions[1].energy_credits = 2400;
    pick(2, (int[]){0, 12});
    random_events(1);
    EXPECT(factions[1].energy_credits, 600);
    // A human's 1500 crash too; a boom doubles 400 for a high-ranked one.
    humans = 2;
    factions[1].energy_credits = 1500;
    pick(2, (int[]){0, 12});
    random_events(1);
    EXPECT(factions[1].energy_credits, 375);
    factions[1].energy_credits = 400, factions[1].ranking = 3;
    pick(2, (int[]){0, 12});
    random_events(1);
    EXPECT(factions[1].energy_credits, 800);
    humans = 0;
}

static void test_sky(void) {
    events_base(1, 6);
    // Event 9: a solar flare burns out energy satellites and defense pods.
    factions[3].satellites[2] = 2, factions[4].satellites[3] = 1, factions[4].satellites[1] = 4;
    flares = 0;
    pick(3, (int[]){0, 9, 0});
    random_events(1);
    EXPECT(flares, 1);
    EXPECT(factions[3].satellites[2] + factions[4].satellites[3], 0);
    EXPECT(factions[4].satellites[1], 4);
    // Event 10: asteroids wreck mineral satellites.
    pick(3, (int[]){0, 10, 0});
    random_events(1);
    EXPECT(factions[4].satellites[1], 0);
    // Event 11: sunspots, 40 turns after the last at least.
    sunspots = -30;
    pick(2, (int[]){0, 11});
    random_events(1);
    EXPECT(sunspots, -30);
    sunspots = -90;
    pick(3, (int[]){0, 11, 4});
    random_events(1);
    EXPECT(sunspots, 14);
}

static void test_countdowns(void) {
    events_base(1, 6);
    // Flag 0, early in the game: only the countdowns run.
    turn = 10, flares = 1, sunspots = 1, dust = 2;
    bases[0].event = 0x800, bases[0].random_event_turns = 1;
    bases[1].event = 0x800, bases[1].random_event_turns = 0;
    humans = 2;
    int n = pop2s;
    random_events(0);
    EXPECT(flares, 2);
    EXPECT(sunspots, 0);
    EXPECT(dust, 1);
    EXPECT(pop2s, n + 1);  // the sunspots are over
    EXPECT(bases[0].event, 0);
    // From 0 the turns go to -1, and the event stays.
    EXPECT(bases[1].random_event_turns, -1);
    EXPECT(bases[1].event, 0x800);
    // The AI watches the human.
    factions[3].diplo_status[1] = 0xc00;
    flares = 3;  // a flare's second turn ends it
    random_events(0);
    EXPECT(factions[3].diplo_status[1], 0);
    EXPECT(flares, 0);
    // Perihelion begins in 2190 and every 80 years, and ends 20 years on.
    turn = 90;
    random_events(0);
    EXPECT(state & GAME_STATE_PERIHELION, GAME_STATE_PERIHELION);
    turn = 110;
    random_events(0);
    EXPECT(state & GAME_STATE_PERIHELION, 0);
    humans = 0;
}

static void test_plague(void) {
    events_base(1, 7);
    put_base(2, 10, 4, 2);
    bases[2].pop_size = 5;
    put_base(3, 14, 8, 2);
    bases[3].pop_size = 5;
    factions[1].ranking = 6;
    current_faction = 2;
    // Event 6: the bases within 4 are halved, rounded up; the message goes
    // to the player's last base struck.
    int n = pop2s;
    pick(2, (int[]){0, 6});
    random_events(1);
    EXPECT(bases[0].pop_size, 4);
    EXPECT(bases[2].pop_size, 3);
    EXPECT(bases[3].pop_size, 5);
    EXPECT(pop2s, n + 1);
    EXPECT(last_pop2_base, 2);
    current_faction = 1;
}

static void test_eliminate(void) {
    land();
    put_base(0, 8, 4, 2);
    put_unit(0, 2, 2, 3, 0);
    put_unit(1, 4, 2, 2, 0);
    // A faction with a base isn't out.
    EXPECT(eliminate_player(2, 1), 0);
    // Without: its units are disbanded, the killer counts it, and the
    // monument is raised.
    EXPECT(eliminate_player(3, 1), 1);
    EXPECT(veh_count, 1);
    EXPECT(vehs[0].faction, 2);
    EXPECT(*(int32_t *)((char *)&factions[1] + 0x20a0), 1);
    EXPECT(setups, 1);
    EXPECT(monuments, 1);
    // The player's own end is left to the caller.
    current_faction = 3;
    int n = popps;
    EXPECT(eliminate_player(3, 0), 1);
    EXPECT(popps, n);
    current_faction = 1;
}

static void test_faction_file(void) {
    struct faction_def *d = &faction_defs[2];
    d->rule_techcost = 80, d->rule_flags = 0x80, d->bonus_count = 3, d->rule_commerce = 2;
    d->bonus_id[0] = 3, d->bonus_val1[0] = 2, d->bonus_val2[0] = 2;
    d->bonus_id[1] = 7, d->bonus_val1[1] = 2, d->bonus_val2[1] = 5;
    d->bonus_id[2] = 3, d->bonus_val1[2] = 2, d->bonus_val2[2] = -1;
    factions[2].se_base[4] = 9;
    compute_faction_modifiers(2);
    EXPECT(factions[2].se_base[2], 1);
    EXPECT(factions[2].se_base[4], 0);
    clear_faction(d);
    EXPECT(d->rule_techcost, 100);
    EXPECT(d->rule_hurry, 100);
    EXPECT(d->bonus_count + (int)d->rule_flags + d->rule_commerce, 0);
    // Social engineering left out: no screen.
    more_rules_ = 8, state = 0;
    social_select(2);
    more_rules_ = 0;
}

// ---- The landing ---------------------------------------------------------------

static int flics_loaded, frames_drawn, popups_run, waves;
static uint8_t map_win[0x1de00];
static int32_t map_win_base[2];  // the window's virtual base table: offset 0
static void *map_wins[1] = {map_win};
#define THIS __attribute__((thiscall))
static void THIS obj_(void *o) { (void)o; }
static void THIS buffer_init_(void *b, int w, int h, int a, int c) { (void)b, (void)w, (void)h, (void)a, (void)c; }
static void THIS buffer_copy_(void *b, void *to, int x, int y, int w, int h) { (void)b, (void)to, (void)x, (void)y, (void)w, (void)h; }
static void THIS buffer_copy_2_(void *b, void *from, int *r) { (void)b, (void)from, (void)r; }
static void THIS draw_2_(void *b, void *to, int m, int x, int y, int a, int c) {
    (void)b, (void)to, (void)m, (void)x, (void)y, (void)a, (void)c;
    frames_drawn++;
}
static void THIS draw_multi_(void *b, void *to, int col, int x, int y, int a, int c, void *t) {
    (void)b, (void)to, (void)col, (void)x, (void)y, (void)a, (void)c, (void)t;
}
static int THIS flic_load_(void *f, void *b, char *file, int a, int c, int d) {
    (void)f, (void)b, (void)file, (void)a, (void)c, (void)d;
    return flics_loaded++, 0;
}
static int THIS flic_frame_(void *f, int a, int b) { return (void)f, (void)a, (void)b, 0; }
static void THIS wave_init_(void *w, char *file, int u) { (void)w, (void)file, (void)u, waves++; }
static void THIS wave_(void *w) { (void)w; }
static void THIS fade_(void *w, int ms) { (void)w, (void)ms; }
static void THIS centre_(void *w, int x, int y, int u) { (void)w, (void)x, (void)y, (void)u; }
static void THIS tile_to_pixel_(void *w, int x, int y, int *px, int *py) { (void)w, *px = 8 * x, *py = 4 * y; }
static void THIS update_2_(void *w, int *r, int u) { (void)w, (void)r, (void)u; }
static int __attribute__((stdcall)) union_(int *to, const int *a, const int *b) { return (void)to, (void)a, (void)b, 1; }
static uint32_t now_ms;
static uint32_t __attribute__((stdcall)) time_(void) { return now_ms += 50; }
static void void_(void) {}
static int THIS pop_start_(void *p, char *f, char *l, int a, int b, int c, int d) {
    return (void)p, (void)f, (void)l, (void)a, (void)b, (void)c, (void)d, 0;
}
static int THIS pop_exec_(void *p, int a, int b) {
    (void)p, (void)a, (void)b;
    strcpy(game_parse_text, "Landing");
    return popups_run++, 0;
}
static uint32_t __attribute__((stdcall)) upper_(char *s) {
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32);
    return 0;
}
static uint32_t(__attribute__((stdcall)) * upper_ptr)(char *) = upper_;
#undef THIS

static void test_landing(void) {
    game_buffer_ctor = game_buffer_dtor = game_buffer_close = obj_, game_buffer_init = buffer_init_;
    game_buffer_copy = buffer_copy_, game_buffer_copy_2 = buffer_copy_2_, game_buffer_draw_2 = draw_2_;
    game_buffer_draw_multi = draw_multi_, game_flic_ctor = game_flic_dtor = game_palette_dtor = obj_;
    game_flic_load = flic_load_, game_flic_frame = flic_frame_, game_wave_init = wave_init_, game_wave_play = wave_;
    game_sound_fade = fade_, game_map_win_set_center = centre_, game_map_win_tile_to_pixel = tile_to_pixel_;
    game_graphic_win_update_2 = update_2_, game_union_rect = union_, game_time_get_time = time_;
    game_popup_ctor = game_popup_close = game_scroll_close = game_flat_button_dtor = obj_;
    game_graphic_win_dtor = game_base_pop_dtor = obj_, game_popup_start = pop_start_, game_base_pop_exec_3 = pop_exec_;
    game_do_all_draws = game_flush_input = void_, game_char_upper = &upper_ptr, game_map_wins = map_wins;
    static char pop_file[16];
    game_pop_file = pop_file;
    *(int32_t **)map_win = map_win_base;
    *(int32_t *)(map_win + 0x4c4) = 640, *(int32_t *)(map_win + 0x4c8) = 480;
    strcpy((char *)game_at(0x691464), "FUNGBOY");
    for (int f = 0; f < FACTION_COUNT; f++) faction_defs[f].rule_flags = 0;
    strcpy(faction_defs[1].filename, "gaians");
    strcpy(faction_defs[2].filename, "fungboy");

    // No base or unit: nothing happens.
    base_count = 0, veh_count = 0, turn = 0, current_faction = 1;
    crash_landing(1);
    EXPECT(flics_loaded, 0);
    EXPECT(popups_run, 0);
    // A unit, no base: the pod falls (30 frames), and no base is named.
    put_unit(0, 4, 4, 1, 0);
    veh_count = 1;
    crash_landing(1);
    EXPECT(flics_loaded, 2);
    EXPECT(frames_drawn, 30);
    EXPECT(popups_run, 0);
    EXPECT(turn, 0);
    EXPECT(*(uint32_t *)(map_win + 0x1dd70) & 0x8c0, 0x8c0);
    // A base: it is named by the player, and made unique.
    base_count = 2;
    put_base(0, 6, 6, 1);
    put_base(1, 8, 8, 3);
    strcpy(bases[1].name, "Landing");
    crash_landing(1);
    EXPECT(popups_run, 1);
    EXPECT(strcmp(bases[0].name, "Landing-2"), 0);
    // An alien faction lands on turn 5; the Cult of Planet too, unseen.
    faction_defs[1].rule_flags = RULE_FLAG_ALIEN;
    crash_landing(1);
    EXPECT(turn, 5);
    EXPECT(flics_loaded, 6);
    turn = 0, current_faction = 2, frames_drawn = 0;
    bases[0].faction = 2;
    crash_landing(2);
    EXPECT(turn, 5);
    EXPECT(flics_loaded, 6);
    EXPECT(frames_drawn, 0);
    EXPECT(popups_run, 3);
    faction_defs[1].rule_flags = 0, current_faction = 1, base_count = veh_count = 0, turn = 0;
}

// The Unity crash site (do_unity_crash): a unit with morale 2 on two squares,
// energy on another (more early in the game), the bases shown on the last,
// nothing on the rest or a square switched off.
static void test_unity(void) {
    struct map_tile *t = &tiles[1 + 2 * (W / 2)];  // (2, 2)
    base_count = veh_count = 0, turn = 60, current_faction = 2;
    t->bits2 = 0;
    EXPECT(do_unity_crash(2, 2, 1), 0);
    t->bits2 = 0x4000u | 5u << 24;
    EXPECT(do_unity_crash(2, 2, 1), 0);
    t->bits2 = 0x4000u | 0x80000000u;
    EXPECT(do_unity_crash(2, 2, 1), 0);
    // (The units, squares 0 and 8, go through veh_init(), which the spotters need the game for.)
    t->bits2 = 0x4000u | 10u << 24;
    factions[1].energy_credits = 100;
    EXPECT(do_unity_crash(2, 2, 1), 1);
    EXPECT(factions[1].energy_credits, 400);
    turn = 20;
    EXPECT(do_unity_crash(2, 2, 1), 1);
    EXPECT(factions[1].energy_credits, 550);
    t->bits2 = 0x4000u | 0x13u << 24;
    EXPECT(do_unity_crash(2, 2, 1), 1);
    t->bits2 = 0, veh_count = 0, turn = 0, current_faction = 1;
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
    test_unity();
    test_energy();
    test_resource();
    test_fungus();
    test_terrain();
    test_monolith();
    test_artifact();
    test_build();
    game_random = random_, game_random_reseed = reseed_, game_pop2 = pop2_, game_console_focus = focus_;
    game_get_async_key_state = key_, game_clock_wait = wait_, game_faction_rankings = rankings, game_pbem = &pbem;
    game_solar_flares = &flares, game_sunspot_duration = &sunspots, game_dust_cloud = &dust;
    game_start_year = &start_year, game_setup_player = setup_, game_get_his_her = his_her_;
    game_mon_killed_faction = mon_;
    game_console_update_data = update_, game_draw_radius = draw_radius_;
    static int32_t prefs;
    game_preferences = &prefs;
    image[0x68b0f4 - 0x400000] = image[0x68b108 - 0x400000] = 'X';  // the labels of two base events
    static int32_t gender, plural;
    game_parse_gender = &gender, game_parse_plural = &plural;
    static struct rules_facility facilities[FACILITY_COUNT];
    for (int i = 0; i < FACILITY_COUNT; i++) facilities[i].name = text_, facilities[i].preq_tech = TECH_DISABLED;
    game_facilities = facilities;
    test_base_events();
    test_market();
    test_sky();
    test_countdowns();
    test_plague();
    test_eliminate();
    test_faction_file();
    test_landing();
    if (failures) return 1;
    printf("engine_events: ok\n");
    return 0;
}
