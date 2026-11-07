// The path finder's map work: numbering the continents and seas, the
// coarse map the AI plans on, and flood fills of a faction's territory.
// The game keeps one path object (game_path) with a table the size of the
// map and two coordinate lists it uses as a queue. Reimplemented from
// terranx.exe 2.0's disassembly; names follow Thinker (MIT), which calls
// these methods of a class Path.
#include <stdlib.h>

#include "base.h"
#include "game.h"
#include "map.h"
#include "path.h"
#include "shadow.h"

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

// ---- Hooks and shadow checks ------------------------------------------------

extern struct hook_module path_module;
#define PATH (&path_module)

// The map, the coarse map, the regions and the game's state, and the path
// object's queue.
static void writes_paths(struct hook_writes *w, struct game_path *p) {
    hooks_writes_game(w);
    size_t area = (size_t)*game_map_area;
    hooks_writes_add(w, *game_map, area * sizeof(struct map_tile));
    hooks_writes_add(w, *game_abstract_map, (size_t)(*game_abstract_width >> 1) * *game_abstract_height);
    if (p->x) hooks_writes_add(w, p->x, area * 2);
    if (p->y) hooks_writes_add(w, p->y, area * 2);
}

// SHADOW_WRITER for the path object's methods, which take it in ecx.
#define PATH_WRITER(name, params, args, fmt, ...)                                              \
    static void(__attribute__((thiscall)) * orig_##name) params;                               \
    static struct hook_stats stats_##name = {#name};                                           \
    static void check_##name params {                                                          \
        struct hook_writes w_ = {0};                                                           \
        writes_paths(&w_, p);                                                                  \
        hooks_writes_begin(&w_);                                                               \
        hooks_nested++;                                                                        \
        orig_##name args;                                                                      \
        hooks_nested--;                                                                        \
        hooks_writes_middle(&w_);                                                              \
        name args;                                                                             \
        const char *diff_ = hooks_writes_end(&w_, 0, 0);                                       \
        if (diff_) hooks_mismatch(&stats_##name, "(" fmt ") %s", __VA_ARGS__, diff_);          \
        hooks_ui_release();                                                                    \
    }                                                                                          \
    static void __attribute__((thiscall)) shadow_##name params {                               \
        if (hooks_nested) return orig_##name args;                                             \
        if (shadow_bypass()) return name args;                                                 \
        stats_##name.calls++;                                                                  \
        shadow_tick(PATH);                                                                     \
        check_##name args;                                                                     \
    }

PATH_WRITER(path_make_abstract, (struct game_path * p), (p), "%s", "")
PATH_WRITER(path_merge, (struct game_path * p, int a, int b), (p, a, b), "%d, %d", a, b)
PATH_WRITER(path_territory, (struct game_path * p, int a, int b, int c, int d), (p, a, b, c, d), "%d, %d, %d, %d", a, b, c, d)
PATH_WRITER(path_continent, (struct game_path * p, int a, int b, int c), (p, a, b, c), "%d, %d, %d", a, b, c)
PATH_WRITER(path_continents, (struct game_path * p), (p), "%s", "")

// Once a turn: the whole numbering again on the real map (which should
// come out as it is), a few regions numbered again from scratch, some
// merges, the coarse map, and each faction's territory from its bases.
static void path_sweep(void) {
    struct game_path *p = game_path;
    if (!*game_map || !p->x || !p->y) return;
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
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook path_hooks[] = {
    HOOK(0x59c200, path_make_abstract, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x1c),  // sub esp, 28
    HOOK(0x59c340, path_merge, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x55, 0x0c),         // mov edx, [ebp+12]
    HOOK(0x59c3c0, path_territory, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0xf0, 0xfa, 0x68, 0x00),  // mov eax, [map_half_width]
    HOOK(0x59c520, path_continent, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),     // sub esp, 16
    HOOK(0x59c790, path_continents, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x28),    // sub esp, 40
};
struct hook_module path_module = {"path", path_hooks, HOOK_COUNT(path_hooks), path_sweep, NULL, -1};
