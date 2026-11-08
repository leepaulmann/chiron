// The path finder's map work: numbering the continents and seas, the
// coarse map the AI plans on, and flood fills of a faction's territory.
// The game keeps one path object (game_path) with a table the size of the
// map and two coordinate lists it uses as a queue. Reimplemented from
// terranx.exe 2.0's disassembly; names follow Thinker (MIT), which calls
// these methods of a class Path.
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "game.h"
#include "map.h"
#include "path.h"
#include "shadow.h"
#include "tech.h"
#include "veh.h"

enum {
    ALT_SHORE = 3 << 5,  // climate's altitude bits from the shore line up
    REGION_LAND_LAST = 0x3e, REGION_LAND_POLES = 0x3f,  // land regions 1-62; 63 for the poles and the leftovers
    REGION_SEA_FIRST = 0x40, REGION_SEA_LAST = 0x7e, REGION_SEA_POLES = 0x7f,
    STATE_ONE_CONTINENT = 0x100,  // game_state: a single continent holds most of the land
};

static inline struct map_tile *tile(int x, int y) { return &(*game_map)[(x >> 1) + y * *game_map_half_width]; }
static inline int is_sea(const struct map_tile *t) { return (t->climate & 0xe0) < ALT_SHORE; }

static void queue_push(struct game_path *p, int x, int y) {
    p->x[p->tail] = (int16_t)x;
    p->y[p->tail] = (int16_t)y;
    p->tail++;
}

// Each square of the coarse map, which takes every fifth square each way,
// gets the region of the first land square among the 9 around it, or 0.
void __attribute__((thiscall)) path_make_abstract(struct game_path *p) {
    (void)p;
    for (int ay = 0; ay < *game_abstract_height; ay++)
        for (int ax = ay & 1; ax < *game_abstract_width; ax += 2) {
            int region = 0;
            for (int i = 0; i < 9; i++) {
                int x = xrange(ax * 5 + game_radius_x[i]), y = ay * 5 + game_radius_y[i];
                if (!on_map(x, y) || is_sea(tile(x, y))) continue;
                region = tile(x, y)->region;
                break;
            }
            abstract_set(ax, ay, region);
        }
}

// Folds region `from` into region `to`.
void __attribute__((thiscall)) path_merge(struct game_path *p, int from, int to) {
    (void)p;
    game_continents[to].tile_count += game_continents[from].tile_count;
    game_continents[from].tile_count = 0;
    for (int i = 0; i < *game_map_area; i++)
        if ((*game_map)[i].region == from) (*game_map)[i].region = (uint8_t)to;
}

// From a land square, marks (map_tile.unk_6 = 1) the land squares the
// faction owns that join up with it, and leaves them in the queue. The
// first square is marked only if it is reached again from a neighbour. As in Path_continent, the squares of the top
// and bottom rows lead nowhere: the test is on the square being left, not
// the one entered. The third argument is unused.
void __attribute__((thiscall)) path_territory(struct game_path *p, int x, int y, int unused, int faction) {
    (void)unused;
    if (is_sea(tile(x, y))) return;
    p->head = p->tail = 0;
    queue_push(p, x, y);
    do {
        int cx = p->x[p->head], cy = p->y[p->head];
        p->head++;
        for (int i = 0; i < 8; i++) {
            int x2 = xrange(cx + game_radius_near_x[i]), y2 = cy + game_radius_near_y[i];
            if (!on_map(x2, y2) || !cy || cy == *game_map_height - 1) continue;
            struct map_tile *t = tile(x2, y2);
            if (is_sea(t) || t->unk_6 || t->territory != faction) continue;
            t->unk_6 = 1;
            queue_push(p, x2, y2);
        }
    } while (p->head && p->tail != p->head);
    game_do_all_non_input();
}

// Numbers the land or sea joined to a square (whichever it is) that has no
// region yet, and counts its squares. A sea most of whose squares are of
// the Freshwater Sea landmark becomes all freshwater, and a sea with only
// some has the landmark's bit cleared from all of it.
void __attribute__((thiscall)) path_continent(struct game_path *p, int x, int y, int region) {
    struct continent *c = &game_continents[region];
    int fresh = 0;
    c->tile_count = 0;
    p->head = p->tail = 0;
    queue_push(p, x, y);
    region_set(x, y, region);
    int sea = is_sea(tile(x, y));
    do {
        int cx = p->x[p->head], cy = p->y[p->head];
        p->head++;
        c->tile_count++;
        for (int i = 0; i < 8; i++) {
            int x2 = xrange(cx + game_radius_near_x[i]), y2 = cy + game_radius_near_y[i];
            if (!on_map(x2, y2) || !cy || cy == *game_map_height - 1) continue;
            const struct map_tile *t = tile(x2, y2);
            if (is_sea(t) != sea || t->region) continue;
            if (sea && (t->bits2 & MAP2_LANDMARK(LM_FRESH))) fresh++;
            region_set(x2, y2, region);
            queue_push(p, x2, y2);
        }
    } while (p->head && p->tail != p->head);
    if (fresh) {
        int all = fresh >= c->tile_count * 3 / 4;
        for (int y2 = 0; y2 < *game_map_height; y2++)
            for (int x2 = y2 & 1; x2 < *game_map_width; x2 += 2)
                if (tile(x2, y2)->region == region) bit2_set(x2, y2, MAP2_LANDMARK(LM_FRESH), all);
    }
    game_do_all_non_input();
}

// All the map's regions: land 1-62 and sea 64-126 numbered in the order
// they are first met (row by row from the second row), the top and bottom
// rows given the leftover regions 63 and 127. When the numbers run out,
// the last region takes the place of the smallest so far (which joins the
// leftovers) or, if it is the smallest, joins them itself. Then whether one
// continent holds at least 4/5 of the land (counting what regions not used
// this time were left with: their counts aren't cleared), which seas each continent
// touches, and the coarse map.
void __attribute__((thiscall)) path_continents(struct game_path *p) {
    for (int i = 0; i < *game_map_area; i++) (*game_map)[i].region = 0;
    int land = 0, seas = REGION_SEA_FIRST;
    for (int y = 1; y < *game_map_height - 1; y++)
        for (int x = y & 1; x < *game_map_width; x += 2) {
            if (tile(x, y)->region) continue;
            int r, lo, hi;
            if (is_sea(tile(x, y))) {
                seas++, lo = REGION_SEA_FIRST, hi = REGION_SEA_LAST;
                r = seas < lo ? lo : seas > hi ? hi : seas;
            } else {
                land++, lo = 0, hi = REGION_LAND_LAST;
                r = land < 0 ? 0 : land > hi ? hi : land;
            }
            path_continent(p, x, y, r);
            if (r != hi) continue;
            int smallest = -1, least = game_continents[hi].tile_count;
            for (int j = lo; j < hi; j++)
                if (game_continents[j].tile_count < least) least = game_continents[j].tile_count, smallest = j;
            if (smallest >= 0) {
                path_merge(p, smallest, hi + 1);
                path_merge(p, hi, smallest);
            } else {
                path_merge(p, hi, hi + 1);
            }
        }
    for (int x = 0; x < *game_map_width; x += 2) {
        int r = is_sea(tile(x, 0)) ? REGION_SEA_POLES : REGION_LAND_POLES;
        region_set(x, 0, r);
        game_continents[r].tile_count++;
    }
    int bottom = *game_map_height - 1;
    for (int x = bottom & 1; x < *game_map_width; x += 2) {
        int r = is_sea(tile(x, bottom)) ? REGION_SEA_POLES : REGION_LAND_POLES;
        region_set(x, bottom, r);
        game_continents[r].tile_count++;
    }
    int total = 0, most = 0;
    for (int r = 1; r <= REGION_LAND_LAST; r++) {
        total += game_continents[r].tile_count;
        if (game_continents[r].tile_count > most) most = game_continents[r].tile_count;
    }
    if (most >= total * 4 / 5) *game_state |= STATE_ONE_CONTINENT;
    else *game_state &= ~STATE_ONE_CONTINENT;
    // Only the first 64 regions' sea coasts are cleared, which covers land.
    for (int r = 0; r < REGION_SEA_FIRST; r++)
        for (int i = 0; i < 8; i++) game_continents[r].sea_coasts[i] = 0;
    for (int y = 0; y < *game_map_height; y++)
        for (int x = y & 1; x < *game_map_width; x += 2) {
            int r = tile(x, y)->region;
            if (r >= REGION_SEA_FIRST) continue;
            for (int i = 0; i < 8; i++) {
                int x2 = xrange(x + game_radius_near_x[i]), y2 = y + game_radius_near_y[i];
                if (!on_map(x2, y2)) continue;
                int r2 = tile(x2, y2)->region;
                if (r2 < REGION_SEA_FIRST) continue;
                int offset;
                uint32_t mask;
                bitmask(r2 - REGION_SEA_FIRST, &offset, &mask);
                game_continents[r].sea_coasts[offset] |= (uint8_t)mask;
                // Having found a sea, the original skips the next two
                // neighbours (one after an odd one).
                i += 2 - (i & 1);
            }
        }
    path_make_abstract(p);
    game_do_all_non_input();
}

