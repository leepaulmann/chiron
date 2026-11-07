// Unit test for the reimplemented spotting, on a small made-up map: no
// game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/spot.h"

enum { W = 16, H = 12 };

static struct map_tile tiles[W / 2 * H], *tiles_ptr = tiles;
static int32_t width = W, height = H, half_width = W / 2, base_count = 2, veh_count = 0, turn = 10;
static int32_t current = 1, net = 0, more_prefs = 0, find_dist, lift[6], pbem;
static uint32_t flat = 0, state = 0, draw_state = 0;
static uint8_t humans = 0, alive = 0xff;
static struct base bases[2];
static struct veh vehs[VEH_COUNT + 1];
// The original reads one faction before the first for an empty square.
static struct {
    struct faction_data before, f[FACTION_COUNT];
} factions;
static struct faction_def defs[FACTION_COUNT];
static struct unit_proto protos[PROTO_COUNT];
static struct rules_chassis chassis[CHASSIS_COUNT];
static struct rules_weapon weapons[WEAPON_COUNT];
static int32_t radius_x[RADIUS_SQUARES], radius_y[RADIUS_SQUARES], near_x[9], near_y[9];
static int failures, drawn, redrawn, goals, met[FACTION_COUNT][FACTION_COUNT], met_native, interludes;

