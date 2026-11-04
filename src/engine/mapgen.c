// Map generation: the passes that shape a new map's terrain and climate, and
// the terrain changes that reuse them in play. Reimplemented from terranx.exe
// 2.0 by reading the original code; OpenSMACX (Brendan Casey,
// GPL-3.0-or-later), whose names we use, marks most of these as untested,
// and they differ from the original in places noted below.
//
// These draw random numbers from the C runtime's rand() and from the game's
// own generator, and write widely, so in shadow mode each check covers all of
// the game's writable memory, the map and rand()'s state.
#include "mapgen.h"

#include "game.h"
#include "map.h"
#include "shadow.h"

static inline struct map_tile *tile(int x, int y) { return &(*game_map)[(x >> 1) + y * *game_map_half_width]; }

// ---- Changing altitude -------------------------------------------------------

static inline int alt_of(const struct map_tile *t) { return t->climate >> 5; }

// Tells the player a base they can see went under. `kept` when it was
// their last and survives with one citizen.
static void flood_message(const struct base *base, int kept) {
    int player = *game_current_faction;
    if (base->faction != player && !(base->visibility & (1u << (player & 31)))) return;
    game_console_focus(game_console, base->x, base->y, player);
    game_parse_says(0, (char *)base->name, -1, -1);
    // Labels and pictures: BASESUBMERGED1 (yours), 3 (another's), then for a
    // last base BASESUBMERGED (yours) and 2.
    int own = base->faction == player, label = kept ? (own ? 5 : 7) : (own ? 1 : 3);
    if (!own) game_parse_says(1, game_faction_defs[base->faction].adjective, -1, -1);
    char *text = game_flood_texts + label * 16, *picture = text - 16;
    if (kept) game_x_pop2(text, picture, 0);
    else game_popp(*game_script_file, text, 0, picture, 0);
}

// The sea has risen over a base: without a pressure dome it loses citizens
// and gets one, or is lost if none are left (unless it is its faction's
// last, or a network game won't allow it now). Returns whether it was
// lost, leaving its units to the sea.
static int flood_base(int b) {
    struct base *base = &game_bases[b];
    if (game_has_facility(base, FAC_PRESSURE_DOME)) return 0;
    int lost = game_rand() % 3 + 2, half = base->pop_size / 2;
    if (half < lost || lost > 10) half = lost;
    else if (half > 10) half = 10;
    base->pop_size = (int8_t)(base->pop_size - half);
    game_set_fac(FAC_PRESSURE_DOME, b, 1);
    int f = base->faction;
    game_factions[f].flags |= FACTION_FLAG_BASE_SUBMERGED;
    game_base_submerged_count[f]++;
    if (game_is_objective(b) && base->pop_size < 1) base->pop_size = 1;
    if (base->pop_size > 0) return 0;
    if (game_factions[f].base_count > 1 && (!*game_net_multiplayer || *game_control_turn)) {
        if (!*game_battle_state) flood_message(base, 0);
        game_base_kill(b);
        return 1;
    }
    base->pop_size = 1;
    if (!*game_battle_state) flood_message(base, 1);
    return 0;
}

// Sets the square's altitude. In a game under way, land that becomes sea or
// sea that becomes land loses its improvements, and, unless
// game_world_skip_territory, floods the base there and loses the units
// that can't stay; land raised or lowered may grow or lose rockiness.
void alt_set(int x, int y, int altitude) {
    struct map_tile *t = tile(x, y);
    if (*game_turn) {
        int old = alt_of(t), sea = altitude < ALT_SHORE_LINE;
        if (altitude != old) game_delete_landmark(x, y);
        if ((old < ALT_SHORE_LINE) != sea) {
            t->bits &= 0x7e1377a3;
            for (int f = 0; f < 7; f++) t->bits_seen[f] = t->bits;
            int b = *game_world_skip_territory ? -2 : game_base_at(x, y);
            if (b >= 0 && (!sea || !flood_base(b))) b = -2;
            for (int v = b == -2 ? -1 : game_stack_fix(game_veh_at(x, y)); v >= 0;) {
                const struct veh *u = &game_vehs[v];
                int next = u->next_in_stack, triad = game_veh_triad(v);
                if (triad != TRIAD_AIR && !(u->flags & VEH_FLAG_UNK_20) && (triad == TRIAD_SEA) != sea) {
                    game_base_submerged_veh[u->faction] = game_base_find2(x, y, u->faction);
                    game_veh_kill(v);
                    if (next > v) next--;
                }
                v = next;
            }
        } else if (!sea && altitude != old) {
            int rocky = t->rocky_lock >> 6;
            if (altitude > old ? rocky < 2 && rocky + 1 <= altitude - 3 && game_rand() % (rocky + 4) == 0
                               : rocky && rocky > altitude - 3 && game_rand() % (rocky + 1)) {
                rocky += altitude > old ? 1 : -1;
                t->rocky_lock = (uint8_t)(rocky << 6 | (t->rocky_lock & 0x3f));
                t->bits2 |= MAP2_CHANGED;
                *game_draw_state |= DRAW_TERRAIN;
            }
        }
    }
    t->climate = (uint8_t)((t->climate & 0x1f) | altitude << 5);
    *game_draw_state |= DRAW_LANDMARKS;
}

// Sets the square's altitude, and gives it a contour within that altitude's
// band (a random one) unless it is in the band already.
void alt_set_both(int x, int y, int altitude) {
    alt_set(x, y, altitude);
    if (alt_natural(x, y) == altitude) return;
    int lo = game_alt_natural[altitude], span = game_alt_natural[altitude + 1] - lo;
    int r = game_rnd(span);
    alt_put_detail(x, y, (uint8_t)(lo + *game_sea_level + r));
}

// One ring of squares around (x, y): those below `altitude` are raised to it
// (rising), or those above it lowered (not rising). Returns whether any were.
static int slope_ring(int x, int y, int ring, int altitude, int rising, int both) {
    int changed = 0;
    for (int i = game_radius_count[ring - 1]; i < game_radius_count[ring]; i++) {
        int xi = xrange(x + game_radius_x[i]), yi = y + game_radius_y[i];
        if (!on_map(xi, yi)) continue;
        int alt = alt_at(xi, yi);
        if (rising ? alt >= altitude : alt <= altitude) continue;
        if (both) {
            alt_set_both(xi, yi, altitude);
            if (anything_at(xi, yi) < 0 && !bonus_at(xi, yi, 0)) owner_set(xi, yi, -1);
        } else {
            alt_set(xi, yi, altitude);
        }
        changed = 1;
    }
    return changed;
}

// Sets the square's altitude, then slopes the land around it: each ring out
// is raised to at least one less, and lowered to at most one more, until a
// ring needs no change. (OpenSMACX has the lowering pass raise instead.)
void world_alt_set(int x, int y, int altitude, int both) {
    for (int i = 0; i < FACTION_COUNT; i++) game_base_submerged_count[i] = 0, game_base_submerged_veh[i] = -1;
    if (on_map(x, y)) {
        if (both) alt_set_both(x, y, altitude);
        else alt_set(x, y, altitude);
    }
    for (int ring = 1; ring < altitude; ring++)
        if (!slope_ring(x, y, ring, altitude - ring, 1, both)) break;
    for (int ring = 1; ring < 8 - altitude; ring++)
        if (!slope_ring(x, y, ring, altitude + ring, 0, both)) break;
}

// One level up or down, sloping the land around. (OpenSMACX passes the
// altitude unchanged.)
void world_raise_alt(int x, int y) {
    if (!on_map(x, y)) return;
    int alt = alt_at(x, y);
    if (alt < 6) world_alt_set(x, y, alt + 1, 1);
}

void world_lower_alt(int x, int y) {
    if (!on_map(x, y)) return;
    int alt = alt_at(x, y);
    if (alt) world_alt_set(x, y, alt - 1, 1);
}

// ---- Painting land -------------------------------------------------------------

// Whether (x, y) is a neighbour of the square in direction d, and on the map.
static int near_square(int x, int y, int d, int *xn, int *yn) {
    *xn = xrange(x + game_radius_near_x[d & 7]);
    *yn = y + game_radius_near_y[d & 7];
    return on_map(*xn, *yn);
}

// Raises the square and its neighbours to the north-east and south-east to
// the altitude, sloping the land around, unless that would touch land of no
// region yet (another continent being made), or, at shore level, join the
// sea on both sides of a shore square (it would pinch a channel shut).
// (OpenSMACX makes that last check at every altitude but the shore's.)
void brush(int x, int y, int altitude) {
    int changed = 0;
    for (int i = 0; i < 4; i++) {
        if (i == 2) continue;
        int xi = xrange(x + game_radius_x[i]), yi = y + game_radius_y[i];
        if (!on_map(xi, yi) || alt_at(xi, yi) == altitude) continue;
        int blocked = 0;
        for (int d = 0; d < 8; d++) {
            int xn, yn;
            if (near_square(xi, yi, d, &xn, &yn) && !is_ocean(xn, yn) && !region_at(xn, yn)) blocked = 1;
        }
        if (blocked) continue;
        if (altitude == ALT_SHORE_LINE)
            for (int k = 0; k + 1 <= 7 && !blocked; k += 2) {
                int xn, yn, xa, ya, xb, yb;
                if (!near_square(xi, yi, k + 1, &xn, &yn) || (tile(xn, yn)->climate & 0xe0) != ALT_SHORE_LINE << 5)
                    continue;
                int a = near_square(xi, yi, k + 2, &xa, &ya), b = near_square(xi, yi, k, &xb, &yb);
                if (a && b && is_ocean(xa, ya) && is_ocean(xb, yb)) blocked = 1;
            }
        if (blocked) continue;
        tile(xi, yi)->region = 1;
        changed |= alt_at(xi, yi) != altitude;
        world_alt_set(xi, yi, altitude, 1);
        if ((*game_state & GAME_STATE_OMNISCIENT) && altitude > ALT_SHORE_LINE) temp_set(xi, yi, 1);
        ++*game_brush_stroke;
        ++*game_brush_total;
    }
    if ((*game_state & GAME_STATE_OMNISCIENT) && changed) game_draw_radius(x, y, 2, 2);
}

// Paints land at the altitude in random walks from (x, y), each going on
// from where the last step left it, until `size` squares are painted, a
// walk leaves the map, or 2000 steps in all. A walk that keeps the same
// direction too long (longer on wetter maps) turns.
void paint_land(int x, int y, int altitude, int size) {
    int steps = 0;
    *game_brush_stroke = 0;
    do {
        int xi = x, yi = y, last = -1, run = 0;
        do {
            brush(xi, yi, altitude);
            int d = game_rand() % 8 | 1;
            if (d == last) {
                int c = *game_land_coverage * *game_land_coverage;
                if (++run > c + 2) d = (d - c + run - 2) & 7;
            } else {
                last = d;
                run = 0;
            }
            xi = xrange(xi + game_radius_near_x[d]);
            yi += game_radius_near_y[d];
        } while (++steps < 2000 && on_map(xi, yi) && *game_brush_stroke < size);
    } while (++steps < 2000 && *game_brush_stroke < size);
}