// ---- Finding a way ------------------------------------------------------------
//
// Path_find fills the path object's table from the destination outwards, a
// square's value being what it costs to reach the destination from it (1
// at the destination), until the start is reached; then it picks the start's
// neighbour that is cheapest to go on from. It keeps the table, and when
// asked again for the same destination, flags, faction and kind of unit
// (by plan), goes straight to the choice if the table reaches the start.

enum {
    PATH_PLAIN = 0x1,        // every step costs 1
    PATH_ROADS = 0x2,        // only along roads and through bases
    PATH_NO_FUNGUS = 0x4,    // not into fungus (deep sea fungus aside)
    PATH_MIN_STEP = 0x8,     // a step costs at least a road move less than a full move
    PATH_FUNGUS = 0x10,      // fungus costs 6 road moves more
    PATH_ZOC = 0x40,         // squares in an enemy's zone of control cost more
    PATH_BORDERS = 0x80,     // others' territory costs more, and for colony pods and formers enemy land near the goal
    PATH_TERRITORY = 0x200,  // only through the faction's own territory, or that of the faction given as the unit type
    PATH_NO_UNOWNED = 0x400,  // with PATH_TERRITORY: not through squares no one owns
    PATH_OTHERS = 0x800,      // with PATH_TERRITORY: through others' territory, though not a human's it's at odds with
    TREATY_UNK_100000 = 0x100000,
    TREATY_TRUCE_ = 0x4, TREATY_BETRAYING = 0xc00,  // TREATY_SHALL_BETRAY | TREATY_UNK_800
    PLAN_TERRAFORM = 9, PLAN_ARTIFACT = 12,
    ABL_CLOAKED_ = 0x4,
    MAP_AIRBASE = 0x40000,
    SP_XENOEMPATHY_DOME = 8,
    PATH_UNREACHED = 2000000000,
};

static inline int path_at(int x, int y) { return (*game_map_width >> 1) * y + (x >> 1); }
static inline int alt_of(const struct map_tile *t) { return t->climate & 0xe0; }
static inline int owner_of(const struct map_tile *t) { return t->owner_site & 0xf; }
static inline int has_base(const struct map_tile *t) { return (t->bits & MAP_BASE) && owner_of(t) < 8; }
static inline uint32_t treaty(int faction, int other) { return game_factions[faction].diplo_status[other]; }
// The bit for a square's owner, as the original shifts by it: no owner (-1)
// gives the top bit.
static inline uint32_t owner_bit(int8_t territory) { return 1u << ((uint8_t)territory & 31); }
static inline int iabs_(int v) { return v < 0 ? -v : v; }

// Whether a unit of the faction moving into the square would be held by a
// zone of control: the owner + 1 of a unit next to it, on the same land or
// sea, that is neither the faction's nor its pact's or vendetta's (the
// original tests TREATY_PACT | TREATY_VENDETTA), or 0. A square with a base
// has none. For a human player, only a stack it can see counts, or one with
// a unit of its own that isn't both cloaked and lurking.
int __attribute__((thiscall)) path_zoc_path(struct game_path *p, int x, int y, int faction) {
    (void)p;
    const struct map_tile *t = tile(x, y);
    if (has_base(t)) return 0;
    int sea = is_sea(t);
    for (int i = 0; i < 8; i++) {
        int x2 = xrange(x + game_radius_near_x[i]), y2 = y + game_radius_near_y[i];
        if (!on_map(x2, y2)) continue;
        const struct map_tile *t2 = tile(x2, y2);
        if (!(t2->bits & MAP_UNIT)) continue;
        int owner = owner_of(t2);
        if (owner >= 8 || owner == faction || is_sea(t2) != sea) continue;
        if (treaty(faction, owner) & (TREATY_PACT | TREATY_VENDETTA)) continue;
        if (!game_is_human(faction)) return owner + 1;
        for (int v = veh_at(x2, y2); v >= 0; v = game_vehs[v].next_in_stack) {
            const struct veh *V = &game_vehs[v];
            if (V->faction == faction && (V->flags & 0x440) != 0x440) return owner + 1;
            if (V->visibility & (1u << faction)) return owner + 1;
        }
    }
    return 0;
}

// For colony pods and formers: whether an enemy (in `war`) owns the square
// (1) or one of the four next to it along the grid's diagonals (2).
static int war_near(int x, int y, uint32_t war) {
    for (int k = 0; k < 9; k++) {
        if (k && !(k & 1)) continue;
        int x2 = xrange(x + game_radius_x[k]), y2 = y + game_radius_y[k];
        if (on_map(x2, y2) && (war & owner_bit(tile(x2, y2)->territory))) return (k != 0) + 1;
    }
    return 0;
}

// What Path_find adds with PATH_BORDERS for entering (x, y), with `here`
// the war_near() of the square left.
struct borders {
    uint32_t treaty, truce, war;  // others with a treaty (no pact), a truce, or a vendetta and bases on the goal's region
    uint32_t self, start_owner, goal_owner;
    int colony, goal;  // a colony pod or former; war_near() of the goal
};
static int borders_cost(const struct borders *b, int faction, int x, int y, int here) {
    int add = 0;
    if (b->war && b->colony)
        for (int k = 0; k < 9; k++) {
            int x2 = xrange(x + game_radius_x[k]), y2 = y + game_radius_y[k];
            if (!on_map(x2, y2) || !(b->war & owner_bit(tile(x2, y2)->territory))) continue;
            if (!k) {
                if (b->goal == 1) continue;
                add += 2500;
            } else if (k & 1) {
                if (b->goal || here == 1) continue;
                add += 500;
            } else if (!here && !b->goal) {
                add += 100;
            }
            break;
        }
    // Others' territory, unless a human can't see the square.
    const struct map_tile *t = tile(x, y);
    if (game_is_human(faction) && !(game_factions[faction].flags & FACTION_FLAG_MAP_REVEALED) &&
        !(t->visibility & b->self))
        return add;
    uint32_t owner = owner_bit(t->territory);
    if (b->self & owner) return add;
    uint32_t cost = 0;
    if (b->treaty & owner) cost = *game_sunspot_duration > 0 ? 2500 : 1000;
    else if (b->truce & owner) cost = game_is_human(faction) ? 1 : 100;
    if (!cost) return add;
    if ((b->start_owner | b->goal_owner) & owner) cost = 1;
    // Doubled for every base of factions 1-7 within two squares: the
    // original ORs the base owner's bit with the treaty masks where it
    // would test it against them, so any such base counts.
    for (int j = 0; j < 21; j++) {
        int x2 = xrange(x + game_radius_x[j]), y2 = y + game_radius_y[j];
        if (!on_map(x2, y2)) continue;
        const struct map_tile *t2 = tile(x2, y2);
        if (has_base(t2) && owner_of(t2) > 0) cost += cost;
    }
    return add + (int)cost;
}

