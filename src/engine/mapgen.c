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

// Sets the square's altitude, and gives it a contour within that altitude's
// band (a random one) unless it is in the band already.
void alt_set_both(int x, int y, int altitude) {
    game_alt_set(x, y, altitude);
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
            game_alt_set(xi, yi, altitude);
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
        else game_alt_set(x, y, altitude);
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

// Whether the land is spread well enough: the second largest land region
// must be under half the largest (two thirds with land coverage 1). Finds
// the regions first.
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

// In shadow mode, the game's redraw and message pump does nothing while a
// check runs, so the original and ours see the same state and no input is
// handled twice. Map generation redraws less often as a result.
static void (*orig_do_all_non_input)(void);
static struct hook_stats stats_do_all_non_input = {"(quiet pump)"};
static void shadow_do_all_non_input(void) {
    if (hooks_checking()) {
        stats_do_all_non_input.calls++;
        return;
    }
    orig_do_all_non_input();
}

// Once a turn, on a protected copy of the game: bases and units hidden so
// alt_set() only shapes terrain, then each pass run on the live map and put
// back.
static int slice;

static void mapgen_sweep(void) {
    slice++;
    if (!*game_map || *game_map_width <= 0 || *game_map_height <= 0) return;
    int w = *game_map_width, h = *game_map_height, sweeping = hooks_sweeping;
    struct hook_writes outer = {0};
    writes_state(&outer);
    hooks_writes_begin(&outer);
    hooks_sweeping = 1;
    *game_world_skip_territory = 1;
    for (int i = 0; i < 4; i++) {
        int y = (slice * 7 + i * 13) % h, x = ((slice * 11 + i * 23) % w & ~1) | (y & 1);
        if (x >= w) continue;
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
    check_map_wipe();
    hooks_writes_abandon(&outer);
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook mapgen_hooks[] = {
    HOOK(0x5918f0, alt_set_both, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x10),  // mov eax, [ebp+16]
    HOOK(0x5c2020, world_alt_set, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x18),
    HOOK(0x5c2380, world_raise_alt, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x55, 0x0c),
    HOOK(0x5c23e0, world_lower_alt, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x55, 0x0c),
    HOOK(0x5c2440, brush, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x20),
    HOOK(0x5c27f0, paint_land, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),
    HOOK(0x5c28f0, build_continent, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),
    HOOK(0x5c2b40, build_hills, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),
    HOOK(0x5c3680, world_riverbeds, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x18),
    HOOK(0x591040, map_wipe, 0x53, 0x56, 0x33, 0xdb, 0x68, 0xff, 0x7f, 0x00, 0x00),  // push ebx; push esi; xor ebx, ebx; push 0x7fff
    HOOK(0x5c40f0, world_validate, 0x56, 0x57, 0xb9, 0x00, 0x5b, 0x94, 0x00),          // push esi; push edi; mov ecx, 0x945b00
    HOOK(0x5c4170, world_temperature, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x24),
    HOOK(0x5c55c0, world_analysis, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x1c),
    HOOK(0x5c58c0, world_alt_put_detail, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x58, 0xfb, 0x68, 0x00),  // mov eax, [0x68fb58]
    HOOK(0x5c58e0, world_polar_caps, 0x8b, 0x0d, 0x70, 0x98, 0x94, 0x00),                     // mov ecx, [0x949870]
    HOOK(0x5c5ae0, world_linearize_contours, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),
    {0x5fcb20, (const uint8_t[]){0xc7, 0x05, 0x9c, 0x7b, 0x9b, 0x00, 0x20, 0x00, 0x00, 0x00}, 10,  // mov dword [0x9b7b9c], 0x20
     NULL, (void *)shadow_do_all_non_input, (void **)&orig_do_all_non_input, &stats_do_all_non_input, 1},
};
struct hook_module mapgen_module = {"map generation", mapgen_hooks, HOOK_COUNT(mapgen_hooks), mapgen_sweep, NULL, -1};