// A random square away from the poles (and, on flat maps, the east and
// west edges), moved onto the map's grid.
static void random_site(int *x, int *y) {
    int flat = *game_map_flat & 1;
    *x = game_rnd(*game_map_width - flat * 8) + flat * 4;
    *y = game_rnd(*game_map_height - 8) + 4;
    if (*x & 1) --*x;
    if (*y & 1) ++*x;
}

// Paints a continent at shore level from a random sea square, sized by the
// map's land coverage and how much of `size` (land squares wanted) it is,
// smaller once the land wanted is reached. Clears every region first.
void build_continent(int size) {
    struct map_tile *t = *game_map;
    for (int i = 0; i < *game_map_area; i++, t++) t->region = 0;
    const struct rules_worldbuilder *wb = game_worldbuilder;
    int coverage = *game_land_coverage;
    int enough = *game_brush_total >= *game_brush_land_wanted;
    if (coverage && enough) coverage--;
    int radius = wb->continent_mod * coverage * coverage + wb->continent_base;
    int x, y;
    for (int tries = 0;;) {
        random_site(&x, &y);
        if (++tries >= 100 || alt_at(x, y) < ALT_SHORE_LINE) break;
    }
    if (alt_at(x, y) >= ALT_SHORE_LINE) return;
    int ratio = size * 3200 / *game_map_area;
    if (ratio > wb->cont_size_ratio[4] || enough) {
        radius /= 4;
        if (enough && ratio > wb->cont_size_ratio[4]) radius = game_rand() % 3 + 1;
    } else if (ratio > wb->cont_size_ratio[3]) {
        radius /= 3;
    } else if (ratio > wb->cont_size_ratio[2]) {
        radius /= 2;
    } else if (ratio > wb->cont_size_ratio[1]) {
        if (game_rnd(4 - *game_land_coverage)) radius /= 2;
    } else if (ratio < wb->cont_size_ratio[0] && *game_land_coverage > 1) {
        radius += radius / 2;
    }
    if (*game_brush_total < *game_brush_land_wanted && radius < 4) radius = 4;
    paint_land(x, y, ALT_SHORE_LINE, radius);
    game_do_all_non_input();
}

// Paints hills or plateaus at the altitude, on a random site whose square
// and neighbours are no more than one level below it; gives up after 50
// sites. (OpenSMACX doesn't retry.)
void build_hills(int altitude) {
    int x, y;
    for (int tries = 0;;) {
        random_site(&x, &y);
        if ((tries += 2) > 100) return;
        int low = 0;
        for (int i = 0; i < 9 && !low; i++) {
            int xi = xrange(x + game_radius_x[i]), yi = y + game_radius_y[i];
            low = on_map(xi, yi) && alt_at(xi, yi) < altitude - 1;
        }
        if (!low) break;
    }
    const struct rules_worldbuilder *wb = game_worldbuilder;
    int size = (2 - *game_ocean_coverage) * wb->plateau_mod;
    if (altitude != 4) size /= 4;
    int base = wb->plateau_base;
    if (*game_ocean_coverage >= 2) base /= 2;
    paint_land(x, y, altitude, base + size);
    game_do_all_non_input();
}

// Marks riverbeds: from random land squares, the highest square within 3,
// unless a riverbed lies within 4 already (OpenSMACX places one anyway).
// More on wetter, cloudier, landier maps.
void world_riverbeds(void) {
    struct map_tile *t = *game_map;
    for (int i = 0; i < *game_map_area; i++, t++) t->bits &= ~MAP_RIVERBED;
    const struct rules_worldbuilder *wb = game_worldbuilder;
    int wanted = (4 - *game_ocean_coverage) * (wb->rivers_base + *game_cloud_cover * wb->rivers_rain_mod) / 3 *
                 *game_map_area / 3200;
    for (int tries = 0, placed = 0; placed < wanted && tries < 4000; tries++) {
        int x = game_rnd(*game_map_width), y = game_rnd(*game_map_height);
        if (x & 1) x--;
        if (y & 1) x++;
        const struct map_tile *s = tile(x, y);
        if ((s->climate & 0xe0) < ALT_SHORE_LINE << 5 || (s->bits & MAP_RIVERBED)) continue;
        int best = -1, best_alt = 0, near_river = 0;
        for (int j = 0; j < 81 && !near_river; j++) {  // radius 4, a constant in the original
            int xi = xrange(x + game_radius_x[j]), yi = y + game_radius_y[j];
            if (!on_map(xi, yi)) continue;
            if (bit_at(xi, yi) & MAP_RIVERBED) near_river = 1;
            else if (j < 49 && alt_at(xi, yi) > best_alt) best_alt = alt_at(xi, yi), best = j;
        }
        if (near_river || best < 0) continue;
        bit_set(xrange(x + game_radius_x[best]), y + game_radius_y[best], MAP_RIVERBED, 1);
        placed++;
    }
}

// ---- Making a map --------------------------------------------------------------

// A blank map: all ocean, no landmarks, a new seed. Leaves each square's
// unk_6 and territory.
void map_wipe(void) {
    *game_sea_level_council = 0;
    *game_landmark_count = 0;
    *game_sea_level = 0;
    *game_map_seed = game_random(0, 0x7fff) + 1;
    struct map_tile *t = *game_map;
    for (int i = 0; i < *game_map_area; i++, t++) {
        t->climate = 1 << 5;
        t->contour = 20;
        t->owner_site = 0xf;
        t->region = 0;
        t->visibility = 0;
        t->bits = 0;
        t->rocky_lock = 0;
        t->bits2 = 0;
        for (int f = 0; f < 7; f++) t->bits_seen[f] = 0;
    }
}

// Whether one land region dominates: the second largest under half the
// largest (two thirds with land coverage 1). world_build() makes such a map
// again. Finds the regions first.
int world_validate(void) {
    game_path_continents(game_path);
    int largest = 0, second = 0;
    for (int r = 1; r < LAND_REGION_COUNT; r++) {
        if (bad_reg(r)) continue;
        int n = game_continents[r].tile_count;
        if (n >= largest) second = largest, largest = n;
        else if (n >= second) second = n;
    }
    if (*game_land_coverage == 1) return second < largest * 2 / 3;
    return second < largest / 2;
}

// Temperature by latitude, the planet's orbit and the council's sea level
// changes, with random deviation; warmer next to a thermal borehole, cooler
// on high ground. Craters with codes below 21 keep theirs.
void world_temperature(void) {
    const struct rules_worldbuilder *wb = game_worldbuilder;
    int h = *game_map_height, orbit = *game_planet_orbit;
    game_random_reseed(*game_map_seed + 17);
    int heat = h / wb->solar_energy, band = h / wb->thermal_band;
    int deviance = h / wb->thermal_deviance, warming = h / wb->global_warming;
    for (int y = 0; y < h; y++) {
        for (int x = y & 1; x < *game_map_width; x += 2) {
            uint32_t bits2 = tile(x, y)->bits2;
            if ((bits2 & (MAP2_UNK_80000000 | MAP2_CRATER)) == MAP2_CRATER && (int32_t)(bits2 & MAP2_LANDMARK_CODE) < 21 << 24)
                continue;
            int dev = game_random(0, deviance * 2);
            int orbit_roll = game_random(0, orbit + 1);
            int v = h / 2 - dev - y + deviance;
            if (v < 0) v = dev - h / 2 - deviance + y;
            v -= (orbit - 1) * heat + *game_sea_level_council * warming;
            int t = (band / 2 + v * 2) / band;
            int temp = t <= 2 ? 3 : (t <= 9) + 1;
            int alt = alt_at(x, y);
            for (int i = 0; i < 9; i++) {  // the square and its neighbours
                int xi = xrange(x + game_radius_x[i]), yi = y + game_radius_y[i];
                if (on_map(xi, yi) && (bit_at(xi, yi) & MAP_THERMAL_BORE) && (alt <= 4 || alt_at(xi, yi) >= alt)) {
                    temp++;
                    break;
                }
            }
            if (temp > 1) {
                if (alt > 5) {
                    if (orbit < 2 || !game_random(0, 2)) temp--;
                } else if (alt > 4 && !orbit_roll) {
                    temp--;
                }
            }
            temp_set(x, y, game_range(temp - 1, 0, 2));
        }
        game_do_all_non_input();
    }
}

// Counts each region's open land, and marks land squares where the coast
// winds (MAP_UNK_40000000: sea and land alternate four times around the
// square, in a region of 80 squares or more, and the next ring out changes
// too) or that lie between two large regions (MAP_UNK_4000). Then clears
// every square's site. Reads squares by position and writes them in
// storage order, as the original.
void world_analysis(void) {
    for (int r = 0; r < REGION_COUNT; r++) game_continents[r].open_terrain = 0;
    int w = *game_map_width, h = *game_map_height;
    struct map_tile *p = *game_map;
    for (int y = 0; y < h; y++) {
        for (int x = y & 1; x < w; x += 2, p++) {
            const struct map_tile *t = tile(x, y);
            int region = t->region, alt = t->climate & 0xe0;
            if (alt >= ALT_SHORE_LINE << 5 && (t->climate & 0x18) && (t->rocky_lock & 0xc0) < 0x80 &&
                (!(t->bits & MAP_FUNGUS) || alt < 0x40))
                game_continents[region].open_terrain++;
            p->bits &= ~(MAP_UNK_40000000 | MAP_UNK_4000);
            if (is_ocean(x, y)) continue;
            int last = -1, near_changes = 0, far_changes = 0;
            for (int i = 1; i < 21; i++) {
                int xi = xrange(x + game_radius_x[i]), yi = y + game_radius_y[i];
                if (!on_map(xi, yi)) continue;
                int sea = is_ocean(xi, yi);
                if (i < 9) {
                    if (sea == last) continue;
                    last = sea;
                    if (++near_changes >= 4 && game_continents[region].tile_count >= 80) p->bits |= MAP_UNK_40000000;
                } else {
                    if (sea != last || i == 9) last = sea, far_changes++;
                    if (yi > 4 && yi < h - 4) {
                        int other = region_at(xi, yi);
                        if (other < LAND_REGION_COUNT && other != region && game_continents[other].tile_count >= 40 &&
                            game_continents[region].tile_count >= 40)
                            p->bits |= MAP_UNK_4000;
                    }
                }
            }
            if (far_changes < 4) p->bits &= ~MAP_UNK_40000000;
        }
        game_do_all_non_input();
    }
    struct map_tile *t = *game_map;
    for (int i = 0; i < *game_map_area; i++, t++) t->owner_site &= 0xf;
}