// A step's cost from (xs, ys) to (xd, yd), in sixteenths.
static int step_cost(int proto, int faction, int xs, int ys, int xd, int yd, int fast, int flags) {
    int c = hex_cost(proto, faction, xs, ys, xd, yd, fast);
    int least = game_rules_basic->move_rate_roads - 1;
    if ((flags & PATH_MIN_STEP) && c <= least) c = least;
    return c << 4;
}

// The first step from (x1, y1) towards (x2, y2), as a direction (an index
// into the 8 neighbours), or -1 if there is no way. `stale` is what the
// original finds in a local it reads before setting it (see path_find).
int path_find_at(struct game_path *p, int x1, int y1, int x2, int y2, int proto, int faction, int flags, int unk,
                 int32_t stale) {
    int ally = -1;  // only read with PATH_TERRITORY
    if (flags & PATH_TERRITORY) ally = proto, proto = 0;
    const struct unit_proto *P = &game_protos[proto];
    int sea = game_chassis[P->chassis].triad == TRIAD_SEA;
    int fast = speed_proto(proto) == game_rules_basic->move_rate_roads;
    int mr = game_rules_basic->move_rate_roads;
    // Ships ignore zones of control and borders. That leaves out of reach
    // the original's count of enemy artillery near the sea squares a ship
    // enters (which would also end the look at the square's neighbours, as
    // it reuses their counter), so it isn't here.
    if (sea) flags &= ~(PATH_ZOC | PATH_BORDERS);
    if ((flags & PATH_ZOC) && faction > 0 && proto >= 0 && has_abil(proto, ABL_CLOAKED_)) flags &= ~PATH_ZOC;
    int borders = flags & PATH_BORDERS;
    struct borders b = {0};
    if (borders && faction > 0) {
        const struct map_tile *goal = tile(x2, y2);
        b.start_owner = owner_bit(tile(x1, y1)->territory);
        b.goal_owner = owner_bit(goal->territory);
        for (int f = 1; f < 8; f++) {
            if (f == faction) continue;
            uint32_t d = treaty(faction, f);
            if ((d & TREATY_TREATY) && !(d & TREATY_BETRAYING)) {
                if (!(d & TREATY_PACT)) b.treaty |= 1u << f;
            } else if ((d & TREATY_TRUCE_) && !(d & TREATY_BETRAYING)) {
                b.truce |= 1u << f;
            } else if ((d & TREATY_VENDETTA) && game_factions[f].region_total_bases[goal->region]) {
                b.war |= 1u << f;
            }
        }
        b.self = 1u << faction;
        b.colony = P->plan == PLAN_TERRAFORM || P->plan == PLAN_COLONY;
        if (b.colony) b.goal = war_near(x2, y2, b.war);
    }
    int32_t *map = p->map;
    int best = PATH_UNREACHED, zoc_here = stale, here = 0;
    if (x2 == p->x_dst && y2 == p->y_dst && P->plan == game_protos[p->proto].plan && flags == p->flags &&
        faction == p->faction2) {
        // The same search: straight to the choice if it reached the start,
        // or else on again from the goal over the table as it is.
        if (map[path_at(x1, y1)]) goto choose;
    } else {
        memset(map, 0, (size_t)*game_map_area * 4);
        p->flags = flags;
        p->x_dst = x2;
        p->y_dst = y2;
        p->proto = proto;
        p->faction2 = faction;
    }
    p->head = p->tail = 0;
    queue_push(p, x2, y2);
    map[path_at(x2, y2)] = 1;
    // zoc_here is set for every square but the goal, the first one taken:
    // there the original goes on with whatever its stack held.
    do {
        int cx = p->x[p->head], cy = p->y[p->head];
        if (++p->head == *game_map_area) p->head = 0;
        if (*game_net_multiplayer && !(p->head & 0x3f)) game_do_all_non_input();
        int cv = map[path_at(cx, cy)];
        if (cv > best) continue;
        if (cx == x1 && cy == y1) {
            best = cv;
            continue;
        }
        if ((flags & PATH_ZOC) && faction > 0 && cv > 1)
            zoc_here = is_sea(tile(cx, cy)) ? 0 : path_zoc_path(p, cx, cy, faction);
        if (borders && faction > 0 && b.war && b.colony) here = war_near(cx, cy, b.war);
        for (int i = 0; i < 8; i++) {
            int nx = xrange(cx + game_radius_near_x[i]), ny = cy + game_radius_near_y[i];
            if (!on_map(nx, ny)) continue;
            const struct map_tile *t = tile(nx, ny);
            // Across the shore only at the ends, or for a ship into a base
            // of its own or a pact's.
            if (is_sea(t) != sea && !(nx == x1 && ny == y1) && !(nx == x2 && ny == y2)) {
                if (!sea || !has_base(t)) continue;
                int o = owner_of(t);
                if (o != faction && !(treaty(faction, o) & TREATY_PACT)) continue;
            }
            // An artifact keeps off pods, and off squares next to ones the
            // faction hasn't seen.
            if (P->plan == PLAN_ARTIFACT) {
                if (goody_at(nx, ny)) continue;
                if (faction >= 0) {
                    int unseen = 0;
                    for (int j = 0; j < 8 && !unseen; j++) {
                        int jx = xrange(nx + game_radius_near_x[j]), jy = ny + game_radius_near_y[j];
                        if (!on_map(jx, jy) || (game_factions[faction].flags & FACTION_FLAG_MAP_REVEALED)) continue;
                        if (!(tile(jx, jy)->visibility & (1u << faction))) unseen = 1;
                    }
                    if (unseen) continue;
                }
            }
            if ((flags & PATH_ROADS) && !(t->bits & MAP_ROAD) && !has_base(t)) continue;
            if ((flags & PATH_NO_FUNGUS) && (t->bits & MAP_FUNGUS) && alt_of(t) >= 0x40) continue;
            int nv;
            if (flags & PATH_PLAIN) {
                nv = cv + 1;
            } else {
                nv = cv + 8 + step_cost(proto, faction, nx, ny, cx, cy, fast, flags);
                if (alt_of(t) > alt_of(tile(cx, cy))) nv--;
                if (goody_at(nx, ny)) nv += mr;
            }
            if ((flags & PATH_FUNGUS) && (t->bits & MAP_FUNGUS) && alt_of(t) >= 0x40) nv += 6 * mr;
            if (faction >= 0 && (flags & PATH_TERRITORY)) {
                int o = t->territory;
                if (o != faction && o != ally) {
                    if (o < 0) {
                        if (flags & PATH_NO_UNOWNED) continue;
                    } else {
                        if (!(flags & PATH_OTHERS)) continue;
                        uint32_t d = treaty(faction, o);
                        if ((d & TREATY_UNK_100000) && !(d & (TREATY_PACT | TREATY_VENDETTA))) continue;
                        if (!(treaty(o, faction) & (TREATY_PACT | TREATY_VENDETTA)) && game_is_human(o)) continue;
                    }
                }
            } else if (faction >= 0) {
                // Not past others' units, but those of a pact.
                int owner = -1;
                if ((t->bits & MAP_UNIT) && owner_of(t) < 8) {
                    owner = owner_of(t);
                    if (owner != faction && !(treaty(faction, owner) & TREATY_PACT)) continue;
                }
                // A zone of control next to the goal counts only coming
                // from one.
                if ((flags & PATH_ZOC) && faction > 0 && owner < 0 &&
                    (zoc_here || ((x_dist(nx, x2) + iabs_(ny - y2)) & ~1) > 2) && path_zoc_path(p, nx, ny, faction)) {
                    nv += 1000;
                    if (zoc_here) nv += 2000;
                }
                if (borders && faction > 0 && owner < 0) nv += borders_cost(&b, faction, nx, ny, here);
            }
            if (nv < cv + 1) nv = cv + 1;
            int32_t *m = &map[path_at(nx, ny)];
            if (*m && *m <= nv) continue;
            *m = nv;
            int j = p->head;
            while (j < p->tail && !(p->x[j] == nx && p->y[j] == ny)) j++;
            if (j != p->tail || p->tail >= *game_map_area) continue;
            queue_push(p, nx, ny);
        }
    } while (p->head && p->tail != p->head);
    void *win = game_map_wins[0];
    if (*(uint32_t *)((uint8_t *)win + 0x1dd70) & 0x4000) game_map_win_draw_map(win, 1);
    if (best == PATH_UNREACHED) return -1;
choose:;
    // The neighbour of the start with the least cost from there on: of
    // equals the later, and a straight step over a diagonal one (an odd
    // direction scores 1 more). The goal itself is taken at once.
    int best_dir = -1, best_score = PATH_UNREACHED;
    if ((flags & PATH_ZOC) && faction > 0) zoc_here = is_sea(tile(x1, y1)) ? 0 : path_zoc_path(p, x1, y1, faction);
    if (borders && faction > 0 && b.war && b.colony) here = war_near(x1, y1, b.war);
    for (int i = 0; i < 8; i++) {
        int nx = xrange(x1 + game_radius_near_x[i]), ny = y1 + game_radius_near_y[i];
        if (!on_map(nx, ny)) continue;
        if (nx == x2 && ny == y2) return i;
        int nv = map[path_at(nx, ny)];
        if (!nv) continue;
        if (!(flags & PATH_PLAIN)) {
            nv += step_cost(proto, faction, x1, y1, nx, ny, fast, flags);
            if (!sea && alt_of(tile(nx, ny)) >= alt_of(tile(x1, y1))) nv = nv - 1 < 0 ? 0 : nv - 1 > 999999 ? 999999 : nv - 1;
            if (goody_at(nx, ny)) nv += mr;
        }
        if (faction >= 0 && !(flags & PATH_TERRITORY)) {
            const struct map_tile *t = tile(nx, ny);
            int owner = -1;
            if ((t->bits & MAP_UNIT) && owner_of(t) < 8) {
                owner = owner_of(t);
                if (owner != faction && !(treaty(faction, owner) & TREATY_PACT)) continue;
            }
            if ((flags & PATH_ZOC) && faction > 0 && (zoc_here || ((x_dist(nx, x2) + iabs_(ny - y2)) & ~1) > 2) &&
                owner < 0 && path_zoc_path(p, nx, ny, faction)) {
                nv += 1000;
                if (zoc_here) nv += 2000;
            }
            // Unlike in the search, also next to the faction's own units.
            if (borders && faction > 0) nv += borders_cost(&b, faction, nx, ny, here);
        }
        // Not straight back the way the unit came (unk, a direction).
        if (unk >= 0 && unk < 8 && i == (unk ^ 4)) nv += 20 * mr;
        int score = (i & 1) + nv * 2;
        if (score <= best_score) best_score = score, best_dir = i;
    }
    game_do_all_non_input();
    return best_dir;
}