#define EXPECT(expr, want)                                                       \
    do {                                                                         \
        int got_ = (expr);                                                       \
        if (got_ != (want)) {                                                    \
            printf("FAIL %s = %d, expected %d\n", #expr, got_, (int)(want));    \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static struct map_tile *at(int x, int y) { return &tiles[x / 2 + y * (W / 2)]; }
static void draw_radius_stub(int x, int y, int r, int u) { drawn = r; }
static void __attribute__((thiscall)) redraw_stub(void *w) { redrawn++; }
static void add_goal_stub(int f, int type, int pri, int x, int y, int base) { goals++; }
static void encounter_stub(int f, int other, int x, int y, int tower, int native) {
    met[f][other]++;
    if (native) met_native++;
}
static void interlude_stub(int id, char *text, int a, int b) { interludes++; }

// A unit of `faction` alone on (x, y).
static int put(int faction, int proto, int x, int y) {
    int v = veh_count++;
    vehs[v] = (struct veh){.x = (int16_t)x, .y = (int16_t)y, .faction = (uint8_t)faction, .proto = (int16_t)proto,
                           .next_in_stack = -1, .prev_in_stack = -1, .waypoint_x = {-1}};
    struct map_tile *t = at(x, y);
    t->bits |= MAP_UNIT;
    if (!(t->bits & MAP_BASE)) t->owner_site = (uint8_t)faction;
    return v;
}
static void clear_units(void) {
    veh_count = 0;
    for (int i = 0; i < W / 2 * H; i++) tiles[i].bits &= ~(uint32_t)MAP_UNIT, tiles[i].owner_site = 0xf;
    for (int b = 0; b < base_count; b++) at(bases[b].x, bases[b].y)->owner_site = bases[b].faction;
}
static void forget_all(void) {
    for (int i = 0; i < W / 2 * H; i++) tiles[i].visibility = 0, tiles[i].bits2 = 0;
    for (int b = 0; b < base_count; b++) bases[b].visibility = 0;
}

int main(void) {
    game_map = &tiles_ptr, game_map_width = &width, game_map_height = &height, game_map_half_width = &half_width;
    game_map_flat = &flat, game_turn = &turn, game_state = &state, game_draw_state = &draw_state;
    game_bases = bases, game_base_count = &base_count, game_vehs = vehs, game_veh_count = &veh_count;
    game_factions = factions.f, game_faction_defs = defs, game_protos = protos, game_chassis = chassis;
    game_weapons = weapons, game_current_faction = &current, game_net_multiplayer = &net;
    game_more_preferences = &more_prefs, game_human_factions = &humans, game_factions_alive = &alive;
    game_base_find_dist = &find_dist, game_pbem = &pbem;
    game_veh_lift_x = &lift[0], game_veh_lifted = &lift[2], game_veh_lift_y = &lift[3], game_veh_at_rebuilt = &lift[5];
    game_radius_x = radius_x, game_radius_y = radius_y, game_radius_near_x = near_x, game_radius_near_y = near_y;
    game_draw_radius = draw_radius_stub, game_graphic_win_redraw = redraw_stub, game_add_goal = add_goal_stub;
    game_encounter = encounter_stub, game_interlude = interlude_stub;
    // The radius table: the square, its 8 neighbours, then the 16 two
    // squares out (as dx, dy pairs on the diamond grid).
    static const int ring1[8][2] = {{1, -1}, {2, 0}, {1, 1}, {0, 2}, {-1, 1}, {-2, 0}, {-1, -1}, {0, -2}};
    static const int ring2[16][2] = {{2, -2}, {3, -1}, {4, 0}, {3, 1}, {2, 2}, {1, 3}, {0, 4}, {-1, 3},
                                     {-2, 2}, {-3, 1}, {-4, 0}, {-3, -1}, {-2, -2}, {-1, -3}, {0, -4}, {1, -3}};
    for (int i = 0; i < 8; i++) radius_x[i + 1] = near_x[i] = ring1[i][0], radius_y[i + 1] = near_y[i] = ring1[i][1];
    for (int i = 0; i < 16; i++) radius_x[i + 9] = ring2[i][0], radius_y[i + 9] = ring2[i][1];
    for (int i = 25; i < 49; i++) radius_x[i] = 2 * ring2[i % 16][0], radius_y[i] = 2 * ring2[i % 16][1];
    // Land everywhere, one region; sea on the right half.
    for (int y = 0; y < H; y++)
        for (int x = y & 1; x < W; x += 2)
            *at(x, y) = (struct map_tile){.climate = (x < 10 ? 4 : 1) << 5, .owner_site = 0xf, .region = x < 10 ? 1 : 2,
                                          .territory = -1};
    chassis[0].triad = TRIAD_LAND, chassis[1].triad = TRIAD_SEA, chassis[2].triad = TRIAD_AIR, chassis[2].range = 2;
    protos[64] = (struct unit_proto){.chassis = 0, .plan = PLAN_COMBAT};
    protos[65] = (struct unit_proto){.chassis = 1};
    protos[66] = (struct unit_proto){.chassis = 0, .abilities = 0x2};  // deep radar
    protos[67] = (struct unit_proto){.chassis = 0, .abilities = 0x4};  // cloaked
    protos[8] = (struct unit_proto){.chassis = 0, .weapon = 1};        // a native (psi) unit
    weapons[1].offense = -1;
    for (int f = 0; f < FACTION_COUNT; f++) factions.f[f].target_x = factions.f[f].target_y = -1;
    bases[0] = (struct base){.x = 2, .y = 2, .faction = 1, .pop_size = 3};
    bases[1] = (struct base){.x = 6, .y = 6, .faction = 2, .pop_size = 5};
    clear_units();
    at(2, 2)->bits |= MAP_BASE, at(6, 6)->bits |= MAP_BASE;

    // A square: seen; the player's faction's first sight of it is drawn.
    spot_square(4, 4, 2);
    EXPECT(at(4, 4)->visibility, 1 << 2);
    EXPECT(draw_state, 0);
    spot_square(4, 4, 1);
    EXPECT(at(4, 4)->visibility, 1 << 2 | 1 << 1);
    EXPECT(at(4, 4)->bits2 & 0x400000, 0x400000);
    EXPECT(draw_state & DRAW_TERRAIN, DRAW_TERRAIN);
    at(4, 4)->bits = 0x1234, spot_square(4, 4, 3);
    EXPECT(at(4, 4)->bits_seen[2], 0x1234);
    at(4, 4)->bits = 0;
    spot_square(-1, 4, 1), spot_square(4, H, 1), spot_square(W, 0, 1);  // off the map: nothing
    // Not drawn with the map revealed to the faction, or shown whole.
    draw_state = 0, at(8, 4)->bits2 = 0;
    factions.f[1].flags = FACTION_FLAG_MAP_REVEALED, spot_square(8, 4, 1);
    EXPECT(draw_state, 0);
    factions.f[1].flags = 0, state = GAME_STATE_OMNISCIENT, spot_square(8, 6, 1);
    EXPECT(draw_state, 0);
    EXPECT(at(8, 6)->visibility, 1 << 1);
    state = 0;

    // A base: its size as seen now, and its square.
    bases[1].pop_seen[3] = 1;
    spot_base(1, 3);
    EXPECT(bases[1].visibility, 1 << 3);
    EXPECT(bases[1].pop_seen[3], 5);
    EXPECT(at(6, 6)->visibility & 1 << 3, 1 << 3);

    // A stack: its square and every unit in it; another player's sight
    // lifts cloaks, the natives' doesn't.
    int a = put(2, 64, 4, 8), b = put(2, 64, 4, 8);
    vehs[a].next_in_stack = (int16_t)b, vehs[b].prev_in_stack = (int16_t)a;
    vehs[a].flags = vehs[b].flags = VEH_FLAG_INVISIBLE | 0x40 | 0x1;
    spot_stack(b, 0);
    EXPECT(vehs[a].visibility, 1);
    EXPECT(vehs[b].flags, VEH_FLAG_INVISIBLE | 0x40 | 0x1);
    spot_stack(b, 3);
    EXPECT(vehs[a].visibility, 1 | 1 << 3);
    EXPECT(vehs[b].visibility, 1 | 1 << 3);
    EXPECT(vehs[a].flags, 0x1);
    EXPECT(at(4, 8)->visibility & 1 << 3, 1 << 3);
    spot_stack(-1, 3);  // none
    unspot_stack(b);
    EXPECT(vehs[a].visibility, 0);
    EXPECT(vehs[b].visibility, 0);
    unspot_stack(-1);

    // A location: the base on it, or else the stack.
    spot_loc(2, 2, 4);
    EXPECT(bases[0].visibility, 1 << 4);
    spot_loc(4, 8, 4);
    EXPECT(vehs[a].visibility, 1 << 4);
    spot_loc(-2, 0, 4);  // off the map

    // A unit's sight: its neighbours, and the squares beyond filled in
    // once most of their neighbours have been seen.
    clear_units(), forget_all();
    int u = put(3, 64, 4, 4);
    spot_all(u, 0);
    for (int i = 0; i < 9; i++) EXPECT(at(4 + radius_x[i], 4 + radius_y[i])->visibility, 1 << 3);
    EXPECT(at(8, 4)->visibility, 0);
    EXPECT(drawn, 0);  // not the player's faction
    // As the player's: drawn to two squares out, the window redrawn.
    current = 3, redrawn = 0;
    forget_all(), spot_all(u, 0);
    EXPECT(drawn, 2);
    EXPECT(redrawn, 1);
    // Seen again with nothing changed: nothing to draw.
    spot_all(u, 0);
    EXPECT(drawn, 0);
    // A square two out with most of its neighbours seen is filled in.
    forget_all();
    for (int i = 0; i < W / 2 * H; i++) tiles[i].visibility = 1 << 3;
    at(8, 4)->visibility = 0, at(4, 8)->visibility = 0;
    spot_all(u, 1);
    EXPECT(at(8, 4)->visibility, 1 << 3);
    EXPECT(at(4, 8)->visibility, 1 << 3);
    EXPECT(drawn, 2);
    // Radar: sees two squares out.
    vehs[u].proto = 66, forget_all();
    spot_all(u, 1);
    EXPECT(at(8, 4)->visibility, 1 << 3);
    EXPECT(at(4, 0)->visibility, 1 << 3);
    vehs[u].proto = 64, current = 1;

    // Neighbours: a unit of another faction next to it sees it and is
    // seen; at war, the AI's goals follow and the factions meet.
    clear_units(), forget_all(), goals = 0;
    memset(met, 0, sizeof met);
    u = put(3, 64, 4, 4);
    int o = put(4, 64, 5, 5);
    spot_all(u, 1);
    EXPECT(vehs[u].visibility & 1 << 4, 1 << 4);
    EXPECT(vehs[o].visibility & 1 << 3, 1 << 3);
    EXPECT(met[3][4], 1);
    EXPECT(goals > 0, 1);
    // With a pact: seen, met, but no goals.
    factions.f[3].diplo_status[4] = factions.f[4].diplo_status[3] = TREATY_PACT | TREATY_COMMLINK;
    goals = 0, memset(met, 0, sizeof met);
    spot_all(u, 1);
    EXPECT(goals, 0);
    EXPECT(met[3][4], 1);
    // A cloaked unit stays hidden, alone, from those it talks to.
    vehs[u].visibility = vehs[o].visibility = 0;
    vehs[o].proto = 67;
    spot_all(u, 1);
    EXPECT(vehs[o].visibility & 1 << 3, 0);
    EXPECT(vehs[u].visibility & 1 << 4, 1 << 4);
    vehs[o].proto = 64;
    factions.f[3].diplo_status[4] = factions.f[4].diplo_status[3] = 0;
    // A native unit met: the natives, flagged as such.
    vehs[o].faction = 0, vehs[o].proto = 8, at(5, 5)->owner_site = 0, met_native = 0;
    memset(met, 0, sizeof met);
    spot_all(u, 1);
    EXPECT(met[3][0], 1);
    EXPECT(met_native, 1);
    // A neighbouring base of another faction: seen, and it sees the unit.
    clear_units(), forget_all(), memset(met, 0, sizeof met);
    u = put(3, 64, 5, 5);
    spot_all(u, 1);
    EXPECT(bases[1].visibility & 1 << 3, 1 << 3);
    EXPECT(bases[1].pop_seen[3], 5);
    EXPECT(vehs[u].visibility & 1 << 2, 1 << 2);
    EXPECT(met[3][2], 1);
    // Not in a network game without the flag (the natives still would be).
    net = 1, memset(met, 0, sizeof met);
    spot_all(u, 0);
    EXPECT(met[3][2], 0);
    net = 0;

    // Two squares out: a base sees a land unit on its land that its faction
    // has seen the square of, and drops it as its target.
    clear_units(), forget_all();
    u = put(3, 64, 2, 6);
    at(2, 6)->visibility = 1 << 2;
    factions.f[2].target_x = 6, factions.f[2].target_y = 6;
    spot_all(u, 1);
    EXPECT(vehs[u].visibility & 1 << 2, 1 << 2);
    EXPECT(factions.f[2].target_x, -1);
    // Unless it is hidden.
    vehs[u].visibility = 0, vehs[u].proto = 67;
    spot_all(u, 1);
    EXPECT(vehs[u].visibility & 1 << 2, 0);
    vehs[u].proto = 64;

    // The natives' units: seen only by those who see their square.
    clear_units(), forget_all();
    u = put(0, 8, 4, 4);
    vehs[u].visibility = 0xff, at(4, 4)->visibility = 1 << 2;
    spot_all(u, 1);
    EXPECT(vehs[u].visibility, 1 << 2);

    // Nothing drawn or met before the game begins.
    clear_units(), forget_all(), memset(met, 0, sizeof met);
    u = put(3, 64, 4, 4), o = put(4, 64, 5, 5);
    turn = 0, drawn = -1;
    spot_all(u, 1);
    EXPECT(drawn, -1);
    EXPECT(met[3][4], 0);
    turn = 10;
    (void)interludes;

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("engine_spot: ok\n");
    return 0;
}