void world_alt_put_detail(int x, int y) { alt_put_detail(x, y, (uint8_t)game_alt_natural[3]); }

// Shore-level contours along both poles, and a few more a row in. (OpenSMACX
// puts the south pole's one column to the west.)
void world_polar_caps(void) {
    for (int x = 0; x < *game_map_width; x += 2) {
        alt_put_detail(x, 0, (uint8_t)game_alt_natural[3]);
        alt_put_detail(x + 1, *game_map_height - 1, (uint8_t)game_alt_natural[3]);
    }
    for (int i = 0; i < *game_map_width / 16; i++) {
        int c = game_rnd(*game_map_width / 2) * 2;
        alt_put_detail(c, 0, (uint8_t)game_alt_natural[3]);
        c = game_rnd(*game_map_width / 2) * 2 + 1;
        alt_put_detail(c, 1, (uint8_t)game_alt_natural[3]);
        c = game_rnd(*game_map_width / 2) * 2 + 1;
        alt_put_detail(c, *game_map_height - 1, (uint8_t)game_alt_natural[3]);
        c = game_rnd(*game_map_width / 2) * 2;
        alt_put_detail(c, *game_map_height - 2, (uint8_t)game_alt_natural[3]);
    }
}

// Maps each square's contour from the generator's altitude bands
// (game_alt_natural_default) onto the elevation bands, then makes those the
// altitude bands for the rest of the game. The top band stretches to the
// highest elevation.
void world_linearize_contours(void) {
    const int32_t *from = game_alt_natural_default, *to = game_elev_detail;
    for (int y = 0; y < *game_map_height; y++) {
        for (int x = y & 1; x < *game_map_width; x += 2) {
            int n = alt_natural(x, y);
            int top = n < 6 ? to[n + 1] : to[n + 4];
            int contour = (top - to[n]) * (alt_detail_at(x, y) - from[n]) / (from[n + 1] - from[n]) + to[n];
            alt_put_detail(x, y, (uint8_t)contour);
        }
        game_do_all_non_input();
    }
    for (int i = 0; i < 11; i++) game_alt_natural[i] = to[i];
}

// ---- Shaping the land and its climate ----------------------------------------

// Value noise that shapes erosion and fungus (a C++ object on the
// original's stack): a random 18 x 18 grid of 0 to 15, wrapped to 19 x 19,
// and a 17 x 17 copy of its corner, wrapped at 16, that noise_at() reads.
// (The original's constructor also sums each cell's neighbours, and throws
// the sum away.)
struct noise {
    int8_t cell[0x2e4];
};
#define NOISE_FINE(n, r, c) ((n)->cell[(r) * 19 + (c)])
#define NOISE_COARSE(n, r, c) ((n)->cell[0x169 + (r) * 19 + (c)])

static void noise_init(struct noise *n) {
    for (int r = 0; r < 18; r++) {
        for (int c = 0; c < 18; c++) NOISE_COARSE(n, r, c) = (int8_t)(game_rand() % 16);
        NOISE_COARSE(n, r, 18) = NOISE_COARSE(n, r, 0);
    }
    for (int c = 0; c < 18; c++) NOISE_COARSE(n, 18, c) = NOISE_COARSE(n, 0, c);
    for (int r = 0; r <= 16; r++)
        for (int c = 0; c <= 16; c++) NOISE_FINE(n, r, c) = NOISE_COARSE(n, r, c);
    for (int r = 0; r <= 16; r++) NOISE_FINE(n, r, 16) = NOISE_FINE(n, r, 0);
    for (int c = 0; c <= 16; c++) NOISE_FINE(n, 16, c) = NOISE_FINE(n, 0, c);
}

// The noise at (x, y), in 256ths of a cell: bilinear between the four cells
// around it, in 32 steps, 0 to 480.
static int noise_at(const struct noise *n, int x, int y) {
    x -= 128, y -= 128;
    int ix = x >> 8 & 15, iy = y >> 8 & 15, fx = x >> 3 & 31, fy = y >> 3 & 31;
    int v = (NOISE_FINE(n, ix + 1, iy) * fx + NOISE_FINE(n, ix, iy) * (32 - fx)) * (32 - fy) +
            (NOISE_FINE(n, ix, iy + 1) * (32 - fx) + NOISE_FINE(n, ix + 1, iy + 1) * fx) * fy;
    return v >> 5;
}

// Two scales of the noise at a square, 0 to 100.
static int noise_square(const struct noise *n, int x, int y, int fine_shift) {
    int a = noise_at(n, x << 4, y << 4), b = noise_at(n, x << fine_shift, y << fine_shift);
    return game_range(7 * (5 * a + 2 * b) >> 8, 0, 100);
}

// The game's quick_root(): an integer square root by Newton's method.
static int quick_root(int n) {
    if (n <= 1) return n;
    int c = n >> 1, s;
    do {
        s = c;
        c = (n / c + c) >> 1;
    } while (c < s);
    return s;
}

// Squares around (x, y) with an altitude at most 2 (sea), of the eight.
static int low_neighbours(int x, int y) {
    int n = 0;
    for (int i = 0; i < 8; i++) {
        int xi = xrange(x + game_radius_near_x[i]), yi = y + game_radius_near_y[i];
        if (on_map(xi, yi) && (tile(xi, yi)->climate & 0xe0) <= 0x40) n++;
    }
    return n;
}

// Gives each square a contour from noise, the land around it and the
// erosive forces setting: first the seas, then (after the shorelines are
// redrawn) the land. Shallows between two large land regions, and shore
// land next to another region, count as more enclosed.
void world_erosion(void) {
    struct noise noise;
    noise_init(&noise);
    int scale = 1, scale_base = 1;  // land squares a map has, against a standard 3200
    if (*game_land_coverage == 1) scale_base = quick_root(3200), scale = quick_root(*game_map_area);
    const struct rules_worldbuilder *wb = game_worldbuilder;
    int erosion = *game_erosive_forces;
    for (int pass = 0; pass < 2; pass++) {
        if (pass) world_shorelines();
        game_path_continents(game_path);
        for (int y = 0; y < *game_map_height; y++) {
            for (int x = y & 1; x < *game_map_width; x += 2) {
                const struct map_tile *t = tile(x, y);
                int alt = alt_of(t), contour = t->contour;
                int n = noise_square(&noise, x, y, 7);
                if ((alt <= 2) != !pass) continue;
                int low = 0, crowded = 0;
                if (alt == 2) {
                    low = low_neighbours(x, y);
                    for (int i = 0, best = -1; i < 8; i++) {
                        int xi = xrange(x + game_radius_near_x[i]), yi = y + game_radius_near_y[i];
                        if (!on_map(xi, yi) || (tile(xi, yi)->climate & 0xe0) <= 0x40) continue;
                        int region = tile(xi, yi)->region;
                        if (best < 0) {
                            best = region;
                        } else if (best != region) {
                            int here = game_continents[region].tile_count, there = game_continents[best].tile_count;
                            if (here + there >= *game_map_area * 50 / 3200) crowded = 1;
                            if (here > there) best = region;
                        }
                    }
                } else if (alt == 3) {
                    low = low_neighbours(x, y);
                    for (int i = 9; i < 25; i++) {
                        int xi = xrange(x + game_radius_x[i]), yi = y + game_radius_y[i];
                        if (!on_map(xi, yi)) continue;
                        const struct map_tile *s = tile(xi, yi);
                        if ((s->climate & 0xe0) > 0x40 && s->region < 64 && s->region != t->region) crowded++;
                    }
                }
                int v = n;
                if (alt <= 1) {
                    v = n - wb->deep_water;
                } else if (alt == 2) {
                    if (crowded) low = game_range(low + 4, 0, 8);
                    if (n < contour) v = wb->shelf + n;
                    if (v >= game_alt_natural[alt + 1]) v -= (low - 3) * wb->shelf / 4;
                } else if (alt == 3) {
                    if (crowded) low = game_range(low + 4, 0, 8);
                    int open = 8 - low, ocean = game_range(*game_ocean_coverage, 1, 2);
                    v = n + open * wb->plains * scale / (scale_base * 8);
                    if (v < contour)
                        v += (wb->hills * erosion / 2 * open / (ocean * 8) + wb->beach) * scale / scale_base;
                    else
                        v = (v - contour) * (8 - game_range(low, 0, 8)) * erosion / (ocean * 8) + contour;
                } else if (alt == 4) {
                    if (n > contour) v = 2 * n - contour;
                    v += wb->hills * erosion + wb->peaks;
                } else {
                    v = wb->hills * erosion + n;
                }
                v = (v + contour + 1) / 2;
                if (!erosion && v > 74) v = 74;
                alt_put_detail(x, y, v);
            }
            game_do_all_non_input();
        }
    }
}

// Rockiness and the resource kinds: each square's rockiness from its
// minerals; then sea squares and half the jungle get the energy kind, high
// or rainy land the mineral kind. Landmarks that set their own are left.
void world_rocky(void) {
    for (int y = 0; y < *game_map_height; y++) {
        for (int x = y & 1; x < *game_map_width; x += 2) {
            uint32_t bits2 = tile(x, y)->bits2, code = bits2 & MAP2_LANDMARK_CODE;
            if ((bits2 & (MAP2_UNK_80000000 | MAP2_CRATER)) == MAP2_CRATER && code < 21u << 24) continue;
            if ((bits2 & (MAP2_UNK_80000000 | MAP2_LANDMARK(LM_UNITY))) == MAP2_LANDMARK(LM_UNITY)) {
                int c = (int)(code >> 24);
                if (c < 9 || c == 12 || c == 10 || c == 19 || c == 15 || c == 20 || c == 16) continue;
            }
            rocky_set(x, y, minerals_at(x, y));
            const struct map_tile *t = tile(x, y);
            int alt = t->climate & 0xe0;
            if ((t->bits2 & MAP2_LANDMARK(LM_FRESH)) && alt < 0x60) continue;
            uint32_t kind;
            if (alt < 0x60 || ((t->bits2 & (MAP2_UNK_80000000 | MAP2_LANDMARK(LM_JUNGLE))) == MAP2_LANDMARK(LM_JUNGLE) &&
                               game_rand() % 2 == 0)) {
                kind = MAP_ENERGY_RESOURCE;
            } else {
                uint8_t c = tile(x, y)->climate;
                if ((c & 0xe0) <= 0x80 && (c & 0x18) <= 8) continue;
                kind = MAP_MINERAL_RESOURCE;
            }
            bit_set(x, y, kind, 1);
        }
        game_do_all_non_input();
    }
}