// Path_find reads the zone of control flag of the square it is at before
// it has set it, for the goal: what lies 0x30 bytes below its entry stack
// pointer. This entry passes that on.
__asm__(".text\n"
        ".globl path_find\n"
        "path_find:\n"
        "    movl -0x30(%esp), %eax\n"
        "    pushl %eax\n"
        "    pushl 0x24(%esp)\n"
        "    pushl 0x24(%esp)\n"
        "    pushl 0x24(%esp)\n"
        "    pushl 0x24(%esp)\n"
        "    pushl 0x24(%esp)\n"
        "    pushl 0x24(%esp)\n"
        "    pushl 0x24(%esp)\n"
        "    pushl 0x24(%esp)\n"
        "    pushl %ecx\n"
        "    call path_find_at\n"
        "    addl $0x28, %esp\n"
        "    ret $0x20\n");

// What Path_move hands Path_find for that flag. The original's comes from
// whatever its earlier calls left on the stack, nonzero in all but about
// 1 call in 200 in a shadow soak; in a check ours gets what the original's
// got (see shadow_path_find_in).
static int32_t move_stale = 1;
// A check of a caller that moves many units (ai.c) hands ours the words
// the original's Path_find calls found, in order (path_stale_record).
enum { STALE_LOG = 8192 };  // enemy_move's site searches make thousands
static int32_t stale_log[STALE_LOG];
static int stale_logging, stale_replaying, stale_n, stale_next, stale_asked;
static int32_t stale_for_move(void) {
    if (stale_replaying) stale_asked++;
    if (stale_replaying && stale_next < stale_n) return stale_log[stale_next++];
    return move_stale;
}

static int moves_left(int veh) {
    int m = veh_speed(veh, 0) - game_vehs[veh].moves_spent;
    return m < 0 ? 0 : m > 999 ? 999 : m;
}

