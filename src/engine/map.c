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

#include <string.h>

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

// ---- Territory, units and sensors ----------------------------------------------

// The faction whose territory the square is, or -1: for none, for the
// natives, and for a faction `faction` hasn't a commlink and
// TREATY_UNK_8000000 with (unless ignore_comm, or the omniscient view). For
// another faction's territory, *base gets the nearest base in the square's
// region.
int whose_territory(int faction, int x, int y, int *base, int ignore_comm) {
    const struct map_tile *t = tile(x, y);
    int owner = t->territory;
    if (owner <= 0) return -1;
    if (owner == faction) return owner;
    const uint32_t known = TREATY_COMMLINK | TREATY_UNK_8000000;
    if (!ignore_comm && !(*game_state & GAME_STATE_OMNISCIENT) && game_has_treaty(faction, owner, known) != known)
        return -1;
    if (base) *base = game_base_find3(x, y, -1, t->region, -1, -1);
    return owner;
}

// The nearest base of a faction whose territory the square is, when one of
// the two is human and they aren't at war, or -1.
int base_territory(int faction, int x, int y) {
    int base;
    int owner = whose_territory(faction, x, y, &base, 0);
    if (owner < 0 || owner == faction) return -1;
    if (!game_is_human(faction) && !game_is_human(owner)) return -1;
    if (game_has_treaty(faction, owner, TREATY_VENDETTA)) return -1;
    return base;
}

// Whether the unit can run a supply convoy between its home base and `base`.
int convoy(int veh, int base) {
    int home = game_vehs[veh].home_base;
    if (home < 0 || home == base) return 0;
    int triad = game_veh_triad(veh);
    if (triad == TRIAD_AIR) return 1;
    int region = region_at(game_bases[base].x, game_bases[base].y);
    if (region == region_at(game_bases[home].x, game_bases[home].y) &&
        (region >= LAND_REGION_COUNT) == (triad == TRIAD_SEA))
        return 1;
    if (triad == TRIAD_LAND) return 0;
    return port_to_port(base, home);
}

// Whether the unit could get to the square at all: by land within its
// region, by sea along shores and through ports.
int get_there(int veh, int x, int y) {
    int triad = game_veh_triad(veh);
    if (triad == TRIAD_AIR) return 1;
    int vx = game_vehs[veh].x, vy = game_vehs[veh].y;
    int from = region_at(vx, vy), to = region_at(x, y);
    if (triad == TRIAD_LAND) return from == to;
    int base_from = game_base_at(vx, vy), base_to = game_base_at(x, y);
    if (base_to >= 0) return base_from >= 0 ? port_to_port(base_from, base_to) : base_on_sea(base_to, from);
    if (is_ocean(x, y)) return base_from >= 0 ? base_on_sea(base_from, to) : from == to;
    return base_from >= 0 ? port_to_coast(base_from, to) : sea_coast(to, from);
}

// For a square in the faction's territory, the index of the first neighbour
// that is sea or outside it, or of the first neighbour at all when the
// second square's region differs from the first's; otherwise 0. Callers
// treat it as true or false, and pass the same square twice.
int coast_or_border(int x, int y, int x2, int y2, int faction) {
    if (whose_territory(faction, x, y, NULL, 0) != faction) return 0;
    int region2 = region_at(x2, y2);
    for (int i = 1; i < 9; i++) {
        int xi = xrange(x + game_radius_x[i]), yi = y + game_radius_y[i];
        if (!on_map(xi, yi)) continue;
        if (is_ocean(xi, yi) || whose_territory(faction, xi, yi, NULL, 0) != faction || region_at(x, y) != region2)
            return i;
    }
    return 0;
}

// Whether the faction may name a landmark on the square: its own territory,
// or no one's with no other faction's base nearest.
int valid_landmark(int x, int y, int faction) {
    int owner = *game_net_multiplayer ? tile(x, y)->territory : whose_territory(faction, x, y, NULL, 0);
    if (owner == faction) return 1;
    if (owner > 0) return 0;
    int base = game_base_find(x, y);
    return base < 0 || game_bases[base].faction == faction;
}