// Fungus in bands of the noise, wider with more native life, and on half the
// polar squares; not on rocky land or the volcano's crater. In Alien
// Crossfire one land square in 40 of it gets a fungal tower.
void world_fungus(void) {
    struct noise noise;
    noise_init(&noise);
    int more = (*game_native_life - 1) * game_worldbuilder->fungus;
    for (int y = 0; y < *game_map_height; y++) {
        for (int x = y & 1; x < *game_map_width; x += 2) {
            int n = noise_square(&noise, x, y, 8);
            struct map_tile *t = tile(x, y);
            if ((t->bits2 & (MAP2_UNK_80000000 | MAP2_LANDMARK(LM_VOLCANO))) == MAP2_LANDMARK(LM_VOLCANO) &&
                (t->bits2 & MAP2_LANDMARK_CODE) < 9u << 24)
                continue;
            int band = 0;
            for (int b = 20; b <= 60 && !band; b += 20) band = n >= b - more && n <= 2 * more + b + 5;
            if (!band && ((y && y != *game_map_height - 1) || game_rand() % 2 == 0)) continue;
            if ((t->rocky_lock & 0xc0) >= 0x80 && (t->climate & 0xe0) >= 0x60) continue;
            t->bits |= MAP_FUNGUS;
            t->bits &= ~*game_fungus_clears;
            if (game_rand() % 40 == 0 && (t->climate & 0xe0) >= 0x60 && *game_expansion)
                game_veh_init(UNIT_FUNGAL_TOWER, 0, x, y);
        }
        game_do_all_non_input();
    }
}

// Lays a river from each riverbed down the land, a diagonal step at a time:
// each step picks at random among the neighbours no higher, weighted to the
// steepest and to keeping on (wandering a little), avoiding other rivers'
// banks; it may climb a little where nothing is lower, cutting the land as
// it goes. A river ends at the sea, another river, a thermal borehole, or
// nowhere to go (a lake shows there), or after 1000 steps.
void world_rivers(void) {
    struct map_tile *m = *game_map;
    for (int i = 0; i < *game_map_area; i++) m[i].bits &= ~(MAP_RIVER | MAP_RIVER_LAKE);
    for (int y = 0; y < *game_map_height; y++) {
        for (int x = y & 1; x < *game_map_width; x += 2) {
            struct map_tile *t = tile(x, y);
            if (!(t->bits & MAP_RIVERBED) || (t->climate & 0xe0) < 0x60) continue;
            for (int i = 0; i < *game_map_area; i++) m[i].bits2 &= ~MAP2_RIVER_PATH;
            game_random_reseed(y * 47 + x * 31);
            int dir = game_random(0, 4);
            int cx = x, cy = y, alt = alt_of(t), contour = t->contour;
            t->bits2 |= MAP2_RIVER_PATH;
            int steps = 0, joining = 0, lake = 1, step = 1, level_run = 1;
            if (t->bits & MAP_THERMAL_BORE) lake = 0;
            else for (;;) {
                int r = game_random(0, 2);
                int wander = (r + dir - (step & 1)) & 3, back = dir ^ 2, n = 0, total = 0;
                int lowest_rise = 9999, climb = 8, weight[4], join[4], to[4];
                for (int d = 0; d < 4; d++) {
                    if (d == back) continue;
                    int nx = xrange(cx + game_diag_x[d]), ny = cy + game_diag_y[d];
                    if (!on_map(nx, ny)) continue;
                    const struct map_tile *s = tile(nx, ny);
                    if (s->bits & MAP_RIVER) goto ended;
                    if (s->bits & MAP_RIVERBED) {
                        int beds = 0;
                        for (int e = 0; e < 4; e++) {
                            int ex = xrange(nx + game_diag_x[e]), ey = ny + game_diag_y[e];
                            if (on_map(ex, ey) && (tile(ex, ey)->bits & MAP_RIVERBED)) beds++;
                        }
                        if (beds > 1) goto ended;
                    }
                    int s_climate = s->climate;
                    if (s_climate >> 5 > alt) continue;
                    int s_contour = s->contour;
                    int w = game_range(contour - s_contour + 2, 1, 9999) << 5;
                    if (d == wander) w += 31;
                    if (s_contour > contour) {
                        if (s_contour > lowest_rise) continue;
                        lowest_rise = s_contour;
                        w = climb++;
                    }
                    int higher = 0, joins = 0, blocked = 0;
                    for (int e = 0; e < 4 && !blocked; e++) {
                        if (e == (d ^ 2)) continue;
                        int ex = xrange(nx + game_diag_x[e]), ey = ny + game_diag_y[e];
                        if (!on_map(ex, ey)) continue;
                        const struct map_tile *q = tile(ex, ey);
                        int q_alt = q->climate & 0xe0;
                        if (q_alt < 0x60 && (s_climate & 0xe0) >= 0x60 && level_run < 5) w /= 2;
                        if (q->contour > s_contour) {
                            higher++;
                            if (q->bits & MAP_RIVER) blocked = 1;
                        }
                        if (!blocked && (q->bits2 & MAP2_RIVER_PATH)) {
                            if (joining || q_alt < 0x60) blocked = 1;
                            else w /= 8, joins = 1;
                        }
                    }
                    if (blocked || !w) continue;
                    if (higher == 3) w = 1;
                    total += w;
                    weight[n] = w, join[n] = joins, to[n] = d, n++;
                }
                if (!n) break;
                int pick = game_random(0, total);
                dir = 0;
                for (int i = 0; i < n; i++) {
                    if (pick < weight[i]) {
                        joining = join[i], dir = to[i];
                        break;
                    }
                    pick -= weight[i];
                }
                cx = xrange(cx + game_diag_x[dir]), cy += game_diag_y[dir];
                t = tile(cx, cy);
                if (alt_of(t) != alt) level_run = 0;
                alt = alt_of(t);
                int c = t->contour;
                if (c > contour) {
                    c = contour - 1 > game_elev_detail[alt] ? contour - 1 : game_elev_detail[alt];
                    alt_put_detail(cx, cy, c);
                }
                contour = c;
                if (++steps >= 1000) break;
                t->bits2 |= MAP2_RIVER_PATH;
                step++, level_run++;
                if (t->bits & MAP_THERMAL_BORE) {
                    lake = 0;
                    break;
                }
                continue;
            ended:
                lake = 0;
                break;
            }
            if (alt >= ALT_SHORE_LINE) {
                struct map_tile *end = tile(cx, cy);
                if (!(end->bits & MAP_RIVER) && lake &&
                    (!(end->bits & (MAP_BASE | MAP_UNIT)) || (end->owner_site & 0xf) >= 8))
                    end->bits |= MAP_RIVER_LAKE;
            }
            for (int i = 0; i < *game_map_area; i++)
                if (m[i].bits2 & MAP2_RIVER_PATH) m[i].bits |= MAP_RIVER;
        }
        game_do_all_non_input();
    }
    for (int i = 0; i < *game_map_area; i++) m[i].bits2 &= ~MAP2_RIVER_PATH;
}

// Redraws the map's height: each square's altitude from its contour, then,
// from the peaks down, each square's slope around it made smooth. While a
// new map is shown growing, land gets its temperature as it appears.
void world_shorelines(void) {
    for (int y = 0; y < *game_map_height; y++) {
        for (int x = y & 1; x < *game_map_width; x += 2) {
            int c = tile(x, y)->contour - *game_sea_level, alt = 0;
            for (int i = 6; i >= 0; i--)
                if (c >= game_alt_natural[i]) {
                    alt = i;
                    break;
                }
            alt_set(x, y, alt);
            if ((*game_state & GAME_STATE_OMNISCIENT) && *game_world_add_temperature) {
                if (alt >= ALT_SHORE_LINE) temp_set(x, y, game_range(alt - 3, 0, 2));
                game_draw_tile(x, y, 2);
            }
        }
        game_do_all_non_input();
    }
    for (int alt = 6; alt >= 2; alt--)
        for (int y = 0; y < *game_map_height; y++) {
            for (int x = y & 1; x < *game_map_width; x += 2)
                if (alt_of(tile(x, y)) == alt) world_alt_set(x, y, alt, 1);
            game_do_all_non_input();
        }
    if ((*game_state & GAME_STATE_OMNISCIENT) && *game_world_add_temperature) {
        game_map_win_clear_terrain(game_map_wins[0]);
        game_draw_map(1);
    }
}

// Rainfall, from the winds: squares learn from the land and sea to their
// west and east (in the unk_6 byte, cleared first) whether moist air
// reaches them, is wrung out by hills, or is helped by forest and
// condensers; then one pass along each row carries moisture east, and a
// last pass wets arid squares beside wet land. During a game, a base near
// a square whose rainfall changed is noted.
enum {  // map_tile.unk_6 while rainfall is worked out
    RAIN_CONDENSER = 0x1, RAIN_SHADOWED = 0x2, RAIN_CONDENSER2 = 0x4, RAIN_LEE = 0x10, RAIN_WINDWARD = 0x20,
    RAIN_FOREST = 0x40, RAIN_SEA_WIND = 0x80,
};

static struct map_tile *rain_square(int x, int y) {
    x = xrange(x);
    return on_map(x, y) ? tile(x, y) : NULL;
}

