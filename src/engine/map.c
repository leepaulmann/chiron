// Map queries: coordinates and distances, what a square holds, what lies
// around it, and how regions, coasts and ports connect. Reimplemented from
// terranx.exe 2.0, with behaviour as documented by OpenSMACX (Brendan Casey,
// GPL-3.0-or-later), whose names we use, and checked against the original
// code where the two differ.
//
// Coordinates are signed throughout, as in the original: it halves x with
// an arithmetic shift and does no range checks of its own, so callers keep
// squares on the map.
#include "map.h"

#include "game.h"
#include "shadow.h"

static inline int map_flat(void) { return *game_map_flat & 1; }

static inline struct map_tile *tile(int x, int y) { return &(*game_map)[(x >> 1) + y * *game_map_half_width]; }

// Wraps as x86 does; abs(INT_MIN) stays INT_MIN.
static inline int iabs(int v) { return v < 0 ? -v : v; }

static inline int sign(int v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

// ---- Coordinates and distances ------------------------------------------------

int on_map(int x, int y) { return y >= 0 && y < *game_map_height && x >= 0 && x < *game_map_width; }

// Brings x back onto a map that wraps east to west, from up to one width
// outside it.
int xrange(int x) {
    if (!map_flat()) {
        if (x < 0) x += *game_map_width;
        else if (x >= *game_map_width) x -= *game_map_width;
    }
    return x;
}

// A region number that stands for no real land or sea area.
int bad_reg(int region) { return game_bad_region(region); }

int x_dist(int x1, int x2) {
    int d = iabs(x1 - x2);
    if (!map_flat() && d > *game_map_half_width) d = *game_map_width - d;
    return d;
}

// How many rings of squares out a square at this offset lies: roughly the
// distance, along the diagonal grid the map is drawn on.
int vector_dist_offset(int dx, int dy) {
    int a = iabs(dx), b = iabs(dy);
    int hi = a > b ? a : b, lo = a < b ? a : b;
    return hi - ((((a + b) >> 1) - lo + 1) >> 1);
}

int vector_dist(int x1, int y1, int x2, int y2) { return vector_dist_offset(x_dist(x1, x2), y1 - y2); }

// The index of the offset in the radius table, among its first `range`
// entries, or -1.
int radius_move_offset(int dx, int dy, int range) {
    for (int i = 0; i < range; i++)
        if (dx == game_radius_x[i] && dy == game_radius_y[i]) return i;
    return -1;
}

// The shortest x offset from x1 to x2. Unlike x_dist this wraps on flat
// maps too, and uses half the width rather than the row length.
static int wrapped_dx(int x1, int x2) {
    int dx = x2 - x1, half = *game_map_width / 2;
    if (dx < -half) dx += *game_map_width;
    if (dx > half) dx -= *game_map_width;
    return dx;
}

int radius_move(int x1, int y1, int x2, int y2, int range) {
    return radius_move_offset(wrapped_dx(x1, x2), y2 - y1, range);
}

// The neighbour in the direction of the second square (8 for the square
// itself).
int compass_move(int x1, int y1, int x2, int y2) {
    int sx = sign(wrapped_dx(x1, x2)), sy = sign(y2 - y1);
    for (int i = 0; i < 9; i++)
        if (sx == sign(game_radius_near_x[i]) && sy == sign(game_radius_near_y[i])) return i;
    return -1;
}

// ---- One square ----------------------------------------------------------------

int alt_at(int x, int y) { return tile(x, y)->climate >> 5; }
int alt_detail_at(int x, int y) { return tile(x, y)->contour; }
int region_at(int x, int y) { return tile(x, y)->region; }
uint32_t bit_at(int x, int y) { return tile(x, y)->bits; }
int is_ocean(int x, int y) { return tile(x, y)->climate >> 5 < ALT_SHORE_LINE; }

// The faction with a unit, a base or either here, or -1.
static int owner_if(int x, int y, uint32_t what) {
    const struct map_tile *t = tile(x, y);
    if (!(t->bits & what)) return -1;
    int owner = t->owner_site & 0xf;
    return owner < FACTION_COUNT ? owner : -1;
}
int veh_who(int x, int y) { return owner_if(x, y, MAP_UNIT); }
int base_who(int x, int y) { return owner_if(x, y, MAP_BASE); }
int anything_at(int x, int y) { return owner_if(x, y, MAP_BASE | MAP_UNIT); }

uint8_t abstract_at(int x, int y) { return (*game_abstract_map)[(x >> 1) + y * (*game_abstract_width >> 1)]; }

// Whether the faction has seen the square. The shift wraps at 32, as on x86.
int is_known(int x, int y, int faction) {
    if (game_factions[faction].flags & FACTION_FLAG_MAP_REVEALED) return 1;
    return (tile(x, y)->visibility & (uint8_t)(1u << (faction & 31))) != 0;
}

// How poor the square is for growing things: one for arid, one for rocky,
// one for flat and not rainy.
int crappy(int x, int y) {
    const struct map_tile *t = tile(x, y);
    int rain = t->climate & 0x18, rocky = t->rocky_lock & 0xc0;
    int poor = !rain;
    if (rocky == 0x80) poor++;
    if (!rocky && rain < 0x10) poor++;
    return poor;
}

// The altitude the square's contour gives, from 0 (trench) to 6.
int alt_natural(int x, int y) {
    int contour = tile(x, y)->contour - *game_sea_level;
    for (int alt = 6; alt >= 0; alt--)
        if (contour >= game_alt_natural[alt]) return alt;
    return 0;
}

// Elevation in metres, as the map shows it, from -3000 to 3500.
int elev_at(int x, int y) {
    const struct map_tile *t = tile(x, y);
    int contour = t->contour, sea = *game_sea_level;
    int elev = (contour - game_elev_detail[3] - sea) * 50;
    if (contour <= game_elev_detail[t->climate >> 5]) elev += 10;
    else elev += (113 * x + 217 * y + 301 * sea) % 50;
    return elev < -3000 ? -3000 : elev > 3500 ? 3500 : elev;
}

// The rockiness the map generator gives a square: 0 flat, 1 rolling, 2
// rocky. It depends on the altitude and a pattern from the map's seed; the
// poles are always rocky.
int minerals_at(int x, int y) {
    if (!y || y == *game_map_height - 1) return 2;
    int avg = (x + y) / 2, dx = x - avg;
    int v1 = 3 * (dx / 2) + *game_map_seed + 2 * (avg / 2);
    int v2 = (v1 - 2 * (dx & 1) - (avg & 1)) & 3;
    int alt = alt_at(x, y);
    int band = alt < ALT_SHORE_LINE ? ALT_SHORE_LINE - alt - 1 : alt - ALT_SHORE_LINE;
    switch (band) {
    case 0: return v2 == 0 ? 1 : v2 == 2 ? ((v1 & 4) != 0) + 1 : 0;
    case 1: return v2 == 0 ? 0 : v2 == 3 ? 2 : 1;
    case 2: return v2 == 0 ? 0 : v2 == 1 ? 1 : 2;
    case 3: return v2 <= 1 ? 1 : 2;
    default: return ~v2 & 1;
    }
}

// The pattern resources and pods are placed in: the square's position in a
// 4 by 4 block, along the map's two diagonals.
struct diag {
    int avg, dx, cell;
};
static struct diag diag_of(int x, int y) {
    struct diag d = {(x + y) >> 1};
    d.dx = x - d.avg;
    d.cell = (d.avg & 3) + 4 * (d.dx & 3);
    return d;
}
static int diag_pick(struct diag d) { return (-5 * (d.avg >> 2) - 3 * (d.dx >> 2) + *game_map_seed) & 0xf; }

// The square's resource: 0 none, 1 nutrient, 2 mineral, 3 energy. Unmarked
// squares get one from the seed's pattern at sea, and on land too without
// unity scattering. The third argument is unused.
int bonus_at(int x, int y, int unused) {
    const struct map_tile *t = tile(x, y);
    uint32_t bits = t->bits;
    int alt = t->climate >> 5, marked = bits & MAP_RESOURCE;
    if (!marked) {
        if (!*game_map_seed) return 0;
        if (alt >= ALT_SHORE_LINE && !(*game_rules & RULES_NO_UNITY_SCATTERING)) return 0;
    }
    struct diag d = diag_of(x, y);
    if (!marked && d.cell != diag_pick(d)) return 0;
    if (alt < ALT_SHORE_LINE - 1) return 0;
    int kind = alt < ALT_SHORE_LINE ? d.cell % 3 + 1 : (d.cell % 5) & 3;
    if (!kind || (bits & MAP_NUTRIENT_RESOURCE)) {
        if (bits & MAP_ENERGY_RESOURCE) return 3;
        return bits & MAP_MINERAL_RESOURCE ? 2 : 1;
    }
    return kind;
}

// The pod on the square: 0 none, 1 a supply pod, 2 a unity pod.
int goody_at(int x, int y) {
    const struct map_tile *t = tile(x, y);
    uint32_t bits = t->bits;
    if (bits & (MAP_POD_TAKEN | MAP_MONOLITH)) return 0;
    if (*game_rules & RULES_NO_UNITY_SCATTERING) return bits & MAP_UNITY_POD ? 2 : 0;
    if (bits & MAP_SUPPLY_POD) return 1;
    if (!*game_map_seed) return 0;
    struct diag d = diag_of(x, y);
    if (t->climate >> 5 >= ALT_SHORE_LINE && d.cell == diag_pick(d)) return 2;
    return d.cell == ((d.avg / 4 * game_goody_mult[1] + d.dx / 4 * game_goody_mult[0] + *game_map_seed + 8) & 0x1f);
}

// ---- Around a square ---------------------------------------------------------

// The index of the first ocean square among the 8 neighbours, or the 20
// squares of a base's radius, or 0. Callers treat it as true or false.
int is_coast(int x, int y, int base_radius) {
    int n = base_radius ? 21 : 9;
    for (int i = 1; i < n; i++) {
        int xi = xrange(x + game_radius_x[i]), yi = y + game_radius_y[i];
        if (on_map(xi, yi) && is_ocean(xi, yi)) return i;
    }
    return 0;
}

// The first landmark within the radius, nearest squares first, or -1.
int find_landmark(int x, int y, int radius) {
    int n = game_radius_count[radius];
    for (int i = 0; i < n; i++) {
        int xi = xrange(x + game_radius_x[i]), yi = y + game_radius_y[i];
        if (!on_map(xi, yi)) continue;
        for (int l = 0; l < *game_landmark_count; l++)
            if (game_landmarks[l].x == xi && game_landmarks[l].y == yi) return l;
    }
    return -1;
}

// Whether a landmark covers any square within 8.
int near_landmark(int x, int y) {
    for (int i = 0; i < RADIUS_SQUARES; i++) {
        int xi = xrange(x + game_radius_x[i]), yi = y + game_radius_y[i];
        if (on_map(xi, yi) && (tile(xi, yi)->bits2 & MAP2_LANDMARK_CODE)) return 1;
    }
    return 0;
}

// Zone of control: a neighbour holding a faction without a pact with
// `faction`. zoc_any counts bases and units and gives the first such
// faction plus one; zoc_veh counts units and gives the highest.
int zoc_any(int x, int y, int faction) {
    for (int i = 0; i < 8; i++) {
        int xi = xrange(x + game_radius_near_x[i]), yi = y + game_radius_near_y[i];
        if (!on_map(xi, yi)) continue;
        int owner = anything_at(xi, yi);
        if (owner >= 0 && owner != faction && !game_has_treaty(faction, owner, TREATY_PACT)) return owner + 1;
    }
    return 0;
}

int zoc_veh(int x, int y, int faction) {
    int zoc = 0;
    for (int i = 0; i < 8; i++) {
        int xi = xrange(x + game_radius_near_x[i]), yi = y + game_radius_near_y[i];
        if (!on_map(xi, yi)) continue;
        int owner = veh_who(xi, yi);
        if (owner >= 0 && owner != faction && !game_has_treaty(faction, owner, TREATY_PACT) && zoc <= owner + 1)
            zoc = owner + 1;
    }
    return zoc;
}

// ---- Regions, coasts and ports -------------------------------------------------

// Whether the shore of `region` touches land region `from`.
int sea_coast(int region, int from) {
    int bit = from & 63;
    return (game_continents[region].sea_coasts[bit / 8] & (1 << (bit & 7))) != 0;
}

// How many land regions have `region` on their shore.
int sea_coasts(int region) {
    int n = 0;
    for (int i = 1; i < 63; i++)
        if (sea_coast(i, region)) n++;
    return n;
}

// For each ocean neighbour of the base, in order; a port's neighbours.
#define FOR_OCEAN_NEIGHBOURS(base, i, xi, yi)                                                       \
    for (int i = 0, bx_ = game_bases[base].x, by_ = game_bases[base].y, xi, yi; i < 8; i++)          \
        if (xi = xrange(bx_ + game_radius_near_x[i]), yi = by_ + game_radius_near_y[i],              \
            on_map(xi, yi) && is_ocean(xi, yi))

// Whether the base is next to the sea of region `sea` (taken modulo 64).
int base_on_sea(int base, int sea) {
    sea &= 63;
    if (sea >= 63) return 0;
    FOR_OCEAN_NEIGHBOURS(base, i, xi, yi) {
        if ((region_at(xi, yi) & 63) == sea) return 1;
    }
    return 0;
}

// The sea region next to the base, or -1. Having found one, the search skips
// the next neighbour or two, so a base on two seas gets the last one it
// reaches. (The original also finds the largest sea's size, then ignores it.)
int base_coast(int base) {
    int region = -1;
    FOR_OCEAN_NEIGHBOURS(base, i, xi, yi) {
        region = region_at(xi, yi);
        i += 2 - (i & 1);
    }
    return region;
}

// Whether the base is in `region`, or next to a sea whose shore touches it.
int port_to_coast(int base, int region) {
    if (region_at(game_bases[base].x, game_bases[base].y) == region) return 1;
    FOR_OCEAN_NEIGHBOURS(base, i, xi, yi) {
        if (sea_coast(region, region_at(xi, yi))) return 1;
        i += 2 - (i & 1);
    }
    return 0;
}

// Whether the two bases are on a common sea.
int port_to_port(int base, int other) {
    int last = -1;
    FOR_OCEAN_NEIGHBOURS(base, i, xi, yi) {
        int region = region_at(xi, yi);
        if (region == last) continue;
        last = region;
        if (base_on_sea(other, region)) return 1;
    }
    return 0;
}

// Whether the base should build transports: it is at sea, or its sea touches
// more than one land region.
int transport_base(int base) {
    int region = base_coast(base);
    if (region < 0) return 0;
    if (is_ocean(game_bases[base].x, game_bases[base].y)) return 1;
    return sea_coasts(region) > 1;
}

// Whether another faction's base is on a sea this base is on.
int naval_base(int base) {
    if (base_coast(base) < 0 || *game_base_count <= 0) return 0;
    for (int i = 0; i < *game_base_count; i++)
        if (game_bases[i].faction != game_bases[base].faction && port_to_port(base, i)) return 1;
    return 0;
}

// ---- Hooks and shadow checks ------------------------------------------------

// Functions that read squares or bases start the sweep; the others can be
// called while a map is being loaded or made, so they don't.
extern struct hook_module map_module;
#define MAP (&map_module)
#define PURE NULL

SHADOW2(PURE, int, on_map, int, int)
SHADOW1(PURE, int, xrange, int)
SHADOW1(PURE, int, bad_reg, int)
SHADOW2(PURE, int, x_dist, int, int)
SHADOW2(PURE, int, vector_dist_offset, int, int)
SHADOW4(PURE, int, vector_dist, int, int, int, int)
SHADOW3(PURE, int, radius_move_offset, int, int, int)
SHADOW5(PURE, int, radius_move, int, int, int, int, int)
SHADOW4(PURE, int, compass_move, int, int, int, int)
SHADOW2(MAP, int, alt_at, int, int)
SHADOW2(MAP, int, alt_detail_at, int, int)
SHADOW2(MAP, int, region_at, int, int)
SHADOW2(MAP, uint32_t, bit_at, int, int)
SHADOW2(MAP, int, is_ocean, int, int)
SHADOW2(MAP, int, veh_who, int, int)
SHADOW2(MAP, int, base_who, int, int)
SHADOW2(MAP, int, anything_at, int, int)
SHADOW2(MAP, uint8_t, abstract_at, int, int)
SHADOW3(MAP, int, is_known, int, int, int)
SHADOW2(MAP, int, crappy, int, int)
SHADOW2(MAP, int, alt_natural, int, int)
SHADOW2(MAP, int, elev_at, int, int)
SHADOW2(MAP, int, minerals_at, int, int)
SHADOW3(MAP, int, bonus_at, int, int, int)
SHADOW2(MAP, int, goody_at, int, int)
SHADOW3(MAP, int, is_coast, int, int, int)
SHADOW3(MAP, int, find_landmark, int, int, int)
SHADOW2(MAP, int, near_landmark, int, int)
SHADOW3(MAP, int, zoc_any, int, int, int)
SHADOW3(MAP, int, zoc_veh, int, int, int)
SHADOW2(PURE, int, sea_coast, int, int)
SHADOW1(PURE, int, sea_coasts, int)
SHADOW2(MAP, int, base_on_sea, int, int)
SHADOW1(MAP, int, base_coast, int)
SHADOW2(MAP, int, port_to_coast, int, int)
SHADOW2(MAP, int, port_to_port, int, int)
SHADOW1(MAP, int, transport_base, int)
SHADOW1(MAP, int, naval_base, int)

// Input sweeps, run once a turn in shadow mode. Squares are the map's own
// (x + y even), as the originals don't check; functions that look around a square reach
// past the edges by themselves. The slowest sweeps take a different share
// of the squares or bases each turn.
static int slice;  // changes every sweep

static void sweep_pure(void) {
    int w = *game_map_width, h = *game_map_height;
    for (int y = -2; y < h + 2; y++)
        for (int x = -2; x < w + 2; x++) check_on_map(x, y);
    for (int x = -w - 2; x <= 2 * w + 2; x++) check_xrange(x);
    for (int r = -300; r <= 300; r++) check_bad_reg(r);
    for (int a = -2; a < w + 2; a++)
        for (int b = -2; b < w + 2; b++) check_x_dist(a, b);
    for (int dx = -40; dx <= 40; dx++)
        for (int dy = -40; dy <= 40; dy++) check_vector_dist_offset(dx, dy);
    for (int x1 = 0; x1 < w; x1 += 3)
        for (int x2 = 0; x2 < w; x2++)
            for (int dy = -9; dy <= 9; dy += 3) check_vector_dist(x1, 20, x2, 20 + dy);
    static const int ranges[] = {0, 1, 9, 21, 25, 49, RADIUS_SQUARES};
    for (int dx = -17; dx <= 17; dx++)
        for (int dy = -17; dy <= 17; dy++)
            for (size_t r = 0; r < sizeof(ranges) / sizeof(ranges[0]); r++) check_radius_move_offset(dx, dy, ranges[r]);
    const int xs[] = {0, 1, 2, w / 2, w - 2, w - 1};
    for (size_t i = 0; i < sizeof(xs) / sizeof(xs[0]); i++)
        for (int x2 = -1; x2 <= w; x2++)
            for (int dy = -4; dy <= 4; dy++) {
                check_radius_move(xs[i], 10, x2, 10 + dy, 49);
                check_compass_move(xs[i], 10, x2, 10 + dy);
            }
    for (int r = 0; r < REGION_COUNT; r++)
        for (int from = 0; from < 256; from++) check_sea_coast(r, from);
    for (int r = 0; r < 256; r++) check_sea_coasts(r);
}

static void sweep_squares(void) {
    int w = *game_map_width, h = *game_map_height;
    for (int y = 0; y < h; y++)
        for (int x = y & 1; x < w; x += 2) {
            check_alt_at(x, y);
            check_alt_detail_at(x, y);
            check_region_at(x, y);
            check_bit_at(x, y);
            check_is_ocean(x, y);
            check_veh_who(x, y);
            check_base_who(x, y);
            check_anything_at(x, y);
            check_crappy(x, y);
            check_alt_natural(x, y);
            check_elev_at(x, y);
            check_minerals_at(x, y);
            check_bonus_at(x, y, 0);
            check_goody_at(x, y);
            check_is_coast(x, y, 0);
            check_is_coast(x, y, 1);
            for (int f = 0; f < FACTION_COUNT; f++) {
                check_is_known(x, y, f);
                check_zoc_any(x, y, f);
                check_zoc_veh(x, y, f);
            }
            int n = x + y * w;
            if ((n + slice) % 8 == 0) {
                check_near_landmark(x, y);
                for (int r = 0; r <= 2; r++) check_find_landmark(x, y, r);
            }
            if ((n + slice) % 64 == 0) check_find_landmark(x, y, 8);
        }
    if (*game_abstract_map)
        for (int y = 0; y < *game_abstract_height; y++)
            for (int x = 0; x < *game_abstract_width; x++) check_abstract_at(x, y);
}

static void sweep_bases(void) {
    int n = *game_base_count;
    for (int b = 0; b < n; b++) {
        check_base_coast(b);
        check_transport_base(b);
        if ((b + slice) % 4 == 0) check_naval_base(b);
        for (int r = 0; r < REGION_COUNT; r++) {
            check_base_on_sea(b, r);
            check_port_to_coast(b, r);
        }
        for (int o = 0; o < n; o++)
            if ((b + o + slice) % 4 == 0) check_port_to_port(b, o);
    }
}

static void map_sweep(void) {
    slice++;
    if (!*game_map || *game_map_width <= 0 || *game_map_height <= 0) return;
    sweep_pure();
    sweep_squares();
    sweep_bases();
}

// The first bytes of each original: whole instructions, no relative jumps.
// Some read a global; the trampoline copies those as they are, which holds
// as terranx.exe always loads at its preferred base.
#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
#define LOAD_HALF_WIDTH 0xa1, 0xf0, 0xfa, 0x68, 0x00  // mov eax, [0x68faf0]
static const struct hook map_hooks[] = {
    HOOK(0x4712a0, on_map, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x0c),                          // mov eax, [ebp+12]
    HOOK(0x48bee0, xrange, PUSH_EBP_MOV_EBP_ESP, 0xf6, 0x05, 0x8c, 0x98, 0x94, 0x00, 0x01),  // test byte [0x94988c], 1
    HOOK(0x5591c0, bad_reg, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                         // mov eax, [ebp+8]
    HOOK(0x579790, x_dist, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),
    HOOK(0x4f8090, vector_dist_offset, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),
    HOOK(0x5a5910, vector_dist, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),
    HOOK(0x5a65a0, radius_move_offset, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x4d, 0x10),  // mov ecx, [ebp+16]
    HOOK(0x5a65d0, radius_move, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),
    HOOK(0x5a6630, compass_move, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x4d, 0x10),
    HOOK(0x500150, alt_at, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x500180, alt_detail_at, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x500220, region_at, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x5001b0, bit_at, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x5001e0, is_ocean, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x500250, veh_who, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x5798a0, base_who, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x5798e0, anything_at, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x591210, abstract_at, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x94, 0xa2, 0x94, 0x00),  // mov eax, [0x94a294]
    HOOK(0x579840, is_known, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x4d, 0x10),
    HOOK(0x4ecb90, crappy, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x0d, 0xf0, 0xfa, 0x68, 0x00),       // mov ecx, [0x68faf0]
    HOOK(0x5918a0, alt_natural, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x0d, 0xf0, 0xfa, 0x68, 0x00),
    HOOK(0x5919c0, elev_at, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x591f00, minerals_at, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x0c),
    HOOK(0x592030, bonus_at, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x592140, goody_at, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x4e49d0, is_coast, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x45, 0x10),  // push ecx; mov eax, [ebp+16]
    HOOK(0x592550, find_landmark, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x45, 0x10),
    HOOK(0x5c5bd0, near_landmark, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x1d, 0x8c, 0x98, 0x94, 0x00),  // push ebx; mov ebx, [0x94988c]
    HOOK(0x5c89f0, zoc_any, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x0d, 0x8c, 0x98, 0x94, 0x00),        // push ecx; mov ecx, [0x94988c]
    HOOK(0x5c8ac0, zoc_veh, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),                                // sub esp, 8
    HOOK(0x50ddc0, sea_coast, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x55, 0x0c),  // push ecx; mov edx, [ebp+12]
    HOOK(0x50de00, sea_coasts, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x53),             // push ecx; push ebx
    HOOK(0x50de50, base_on_sea, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x45, 0x0c),
    HOOK(0x50df30, base_coast, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),
    HOOK(0x50e030, port_to_coast, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),
    HOOK(0x50e160, port_to_port, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x18),
    HOOK(0x50e310, transport_base, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x56),  // push ecx; push esi
    HOOK(0x50e3c0, naval_base, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x28),
};
struct hook_module map_module = {"map", map_hooks, HOOK_COUNT(map_hooks), map_sweep, -1};