// The sensor covering the square: 1 a sensor array on it, 2 the nearest
// base's Geosynchronous Survey Pod (on a square two or none away along each
// axis, as the original checks), or 0.
int is_sensor(int x, int y) {
    if (tile(x, y)->bits & MAP_SENSOR) return 1;
    int base = game_base_find(x, y);
    if (base == -1) return 0;
    const struct base *b = &game_bases[base];
    int dx = x_dist(x, b->x), dy = iabs(y - b->y);
    if ((dx && dx != 2) || (dy && dy != 2)) return 0;
    return game_has_facility(b, FAC_GEOSYNC_SURVEY_POD) ? 2 : 0;
}

// Whether the faction holds and has seen the first square of the Manifold
// Nexus.
int has_temple(int faction) {
    for (int y = 0; y < *game_map_height; y++)
        for (int x = y & 1; x < *game_map_width; x += 2) {
            const struct map_tile *t = tile(x, y);
            if ((t->bits2 & (MAP2_UNK_80000000 | MAP2_NEXUS)) == MAP2_NEXUS && !(t->bits2 & MAP2_LANDMARK_CODE) &&
                whose_territory(faction, x, y, NULL, 0) == faction &&
                (t->visibility & (uint8_t)(1u << (faction & 31))))
                return 1;
        }
    return 0;
}

// Zone of control for a move: a neighbour, on land if the square is land
// and at sea if it is sea, holding a unit of a faction without a pact that
// the faction can see (or, outside network games, that isn't cloaked).
// Gives that faction plus one, or 0.
int zoc_sea(int x, int y, int faction) {
    int sea = is_ocean(x, y);
    for (int i = 0; i < 8; i++) {
        int xi = xrange(x + game_radius_near_x[i]), yi = y + game_radius_near_y[i];
        if (!on_map(xi, yi)) continue;
        int owner = veh_who(xi, yi);
        if (owner < 0 || owner == faction || is_ocean(xi, yi) != sea || game_has_treaty(faction, owner, TREATY_PACT))
            continue;
        for (int v = game_veh_at(xi, yi); v >= 0; v = game_vehs[v].next_in_stack) {
            const struct veh *u = &game_vehs[v];
            if (u->faction == faction) continue;
            if (u->visibility & (uint8_t)(1u << (faction & 31))) return owner + 1;
            if (!*game_net_multiplayer && !(u->flags & VEH_FLAG_INVISIBLE)) return owner + 1;
        }
    }
    return 0;
}

// zoc_sea, except that a base square has none.
int zoc_move(int x, int y, int faction) {
    if (base_who(x, y) >= 0) return 0;
    return zoc_sea(x, y, faction);
}

// Of the neighbours that would give zone of control (as zoc_sea, without
// looking at the units), the one furthest from the destination; ties go to
// the last. Leaves *zx and *zy alone if there is none.
void quick_zoc(int x, int y, int faction, int dest_x, int dest_y, int *zx, int *zy) {
    int sea = is_ocean(x, y), furthest = -1;
    for (int i = 0; i < 8; i++) {
        int xi = xrange(x + game_radius_near_x[i]), yi = y + game_radius_near_y[i];
        if (!on_map(xi, yi)) continue;
        int owner = veh_who(xi, yi);
        if (owner < 0 || owner == faction || is_ocean(xi, yi) != sea || game_has_treaty(faction, owner, TREATY_PACT))
            continue;
        int d = vector_dist(xi, yi, dest_x, dest_y);
        if (d >= furthest) {
            furthest = d;
            *zx = xi;
            *zy = yi;
        }
    }
}

// ---- Changing squares ------------------------------------------------------------

void temp_set(int x, int y, int temperature) {
    struct map_tile *t = tile(x, y);
    t->climate = (uint8_t)((t->climate & ~7) | (temperature & 7));
}

void climate_set(int x, int y, int rainfall) {
    struct map_tile *t = tile(x, y);
    t->climate = (uint8_t)((t->climate & 0xe7) | (rainfall & 3) << 3);
    t->bits2 |= MAP2_CHANGED;
    *game_draw_state |= DRAW_TERRAIN;
}

void alt_put_detail(int x, int y, int contour) { tile(x, y)->contour = (uint8_t)contour; }