void world_rainfall(void) {
    int player = *game_current_faction, w = *game_map_width, h = *game_map_height;
    *game_rain_base = *game_rain_faction = -1;
    struct map_tile *m = *game_map;
    for (int i = 0; i < *game_map_area; i++) m[i].unk_6 = 0;
    const struct rules_worldbuilder *wb = game_worldbuilder;
    for (int y = 0; y < h; y++) {
        for (int x = y & 1; x < w; x += 2) {
            const struct map_tile *t = tile(x, y);
            int cloud = *game_cloud_cover;
            if (t->bits & MAP_CONDENSER)
                for (int i = 0; i < 9; i++) {
                    struct map_tile *s = rain_square(x + game_radius_x[i], y + game_radius_y[i]);
                    if (s) s->unk_6 |= s->unk_6 & RAIN_CONDENSER ? RAIN_CONDENSER2 : RAIN_CONDENSER;
                }
            if ((t->climate & 0xe0) < 0x60) {  // sea: wind off it
                for (int k = 0; k < 3; k++) {
                    struct map_tile *a = rain_square(x - 2 * k - 2, y), *b = rain_square(x - 2 * k, y);
                    if (a && b && alt_of(a) >= alt_of(b) && k <= cloud) a->unk_6 &= ~RAIN_LEE;
                }
                struct map_tile *s = rain_square(x + 2, y);
                if (s) s->unk_6 |= RAIN_SEA_WIND;
                if (cloud > 0 && (s = rain_square(x + 4, y)) && ((s->climate & 7) > 1 || cloud > 1)) {
                    s->unk_6 |= RAIN_SEA_WIND;
                    if (cloud > 1 && (s = rain_square(x + 6, y)) && (s->climate & 7)) s->unk_6 |= RAIN_SEA_WIND;
                }
            }
            int alt = alt_of(t);
            if (alt > 4) {  // hills and peaks: wet to the west, dry to the east
                int mass = alt > 5 ? wb->cloudmass_peaks : wb->cloudmass_hills;
                for (int k = -1, x2 = x - 2; k >= -((cloud - 1) * wb->rainfall_coeff + mass); k--, x2 -= 2) {
                    struct map_tile *s = rain_square(x2, y);
                    if (!s) continue;
                    if ((s->climate & 0xe0) < 0x60 || alt_of(s) >= alt) break;
                    s->unk_6 |= RAIN_WINDWARD;
                }
                for (int k = 1, x2 = x + 2; k <= mass - (cloud - 1) * wb->rainfall_coeff; k++, x2 += 2) {
                    struct map_tile *s = rain_square(x2, y);
                    if (!s) continue;
                    if ((s->climate & 0xe0) < 0x60 || alt_of(s) >= alt) break;
                    s->unk_6 |= RAIN_LEE;
                }
            } else if (alt >= ALT_SHORE_LINE && (t->bits & MAP_FOREST)) {
                // (Squares x + 1 a row up, level and down: the level one is
                // stored with its neighbour, as the original indexes it.)
                for (int y2 = y - 1; y2 <= y + 1; y2++) {
                    struct map_tile *s = rain_square(x + 1, y2);
                    if (s) s->unk_6 |= RAIN_FOREST;
                }
                struct map_tile *s = rain_square(x + 2, y);
                if (s) s->unk_6 |= RAIN_FOREST;
            }
        }
        game_do_all_non_input();
    }
    // Along each row, west to east, carrying moisture.
    int moist = 0, dry_run = 0, flat_run = 0;
    struct map_tile *p = *game_map;
    for (int y = 0; y < h; y++) {
        for (int x = y & 1; x < w; x += 2, p++) {
            if (y <= 0 || y >= h - 1) {
                p->climate &= 0xe7;
                continue;
            }
            const struct map_tile *t = tile(x, y);
            uint32_t bits2 = t->bits2;
            int climate = t->climate, temp = climate & 7, alt = climate >> 5, cloud = *game_cloud_cover;
            int base_rain = 0, bonus = 0;
            uint8_t u = p->unk_6;
            if ((bits2 & (MAP2_UNK_80000000 | MAP2_LANDMARK(LM_DUNES))) == MAP2_LANDMARK(LM_DUNES) ||
                (bits2 & (MAP2_UNK_80000000 | MAP2_LANDMARK(LM_MESA))) == MAP2_LANDMARK(LM_MESA))
                moist = 1;
            if (u & RAIN_WINDWARD) base_rain = 1, flat_run = 0, moist = 1;
            if (u & RAIN_SEA_WIND) {
                moist = temp > 1, dry_run = 0, flat_run = 0;
                if (cloud >= 1) bonus = 1;
            }
            if (u & RAIN_LEE) {
                if (moist) moist--;
                bonus--;
            }
            if ((u & RAIN_FOREST) && alt <= 4) {
                dry_run = 0, flat_run = 0;
                if (bonus < 0) bonus = 0;
            }
            int decay = 0;
            if (alt > 3) {
                if (++dry_run > wb->rainfall_coeff * cloud * 4 + 1 && moist) moist--;
            } else if (alt < 3) {
                flat_run = 0, moist = 1, decay = 1;
            } else if (!moist || dry_run) {
                decay = 1;
            } else if (++flat_run > cloud * 4 + 2) {
                moist = 0, flat_run = 0;
            }
            if (decay && dry_run && (dry_run += -1 - cloud) < 1) {
                dry_run = 0, flat_run = 0;
                if (cloud || alt < 3) moist = 1;
            }
            if (t->bits & MAP_RIVER) {
                if ((x >> 1) % 3 && bonus < 0) bonus = 0;
                moist = 1, flat_run = 0;
            }
            int rain = base_rain;
            if (u & RAIN_SHADOWED) rain--, moist = 0;
            else if (moist) bonus += moist;
            if (alt <= 4 + (temp != 0)) rain += bonus;
            else if (cloud - alt + temp + 5 >= 2 && bonus > 0) rain++;
            if ((bits2 & (MAP2_UNK_80000000 | MAP2_LANDMARK(LM_JUNGLE))) == MAP2_LANDMARK(LM_JUNGLE) &&
                (climate & 0xe0) >= 0x60 && alt < 6) {
                int code = (int32_t)bits2 >> 24;
                if (code < 0) code = -code;
                rain += code < 0x31 || (code < 0x51 && alt < 5) ? 2 : 1;
            }
            if ((bits2 & (MAP2_UNK_80000000 | MAP2_LANDMARK(LM_DUNES))) == MAP2_LANDMARK(LM_DUNES)) rain = 0;
            if ((bits2 & (MAP2_UNK_80000000 | MAP2_LANDMARK(LM_UNITY))) == MAP2_LANDMARK(LM_UNITY)) rain = 0;
            else rain = game_range(rain, 0, 2);
            if (u & RAIN_CONDENSER) {
                moist = 1;
                if (rain < 2) rain++;
            }
            if (u & RAIN_CONDENSER2) {
                moist = 1;
                if (rain < 2) rain++;
            }
            if (*game_turn) {
                int old = p->climate >> 3 & 3;
                if (old != rain && *game_rain_base < 0) {
                    if (old > rain &&
                        (*game_rain_faction < 0 || !(1u << *game_rain_faction & *game_human_factions))) {
                        int b = game_base_find(x, y);
                        if (b >= 0) *game_rain_faction = game_bases[b].faction;
                    }
                    int b = game_base_find2(x, y, player);
                    if (b >= 0 && *game_base_find_dist <= 2) *game_rain_base = b;
                }
            }
            p->climate = (uint8_t)((p->climate & 0xe7) | rain << 3);
        }
        game_do_all_non_input();
    }
    // Arid squares beside rainy land get a little.
    for (int y = 0; y < h; y++) {
        for (int x = y & 1; x < w; x += 2) {
            if (tile(x, y)->climate & 0x18) continue;
            for (int i = 0; i < 8; i++) {
                const struct map_tile *s = rain_square(x + game_radius_near_x[i], y + game_radius_near_y[i]);
                if (s && (s->climate & 0xe0) >= 0x60 && (s->climate & 0x18) == 0x10) {
                    climate_set(x, y, 1);
                    break;
                }
            }
        }
        game_do_all_non_input();
    }
}

// Every map window and the world view redrawn from the map.
static void redraw_terrain(void) {
    for (int i = 0; i < 8; i++)
        if (*(int32_t *)((uint8_t *)game_map_wins[i] + 0x1dd74)) game_map_win_clear_terrain(game_map_wins[i]);
    game_world_win_clear_terrain(game_world_win);
}

// The map's height, temperature, rivers, rainfall and regions worked out
// again from its contours, as after the sea level changes. Reseeds the
// game's generator from the clock.
void world_climate(void) {
    game_do_checksums(2);
    game_log_say(game_text_climate_begin, 0, 0, 0);
    world_shorelines();
    world_temperature();
    world_rivers();
    world_rainfall();
    game_path_continents(game_path);
    world_analysis();
    if (!*game_world_skip_territory) game_reset_territory();
    game_random_reseed((int)game_time_get_time());
    if (!*game_halted) redraw_terrain();
    game_log_say(game_text_climate_end, 0, 0, 0);
    game_do_checksums(3);
}

// ---- Landmarks -------------------------------------------------------------------

// Each landmark takes a square, or finds one when given a square off the
// map, then shapes the land around it, marks its squares (MAP2_LANDMARK and
// each square's code, its place in the radius table) and names it. Several
// put the name two rows below the centre.

static const char *landmark_name(int id) { return game_strings_get(game_strings, game_natural[id].name); }

// Square i of the radius table around (x, y), and whether it is on the map.
static int radius_square(int x, int y, int i, int *xi, int *yi) {
    *xi = xrange(x + game_radius_x[i]);
    *yi = y + game_radius_y[i];
    return on_map(*xi, *yi);
}

static inline int iabs(int v) { return v < 0 ? -v : v; }

// A random site, away from the poles, on land or at sea as asked, with no
// landmark within 8 squares. 0 after 1000 tries.
static int landmark_site(int *x, int *y, int sea) {
    for (int tries = 0;;) {
        *y = game_rnd(*game_map_height - 16) + 8;
        int xs = game_rnd(*game_map_width);
        *x = ((xs ^ *y) & 1) ^ xs;
        if (++tries >= 1000) return 0;
        if (is_ocean(*x, *y) == sea && !near_landmark(*x, *y)) return 1;
    }
}

// Marks a landmark square.
static void landmark_mark(int x, int y, int id, int code) {
    bit2_set(x, y, MAP2_LANDMARK(id), 1);
    code_set(x, y, code);
}

// Garland Crater: a deep crater in a ring of hills, rolling and of one
// temperature.
void world_crater(int x, int y) {
    if (!on_map(x, y) && !landmark_site(&x, &y, 0)) return;
    static const int8_t shape[] = {0, 6, 3, 2, 3};
    for (int i = 0; i < 5; i++) world_alt_set(x, y, shape[i], 1);
    int temperature = tile(x, y)->climate & 7;
    for (int i = 0; i < 49; i++) {
        int xi, yi;
        if (!radius_square(x, y, i, &xi, &yi)) continue;
        bit_set(xi, yi, MAP_RIVERBED, 0);
        landmark_mark(xi, yi, LM_CRATER, i);
        if (i < 21) {
            rocky_set(xi, yi, 1);
            temp_set(xi, yi, temperature);
        }
    }
    new_landmark(x, y, landmark_name(LM_CRATER));
}

// Monsoon Jungle: a rainy coast near the equator, its sea within two
// squares filled in. (OpenSMACX marks it with the landmark's number, not
// its bit.)
void world_monsoon(int x, int y) {
    world_rainfall();
    if (!on_map(x, y)) {
        for (int tries = 0;;) {
            y = *game_map_height / 2 + game_rand() % 4 - 2;
            int xs = game_rnd(*game_map_width);
            x = ((xs ^ y) & 1) ^ xs;
            int land = 0;
            for (int i = 0; i < 121; i++) {
                int xi, yi;
                if (radius_square(x, y, i, &xi, &yi) && !is_ocean(xi, yi)) land++;
            }
            if (++tries >= 1000) return;
            if (is_ocean(x, y) || !is_coast(x, y, 1) || land < 40 || (tile(x, y)->climate & 0x18) <= 8) continue;
            if (!near_landmark(x, y)) break;
        }
    }
    for (int i = 0; i < 121; i++) {
        int xi, yi, on = radius_square(x, y, i, &xi, &yi);
        if (iabs(yi - y) > 8 || !on) continue;
        if (i < 21 && is_ocean(xi, yi)) world_alt_set(xi, yi, ALT_SHORE_LINE, 1);
        landmark_mark(xi, yi, LM_JUNGLE, i);
    }
    new_landmark(x, y, landmark_name(LM_JUNGLE));
}