// The direction a unit takes towards its first waypoint, or -1 if it is
// there or has none on the map. Land and sea units ask Path_find (from two
// squares away; `faction` is passed on to it), with zones of control and
// borders counted; air units, and others Path_find finds no way for, take
// the neighbour that looks cheapest. A unit with nowhere to go loses its
// orders, and an AI unit its moves; so does one about to step back the way
// it came (unk_29), unless it flies.
int __attribute__((thiscall)) path_move(struct game_path *p, int veh, int faction) {
    struct veh *V = &game_vehs[veh];
    int vf = V->faction, flags = PATH_ZOC | PATH_BORDERS;
    if (vf != p->faction) p->x_dst = p->y_dst = -1, p->faction = vf;
    int vx = V->x, vy = V->y, wx = V->waypoint_x[0], wy = V->waypoint_y[0];
    if (!on_map(wx, wy) || (vx == wx && vy == wy)) return -1;
    if (!game_is_human(vf) && (V->state & 0x60000) == 0x60000) flags |= 0x20;
    if ((V->state & 0x1000200) == 0x1000200) {
        flags &= ~PATH_ZOC;
    } else if (moves_left(veh) <= game_rules_basic->move_rate_roads) {
        flags |= 0x100;
        if (moves_left(veh) <= (V->proto != 8) + 1) flags |= 0x1000;
    }
    const struct unit_proto *P = &game_protos[V->proto];
    if (!game_weapons[P->weapon].offense) {
        int b = game_secret_projects[SP_XENOEMPATHY_DOME];
        if (b < 0 || game_bases[b].faction != vf) flags |= PATH_FUNGUS;
    }
    if (game_chassis[P->chassis].triad != TRIAD_AIR && ((x_dist(vx, wx) + iabs_(vy - wy)) & ~1)) {
        int unk = V->state & 0x10000 ? (int8_t)V->unk_29 : -1;
        int dir = path_find_at(p, vx, vy, wx, wy, V->proto, faction, flags, unk, stale_for_move());
        if (dir >= 0) return dir;
        if ((V->state & 0x1000200) == 0x1000200) {
            V->order = 0;
            V->state &= ~0x1000200u;
            return -1;
        }
    }
    int zoc = zoc_move(vx, vy, vf), best = 99999, best_dir = -1;
    int triad = game_chassis[P->chassis].triad;
    for (int i = 0; i < 8; i++) {
        int nx = xrange(vx + game_radius_near_x[i]), ny = vy + game_radius_near_y[i];
        if (!on_map(nx, ny)) continue;
        int dx = x_dist(nx, wx), dy = ny - wy, d = (dx + iabs_(dy)) >> 1;
        if (!d) return i;
        const struct map_tile *t = tile(nx, ny);
        if ((t->bits & (MAP_BASE | MAP_UNIT)) && owner_of(t) < 8) {
            int o = owner_of(t);
            if (o != vf && !(treaty(vf, o) & TREATY_PACT)) continue;
        }
        int cost;
        if (triad == TRIAD_AIR) {
            if (t->bits & MAP_AIRBASE) continue;
            cost = d * 32 + vector_dist_offset(dx, dy);
        } else {
            if (is_sea(t) != (triad == TRIAD_SEA)) continue;
            // From one zone of control into another only into a base. The
            // original looks for it in the top row, at the square's x / 2,
            // and wants faction 0's.
            if (zoc && zoc_move(nx, ny, vf)) {
                const struct map_tile *bt = &(*game_map)[nx >> 1];
                if (!(bt->bits & MAP_BASE) || owner_of(bt) != 0) continue;
            }
            cost = hex_cost(V->proto, vf, vx, vy, nx, ny, 0) + d * 32;
        }
        if (cost < best) best = cost, best_dir = i;
    }
    if (best_dir < 0) {
        V->order = 0;
        if (!game_is_human(vf)) veh_skip(veh);
    }
    if ((V->state & 0x10000) && best_dir == ((int8_t)V->unk_29 ^ 4) &&
        game_chassis[game_protos[V->proto].chassis].triad != TRIAD_AIR) {
        V->state &= ~0x10000u;
        V->order = 0;
        if (!game_is_human(vf)) veh_skip(veh);
        return -1;
    }
    return best_dir;
}

// ---- Sensors -------------------------------------------------------------------

// Where on the region of (*x, *y) the faction would best put a sensor: a
// square of its territory without a base, others' units (but a pact's), an
// enemy's zone of control, a mine, solar collector, condenser, borehole or
// monolith, or a resource; not flat or rolling with rain unless it is fungus
// on land, and fungus on land only once the faction can improve it. Squares
// with sensor cover within one square are out. Nearer the start is better;
// it costs 400 more with no own base within two squares, 800 more with none
// of those bases short of cover, and 200 more with cover within two squares
// and not the last. The site goes to *x, *y; the table marks each candidate
// not costed for cover (with its cost + 1), which then counts as cover.
// Returns 0 when there is a site, else 1.
int __attribute__((thiscall)) path_sensors(struct game_path *p, int faction, int *x, int *y) {
    int found = 1, best = 9999;
    memset(p->map, 0, (size_t)*game_map_area * 4);
    p->x_dst = p->y_dst = -1;
    int sx = *x, sy = *y, region = tile(sx, sy)->region;
    for (int yy = 0; yy < *game_map_height; yy++)
        for (int xx = yy & 1; xx < *game_map_width; xx += 2) {
            const struct map_tile *t = tile(xx, yy);
            if (t->region != region || whose_territory(faction, xx, yy, NULL, 0) != faction) continue;
            if (has_base(t)) continue;
            if ((t->bits & MAP_UNIT) && owner_of(t) < 8) {
                int o = owner_of(t);
                if (o != faction && !(treaty(faction, o) & TREATY_PACT)) continue;
            }
            // zoc_veh gives the owner + 1, which the original looks up as
            // the faction.
            int z = zoc_veh(xx, yy, faction);
            if (z == 1 || (z && (treaty(faction, z) & TREATY_VENDETTA))) continue;
            if (t->bits & (MAP_MINE | MAP_SOLAR | MAP_THERMAL_BORE | MAP_CONDENSER | MAP_MONOLITH)) continue;
            if (bonus_at(xx, yy, 0)) continue;
            if ((t->rocky_lock & 0xc0) <= 0x40 && (t->climate & 0x18) && (!(t->bits & MAP_FUNGUS) || alt_of(t) < 0x40))
                continue;
            if ((t->bits & MAP_FUNGUS) && alt_of(t) >= 0x40 && !has_tech(game_rules_basic->tech_improve_fungus, faction))
                continue;
            int marks = 0;
            for (int i = 0; i < 25; i++) {
                int x2 = xrange(xx + game_radius_x[i]), y2 = yy + game_radius_y[i];
                if (!on_map(x2, y2)) continue;
                if (!((is_sensor(x2, y2) && whose_territory(faction, x2, y2, NULL, 0) == faction) ||
                      p->map[path_at(x2, y2)]))
                    continue;
                if (i < 9) marks |= 1;
                marks |= 2;
            }
            if (marks & 1) continue;
            int score = vector_dist(sx, sy, xx, yy);
            for (int i = 0; i < 25; i++) {
                int x2 = xrange(xx + game_radius_x[i]), y2 = yy + game_radius_y[i];
                if (!on_map(x2, y2) || !has_base(tile(x2, y2)) || owner_of(tile(x2, y2)) != faction) continue;
                marks |= 4;
                int covered = 0;
                for (int j = 0; j < 25 && !covered; j++) {
                    int x3 = xrange(x2 + game_radius_x[j]), y3 = y2 + game_radius_y[j];
                    if (on_map(x3, y3) && (is_sensor(x3, y3) || p->map[path_at(x3, y3)]) &&
                        whose_territory(faction, x3, y3, NULL, 0) == faction)
                        covered = 1;
                }
                if (!covered) marks |= 8;
            }
            if (!(marks & 4)) score += 400;
            if (!(marks & 8)) score += 800;
            if ((marks & 2) && !(marks & 8)) score += 200;
            else p->map[path_at(xx, yy)] = score + 1;
            if (score < best) {
                best = score;
                *x = xx;
                *y = yy;
                found = 0;
            }
        }
    game_do_all_non_input();
    return found;
}

// ---- Hooks and shadow checks ------------------------------------------------

extern struct hook_module path_module;
#define PATH (&path_module)

// The game's state (with the map and the coarse map), and the path
// object's table and queue.
void writes_paths(struct hook_writes *w, struct game_path *p) {
    hooks_writes_game(w);
    size_t area = (size_t)*game_map_area;
    if (p->map) hooks_writes_add(w, p->map, area * 4);
    if (p->x) hooks_writes_add(w, p->x, area * 2);
    if (p->y) hooks_writes_add(w, p->y, area * 2);
}