void owner_set(int x, int y, int faction) {
    struct map_tile *t = tile(x, y);
    t->owner_site = (uint8_t)((t->owner_site & 0xf0) | (faction & 0xf));
}

void site_set(int x, int y, int site) {
    struct map_tile *t = tile(x, y);
    t->owner_site = (uint8_t)((t->owner_site & 0xf) | site << 4);
}

void region_set(int x, int y, int region) { tile(x, y)->region = (uint8_t)region; }

void using_set(int x, int y, int faction) {
    struct map_tile *t = tile(x, y);
    t->rocky_lock = (uint8_t)((t->rocky_lock & 0xf8) | (faction & 7));
}

void lock_set(int x, int y, int faction) {
    struct map_tile *t = tile(x, y);
    t->rocky_lock = (uint8_t)((t->rocky_lock & 0xc7) | (faction & 7) << 3);
}

// Locks the square for the faction; returns 1 if another faction holds it.
int lock_map(int x, int y, int faction) {
    int lock = tile(x, y)->rocky_lock >> 3 & 7;
    if (lock != faction) {
        if (lock) return 1;
        lock_set(x, y, faction);
    }
    return 0;
}

void unlock_map(int x, int y, int faction) {
    struct map_tile *t = tile(x, y);
    if ((t->rocky_lock >> 3 & 7) == faction) t->rocky_lock &= 0xc7;
}

void rocky_set(int x, int y, int rocky) {
    struct map_tile *t = tile(x, y);
    t->rocky_lock = (uint8_t)((t->rocky_lock & 0x3f) | rocky << 6);
    t->bits2 |= MAP2_CHANGED;
    *game_draw_state |= DRAW_TERRAIN;
}

void bit_put(int x, int y, uint32_t bits) { tile(x, y)->bits = bits; }

void bit_set(int x, int y, uint32_t bits, int set) {
    struct map_tile *t = tile(x, y);
    if (set) t->bits |= bits;
    else t->bits &= ~bits;
}

void bit2_set(int x, int y, uint32_t bits, int set) {
    struct map_tile *t = tile(x, y);
    if (set) t->bits2 |= bits;
    else t->bits2 &= ~bits;
}

void code_set(int x, int y, int code) {
    struct map_tile *t = tile(x, y);
    t->bits2 = (t->bits2 & ~MAP2_LANDMARK_CODE) | (uint32_t)code << 24;
    *game_draw_state |= DRAW_LANDMARKS;
}

// Records what the faction now sees of the square. As in the original,
// faction n writes the n-th word after bits2, whatever n is.
void synch_bit(int x, int y, int faction) {
    if (!faction) return;
    struct map_tile *t = tile(x, y);
    uint32_t *seen = (uint32_t *)((uint8_t *)t + offsetof(struct map_tile, bits2)) + faction;
    *seen = t->bits;
}

void abstract_set(int x, int y, int region) {
    (*game_abstract_map)[(x >> 1) + y * (*game_abstract_width >> 1)] = (uint8_t)region;
}

// Clears the site of the squares in a base's radius. The third argument is
// unused.
void site_radius(int x, int y, int unused) {
    (void)unused;
    for (int i = 0; i < 21; i++) {
        int xi = xrange(x + game_radius_x[i]), yi = y + game_radius_y[i];
        if (on_map(xi, yi)) tile(xi, yi)->owner_site &= 0xf;
    }
}

// Adds a landmark; returns its index, or -1 if there are already 64. The
// name is copied unbounded, as in the original.
int new_landmark(int x, int y, const char *name) {
    int n = *game_landmark_count;
    if (n >= LANDMARK_COUNT) return -1;
    *game_landmark_count = n + 1;
    game_landmarks[n].x = x;
    game_landmarks[n].y = y;
    strcpy(game_landmarks[n].name, name);
    return n;
}

// Removes the first landmark on or next to the square.
void kill_landmark(int x, int y) {
    int l = find_landmark(x, y, 1);
    if (l < 0) return;
    for (int i = l; i < *game_landmark_count - 1; i++) game_landmarks[i] = game_landmarks[i + 1];
    (*game_landmark_count)--;
}

// ---- The whole map ---------------------------------------------------------------