// New Sargasso: shallow sea thick with fungus, with supply pods at its heart.
void world_sargasso(int x, int y) {
    if (!on_map(x, y) && !landmark_site(&x, &y, 1)) return;
    int skipped = 0;
    for (int i = 0; i < 81; i++) {
        int xi, yi;
        if (!radius_square(x, y, i, &xi, &yi) || !(is_ocean(xi, yi) || i < 21)) continue;
        world_alt_set(xi, yi, 2, 1);
        landmark_mark(xi, yi, LM_SARGASSO, i);
        if (i <= 4) bit_set(xi, yi, MAP_SUPPLY_POD, 1);
        if ((skipped || i < 25 || game_rand() % 24) && (i < 49 || game_rand() % 3)) bit_set(xi, yi, MAP_FUNGUS, 1);
        else skipped = 1;
    }
    new_landmark(x, y, landmark_name(LM_SARGASSO));
}

// Ruins: flat land under fungus with monoliths around the centre.
void world_ruin(int x, int y) {
    if (!on_map(x, y) && !landmark_site(&x, &y, 0)) return;
    for (int i = 0; i < 25; i++) {
        int xi, yi;
        if (!radius_square(x, y, i, &xi, &yi)) continue;
        world_alt_set(xi, yi, ALT_SHORE_LINE, 1);
        if (!is_ocean(xi, yi)) {
            bit_set(xi, yi, MAP_FUNGUS | MAP_POD_TAKEN, 1);
            if (!i) bit_set(xi, yi, MAP_FUNGUS, 0);
            else if (i < 9) bit_set(xi, yi, MAP_MONOLITH, 1);
        } else if (i && i < 9) {
            bit_set(xi, yi, MAP_MONOLITH, 1);
        }
        landmark_mark(xi, yi, LM_RUINS, i);
    }
    new_landmark(x, y + 2, landmark_name(LM_RUINS));
}

// Great Dunes: flat arid land in the middle latitudes, with two low ridges.
void world_dune(int x, int y) {
    world_rainfall();
    if (!on_map(x, y)) {
        int h = *game_map_height;
        for (int tries = 0;;) {
            int r = game_rnd(h / 2);
            y = h / 2 - h / 4 + r;
            int xs = game_rnd(*game_map_width);
            x = ((xs ^ y) & 1) ^ xs;
            if (++tries >= 1000) return;
            if (is_ocean(x, y) || (tile(x, y)->climate & 0x18)) continue;
            if (!near_landmark(x, y)) break;
        }
    }
    for (int i = 0; i < 25; i++) {
        int xi, yi;
        if (!radius_square(x, y, i, &xi, &yi)) continue;
        if (is_ocean(xi, yi) && i >= 9 && !(game_rand() % 3)) continue;
        world_alt_set(xi, yi, i == 2 || i == 7 ? 4 : ALT_SHORE_LINE, 1);
        bit_set(xi, yi, MAP_FUNGUS, 0);
        bit2_set(xi, yi, MAP2_LANDMARK(LM_DUNES), 1);
        rocky_set(xi, yi, 0);
        code_set(xi, yi, i);
    }
    new_landmark(x, y + 2, landmark_name(LM_DUNES));
}

// Uranium Flats: flat land without fungus.
void world_diamond(int x, int y) {
    if (!on_map(x, y) && !landmark_site(&x, &y, 0)) return;
    for (int i = 0; i < 21; i++) {
        int xi, yi;
        if (!radius_square(x, y, i, &xi, &yi)) continue;
        if (i >= 9 && is_ocean(xi, yi) && !(game_rand() % 3)) continue;
        world_alt_set(xi, yi, ALT_SHORE_LINE, 1);
        bit2_set(xi, yi, MAP2_LANDMARK(LM_URANIUM), 1);
        rocky_set(xi, yi, 0);
        bit_set(xi, yi, MAP_FUNGUS, 0);
        code_set(xi, yi, i);
    }
    new_landmark(x, y, landmark_name(LM_URANIUM));
}

// Freshwater Sea: the sea region given, or the largest of 3 to 32 squares,
// all marked; named in the middle of the first row it spans, counting from
// the south.
void world_fresh(int x, int y) {
    int region;
    if (on_map(x, y)) {
        region = region_at(x, y);
        if (!is_ocean(x, y)) return;
    } else {
        int best = -1, best_count = 0;
        for (int r = LAND_REGION_COUNT; r < REGION_COUNT; r++) {
            int n = game_continents[r].tile_count;
            if (n >= 3 && n <= 32 && n >= best_count) best_count = n, best = r;
        }
        if (best < 0) return;
        region = best;
    }
    int first_x = -1, named = 0;
    for (int yi = *game_map_height - 1; yi >= 0; yi--)
        for (int xi = yi & 1; xi < *game_map_width; xi += 2) {
            if (region_at(xi, yi) == region) {
                bit2_set(xi, yi, MAP2_LANDMARK(LM_FRESH), 1);
                if (first_x < 0) first_x = xi;
            } else if (!named && first_x >= 0) {
                int xf = (first_x + xi - 2) / 2;
                new_landmark(((xf ^ yi) & 1) ^ xf, yi, landmark_name(LM_FRESH));
                named = 1;
            }
        }
}

// Mount Planet: a rocky peak with a trench under it, cleared of
// improvements and pods. Also used without a name.
void world_volcano(int x, int y, int no_name) {
    if (!on_map(x, y) && !landmark_site(&x, &y, 0)) return;
    world_alt_set(x, y, 0, 1);
    world_alt_set(x, y, 6, 1);
    for (int i = 0; i < 9; i++) {
        int xi, yi;
        if (!radius_square(x, y, i, &xi, &yi)) continue;
        landmark_mark(xi, yi, LM_VOLCANO, i);
        uint32_t bits = bit_at(xi, yi) & 0xee377e83;
        if (!i) bits &= ~MAP_UNITY_POD;
        bit_put(xi, yi, bits | MAP_POD_TAKEN);
        rocky_set(xi, yi, 2);
    }
    *game_mount_planet_x = x;
    *game_mount_planet_y = y;
    if (!no_name) new_landmark(x, y + 2, landmark_name(LM_VOLCANO));
}

// The Ruins of Borehole Cluster: three thermal boreholes among the square
// and its neighbours, chosen by the clock, with land around each.
void world_borehole(int x, int y) {
    if (!on_map(x, y) && !landmark_site(&x, &y, 0)) return;
    uint32_t t = game_time_get_time();
    int s = t & 7, c = ((int32_t)t >> 3) & 3;
    int bore[3] = {8, s / 3 + 5, 3 - s % 3};
    if (c & 2) {
        bore[1]--, bore[2]--;
        if (c & 1) bore[1]++, bore[0] = 8, bore[2]++;
        else bore[1]--, bore[0] = 6, bore[2]--;
        for (int k = 0; k < 3; k++) bore[k] = (bore[k] + 8) % 8 + 1;
    }
    for (int i = 0; i < 9; i++) {
        int xi, yi;
        if (!radius_square(x, y, i, &xi, &yi)) continue;
        world_alt_set(xi, yi, ALT_SHORE_LINE, 1);
        bit_set(xi, yi, MAP_POD_TAKEN, 1);
        if (i == bore[0] || i == bore[1] || i == bore[2]) {
            bit_set(xi, yi, MAP_THERMAL_BORE, 1);
            landmark_mark(xi, yi, LM_BOREHOLE, i);
        }
    }
    for (int k = 0; k < 3; k++) {
        int cx = xrange(x + game_radius_x[bore[k]]), cy = y + game_radius_y[bore[k]];
        for (int j = 1; j < 9; j++) {
            int xn, yn;
            if (radius_square(cx, cy, j, &xn, &yn) && is_ocean(xn, yn)) world_alt_set(xn, yn, ALT_SHORE_LINE, 1);
        }
    }
    bit2_set(x, y, MAP2_LANDMARK(LM_BOREHOLE), 1);
    new_landmark(x, y, landmark_name(LM_BOREHOLE));
}

// The Manifold Nexus.
void world_temple(int x, int y) {
    if (!on_map(x, y) && !landmark_site(&x, &y, 0)) return;
    for (int i = 0; i < 9; i++) {
        int xi, yi;
        if (!radius_square(x, y, i, &xi, &yi)) continue;
        world_alt_set(xi, yi, ALT_SHORE_LINE, 1);
        bit_set(xi, yi, MAP_POD_TAKEN, 1);
        landmark_mark(xi, yi, LM_NEXUS, i);
    }
    new_landmark(x, y, landmark_name(LM_NEXUS));
}

// The Unity wreckage (Alien Crossfire only): raised land around two squares
// diagonally either side of the site, then flat clear ground with supply
// pods at four places.
void world_unity(int x, int y) {
    if (!*game_expansion) return;
    if (!on_map(x, y) && !landmark_site(&x, &y, 0)) return;
    for (int pass = 0; pass < 2; pass++) {
        int cx = pass ? x + 1 : x - 1, cy = pass ? y + 1 : y - 1;
        for (int i = 0; i < 25; i++) {
            int xi, yi;
            if (radius_square(cx, cy, i, &xi, &yi)) world_alt_set(xi, yi, 4, 1);
        }
    }
    for (int i = 0; i < 25; i++) {
        int xi, yi;
        if (!radius_square(x, y, i, &xi, &yi)) continue;
        rocky_set(xi, yi, 0);
        bit_set(xi, yi, MAP_RIVERBED, 0);
        bit_set(xi, yi, MAP_FUNGUS, 0);
        landmark_mark(xi, yi, LM_UNITY, i);
        bit_set(xi, yi, i == 0 || i == 8 || i == 10 || i == 19 ? MAP_SUPPLY_POD : MAP_POD_TAKEN, 1);
    }
    new_landmark(x, y, landmark_name(LM_UNITY));
}

// Fossil Field Ridge: sea squares raised to a reef.
void world_fossil(int x, int y) {
    if (!on_map(x, y) && !landmark_site(&x, &y, 1)) return;
    for (int i = 0; i < 6; i++) {
        int xi, yi;
        if (!radius_square(x, y, i, &xi, &yi) || !is_ocean(xi, yi)) continue;
        world_alt_set(xi, yi, 1, 1);
        landmark_mark(xi, yi, LM_FOSSIL, i);
    }
    new_landmark(x, y, landmark_name(LM_FOSSIL));
}

// Squares from a table of radius-table indices: the land is shaped for all
// of them first, then they are marked, as in the original.
static void shaped_landmark(int x, int y, const int32_t *squares, int n, const int8_t *alts, int n_alts, int id) {
    for (int k = 0; k < n; k++) {
        int xi, yi;
        if (!radius_square(x, y, squares[k], &xi, &yi)) continue;
        for (int a = 0; a < n_alts; a++) world_alt_set(xi, yi, alts[a], 1);
    }
    for (int k = 0; k < n; k++) {
        int xi, yi;
        if (radius_square(x, y, squares[k], &xi, &yi)) landmark_mark(xi, yi, id, squares[k]);
    }
    new_landmark(x, y + 2, landmark_name(id));
}