// SHADOW_WRITER for the path object's methods, which take it in ecx.
#define PATH_WRITER(ret, name, params, args, fmt, ...)                                         \
    static ret(__attribute__((thiscall)) * orig_##name) params;                                \
    static struct hook_stats stats_##name = {#name};                                           \
    static ret check_##name params {                                                           \
        struct hook_writes w_ = {0};                                                           \
        writes_paths(&w_, p);                                                                  \
        hooks_writes_begin(&w_);                                                               \
        hooks_nested++;                                                                        \
        SHADOW_RESULT_##ret(want) orig_##name args;                                            \
        hooks_nested--;                                                                        \
        hooks_writes_middle(&w_);                                                              \
        SHADOW_RESULT_##ret(got) name args;                                                    \
        const char *diff_ = hooks_writes_end(&w_, SHADOW_INT_##ret(got), SHADOW_INT_##ret(want)); \
        if (diff_) hooks_mismatch(&stats_##name, "(" fmt ") %s", __VA_ARGS__, diff_);          \
        hooks_ui_release();                                                                    \
        SHADOW_RETURN_##ret(want);                                                             \
    }                                                                                          \
    static ret __attribute__((thiscall)) shadow_##name params {                                \
        if (hooks_nested) SHADOW_TAIL_##ret(orig_##name args);                                 \
        if (shadow_bypass()) SHADOW_TAIL_##ret(name args);                                     \
        stats_##name.calls++;                                                                  \
        shadow_tick(PATH);                                                                     \
        SHADOW_TAIL_##ret(check_##name args);                                                  \
    }

PATH_WRITER(void, path_make_abstract, (struct game_path * p), (p), "%s", "")
PATH_WRITER(void, path_merge, (struct game_path * p, int a, int b), (p, a, b), "%d, %d", a, b)
PATH_WRITER(void, path_territory, (struct game_path * p, int a, int b, int c, int d), (p, a, b, c, d), "%d, %d, %d, %d", a, b, c, d)
PATH_WRITER(void, path_continent, (struct game_path * p, int a, int b, int c), (p, a, b, c), "%d, %d, %d", a, b, c)
PATH_WRITER(void, path_continents, (struct game_path * p), (p), "%s", "")

// Path_zoc_path only reads.
static int(__attribute__((thiscall)) * orig_path_zoc_path)(struct game_path *p, int x, int y, int faction);
static struct hook_stats stats_path_zoc_path = {"path_zoc_path"};
static int check_path_zoc_path(struct game_path *p, int x, int y, int faction) {
    hooks_nested++;
    int want = orig_path_zoc_path(p, x, y, faction);
    hooks_nested--;
    int got = path_zoc_path(p, x, y, faction);
    if (got != want) hooks_mismatch(&stats_path_zoc_path, "(%d, %d, %d) = %d, original %d", x, y, faction, got, want);
    return want;
}
static int __attribute__((thiscall)) shadow_path_zoc_path(struct game_path *p, int x, int y, int faction) {
    if (hooks_nested) return orig_path_zoc_path(p, x, y, faction);
    if (shadow_bypass()) return path_zoc_path(p, x, y, faction);
    stats_path_zoc_path.calls++;
    shadow_tick(PATH);
    return check_path_zoc_path(p, x, y, faction);
}

// Path_find: both runs get the word the game's call left where the
// original reads it. path_stale_call runs fn (a thiscall function of 8
// arguments) with `stale` 0x30 bytes below its entry stack pointer.
int path_stale_call(void *fn, struct game_path *p, const int32_t *args, int32_t stale);
__asm__(".text\n"
        "path_stale_call:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    pushl %esi\n"
        "    movl 16(%ebp), %esi\n"
        "    pushl 28(%esi)\n"
        "    pushl 24(%esi)\n"
        "    pushl 20(%esi)\n"
        "    pushl 16(%esi)\n"
        "    pushl 12(%esi)\n"
        "    pushl 8(%esi)\n"
        "    pushl 4(%esi)\n"
        "    pushl 0(%esi)\n"
        "    movl 20(%ebp), %eax\n"
        "    movl %eax, -0x34(%esp)\n"
        "    movl 12(%ebp), %ecx\n"
        "    call *8(%ebp)\n"
        "    popl %esi\n"
        "    popl %ebp\n"
        "    ret\n");

static int(__attribute__((thiscall)) * orig_path_find)(struct game_path *p, int x1, int y1, int x2, int y2, int proto,
                                                        int faction, int flags, int unk);
static struct hook_stats stats_path_find = {"path_find"};
// What the original's last call from inside a check found below its
// stack, for path_move's check.
static int32_t nested_stale;
static int nested_stale_seen;

static int find_at(struct game_path *p, const int32_t *a, int32_t stale) {
    return path_find_at(p, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], stale);
}

static int check_path_find(struct game_path *p, const int32_t *a, int32_t stale) {
    struct hook_writes w_ = {0};
    writes_paths(&w_, p);
    hooks_writes_begin(&w_);
    hooks_nested++;
    int want = path_stale_call((void *)orig_path_find, p, a, stale);
    hooks_nested--;
    hooks_writes_middle(&w_);
    int got = find_at(p, a, stale);
    const char *diff_ = hooks_writes_end(&w_, got, want);
    if (diff_)
        hooks_mismatch(&stats_path_find, "(%d, %d, %d, %d, %d, %d, %#x, %d; %s) %s", a[0], a[1], a[2], a[3], a[4], a[5],
                       a[6], a[7], stale ? "zoc" : "no zoc", diff_);
    hooks_ui_release();
    return want;
}

int shadow_path_find_in(struct game_path *p, int x1, int y1, int x2, int y2, int proto, int faction, int flags, int unk,
                        int32_t stale);
int shadow_path_find_in(struct game_path *p, int x1, int y1, int x2, int y2, int proto, int faction, int flags, int unk,
                        int32_t stale) {
    int32_t a[8] = {x1, y1, x2, y2, proto, faction, flags, unk};
    if (hooks_nested) {
        nested_stale = stale, nested_stale_seen = 1;
        if (stale_logging && stale_n < STALE_LOG) stale_log[stale_n++] = stale;
        return path_stale_call((void *)orig_path_find, p, a, stale);
    }
    // Inside a check, a call ours makes through the game's code (contiguous
    // from assemble_passengers or diplomacy_check, say) gets the word the
    // original's call there found, in turn, while the check replays them.
    if (shadow_bypass()) return find_at(p, a, stale_replaying ? stale_for_move() : stale);
    stats_path_find.calls++;
    shadow_tick(PATH);
    return check_path_find(p, a, stale);
}
int __attribute__((thiscall)) shadow_path_find(struct game_path *p, int x1, int y1, int x2, int y2, int proto,
                                               int faction, int flags, int unk);
__asm__(".text\n"
        "shadow_path_find:\n"
        "    movl -0x30(%esp), %eax\n"
        "    pushl %eax\n"
        "    pushl 0x24(%esp)\n"
        "    pushl 0x24(%esp)\n"
        "    pushl 0x24(%esp)\n"
        "    pushl 0x24(%esp)\n"
        "    pushl 0x24(%esp)\n"
        "    pushl 0x24(%esp)\n"
        "    pushl 0x24(%esp)\n"
        "    pushl 0x24(%esp)\n"
        "    pushl %ecx\n"
        "    call shadow_path_find_in\n"
        "    addl $0x28, %esp\n"
        "    ret $0x20\n");