// Marks the squares with units or bases, and their owners, from the unit and
// base lists. Both walk the squares in storage order.
void rebuild_vehicle_bits(void) {
    struct map_tile *t = *game_map;
    for (int y = 0; y < *game_map_height; y++)
        for (int x = y & 1; x < *game_map_width; x += 2, t++) {
            t->bits &= ~MAP_UNIT;
            for (int v = 0; v < *game_veh_count; v++)
                if (game_vehs[v].x == x && game_vehs[v].y == y) {
                    t->bits |= MAP_UNIT;
                    if (!(t->bits & MAP_BASE)) t->owner_site = (uint8_t)((t->owner_site & 0xf0) | (game_vehs[v].faction & 0xf));
                    break;
                }
        }
}

void rebuild_base_bits(void) {
    struct map_tile *t = *game_map;
    for (int y = 0; y < *game_map_height; y++)
        for (int x = y & 1; x < *game_map_width; x += 2, t++) {
            t->bits &= ~MAP_BASE;
            for (int b = 0; b < *game_base_count; b++)
                if (game_bases[b].x == x && game_bases[b].y == y) {
                    t->bits |= MAP_BASE;
                    t->owner_site = (uint8_t)((t->owner_site & 0xf0) | (game_bases[b].faction & 0xf));
                    break;
                }
        }
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
SHADOW_WRITER(MAP, int, whose_territory, (int a, int b, int c, int *d, int e), (a, b, c, d, e), if (d) WRITES(d, sizeof(*d)),
              "%d, %d, %d, %p, %d", a, b, c, (void *)d, e)
SHADOW3(MAP, int, base_territory, int, int, int)
SHADOW2(MAP, int, convoy, int, int)
SHADOW3(MAP, int, get_there, int, int, int)
SHADOW5(MAP, int, coast_or_border, int, int, int, int, int)
SHADOW3(MAP, int, valid_landmark, int, int, int)
SHADOW2(MAP, int, is_sensor, int, int)
SHADOW1(MAP, int, has_temple, int)
SHADOW3(MAP, int, zoc_sea, int, int, int)
SHADOW3(MAP, int, zoc_move, int, int, int)
SHADOW_WRITER(MAP, void, quick_zoc, (int a, int b, int c, int d, int e, int *f, int *g), (a, b, c, d, e, f, g),
              WRITES(f, sizeof(*f)); WRITES(g, sizeof(*g)), "%d, %d, %d, %d, %d", a, b, c, d, e)

// Setters write their square, and some the redraw flags.
#define WRITES_SQUARE(x, y) WRITES(tile(x, y), sizeof(struct map_tile))
#define WRITES_SQUARE_DRAW(x, y) WRITES_SQUARE(x, y); WRITES(game_draw_state, sizeof(*game_draw_state))
#define SET3(ret, name, t3, writes) SHADOW_WRITER(MAP, ret, name, (int a, int b, t3 c), (a, b, c), writes, "%d, %d, %d", a, b, (int)c)
#define SET4(name, t3) \
    SHADOW_WRITER(MAP, void, name, (int a, int b, t3 c, int d), (a, b, c, d), WRITES_SQUARE(a, b), "%d, %d, 0x%x, %d", a, b, (unsigned)c, d)
SET3(void, temp_set, int, WRITES_SQUARE(a, b))
SET3(void, climate_set, int, WRITES_SQUARE_DRAW(a, b))
SET3(void, alt_put_detail, int, WRITES_SQUARE(a, b))
SET3(void, owner_set, int, WRITES_SQUARE(a, b))
SET3(void, site_set, int, WRITES_SQUARE(a, b))
SET3(void, region_set, int, WRITES_SQUARE(a, b))
SET3(void, using_set, int, WRITES_SQUARE(a, b))
SET3(void, lock_set, int, WRITES_SQUARE(a, b))
SET3(int, lock_map, int, WRITES_SQUARE(a, b))
SET3(void, unlock_map, int, WRITES_SQUARE(a, b))
SET3(void, rocky_set, int, WRITES_SQUARE_DRAW(a, b))
SET3(void, bit_put, uint32_t, WRITES_SQUARE(a, b))
SET4(bit_set, uint32_t)
SET4(bit2_set, uint32_t)
SET3(void, code_set, int, WRITES_SQUARE_DRAW(a, b))
SET3(void, synch_bit, int, if (c) WRITES((uint32_t *)((uint8_t *)tile(a, b) + offsetof(struct map_tile, bits2)) + c, 4))
SET3(void, abstract_set, int, WRITES(&(*game_abstract_map)[(a >> 1) + b * (*game_abstract_width >> 1)], 1))

// Functions that may write anywhere on the map, or the landmarks.
static size_t map_bytes(void) { return (size_t)*game_map_half_width * *game_map_height * sizeof(struct map_tile); }
#define WRITES_MAP WRITES(*game_map, map_bytes())
#define WRITES_LANDMARKS \
    WRITES(game_landmark_count, sizeof(*game_landmark_count)); WRITES(game_landmarks, LANDMARK_COUNT * sizeof(struct landmark))
SET3(void, site_radius, int, WRITES_MAP)
SHADOW_WRITER(MAP, int, new_landmark, (int a, int b, const char *c), (a, b, c), WRITES_LANDMARKS, "%d, %d, \"%s\"", a, b, c)
SHADOW_WRITER(MAP, void, kill_landmark, (int a, int b), (a, b), WRITES_LANDMARKS, "%d, %d", a, b)
SHADOW_WRITER(MAP, void, rebuild_vehicle_bits, (void), (), WRITES_MAP, "%s", "")
SHADOW_WRITER(MAP, void, rebuild_base_bits, (void), (), WRITES_MAP, "%s", "")

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

// Units, territory and sensors, on every square for every faction; the
// costlier checks on a share of the squares each turn.
static void sweep_territory(void) {
    int w = *game_map_width, h = *game_map_height;
    for (int y = 0; y < h; y++)
        for (int x = y & 1; x < w; x += 2) {
            int n = x + y * w, base, zx = -1, zy = -1;
            check_is_sensor(x, y);
            for (int f = 0; f < FACTION_COUNT; f++) {
                check_zoc_sea(x, y, f);
                check_zoc_move(x, y, f);
                check_quick_zoc(x, y, f, w / 2, h / 2, &zx, &zy);
                check_whose_territory(f, x, y, NULL, 0);
                check_whose_territory(f, x, y, NULL, 1);
                check_coast_or_border(x, y, x, y, f);
                if ((n + f + slice) % 8 == 0) {
                    check_whose_territory(f, x, y, &base, 0);
                    check_base_territory(f, x, y);
                    check_valid_landmark(x, y, f);
                }
            }
        }
    for (int f = 0; f < FACTION_COUNT; f++) check_has_temple(f);
}

// Every unit: where it could get to, and convoys to a share of the bases.
static void sweep_units(void) {
    int w = *game_map_width, h = *game_map_height;
    for (int v = 0; v < *game_veh_count; v++) {
        for (int b = 0; b < *game_base_count; b++)
            if ((v + b + slice) % 16 == 0) {
                check_convoy(v, b);
                check_get_there(v, game_bases[b].x, game_bases[b].y);
            }
        for (int y = (v + slice) % 7; y < h; y += 7) {
            int x = ((v * 13 + y * 5) % w & ~1) | (y & 1);
            if (x < w) check_get_there(v, x, y);
        }
    }
}

// The setters, each on every square with values that change from turn to
// turn; the map is put back after each.
static void sweep_setters(void) {
    int w = *game_map_width, h = *game_map_height;
    for (int y = 0; y < h; y++)
        for (int x = y & 1; x < w; x += 2) {
            int v = x * 7 + y * 3 + slice, f = v & 7;
            check_temp_set(x, y, v);
            check_climate_set(x, y, v >> 1);
            check_alt_put_detail(x, y, v * 11);
            check_owner_set(x, y, v);
            check_site_set(x, y, v >> 2);
            check_region_set(x, y, v * 5);
            check_using_set(x, y, f);
            check_lock_set(x, y, f);
            check_lock_map(x, y, f);
            check_unlock_map(x, y, f);
            check_unlock_map(x, y, tile(x, y)->rocky_lock >> 3 & 7);
            check_rocky_set(x, y, v >> 3);
            check_bit_put(x, y, (uint32_t)v * 0x9e3779b9u);
            check_bit_set(x, y, 1u << (v & 31), v & 1);
            check_bit2_set(x, y, 1u << (v & 31), ~v & 1);
            check_code_set(x, y, v);
            check_synch_bit(x, y, f);
        }
    if (*game_abstract_map)
        for (int y = 0; y < *game_abstract_height; y++)
            for (int x = 0; x < *game_abstract_width; x++) check_abstract_set(x, y, x + y + slice);
    // These save the whole map: a few squares a turn.
    for (int i = 0; i < 4; i++) {
        int y = (slice * 5 + i * 17) % h, x = ((slice * 11 + i * 29) % w & ~1) | (y & 1);
        if (x < w) check_site_radius(x, y, 0);
    }
    check_rebuild_vehicle_bits();
    check_rebuild_base_bits();
    // On the landmark itself, or diagonally off it, which it still reaches.
    for (int l = 0; l < *game_landmark_count; l++)
        check_kill_landmark(xrange(game_landmarks[l].x + ((l + slice) & 1)), game_landmarks[l].y + ((l + slice) & 1));
    check_new_landmark(slice % w, 0, "Chiron");
}

static void map_sweep(void) {
    slice++;
    if (!*game_map || *game_map_width <= 0 || *game_map_height <= 0) return;
    // The game's base_find() leaves its distance behind, and callers read
    // it; the sweep's calls mustn't change it.
    int find_dist = *game_base_find_dist;
    hooks_sweeping = 1;
    sweep_pure();
    sweep_squares();
    sweep_bases();
    sweep_territory();
    sweep_units();
    sweep_setters();
    hooks_sweeping = 0;
    *game_base_find_dist = find_dist;
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
    HOOK(0x4e3ef0, whose_territory, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x4e3fa0, base_territory, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x4d, 0x10),  // mov ecx, [ebp+16]
    HOOK(0x50e5c0, convoy, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x18),
    HOOK(0x56b320, get_there, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x45, 0x08),     // push ecx; mov eax, [ebp+8]
    HOOK(0x56b480, coast_or_border, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),
    HOOK(0x592650, valid_landmark, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x60, 0xf6, 0x93, 0x00),  // mov eax, [0x93f660]
    HOOK(0x5bf010, is_sensor, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x5bf130, has_temple, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x74, 0x98, 0x94, 0x00),  // mov eax, [0x949874]
    HOOK(0x5c8ba0, zoc_sea, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),
    HOOK(0x5c8d40, zoc_move, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x593830, quick_zoc, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),
    HOOK(0x591ad0, temp_set, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x591a80, climate_set, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x591260, alt_put_detail, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x591b10, owner_set, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x591b50, site_set, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x591b90, region_set, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x591c10, using_set, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x591c50, lock_set, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x591c90, lock_map, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x591cf0, unlock_map, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x591bc0, rocky_set, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x591d30, bit_put, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x591d60, bit_set, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x591db0, bit2_set, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x591e00, code_set, PUSH_EBP_MOV_EBP_ESP, LOAD_HALF_WIDTH),
    HOOK(0x591e50, synch_bit, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x4d, 0x10),
    HOOK(0x591230, abstract_set, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x94, 0xa2, 0x94, 0x00),  // mov eax, [0x94a294]
    HOOK(0x592400, site_radius, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x70, 0x98, 0x94, 0x00),   // mov eax, [0x949870]
    HOOK(0x592600, new_landmark, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x90, 0x98, 0x94, 0x00),  // mov eax, [0x949890]
    HOOK(0x5926f0, kill_landmark, PUSH_EBP_MOV_EBP_ESP, 0x51, 0xa1, 0xc8, 0xf8, 0x66, 0x00),  // push ecx; mov eax, [0x66f8c8]
    HOOK(0x532a90, rebuild_vehicle_bits, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),
    HOOK(0x532b70, rebuild_base_bits, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x0d, 0x74, 0x98, 0x94, 0x00),  // push ecx; mov ecx, [0x949874]
};
struct hook_module map_module = {"map", map_hooks, HOOK_COUNT(map_hooks), map_sweep, -1};