// Nessus Canyon: a winding valley. (The game never places it itself.)
void world_canyon(int x, int y) {
    if (!on_map(x, y) && !landmark_site(&x, &y, 0)) return;
    static const int8_t alts[] = {4, ALT_SHORE_LINE};
    shaped_landmark(x, y, game_canyon_squares, 12, alts, 2, LM_CANYON);
}

// Mount Planet's neighbour mesa: a plateau with a shore-level rim.
void world_mesa(int x, int y) {
    if (!on_map(x, y) && !landmark_site(&x, &y, 0)) return;
    for (int i = 0; i < 49; i++) {
        int xi, yi;
        if (!radius_square(x, y, i, &xi, &yi)) continue;
        world_alt_set(xi, yi, (i < 25) + ALT_SHORE_LINE, 1);
        landmark_mark(xi, yi, LM_MESA, i);
    }
    new_landmark(x, y + 2, landmark_name(LM_MESA));
}

// Sunny Mesa's ridge: a line of hills.
void world_ridge(int x, int y) {
    if (!on_map(x, y) && !landmark_site(&x, &y, 0)) return;
    static const int8_t alts[] = {4};
    shaped_landmark(x, y, game_ridge_squares, 13, alts, 1, LM_RIDGE);
}

// Geothermal Shallows: shallow sea with a few gaps.
void world_geothermal(int x, int y) {
    if (!on_map(x, y) && !landmark_site(&x, &y, 1)) return;
    int skipped = 0;
    for (int i = 0; i < 25; i++) {
        int xi, yi;
        if (!radius_square(x, y, i, &xi, &yi) || (!is_ocean(xi, yi) && i >= 9)) continue;
        if ((!skipped && i && !(game_rand() % 25)) || (i >= 9 && !(game_rand() % 3))) {
            skipped = 1;
            continue;
        }
        world_alt_set(xi, yi, 2, 1);
        landmark_mark(xi, yi, LM_GEOTHERMAL, i);
    }
    new_landmark(x, y, landmark_name(LM_GEOTHERMAL));
}

// All the landmarks a new map gets, in the original's order; then the
// game's own fix-ups.
void world_landmarks(void) {
    world_monsoon(-1, -1);
    world_crater(-1, -1);
    world_volcano(-1, -1, 0);
    world_mesa(-1, -1);
    world_ridge(-1, -1);
    world_diamond(-1, -1);
    world_ruin(-1, -1);
    if (*game_expansion) world_unity(-1, -1);
    if (*game_expansion) world_fossil(-1, -1);
    world_temple(-1, -1);
    world_borehole(-1, -1);
    world_sargasso(-1, -1);
    world_dune(-1, -1);
    world_fresh(-1, -1);
    world_geothermal(-1, -1);
    game_fixup_landmarks();
}

// A whole new map from the map settings (random ones chosen first):
// continents until the land wanted and the islands are there, hills by
// the erosive forces, erosion, polar caps on the less watery maps,
// shorelines; made again (up to four times) while one continent dominates,
// unless the map is shown growing. Then temperature, riverbeds, fungus,
// the landmarks, contours, climate and rockiness. (The count of continents,
// which sizes them, carries over into another attempt.)
void world_build(void) {
    const struct rules_worldbuilder *wb = game_worldbuilder;
    for (int attempt = 0, made = 0;;) {
        for (int i = 0; i < 11; i++) game_alt_natural[i] = game_alt_natural_default[i];
        *game_world_add_temperature = 1;
        *game_world_skip_territory = 1;
        for (int i = 0; i < 7; i++)
            if (game_map_settings[i] < 0) game_map_settings[i] = game_rand() % 3;
        map_wipe();
        int area = *game_map_area;
        *game_map_flat = 0;  // new maps wrap east to west
        *game_brush_total = 0;
        *game_brush_land_wanted = ((2 - *game_ocean_coverage) * wb->land_mod + wb->land_base) * area / 3200;
        int islands = wb->islands * area / 3200;
        if (*game_state & GAME_STATE_OMNISCIENT) {
            game_map_win_clear_terrain(game_map_wins[0]);
            game_draw_map(1);
        }
        for (int tries = 0; *game_brush_total < *game_brush_land_wanted || made < islands;) {
            if (++tries >= 100) break;
            build_continent(++made);
        }
        if (*game_erosive_forces >= 1) {
            int hills = ((*game_erosive_forces - 1) * wb->hills_mod + wb->hills_base) * area / 3200;
            if (*game_ocean_coverage == 2) hills /= 2;
            for (int i = 0; i < hills; i++) build_hills(4);
        }
        world_erosion();
        if (!(*game_map_flat & 1) && *game_ocean_coverage < 2 && (!*game_ocean_coverage || game_rand() % 3 == 0))
            world_polar_caps();
        world_shorelines();
        if ((*game_state & GAME_STATE_OMNISCIENT) || !world_validate() || ++attempt >= 4) break;
    }
    world_temperature();
    world_riverbeds();
    world_fungus();
    world_landmarks();
    world_linearize_contours();
    *game_world_add_temperature = 0;
    world_climate();
    world_rocky();
    *game_world_skip_territory = 0;
    if (!*game_halted) redraw_terrain();
}

// ---- Hooks and shadow checks ------------------------------------------------

extern struct hook_module mapgen_module;
#define MAPGEN (&mapgen_module)

// All the game's state these can touch.
static void writes_state(struct hook_writes *w) {
    hooks_writes_add(w, game_data, game_data_size);
    if (*game_map)
        hooks_writes_add(w, *game_map, (size_t)*game_map_half_width * *game_map_height * sizeof(struct map_tile));
    if (*game_abstract_map) hooks_writes_add(w, *game_abstract_map, (size_t)*game_abstract_area);
    hooks_writes_add(w, game_rand_state(), sizeof(uint32_t));
}
#define WRITES_STATE writes_state(&w_)

SHADOW_WRITER(MAPGEN, void, alt_set, (int a, int b, int c), (a, b, c), WRITES_STATE, "%d, %d, %d", a, b, c)
SHADOW_WRITER(MAPGEN, void, alt_set_both, (int a, int b, int c), (a, b, c), WRITES_STATE, "%d, %d, %d", a, b, c)
SHADOW_WRITER(MAPGEN, void, world_alt_set, (int a, int b, int c, int d), (a, b, c, d), WRITES_STATE, "%d, %d, %d, %d", a,
              b, c, d)
SHADOW_WRITER(MAPGEN, void, world_raise_alt, (int a, int b), (a, b), WRITES_STATE, "%d, %d", a, b)
SHADOW_WRITER(MAPGEN, void, world_lower_alt, (int a, int b), (a, b), WRITES_STATE, "%d, %d", a, b)
SHADOW_WRITER(MAPGEN, void, brush, (int a, int b, int c), (a, b, c), WRITES_STATE, "%d, %d, %d", a, b, c)
SHADOW_WRITER(MAPGEN, void, paint_land, (int a, int b, int c, int d), (a, b, c, d), WRITES_STATE, "%d, %d, %d, %d", a, b,
              c, d)
SHADOW_WRITER(MAPGEN, void, build_continent, (int a), (a), WRITES_STATE, "%d", a)
SHADOW_WRITER(MAPGEN, void, build_hills, (int a), (a), WRITES_STATE, "%d", a)
SHADOW_WRITER(MAPGEN, void, world_riverbeds, (void), (), WRITES_STATE, "%s", "")
SHADOW_WRITER(MAPGEN, void, map_wipe, (void), (), WRITES_STATE, "%s", "")
SHADOW_WRITER(MAPGEN, int, world_validate, (void), (), WRITES_STATE, "%s", "")
SHADOW_WRITER(MAPGEN, void, world_temperature, (void), (), WRITES_STATE, "%s", "")
SHADOW_WRITER(MAPGEN, void, world_analysis, (void), (), WRITES_STATE, "%s", "")
SHADOW_WRITER(MAPGEN, void, world_alt_put_detail, (int a, int b), (a, b), WRITES_STATE, "%d, %d", a, b)
SHADOW_WRITER(MAPGEN, void, world_polar_caps, (void), (), WRITES_STATE, "%s", "")
SHADOW_WRITER(MAPGEN, void, world_linearize_contours, (void), (), WRITES_STATE, "%s", "")
SHADOW_WRITER(MAPGEN, void, world_erosion, (void), (), WRITES_STATE, "%s", "")
SHADOW_WRITER(MAPGEN, void, world_rocky, (void), (), WRITES_STATE, "%s", "")
SHADOW_WRITER(MAPGEN, void, world_fungus, (void), (), WRITES_STATE, "%s", "")
SHADOW_WRITER(MAPGEN, void, world_rivers, (void), (), WRITES_STATE, "%s", "")
SHADOW_WRITER(MAPGEN, void, world_shorelines, (void), (), WRITES_STATE, "%s", "")
SHADOW_WRITER(MAPGEN, void, world_rainfall, (void), (), WRITES_STATE, "%s", "")
SHADOW_WRITER(MAPGEN, void, world_climate, (void), (), WRITES_STATE, "%s", "")
SHADOW_WRITER(MAPGEN, void, world_build, (void), (), WRITES_STATE, "%s", "")

#define LANDMARK_SHADOW(name) \
    SHADOW_WRITER(MAPGEN, void, name, (int a, int b), (a, b), WRITES_STATE, "%d, %d", a, b)
LANDMARK_SHADOW(world_crater)
LANDMARK_SHADOW(world_monsoon)
LANDMARK_SHADOW(world_sargasso)
LANDMARK_SHADOW(world_ruin)
LANDMARK_SHADOW(world_dune)
LANDMARK_SHADOW(world_diamond)
LANDMARK_SHADOW(world_fresh)
LANDMARK_SHADOW(world_borehole)
LANDMARK_SHADOW(world_temple)
LANDMARK_SHADOW(world_unity)
LANDMARK_SHADOW(world_fossil)
LANDMARK_SHADOW(world_canyon)
LANDMARK_SHADOW(world_mesa)
LANDMARK_SHADOW(world_ridge)
LANDMARK_SHADOW(world_geothermal)
SHADOW_WRITER(MAPGEN, void, world_volcano, (int a, int b, int c), (a, b, c), WRITES_STATE, "%d, %d, %d", a, b, c)
SHADOW_WRITER(MAPGEN, void, world_landmarks, (void), (), WRITES_STATE, "%s", "")