// Path_move: ours hands Path_find what the original's call found.
static int(__attribute__((thiscall)) * orig_path_move)(struct game_path *p, int veh, int faction);
static struct hook_stats stats_path_move = {"path_move"};
static int check_path_move(struct game_path *p, int veh, int faction) {
    struct hook_writes w_ = {0};
    writes_paths(&w_, p);
    hooks_writes_begin(&w_);
    hooks_nested++;
    nested_stale_seen = 0;
    int want = orig_path_move(p, veh, faction);
    hooks_nested--;
    hooks_writes_middle(&w_);
    int32_t saved = move_stale;
    if (nested_stale_seen) move_stale = nested_stale;
    int got = path_move(p, veh, faction);
    move_stale = saved;
    const char *diff_ = hooks_writes_end(&w_, got, want);
    if (diff_) hooks_mismatch(&stats_path_move, "(%d, %d) %s", veh, faction, diff_);
    hooks_ui_release();
    return want;
}
static int __attribute__((thiscall)) shadow_path_move(struct game_path *p, int veh, int faction) {
    if (hooks_nested) return orig_path_move(p, veh, faction);
    if (shadow_bypass()) return path_move(p, veh, faction);
    stats_path_move.calls++;
    shadow_tick(PATH);
    return check_path_move(p, veh, faction);
}

// The same for a check of one of path_move's callers (move.c): forget what
// the original's Path_find found, then hand it to ours, and put it back.
void path_stale_reset(void) { nested_stale_seen = 0; }
int32_t path_stale_take(void) {
    int32_t saved = move_stale;
    if (nested_stale_seen) move_stale = nested_stale;
    return saved;
}
void path_stale_put(int32_t saved) { move_stale = saved; }
void path_stale_record(void) { stale_logging = 1, stale_replaying = 0, stale_n = 0; }
void path_stale_replay(void) { stale_logging = 0, stale_replaying = 1, stale_next = stale_asked = 0; }
void path_stale_stop(void) { stale_logging = stale_replaying = 0; }
int32_t path_stale_next(void) { return stale_for_move(); }
int path_stale_logged(void) { return stale_n; }
int path_stale_used(void) { return stale_asked; }

// Path_sensors: each run gets its own copy of the square, and the
// original's goes back to the caller.
static int(__attribute__((thiscall)) * orig_path_sensors)(struct game_path *p, int faction, int *x, int *y);
static struct hook_stats stats_path_sensors = {"path_sensors"};
static int check_path_sensors(struct game_path *p, int faction, int *x, int *y) {
    int ox = *x, oy = *y, mx = *x, my = *y;
    struct hook_writes w_ = {0};
    writes_paths(&w_, p);
    hooks_writes_begin(&w_);
    hooks_nested++;
    int want = orig_path_sensors(p, faction, &ox, &oy);
    hooks_nested--;
    hooks_writes_middle(&w_);
    int got = path_sensors(p, faction, &mx, &my);
    const char *diff_ = hooks_writes_end(&w_, got, want);
    if (diff_) hooks_mismatch(&stats_path_sensors, "(%d, %d, %d) %s", faction, *x, *y, diff_);
    else if (mx != ox || my != oy)
        hooks_mismatch(&stats_path_sensors, "(%d, %d, %d) gave (%d, %d), original (%d, %d)", faction, *x, *y, mx, my, ox,
                       oy);
    hooks_ui_release();
    *x = ox, *y = oy;
    return want;
}
static int __attribute__((thiscall)) shadow_path_sensors(struct game_path *p, int faction, int *x, int *y) {
    if (hooks_nested) return orig_path_sensors(p, faction, x, y);
    if (shadow_bypass()) return path_sensors(p, faction, x, y);
    stats_path_sensors.calls++;
    shadow_tick(PATH);
    return check_path_sensors(p, faction, x, y);
}

static uint32_t mix(uint32_t a) {
    a ^= a >> 16, a *= 0x7feb352du;
    a ^= a >> 15, a *= 0x846ca68bu;
    return a ^ (a >> 16);
}

// A square from a hash, of the given kind (land 0, sea 1, any -1) and
// region (or any, -1), or 0 if a few tries find none.
static int pick_square(uint32_t h, int sea, int region, int *x, int *y) {
    for (int k = 0; k < 24; k++) {
        uint32_t n = mix(h + (uint32_t)k * 0x9e3779b9u) % (uint32_t)*game_map_area;
        int yy = (int)n / *game_map_half_width, xx = (int)n % *game_map_half_width * 2 + (yy & 1);
        const struct map_tile *t = tile(xx, yy);
        if ((sea >= 0 && is_sea(t) != sea) || (region >= 0 && t->region != region)) continue;
        *x = xx, *y = yy;
        return 1;
    }
    return 0;
}

// Path_find over units of every faction: goals near and far, on and off
// their region, the flags' combinations, with and without the stale zone
// of control flag, for no faction, and the same goal again from another
// start over the table the original left (its shortcut). Units of each
// kind (land, sea, air) are taken in turn. Some calls play a human player.
static void sweep_find(struct game_path *p, int turn) {
    static const int flag_sets[] = {0xc0, 0xe0, 0x40, 0x80,  0,     1,    2,     4,    8,    0x10, 0x1d0, 0x11c0,
                                    0x200, 0x600, 0xa00, 0xe00, 0x90, 0x4c, 0x8a, 0x41, 0xd4, 0x2c0, 0x51, 0xc2};
    enum { FLAG_SETS = sizeof(flag_sets) / sizeof(flag_sets[0]) };
    int n = *game_veh_count;
    if (n <= 0) return;
    for (int k = 0, done = 0; k < 96 && done < 18; k++) {
        uint32_t h = mix((uint32_t)turn * 977u + (uint32_t)k);
        int v = (int)(h % (uint32_t)n);
        const struct veh *V = &game_vehs[v];
        if (!on_map(V->x, V->y)) continue;
        int triad = game_chassis[game_protos[V->proto].chassis].triad;
        if (triad != (done + turn) % 3 && k < 64) continue;  // each kind in turn while there are some
        done++;
        int x1 = V->x, y1 = V->y, x2 = x1, y2 = y1, region = tile(x1, y1)->region;
        switch ((h >> 3) % 5) {
        case 0:
            if (on_map(V->waypoint_x[0], V->waypoint_y[0])) {
                x2 = V->waypoint_x[0], y2 = V->waypoint_y[0];
                break;
            }
            // fall through
        case 1: pick_square(h >> 5, -1, -1, &x2, &y2); break;
        case 2:
            if (*game_base_count > 0) {
                const struct base *B = &game_bases[(h >> 7) % (uint32_t)*game_base_count];
                x2 = B->x, y2 = B->y;
            }
            break;
        case 3: pick_square(h >> 5, -1, region, &x2, &y2); break;
        default: {  // two or three squares away
            int i = (int)((h >> 9) % 24) + 1;
            int xx = xrange(x1 + game_radius_x[i]), yy = y1 + game_radius_y[i];
            if (on_map(xx, yy)) x2 = xx, y2 = yy;
        }
        }
        int flags = flag_sets[(k + turn) % FLAG_SETS];
        int faction = (h >> 11) % 13 == 0 ? -1 : (h >> 11) % 13 == 1 ? 0 : V->faction;
        int arg = flags & 0x200 ? (int)((h >> 15) % 8) : V->proto;
        int32_t stale = (h >> 19) & 1 ? (int32_t)(h | 1) : 0;
        int32_t a[8] = {x1, y1, x2, y2, arg, faction, flags, (int)((h >> 21) % 10) - 1};
        uint8_t human = *game_human_factions;
        if ((h >> 25) % 4 == 0 && faction > 0) *game_human_factions |= (uint8_t)(1u << faction);
        check_path_find(p, a, stale);
        // Again from another unit's square: the original's table first.
        if ((h >> 27) % 3 == 0) {
            hooks_nested++;
            path_stale_call((void *)orig_path_find, p, a, stale);
            hooks_nested--;
            const struct veh *O = &game_vehs[mix(h) % (uint32_t)n];
            if (on_map(O->x, O->y)) a[0] = O->x, a[1] = O->y;
            check_path_find(p, a, stale ^ 1);
        }
        *game_human_factions = human;
    }
}