// In shadow mode, the game's message pump and drawing do nothing while a
// check runs or a sweep's copy of the game is in use, so the original and
// ours see the same state, no input is handled twice or lost to a rewind,
// and neither run paints over what the other left in the windows, on the
// heap, which checks don't put back. (Placing a unit, such as a fungal
// tower, reveals and repaints the squares around it at once.) Map
// generation redraws less often as a result.
#define QUIET(name, label, params, args)                   \
    static void (*orig_##name) params;                     \
    static struct hook_stats stats_##name = {label};       \
    static void shadow_##name params {                     \
        if (hooks_protecting()) {                          \
            stats_##name.calls++;                          \
            return;                                        \
        }                                                  \
        orig_##name args;                                  \
    }
QUIET(do_all_non_input, "(quiet pump)", (void), ())
QUIET(do_all_draws, "(quiet paint)", (void), ())
QUIET(draw_radius, "(quiet draw_radius)", (int x, int y, int radius, int unk), (x, y, radius, unk))
QUIET(draw_tile, "(quiet draw_tile)", (int x, int y, int unk), (x, y, unk))
QUIET(draw_map, "(quiet draw_map)", (int unk), (unk))
static void(__attribute__((thiscall)) * orig_map_win_clear_terrain)(void *win);
static struct hook_stats stats_map_win_clear_terrain = {"(quiet MapWin_clear_terrain)"};
static void __attribute__((thiscall)) shadow_map_win_clear_terrain(void *win) {
    if (hooks_protecting()) {
        stats_map_win_clear_terrain.calls++;
        return;
    }
    orig_map_win_clear_terrain(win);
}

// Once a turn, on a protected copy of the game: bases and units hidden so
// alt_set() only shapes terrain, then each pass run on the live map and put
// back.
static int slice;

static void mapgen_sweep(void) {
    slice++;
    if (!*game_map || *game_map_width <= 0 || *game_map_height <= 0) return;
    int w = *game_map_width, h = *game_map_height, sweeping = hooks_sweeping;
    struct hook_writes outer = {.protect_only = 1};
    writes_state(&outer);
    hooks_writes_begin(&outer);
    hooks_sweeping = 1;
    *game_world_skip_territory = 1;
    for (int i = 0; i < 4; i++) {
        int y = (slice * 7 + i * 13) % h, x = ((slice * 11 + i * 23) % w & ~1) | (y & 1);
        if (x >= w) continue;
        check_alt_set(x, y, (slice + 2 * i) % 7);
        check_world_alt_set(x, y, (slice + i) % 7, i & 1);
        check_world_raise_alt(x, y);
        check_world_lower_alt(x, y);
        check_alt_set_both(x, y, (slice + 3 * i) % 7);
        check_world_alt_put_detail(x, y);
    }
    for (int i = 0; i < 2; i++) {
        int y = (slice * 5 + i * 19) % h, x = ((slice * 13 + i * 31) % w & ~1) | (y & 1);
        if (x >= w) continue;
        check_brush(x, y, ALT_SHORE_LINE + (slice + i) % 4);
        check_paint_land(x, y, ALT_SHORE_LINE + (slice + i) % 3, 5 + slice % 20);
    }
    check_build_continent(*game_map_area / (2 + slice % 6));
    check_build_hills(4 + slice % 3);
    check_world_riverbeds();
    check_world_temperature();
    check_world_analysis();
    check_world_validate();
    check_world_polar_caps();
    check_world_linearize_contours();
    // One of the larger passes a turn.
    static void (*const passes[])(void) = {
        check_world_erosion,  check_world_rocky,    check_world_fungus,  check_world_rivers,
        check_world_shorelines, check_world_rainfall, check_world_climate,
    };
    passes[slice % (int)(sizeof(passes) / sizeof(passes[0]))]();
    // Landmarks: at a site of their own, and on a given square.
    static void (*const landmarks[])(int, int) = {
        check_world_crater, check_world_monsoon, check_world_sargasso, check_world_ruin, check_world_dune,
        check_world_diamond, check_world_fresh, check_world_borehole, check_world_temple, check_world_unity,
        check_world_fossil, check_world_canyon, check_world_mesa, check_world_ridge, check_world_geothermal,
    };
    int n = (int)(sizeof(landmarks) / sizeof(landmarks[0]));
    for (int i = 0; i < n; i++) {
        int y = (slice * 3 + i * 7) % h, x = ((slice * 17 + i * 11) % w & ~1) | (y & 1);
        landmarks[i](-1, -1);
        if (x < w) landmarks[i](x, y);
    }
    check_world_volcano(-1, -1, slice & 1);
    if (slice % 4 == 0) check_world_landmarks();
    check_map_wipe();
    hooks_writes_abandon(&outer);
    // Every 16th turn, a whole new map on another copy: the game's builder
    // with each pass it calls checked as it goes, or eight turns on, the
    // whole build checked at once. A game makes its map only once, so this
    // keeps the builder checked in long soaks.
    if (slice % 8 == 1) {
        struct hook_writes copy = {.protect_only = 1};
        writes_state(&copy);
        hooks_writes_begin(&copy);
        *game_world_skip_territory = 1;
        if (slice % 16 == 1) {
            hooks_sweeping = 0;
            orig_world_build();
        } else {
            check_world_build();
        }
        hooks_writes_abandon(&copy);
    }
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook mapgen_hooks[] = {
    HOOK(0x591290, alt_set, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),
    HOOK(0x5918f0, alt_set_both, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x10),  // mov eax, [ebp+16]
    HOOK(0x5c2020, world_alt_set, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x18),
    HOOK(0x5c2380, world_raise_alt, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x55, 0x0c),
    HOOK(0x5c23e0, world_lower_alt, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x55, 0x0c),
    HOOK(0x5c2440, brush, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x20),
    HOOK(0x5c27f0, paint_land, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),
    HOOK(0x5c28f0, build_continent, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),
    HOOK(0x5c2b40, build_hills, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),
    HOOK(0x5c2cb0, world_erosion, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0x14, 0x03, 0x00, 0x00),
    HOOK(0x5c32a0, world_rocky, 0xa1, 0x74, 0x98, 0x94, 0x00),  // mov eax, [0x949874]
    HOOK(0x5c3440, world_fungus, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0xe4, 0x02, 0x00, 0x00),
    HOOK(0x5c3680, world_riverbeds, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x18),
    HOOK(0x5c38b0, world_rivers, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0x8c, 0x00, 0x00, 0x00),
    HOOK(0x5c3f70, world_shorelines, 0xa1, 0x74, 0x98, 0x94, 0x00),
    HOOK(0x5c4470, world_rainfall, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x38),
    HOOK(0x591040, map_wipe, 0x53, 0x56, 0x33, 0xdb, 0x68, 0xff, 0x7f, 0x00, 0x00),  // push ebx; push esi; xor ebx, ebx; push 0x7fff
    HOOK(0x5c40f0, world_validate, 0x56, 0x57, 0xb9, 0x00, 0x5b, 0x94, 0x00),          // push esi; push edi; mov ecx, 0x945b00
    HOOK(0x5c4170, world_temperature, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x24),
    HOOK(0x5c55c0, world_analysis, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x1c),
    HOOK(0x5c58c0, world_alt_put_detail, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x58, 0xfb, 0x68, 0x00),  // mov eax, [0x68fb58]
    HOOK(0x5c58e0, world_polar_caps, 0x8b, 0x0d, 0x70, 0x98, 0x94, 0x00),                     // mov ecx, [0x949870]
    // push 2; call do_checksums
    {0x5c5a30, (const uint8_t[]){0x6a, 0x02, 0xe8, 0x29, 0x8d, 0xf5, 0xff}, 7, (void *)world_climate,
     (void *)shadow_world_climate, (void **)&orig_world_climate, &stats_world_climate, 0, 1},
    HOOK(0x5c5ae0, world_linearize_contours, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),
    HOOK(0x5c5c70, world_crater, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x53, 0x56, 0x8b, 0x75, 0x0c),  // push ecx, ebx, esi; mov esi, [ebp+12]
    HOOK(0x5c5ef0, world_monsoon, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),
    HOOK(0x5c6200, world_sargasso, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),
    HOOK(0x5c64a0, world_ruin, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x45, 0x0c),
    HOOK(0x5c6740, world_dune, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),
    HOOK(0x5c69e0, world_diamond, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x45, 0x0c),
    HOOK(0x5c6c40, world_fresh, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),
    HOOK(0x5c6db0, world_volcano, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x53, 0x56, 0x8b, 0x75, 0x0c),
    HOOK(0x5c7020, world_borehole, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),
    HOOK(0x5c7540, world_temple, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x45, 0x0c),
    HOOK(0x5c7750, world_unity, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),
    HOOK(0x5c7a80, world_fossil, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x45, 0x0c),
    HOOK(0x5c7cb0, world_canyon, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x45, 0x0c),
    HOOK(0x5c7f40, world_mesa, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x45, 0x0c),
    HOOK(0x5c8150, world_ridge, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x53, 0x8b, 0x5d, 0x0c),
    HOOK(0x5c83b0, world_geothermal, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),
    // push -1; push -1; call world_monsoon
    {0x5c8630, (const uint8_t[]){0x6a, 0xff, 0x6a, 0xff, 0xe8, 0xb7, 0xd8, 0xff, 0xff}, 9, (void *)world_landmarks,
     (void *)shadow_world_landmarks, (void **)&orig_world_landmarks, &stats_world_landmarks, 0, 1},
    HOOK(0x5c86e0, world_build, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x53, 0x33, 0xdb),  // push ecx; push ebx; xor ebx, ebx
    // Shadow mode only: the quiet pump and drawing.
#define QUIET_HOOK(addr, name, ...)                                                                     \
    {addr, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}), NULL, (void *)shadow_##name, \
     (void **)&orig_##name, &stats_##name, 1}
    QUIET_HOOK(0x5fcb20, do_all_non_input, 0xc7, 0x05, 0x9c, 0x7b, 0x9b, 0x00, 0x20, 0x00, 0x00, 0x00),  // mov dword [0x9b7b9c], 0x20
    QUIET_HOOK(0x5fcbb0, do_all_draws, 0x83, 0xec, 0x1c, 0xc7, 0x05, 0x9c, 0x7b, 0x9b, 0x00, 0x01, 0x00, 0x00, 0x00),
    QUIET_HOOK(0x46aef0, draw_radius, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x10),  // push ebx; mov ebx, [ebp+16]
    QUIET_HOOK(0x46af40, draw_tile, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x0c),
    QUIET_HOOK(0x46b190, draw_map, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x57),                   // push esi; push edi
    QUIET_HOOK(0x46fd90, map_win_clear_terrain, 0x8b, 0x41, 0x04, 0x53, 0x56),          // mov eax, [ecx+4]; push ebx; push esi
};
struct hook_module mapgen_module = {"map generation", mapgen_hooks, HOOK_COUNT(mapgen_hooks), mapgen_sweep, NULL, -1};