// Path_move for units given a waypoint and their state's path bits.
static void sweep_move(struct game_path *p, int turn) {
    int n = *game_veh_count;
    for (int k = 0, done = 0; k < 64 && done < 6 && n > 0; k++) {
        uint32_t h = mix((uint32_t)turn * 131u + (uint32_t)k + 0x5000);
        int v = (int)(h % (uint32_t)n);
        struct veh *V = &game_vehs[v];
        if (!on_map(V->x, V->y)) continue;
        done++;
        int x = V->x, y = V->y;
        if ((h >> 4) & 1) {
            int i = (int)((h >> 5) % 24) + 1;
            int xx = xrange(x + game_radius_x[i]), yy = y + game_radius_y[i];
            if (on_map(xx, yy)) x = xx, y = yy;
        } else {
            pick_square(h >> 6, -1, (h >> 7) & 1 ? tile(x, y)->region : -1, &x, &y);
        }
        V->waypoint_x[0] = (int16_t)x, V->waypoint_y[0] = (int16_t)y;
        static const uint32_t states[] = {0, 0x60000, 0x1000200, 0x10000, 0x70000, 0x1010200};
        V->state = (V->state & ~0x1070200u) | states[(h >> 9) % 6];
        V->unk_29 = (uint8_t)((h >> 12) % 8);
        if ((h >> 15) % 3 == 0) V->moves_spent = (uint8_t)((h >> 17) % 8);
        check_path_move(p, v, (h >> 20) % 5 ? V->faction : (int)((h >> 22) % 8));
    }
}

// Path_sensors from each faction's bases and a square anywhere, and
// Path_zoc_path next to units, for every faction.
static void sweep_sensors(struct game_path *p, int turn) {
    for (int f = 1; f < FACTION_COUNT; f++) {
        int x = -1, y = -1;
        for (int b = 0, seen = 0; b < *game_base_count; b++)
            if (game_bases[b].faction == f && seen++ == turn % 3) x = game_bases[b].x, y = game_bases[b].y;
        if (x < 0 && !pick_square(mix((uint32_t)turn + (uint32_t)f), 0, -1, &x, &y)) continue;
        check_path_sensors(p, f, &x, &y);
    }
    int x, y;
    if (pick_square(mix((uint32_t)turn * 7u), -1, -1, &x, &y)) check_path_sensors(p, 1 + turn % 7, &x, &y);
    int n = *game_veh_count;
    for (int k = 0; k < 24 && n > 0; k++) {
        uint32_t h = mix((uint32_t)turn * 31u + (uint32_t)k + 0x9000);
        const struct veh *V = &game_vehs[h % (uint32_t)n];
        if (!on_map(V->x, V->y)) continue;
        int i = (int)((h >> 8) % 9);
        int xx = xrange(V->x + game_radius_x[i]), yy = V->y + game_radius_y[i];
        if (!on_map(xx, yy)) continue;
        uint8_t human = *game_human_factions;
        for (int f = 0; f < FACTION_COUNT; f++) {
            if ((h >> 12) & 1) *game_human_factions |= (uint8_t)(1u << f);
            check_path_zoc_path(p, xx, yy, f);
            *game_human_factions = human;
        }
    }
}

// Once a turn: the whole numbering again on the real map (which should
// come out as it is), a few regions numbered again from scratch, some
// merges, the coarse map, and each faction's territory from its bases;
// then the path finder itself on a protected copy of the game.
static void path_sweep(void) {
    struct game_path *p = game_path;
    if (!*game_map || !p->x || !p->y || !p->map) return;
    int sweeping = hooks_sweeping, turn = *game_turn;
    hooks_sweeping = 1;
    check_path_continents(p);
    check_path_make_abstract(p);
    int area = *game_map_area;
    uint8_t *regions = malloc((size_t)area);
    if (regions) {
        for (int i = 0; i < area; i++) regions[i] = (*game_map)[i].region;
        // Three squares' regions cleared and numbered again; the region is
        // given its old number, or one of the leftover numbers.
        for (int k = 0; k < 3; k++) {
            int n = (int)(((unsigned)turn * 2654435761u + (unsigned)k * 40503u) % (unsigned)area);
            int x = n % *game_map_half_width * 2, y = n / *game_map_half_width;
            x += y & 1;
            int r = regions[n];
            if (!r || y == 0 || y == *game_map_height - 1) continue;
            for (int i = 0; i < area; i++)
                if ((*game_map)[i].region == r) (*game_map)[i].region = 0;
            check_path_continent(p, x, y, k == 2 ? (r < REGION_SEA_FIRST ? REGION_LAND_POLES : REGION_SEA_POLES) : r);
            for (int i = 0; i < area; i++) (*game_map)[i].region = regions[i];
        }
        free(regions);
    }
    for (int k = 0; k < 4; k++) {
        int a = (turn + k * 17) % 128, b = (turn * 3 + k * 29) % 128;
        check_path_merge(p, a, b);
    }
    check_path_merge(p, 1, 1);
    // Territory, from each base's square, with the marks cleared first and
    // then as the previous fill left them.
    uint8_t *marks = malloc((size_t)area);
    if (marks) {
        for (int i = 0; i < area; i++) marks[i] = (*game_map)[i].unk_6, (*game_map)[i].unk_6 = 0;
        for (int b = turn % 3; b < *game_base_count; b += 3) {
            const struct base *B = &game_bases[b];
            check_path_territory(p, B->x, B->y, 0, B->faction);
            check_path_territory(p, B->x, B->y, b, (B->faction + 1) % FACTION_COUNT);
        }
        for (int i = 0; i < area; i++) (*game_map)[i].unk_6 = marks[i];
        free(marks);
    }
    struct hook_writes outer = {.protect_only = 1};
    writes_paths(&outer, p);
    hooks_writes_begin(&outer);
    if (turn & 1) *game_sunspot_duration = 2;
    sweep_find(p, turn);
    sweep_move(p, turn);
    sweep_sensors(p, turn);
    hooks_writes_abandon(&outer);
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook path_hooks[] = {
    HOOK(0x59a370, path_zoc_path, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),     // sub esp, 8
    HOOK(0x59a530, path_find, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x6c),         // sub esp, 108
    HOOK(0x59bc10, path_move, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x38),         // sub esp, 56
    HOOK(0x59c200, path_make_abstract, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x1c),  // sub esp, 28
    HOOK(0x59c340, path_merge, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x55, 0x0c),         // mov edx, [ebp+12]
    HOOK(0x59c3c0, path_territory, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0xf0, 0xfa, 0x68, 0x00),  // mov eax, [map_half_width]
    HOOK(0x59c520, path_continent, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),     // sub esp, 16
    HOOK(0x59c790, path_continents, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x28),    // sub esp, 40
    HOOK(0x59cca0, path_sensors, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x38),      // sub esp, 56
};
struct hook_module path_module = {"path", path_hooks, HOOK_COUNT(path_hooks), path_sweep, NULL, -1};
