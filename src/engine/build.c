// What a base builds: base_build(), the AI's (and the governor's) choice
// of a base's next unit, facility or secret project. Reimplemented from
// terranx.exe 2.0's disassembly; Thinker (MIT) names the function and many
// of the game's fields, but replaces it with its own production code
// rather than describing it, so everything here follows the original.
//
// The function scores every unit the base can build, every facility and
// every secret project, and picks the lowest score. Scores start from an
// item's cost and are halved, doubled and divided by how much the faction
// wants what the item does. Many of the original's quirks are kept: some
// of its working values outlive the loop that set them and change how
// later items are scored, and a few read past the tables they index.
//
// base_reset() asks base_build() and sets the answer with base_change(),
// and bases_reset() does so for many bases; base_making() classes items
// for the retooling penalty. These too follow the disassembly. Thinker's
// base_change is close to the original but adds checks of the base and
// the item that the original doesn't make.
#include <string.h>

#include "base.h"
#include "build.h"
#include "game.h"
#include "map.h"
#include "shadow.h"
#include "tech.h"

enum {
    TRIAD_COUNT = 3, PLAN_SLOTS = 18,  // plans 0-14, with 15 (small transports), 16 (artillery), 17 (no goals)
    ABL_AMPHIBIOUS = 0x8, ABL_DROP_POD = 0x10, ABL_AIR_SUPERIORITY = 0x20, ABL_AAA = 0x100, ABL_COMM_JAMMER = 0x200,
    ABL_ARTILLERY = 0x8000, ABL_TRANCE = 0x40000, ABL_HEAVY_TRANSPORT = 0x80000,
    GOV_ACTIVE = 0x80000000u, GOV_EXPLORE = 0x1000000, GOV_DISCOVER = 0x2000000, GOV_BUILD = 0x4000000,
    GOV_CONQUER = 0x8000000, GOV_PRIORITIES = 0xf000000,
    FAC_PERIMETER_DEFENSE = 4, FAC_TACHYON_FIELD = 5, FAC_SKUNKWORKS = 10, FAC_COMMAND_CENTER = 27, FAC_NAVAL_YARD = 28,
    FAC_BIOENHANCEMENT = 30, FAC_PSI_GATE = 33, FAC_COVERT_OPS = 34, FAC_FLECHETTE_DEFENSE = 39,
    FAC_SUBSPACE_GENERATOR = 40, FAC_NESSUS_MINING = 66, FAC_ORBITAL_POWER = 67,
    FIRST_PROJECT = 70, ITEM_ASCENT = -102,
    SP_CLOUDBASE = 35, SP_MARITIME = 10, SP_CITIZENS_DEFENSE = 5, SP_ASCETIC = 13,
    SP_HUNTER_SEEKER = 15, PLAN_NAVAL_SUPERIORITY = 6, PLAN_TERRAFORM = 9, PLAN_PROBE = 11,
};

static inline int human(int faction) { return (*game_human_factions >> faction) & 1; }
static inline struct map_tile *tile(int x, int y) { return &(*game_map)[(x >> 1) + y * *game_map_half_width]; }
static inline char *text(uint32_t addr) { return (char *)game_at(addr); }
static inline int built(int base, int fac) {  // the inline test the original repeats
    return fac < 65 ? (game_bases[base].facilities_built[fac >> 3] >> (fac & 7)) & 1 : 0;
}
static inline int treaty(int faction, int other, uint32_t bits) { return game_factions[faction].diplo_status[other] & bits; }
static inline int triad_of(int proto) { return game_chassis[game_protos[proto].chassis].triad; }

// garrison_check() less one or two for larger garrisons.
static int defensive_check(int base) {
    int n = game_garrison_check(base);
    if (n > 5) n--;
    if (n > 2) n--;
    return n;
}

// The defenders a region's bases call for, among the units the goal pass
// keeps back.
static int guard_check(int faction, int region) {
    if (region >= 64) return 0;
    const struct faction_data *F = &game_factions[faction];
    int c = 2 - F->ai_fight;
    if (F->region_base_plan[region] == PLAN_COLONY) c += 2;
    else if (F->region_base_plan[region] == PLAN_DEFENSIVE) c = 1;
    if (F->flags & 0x40000000) c = 1;
    return (F->region_total_bases[region] + c - 1) / c;
}

// Whether base_reset() already picked this item, scoring at least as well,
// for a governor priority the base doesn't have.

// The base's squares that may lie off the map edge, wrapped.
static int wrap_x(int x) {
    if (!(*game_map_flat & 1)) {
        if (x < 0) x += *game_map_width;
        else if (x >= *game_map_width) x -= *game_map_width;
    }
    return x;
}
static int off_map(int x, int y) { return y < 0 || y >= *game_map_height || x < 0 || x >= *game_map_width; }

void (*build_compute)(int update) = base_compute;

int base_build(int base, int *second, int *third, int *score_out) {
    int neg = 0;  // a negative base: a quick pick, without projects
    if (base < 0) neg = 1, base = -1 - base;
    *game_current_base_id = base;
    *game_current_base = &game_bases[base];
    build_compute(1);
    struct base *CB = *game_current_base;
    const int bx = game_bases[base].x, by = game_bases[base].y, f = game_bases[base].faction;
    const struct map_tile *bt = tile(bx, by);
    const int ocean = (bt->climate & 0xe0) < 0x60;
    const int region = bt->region;
    struct faction_data *F = &game_factions[f];
    const int plan = F->region_base_plan[region];
    const int reactor = game_best_reactor(f);
    const int fbit = 1 << f;
    const int hf = human(f);

    // Priorities: the AI's own, or the governor's.
    int pg, pt, pw, pp, fight, gexp, gdis, gbld, gcon;
    if (hf && (CB->governor & GOV_ACTIVE)) {
        uint32_t g = CB->governor;
        if (!(g & 0xf200000) && *game_current_base_id >= 8) {  // the base id, as the original has it
            int p = F->region_base_plan[region];
            if (p <= 2) g |= GOV_CONQUER;
            if (p == PLAN_COLONY) g |= GOV_EXPLORE;
            if (p == PLAN_NAVAL_TRANSPORT) g |= GOV_DISCOVER;
            g |= GOV_EXPLORE << (*game_current_base_id & 3);
        }
        pg = g >> 24 & 1, pt = g >> 25 & 1, pw = g >> 26 & 1, pp = g >> 27 & 1;
        fight = pp ? 1 : pg ? 0 : (!pt && !pw) ? F->ai_fight : -1;
        gexp = pg, gdis = pt, gbld = pw, gcon = pp;
        if (gexp && gdis && gbld && gcon) gexp = gdis = gbld = gcon = 0;
    } else {
        pg = F->ai_growth, pt = F->ai_tech, pw = F->ai_wealth, pp = F->ai_power, fight = F->ai_fight;
        gexp = gdis = gbld = gcon = 0;
    }
    int regions_with_bases = 0;
    for (int r = 0; r < 128; r++)
        if (F->region_total_bases[r]) regions_with_bases++;

    // Counts by prototype and by plan and triad.
    static int units_building[PROTO_COUNT], units_building_region[PROTO_COUNT], units_region[PROTO_COUNT];
    int plan_triad[54] = {0}, plan_triad_region[54] = {0}, units_all[54] = {0}, units_region_pt[54] = {0};
    memset(units_building, 0, sizeof units_building);
    memset(units_building_region, 0, sizeof units_building_region);
    memset(units_region, 0, sizeof units_region);
    int triad_building[TRIAD_COUNT] = {0};
    int v14c = 0, v134 = 0, v11c = 0, v118 = 0, vac = 0, vec = 0, v104 = 0;
    const int has_skunkworks = built(*game_current_base_id, FAC_SKUNKWORKS);
    int va0 = 0, v120 = 0, vdc = 0, vd4 = 0, v158 = 0, v144 = 0, v154 = 0, va8 = 0;
    int war_plan7 = 0, v140 = 0;
    int threat = plan == 0 || plan == 2;

    int pact = -1;  // a pact partner: the last human, else the first
    for (int i = 1; i < FACTION_COUNT; i++)
        if (f != i && (F->diplo_status[i] & 1) && (human(i) || pact < 0)) pact = i;
    int enemy_here = -1, enemy_away = -1, seen = 0;
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (f == i) continue;
        uint32_t st = F->diplo_status[i];
        int vend = st & TREATY_VENDETTA;
        if (vend || (human(i) && (st & TREATY_COMMLINK) && !(st & 1)))
            for (int j = 0; j < *game_base_count; j++)
                if (game_bases[j].faction == f && (game_bases[j].visibility & 1 << i)) {
                    seen = 1;
                    break;
                }
        if (!vend) continue;
        if (plan == PLAN_NAVAL_TRANSPORT) war_plan7 = 1;
        const struct faction_data *I = &game_factions[i];
        if ((pact >= 0 && (game_factions[pact].diplo_status[i] & TREATY_VENDETTA)) || (human(i) && (*game_rules & 0x40))
            || ((st & 0x20) && I->diff_level >= 3) || (st & 0x40) || (I->ranking > F->ranking && fight >= 0)) {
            if (plan == PLAN_NAVAL_TRANSPORT) threat = 1;
            if (!I->region_total_bases[region]) {
                if (plan == PLAN_NAVAL_TRANSPORT) enemy_away = i;
            } else
                enemy_here = i;
        }
    }
    if (plan == PLAN_NAVAL_TRANSPORT && war_plan7 && (*game_state & 0x200)) threat = 2;
    int target = 0;
    if (F->target_x >= 0) {
        if (tile(F->target_x, F->target_y)->region == region) target = 1;
        if (bx == F->target_x && by == F->target_y) target = 2;
    }
    int sites = 0;
    for (int s = 0; s < 25; s++) {
        const struct goal *g = &F->sites[s];
        if (g->type == 3 && g->priority > 0 && tile(g->x, g->y)->region == region) {
            int o = whose_territory(f, g->x, g->y, NULL, 0);
            if (o < 0 || o == f) sites++;
        }
    }
    const int retool = game_rules_basic->retool_exemption;
    int drones = 0;
    if (CB->drone_total) {
        if (CB->drone_total >= CB->talent_total) drones = 1;
        if (!(hf && CB->minerals > retool)
            && ((CB->state & 2) || (drones && CB->specialist_total - CB->specialist_adjust > 1)))
            drones++;
    }
    const int min_surplus = CB->surplus[1];
    const int min_score = CB->intake_2[1] + 2 * min_surplus;
    int on_project;
    if (score_out) on_project = CB->production_last <= -FIRST_PROJECT && CB->minerals > retool;
    else on_project = CB->queue[0] <= -FIRST_PROJECT;
    int proj_group[8] = {0}, proj_building[64] = {0};
    int projects_here = 0;
    for (int i = 0; i < 64; i++) {
        int pb = game_secret_projects[i];
        if (pb == *game_current_base_id) projects_here++;
        if (pb >= 0 && game_bases[pb].faction == f) proj_group[i / 8]++;
    }
    if (on_project && CB->surplus[0] > 0 && min_surplus > 0
        && CB->specialist_total - CB->specialist_adjust <= CB->pop_size / 4 + 1)
        drones = 1;

    // The bases.
    int v15c = 24, v164 = 24, v6c = 0, vd0 = 0, v160 = 0, vc4 = 0, vd8 = 0;
    int j = 0;  // its final value is read later (quirk)
    for (; j < *game_base_count; j++) {
        const struct base *J = &game_bases[j];
        const int fb = J->faction;
        if (fb != f) {
            if (F->attack_target_base == j && (F->diplo_status[fb] & TREATY_VENDETTA)) {
                if (tile(J->x, J->y)->region == region) v104 |= 2;
                else if (plan == PLAN_NAVAL_TRANSPORT) v104 |= 1;
            }
            int q = J->queue[0];
            if (q == ITEM_ASCENT && human(fb)) v154++;
            if (q <= -FIRST_PROJECT) proj_building[-q - FIRST_PROJECT] |= 1 << fb;
            if (human(fb) && J->faction_former == f && tile(J->x, J->y)->region == region && !(F->diplo_status[fb] & 1))
                if (++va0 > 1 - fight) threat = 1;
            continue;
        }
        if (J->pop_size > vd8) vd8 = J->pop_size;
        if ((tile(J->x, J->y)->climate & 0xe0) < 0x60) va8++;
        int dist = vector_dist(bx, by, J->x, J->y);
        if (built(j, FAC_NAVAL_YARD) && dist < v15c) v15c = dist;
        if (built(j, FAC_AEROSPACE_COMPLEX)
            || (game_secret_projects[SP_CLOUDBASE] >= 0 && game_bases[game_secret_projects[SP_CLOUDBASE]].faction == J->faction))
            if (dist < v164) v164 = dist;
        if (built(j, FAC_PUNISHMENT_SPHERE)) v140++;
        if (built(j, 1) || J->queue[0] == -1) v14c++;
        if (j == *game_current_base_id) continue;
        if (J->state & 0x100000) vd0++;
        if (J->pop_size > v144) v144 = J->pop_size;
        int q = J->queue[0];
        if (q == CB->queue[0]) {
            v120++;
            if (q <= -FIRST_PROJECT) {
                if (CB->mineral_surplus_final <= 0) {
                    if (J->mineral_surplus_final > 0) vdc++;
                } else if (J->mineral_surplus_final > 0) {
                    int cost = cost_factor(CB->faction, 1, -1) * game_facilities[-q].cost;
                    if ((cost - J->minerals) / J->mineral_surplus_final < (cost - CB->minerals) / CB->mineral_surplus_final) vdc++;
                }
            }
        }
        if (q <= -FIRST_PROJECT) proj_building[-q - FIRST_PROJECT] |= fbit;
        // The original looks up a prototype even for a facility (q < 0),
        // reading before the table.
        const struct unit_proto *P = &game_protos[q];
        int pl = P->plan, tri = game_chassis[P->chassis].triad;
        if (pl == PLAN_NAVAL_TRANSPORT && (P->carry < 2 || P->carry < F->naval / 0x400000)) pl = 15;
        if (tri == TRIAD_LAND && game_has_abil(q, ABL_ARTILLERY)) pl = 16;
        if (q >= 0) {
            plan_triad[3 * pl + tri] += (P->reactor >= reactor) + 1;
            units_building[q]++;
        }
        if (tile(J->x, J->y)->region != region) continue;
        if (J->state & 1) v6c = 1;
        if (q <= -FIRST_PROJECT) vd4++;
        if (built(j, FAC_COMMAND_CENTER) || q == -FAC_COMMAND_CENTER) v11c++;
        if (built(j, FAC_BIOENHANCEMENT) || q == -FAC_BIOENHANCEMENT) v134++;
        if (built(j, FAC_NAVAL_YARD) || q == -FAC_NAVAL_YARD) v118++;
        if (built(j, FAC_AEROSPACE_COMPLEX) || q == -FAC_AEROSPACE_COMPLEX) vac++;
        if (built(j, FAC_SKUNKWORKS) || q == -FAC_SKUNKWORKS) vec++;
        if (q >= 0) {
            units_building_region[q]++;
            plan_triad_region[3 * pl + tri] += (P->reactor >= reactor) + 1;
            triad_building[tri]++;
            if (q >= 64 && !(P->flags & PROTO_FLAG_BUILT)) v158++;
        }
        // Bases in the region that lack a Command Center when this one has one.
        if (!built(*game_current_base_id, FAC_COMMAND_CENTER) || built(j, FAC_COMMAND_CENTER)) continue;
        int v = game_stack_fix(game_veh_at(J->x, J->y));
        int gar = game_garrison_check(j);
        int def = defensive_check(j);
        int s5c = game_stack_check(v, 11, 0x10000000, 0x10000000, f);
        int n = game_stack_check(v, 2, 2, f, -1);
        int c;
        if (n < 0 || def < 0) c = 0;
        else {
            c = n > def ? def : n;
            if (c && s5c) c++;
        }
        if (F->region_base_plan[region] == PLAN_DEFENSIVE || F->region_base_plan[region] == PLAN_COLONY)
            c += game_stack_check(v, 2, 1, f, -1);
        if (F->best_armor_value < 2) c += game_stack_check(v, 2, 3, -1, -1);
        else if (!c) c = game_stack_check(v, 2, 3, -1, -1) != 0;
        int here = 0;
        for (int k = 0; k < *game_veh_count; k++) {
            const struct veh *V = &game_vehs[k];
            if (V->faction != f) continue;
            if (game_protos[V->proto].plan != PLAN_DEFENSIVE && !(V->state & 0x10000000)) continue;
            const struct map_tile *vt = tile(V->x, V->y);
            if (vt->region != region) continue;
            if ((vt->bits & MAP_BASE) && (vt->owner_site & 0xf) < 8) continue;
            int hb = V->home_base;
            if (hb != j && hb != *game_current_base_id) continue;
            if (hb == *game_current_base_id) {
                if (++here > vc4) continue;
                vc4++;
            }
            c++;
        }
        if (c < gar) {
            int bq = J->queue[0];
            if (!s5c && gar != def) {
                if (bq >= 0 && game_protos[bq].plan <= 1 && ++c >= gar) continue;
            } else if (bq >= 0 && game_protos[bq].plan == PLAN_DEFENSIVE && ++c >= gar)
                continue;
            v160++;
        }
    }
    if (CB->state & 1) v6c = 2;

    // The faction's units.
    int my_units = 0, trance_scouts = 0, roaming = 0;
    for (int k = 0; k < *game_veh_count; k++) {
        const struct veh *V = &game_vehs[k];
        if (V->faction != f) continue;
        my_units++;
        const struct unit_proto *P = &game_protos[V->proto];
        int pl = P->plan, tri = game_chassis[P->chassis].triad;
        if (pl == PLAN_NAVAL_TRANSPORT && (game_veh_cargo(k) < 2 || game_veh_cargo(k) < F->naval / 0x400000)) pl = 15;
        if (tri == TRIAD_LAND && game_has_abil(V->proto, ABL_ARTILLERY)) pl = 16;
        if (P->group == 21) pl = 17;
        if (V->state & 0x10000000) pl = PLAN_DEFENSIVE;
        int idx = 3 * pl + tri;
        units_all[idx] += (P->reactor >= reactor) + 1;
        if (V->home_base == *game_current_base_id) {
            if (P->plan == PLAN_RECON && game_has_abil(V->proto, ABL_TRANCE) && tile(V->x, V->y)->region == region)
                trance_scouts++;
        }
        const struct map_tile *vt = tile(V->x, V->y);
        if (vt->region != region) continue;
        units_region[V->proto]++;
        units_region_pt[idx] += (P->reactor >= reactor) + 1;
        if (!((vt->bits & MAP_BASE) && (vt->owner_site & 0xf) < 8) && pl <= 3) roaming++;
    }
    for (int i = 0; i < 54; i++) plan_triad[i] /= 2, plan_triad_region[i] /= 2, units_all[i] /= 2, units_region_pt[i] /= 2;
    vc4 = 0;  // now the transports it wants
    for (int r = 0; r < 64; r++)
        if (F->region_total_bases[r] >= 3) vc4++;
    if (F->base_count >= 10) vc4++;
    if (F->base_count >= 20) vc4++;
    vc4 += (va8 + 2) / 3;
    if (has_project(SP_MARITIME, f)) vc4 = 2 * vc4 + 1;
    if (F->se_pending[SE_POLICE] + 2 * built(*game_current_base_id, FAC_BROOD_PIT) <= -3) vc4 = (vc4 + 1) / 2;
    if (F->se_pending[SE_POLICE] + 2 * built(*game_current_base_id, FAC_BROOD_PIT) <= -5) vc4 = vc4 / 2;

    // The base's squares: rival units near, and forests.
    int enemies_near = 0, forests = 0;
    for (int i = 0; i < 21; i++) {
        int x2 = wrap_x(bx + game_radius_x[i]), y2 = by + game_radius_y[i];
        if (off_map(x2, y2)) continue;
        if (tile(x2, y2)->bits & MAP_FOREST) forests += (CB->worked_tiles & 1u << i) ? 2 : 1;
        int v = game_veh_at(x2, y2);
        if (v >= 0 && game_vehs[v].faction != f && (F->diplo_status[game_vehs[v].faction] & 0x30))
            enemies_near += game_stack_check(v, 1, -1, -1, -1);
    }
    // The base's own garrison.
    int gv = game_stack_fix(game_veh_at(CB->x, CB->y));
    const int gar = game_garrison_check(*game_current_base_id);
    const int def = defensive_check(*game_current_base_id);
    const int s5c = game_stack_check(gv, 11, 0x10000000, 0x10000000, f);
    int have;
    {
        int n = game_stack_check(gv, 2, 2, f, -1);
        if (n < 0 || def < 0) have = 0;
        else {
            have = n > def ? def : n;
            if (have && s5c) have++;
        }
    }
    if ((CB->state & 0x4000000) && F->region_base_plan[region] > 2) have++;
    if (F->region_base_plan[region] == PLAN_DEFENSIVE || F->region_base_plan[region] == PLAN_COLONY)
        have += game_stack_check(gv, 2, 1, f, -1);
    if (F->best_armor_value < 2) have += game_stack_check(gv, 2, 3, -1, -1);
    else if (!have) have = game_stack_check(gv, 2, 3, -1, -1) != 0;
    game_stack_check(gv, 2, 7, f, -1);  // results unused
    game_stack_check(gv, 2, 6, f, -1);
    if (fbit & game_protos[2].obsolete_factions) {
        int s = game_stack_check(gv, 0, 2, f, -1);
        have -= s < 0 ? 0 : s > 1 ? 1 : s;
    }
    if (F->region_base_plan[region] > 2 && (CB->state & 0x8000000)) have++;
    int need = have < 1 ? 3 : have < gar - 1 ? 2 : have < gar;
    if (hf && CB->minerals > retool && !(CB->state & 8) && (gexp || gdis || gbld || gcon)) {
        if (gcon || gexp) {
            if (need > 1) need = 1;
        } else
            need = 0;
    } else if (plan == PLAN_COLONY || (plan == PLAN_NAVAL_TRANSPORT && (F->base_count < 4 || !seen)) || (hf && !gcon)) {
        int peaceful = 1;
        for (int i = 1; i < FACTION_COUNT; i++) {
            const struct faction_data *I = &game_factions[i];
            if (f == i || (!I->region_total_combat_units[region] && !I->region_total_bases[region])) continue;
            uint32_t st = F->diplo_status[i];
            if (st & 0x30) peaceful = 0;
            else if (human(i) && (I->diplo_status[f] & TREATY_COMMLINK) && (!(st & TREATY_TREATY) || I->integrity_blemishes))
                peaceful = 0;
        }
        if (need && peaceful
            && ((hf && !gcon) || (CB->pop_size < 3 && (pg || F->base_count < 3) && !(CB->state & 0x1000000)))) {
            need--;
            for (int i = 0; i < 121; i++) {
                int x2 = wrap_x(CB->x + game_radius_x[i]), y2 = CB->y + game_radius_y[i];
                if (off_map(x2, y2)) continue;
                const struct map_tile *t2 = tile(x2, y2);
                if (t2->region != region) continue;
                if (!(F->flags & 0x200) && !(t2->visibility & fbit)) continue;
                if (!(t2->bits & (MAP_BASE | MAP_UNIT))) continue;
                int o = t2->owner_site & 0xf;
                if (o >= 8 || o == f) continue;
                if (o == 0 || (F->diplo_status[o] & 0x30)) {
                    need++;
                    break;
                }
            }
        }
    }
    // Anything unseen, foreign or unfriendly near the base.
    int frontier = 0;
    for (int i = 0; i < 49; i++) {
        int x2 = wrap_x(CB->x + game_radius_x[i]), y2 = CB->y + game_radius_y[i];
        if (off_map(x2, y2)) continue;
        const struct map_tile *t2 = tile(x2, y2);
        if (t2->region != region) continue;
        if (!(F->flags & 0x200) && !(t2->visibility & fbit)) {
            frontier = 1;
            break;
        }
        int o = t2->owner_site & 0xf;
        if ((t2->bits & (MAP_BASE | MAP_UNIT)) && o < 8 && o > 0 && o != f) {
            frontier = 1;
            break;
        }
        int w = whose_territory(f, x2, y2, NULL, 0);
        if (w <= 0 || w == f) continue;
        if (!(F->diplo_status[w] & (human(w) ? 1 : TREATY_TREATY))) {
            frontier = 1;
            break;
        }
    }

    // The choices: [0] the pick, [1] the best non-combat item, [2] the
    // best combat item; lower scores are better.
    int choice[3] = {-69, 99999, 99999};
    int best[3] = {999999, 999999, 999999};
    int v9c = fbit & *game_human_factions;  // stays so when no unit is looked at (quirk)
    int v78 = 0, v18 = 0, combat = 0;
    if (F->total_combat_units < 512) {
        v9c = 999999;
        for (int u = 0; u < PROTO_COUNT; u++) {
            if (!game_veh_avail(u, f, *game_current_base_id)) continue;
            const struct unit_proto *P = &game_protos[u];
            const int tri = game_chassis[P->chassis].triad;
            int pl = P->plan;
            if (tri == TRIAD_LAND && game_has_abil(u, ABL_ARTILLERY)) pl = 16;
            v78 = 0;
            combat = P->plan < 8;
            int score = (P->cost + 2) * 256;
            int m = v18;  // [-0x18]: only a sea or air unit's case sets it before it is read
            if (tri == TRIAD_AIR) {
                if (plan != PLAN_NAVAL_TRANSPORT && !ocean
                    && F->region_total_combat_units[region] < 2 * F->region_total_bases[region] + 2)
                    continue;
                int a48 = plan_triad[3 * pl + 2], ab8 = units_all[3 * pl + 2];
                int s = (a48 + ab8 != 0) + 1;
                if (plan != PLAN_NAVAL_TRANSPORT && pl != PLAN_PLANET_BUSTER && !ocean) {
                    if (F->target_x >= 0) s++;
                    if (plan == PLAN_DEFENSIVE) s++;
                    if (F->region_flags[region] & 0x400) s++;
                    s += triad_building[2];
                }
                score *= s;
                if (built(*game_current_base_id, FAC_AEROSPACE_COMPLEX)) score = (vac + 2) * score / (vac + 3);
                if (!hf && (F->flags & 0x10000000)) score /= 2;
                m = ((pl != PLAN_AIR_SUPERIORITY) + 1) * a48 + game_range(s - 2, 0, 99) + ab8;
                int r8 = game_range(8 - regions_with_bases, 4, 8) * score / 8;
                int cost = game_veh_cost(u, *game_current_base_id, 0) * cost_factor(f, 1, -1) * r8;
                score = game_range(cost / (game_range(CB->surplus[1], 1, 999) * 15), r8, 10 * r8);
                if (ocean) score /= 2;
            } else if (tri == TRIAD_SEA) {
                if (plan != PLAN_NAVAL_TRANSPORT && !ocean
                    && F->region_total_combat_units[region] < 2 * F->region_total_bases[region] + 2)
                    continue;
                if (!is_coast(CB->x, CB->y, 0)) continue;
                int bc = base_coast(*game_current_base_id);
                if ((bc & 0x3f) == 0x3f || !(bc & 0x3f)) continue;
                if (sea_coasts(bc) < 2 && !ocean && (pl == PLAN_NAVAL_TRANSPORT || game_continents[bc].tile_count < 48)) continue;
                m = units_all[3 * pl + 1] + 2 * plan_triad[3 * pl + 1];
                if (plan != PLAN_NAVAL_TRANSPORT && !ocean) {
                    if (plan == PLAN_DEFENSIVE) m++;
                    if (F->region_flags[region] & 0x400) m++;
                    m += triad_building[1];
                }
                if (!hf && (F->flags & 0x8000000)) score /= 2;
                if (!hf && (game_faction_defs[f].rule_flags & RULE_FLAG_AQUATIC)) score /= 2, regions_with_bases++;
                int s = game_range(8 - regions_with_bases, 4, 8) * score / 8;
                int t = game_veh_cost(u, *game_current_base_id, 0) * cost_factor(f, 1, -1) * s
                      / (15 * game_range(CB->surplus[1], 1, 999));
                score = game_range(t, s, 10 * s);
            } else if (tri == TRIAD_LAND) {
                if (game_weapons[P->weapon].mode <= 2) {
                    int cap = 99;
                    if (P->plan <= 1) {
                        if (F->target_units) cap = 1;
                        else if (plan == PLAN_DEFENSIVE) cap = (F->region_flags[region] & 0x400) ? 1 : 2;
                        else if (F->region_flags[region] & 0x600) cap = 2;
                    }
                    int extra = (plan == PLAN_DEFENSIVE || (F->region_flags[region] & 0x400)) ? 0 : plan_triad_region[3 * pl];
                    int c = units_region[u] + plan_triad[3 * pl] + units_region_pt[3 * pl];
                    c = (c < 0 || cap < 0) ? 0 : c > cap ? cap : c;
                    m = (units_building_region[u] + c + extra + 1) / 2;
                    if (built(*game_current_base_id, FAC_COMMAND_CENTER)) score = (v11c + 2) * score / (v11c + 3);
                    if (!hf && (F->flags & 0x4000000)) score /= 2;
                } else if (pl == PLAN_COLONY && plan == PLAN_COLONY && !(CB->state & 0x2000000) && *game_difficulty >= 3) {
                    m = 0;
                } else {
                    m = units_region_pt[3 * pl];
                    if (!(pl == PLAN_COLONY && plan == PLAN_COLONY && !(CB->state & 0x1000000) && *game_difficulty >= 3)) {
                        m += units_building_region[u] + plan_triad_region[3 * pl];
                        if (pl == PLAN_COLONY) m += plan_triad[24];
                    }
                }
            }
            m++;
            int skip = 0;
            switch (P->plan) {
            case PLAN_OFFENSIVE:
                score *= (plan <= 2 || ((plan == PLAN_NAVAL_TRANSPORT || ocean) && war_plan7)) ? 32 : 64;
                if (have && !s5c && F->region_offensive[region] < guard_check(f, region)) score /= 2;
                if (v104 & 2) score /= 2;
                else if (v104 & 1) score = score * 3 / 4;
                if (ocean && tri == TRIAD_LAND) score *= 2;
                if (gcon && (tri == TRIAD_AIR || !ocean)) {
                    score /= 2;
                    if (*game_turn < 50) score /= 2;
                }
                if (tri == TRIAD_AIR && plan != PLAN_DEFENSIVE) {
                    int mx = F->enemy_air_attackers > F->unk_84c ? F->enemy_air_attackers : F->unk_84c;
                    int own = plan_triad[3 * pl + 2] + units_all[3 * pl + 2];
                    if (mx > own) score /= 2;
                    if (2 * mx > own) score /= 2;
                }
                if (!hf && game_chassis[P->chassis].missile && (F->flags_ext & 0x10)) score /= 3;
                break;
            case PLAN_COMBAT:
                score *= (plan <= 2 || ((plan == PLAN_NAVAL_TRANSPORT || ocean) && war_plan7)) ? 24 : 32;
                if (have && !s5c && F->region_offensive[region] < guard_check(f, region)) score /= 2;
                if (ocean && tri == TRIAD_LAND) score *= 2;
                if (gcon && !ocean) {
                    score /= 2;
                    if (*game_turn > 50) score /= 2;
                }
                break;
            case PLAN_DEFENSIVE:
                score *= 16;
                if (v160 && !need && has_fac_built(FAC_COMMAND_CENTER)) score /= 2;
                if (have > 1 && !s5c && F->region_offensive[region] < guard_check(f, region)) score *= 2;
                if (game_has_abil(u, ABL_TRANCE) && !game_stack_check(gv, 6, ABL_TRANCE, -1, -1) && F->clean_minerals >= 4) {
                    score = 2 * score / 3;
                    if (F->se_pending[SE_MORALE] <= 0) score = 2 * score / 3;
                }
                if (game_has_abil(u, ABL_AAA)) {
                    if (CB->state & 0x100000) score /= 2;
                    else {
                        if (game_stack_check(game_veh_at(CB->x, CB->y), 6, ABL_AAA, -1, -1)) score *= 3;
                        if (vd0) score /= 2;
                        else if (!F->enemy_air_attackers && !F->enemy_missiles) score *= 4;
                    }
                }
                if (gcon || gdis || gexp) {
                    if (*game_turn > 25) score *= 2;
                    if (*game_turn > 75) score *= 2;
                }
                break;
            case PLAN_RECON: {
                int trance = game_has_abil(u, ABL_TRANCE);
                if (!need && (trance_scouts || !trance)) {
                    int lim;
                    if (gexp || plan) {
                        if (plan == PLAN_DEFENSIVE) v78 = 1;
                        lim = plan == PLAN_NAVAL_TRANSPORT ? 1 : (game_continents[region].tile_count + 199) / 200;
                    } else {
                        if (units_region[u] || units_building_region[u]) v78 = 1;
                        lim = (game_continents[region].tile_count + 199) / 200;
                    }
                    if (units_region[u] + 2 * units_building_region[u] > lim && plan != PLAN_COLONY) v78 = 1;
                    if (units_region[u] > F->base_count - game_range(*game_turn / 50, 0, F->base_count - 1)) v78 = 1;
                }
                if ((plan == PLAN_COLONY || !roaming) && roaming < sites + 2 * gexp && (gexp || !(gcon || gbld || gdis))
                    && roaming < (F->base_count + 1) / (gexp ? 1 : 2))
                    score *= 16;
                else if (trance && !trance_scouts) {
                    score *= 32;
                    if (CB->eco_damage) {
                        if (plan != PLAN_DEFENSIVE) {
                            score /= 2;
                            if (plan > 2) score /= 2;
                        }
                    } else if (plan > 2)
                        score /= 2;
                } else
                    score *= (!need && !gexp && F->best_armor_value >= 2) ? 128 : 32;
                if (gcon && F->best_armor_value < 2 && !ocean) score /= 2;
                break;
            }
            case PLAN_NAVAL_SUPERIORITY:
                if (!naval_base(*game_current_base_id)) {
                    skip = 1;
                    break;
                }
                score = score * (threat == 1 ? 256 : 128) * 56 / *game_map_area_sqrt;
                if (plan == PLAN_NAVAL_TRANSPORT) score = 2 * score / 3;
                if (has_fac_built(FAC_NAVAL_YARD)) score = (v118 + 2) * score / (v118 + 3);
                if (P->group == 21) {
                    if (ocean) score /= 2;
                    int a = plan_triad[3 * pl + 1] + units_all[3 * pl + 1];
                    if (a >= units_all[22]) score *= 2;
                    if (units_all[22] > 3 * a / 2) score /= 2;
                } else {
                    if (game_at_goal(f, 6, CB->x, CB->y)) score /= 2;
                    int r = game_range(2 * (units_all[18 + tri] - (F->enemy_transports & 0x7ff)) + 8 - F->enemy_naval_reactors / 2, 0, 8);
                    score = (r + 8) * score / 16;
                    if (units_all[19] + 2 * plan_triad[19] < vc4) {
                        score /= 2;
                        if (plan == PLAN_NAVAL_TRANSPORT) score /= 2;
                    }
                    if (plan_triad[19] + units_all[19] < plan_triad[22] + units_all[22]) {
                        score /= 2;
                        if (plan == PLAN_NAVAL_TRANSPORT) score /= 2;
                    }
                    if (gexp) score = 3 * score / 4;
                    if (F->flags & 0x80000) score /= 2;
                    if (game_faction_defs[f].rule_flags & RULE_FLAG_AQUATIC) score /= 2;
                    if (ocean) score /= 2;
                    if (gcon && ocean) score /= 2;
                }
                break;
            case PLAN_AIR_SUPERIORITY: {
                score *= 64;
                int sup = F->air_superiority, off = F->air_offense;
                if (2 * sup >= off) {
                    if (plan_triad[3 * pl + tri] + units_all[3 * pl + tri] > 4 * F->enemy_air_attackers / 3) score *= 2;
                } else {
                    if (sup < off / 2) score /= 2;
                    if (sup < off / 4) score /= 2;
                }
                int idx = 3 * pl + tri, a = units_all[idx], c = plan_triad[idx] + a, ea = F->enemy_air_attackers;
                if (ea > c) {
                    if (!a) score /= 2;
                    score /= 2;
                    if (plan != PLAN_DEFENSIVE) {
                        if (!a) score /= 2;
                        if (ea >= 2 * c) score /= 2;
                        score /= 2;
                    }
                }
                if (!(CB->state & 0x100000) && vd0) score /= 2;
                break;
            }
            case PLAN_PLANET_BUSTER: {
                score *= 64;
                if (gcon) {
                    score /= 2;
                    if (gbld) score /= 2;
                }
                int idx = 3 * pl + tri;
                if (plan_triad_region[idx] >= 2) {
                    skip = 1;
                    break;
                }
                if (!plan_triad[idx] && units_all[idx] < 2) score /= 2;
                int odp_max = 4, halved = 0;
                for (int i = 1; i < FACTION_COUNT; i++) {
                    if (f == i) continue;
                    if (treaty(f, i, 1) && !human(i)) continue;
                    int odp = game_factions[i].satellites[3];
                    if (odp_max <= odp) odp_max = odp;
                    if (halved) continue;
                    if ((treaty(f, i, 0x30) || game_factions[i].major_atrocities) && F->planet_busters < odp + 1) {
                        halved = 1;
                        score /= 2;
                    }
                }
                if (F->planet_busters > odp_max) {
                    skip = 1;
                    break;
                }
                if (hf) break;
                if (F->flags_ext & 0x10) score /= 2;
                if (F->flags & 0x1000000) score /= 2;
                break;
            }
            case 13:
            case 14:
                score *= 64;
                if (P->plan == 14 && F->se_pending[SE_PLANET] > 0) score /= 2;
                if (plan_triad_region[3 * pl + tri] >= 2) {
                    skip = 1;
                    break;
                }
                if (hf) break;
                if ((P->plan == 13 ? pt : pg) && !pp) score /= 2;
                if (F->flags_ext & 0x10) score /= 2;
                break;
            case PLAN_NAVAL_TRANSPORT: {
                if (tri == TRIAD_SEA && !transport_base(*game_current_base_id)) {
                    skip = 1;
                    break;
                }
                score *= plan == PLAN_NAVAL_TRANSPORT ? 16 : plan != PLAN_COLONY ? 128 : 64;
                if (gexp || (gcon && plan == PLAN_NAVAL_TRANSPORT)) {
                    score /= 2;
                    if (!plan_triad[21 + tri] && !F->units_active[u] && pl == PLAN_NAVAL_TRANSPORT) score /= 4;
                }
                if (ocean && need < 3) {
                    score /= 4;
                    if (!units_all[21 + tri]) score /= 4;
                }
                if (game_has_abil(u, ABL_HEAVY_TRANSPORT)) score = 3 * score / 4;
                int h = F->base_count / 2;
                if (h < 2) h = 2;
                if (units_all[21 + tri] >= h) score *= 2;
                if (pl == PLAN_NAVAL_TRANSPORT) {
                    if (plan_triad[18 + tri] + units_all[18 + tri] + units_all[21 + tri] + plan_triad[21 + tri] < 2 && *game_turn < 100)
                        score /= 2;
                    if (!units_all[21 + tri] && !plan_triad[21 + tri] && (units_all[24 + tri] || plan_triad[24 + tri]) && plan > 2)
                        score /= 2;
                }
                if (CB->ai_plan) score *= 2;
                if (plan == PLAN_NAVAL_TRANSPORT) {
                    if (need >= 2 || pl != PLAN_NAVAL_TRANSPORT) break;
                    int cnt = 0;
                    for (int r = 1; r < 128; r++)
                        if (F->region_total_bases[r]) cnt++;
                    int lim = (F->region_total_bases[region] + 3) / 4;
                    if (cnt > lim) lim = cnt;
                    if (units_all[21 + tri] - (F->units_active[13] != 0) < lim && !plan_triad[21 + tri]) score /= 4;
                    break;
                }
                if (threat == 1) score *= 2;
                if (hf) break;
                if (units_region[u] > 1 || plan == PLAN_DEFENSIVE || (F->region_flags[region] & 0x400)) v78 = 1;
                break;
            }
            case PLAN_COLONY: {
                if (!(hf && (CB->governor & 0xdfbe00) == 0x20000)) {
                    if (!(hf && (CB->governor & GOV_ACTIVE))) {
                        if ((hf || need >= 3) && CB->pop_size < 2) {
                            skip = 1;
                            break;
                        }
                        if (plan_triad_region[24 + tri] + units_region_pt[24 + tri] > 1 && plan != PLAN_COLONY) {
                            skip = 1;
                            break;
                        }
                    }
                    if (CB->surplus[0] < 1 && (units_all[24 + tri] || plan_triad_region[24 + tri])) {
                        skip = 1;
                        break;
                    }
                    if (tri != TRIAD_LAND) {
                        if (!ocean && (hf || !(F->flags_ext & 4)) && (F->units_active[u] || units_building[u])) {
                            skip = 1;
                            break;
                        }
                    } else if ((ocean && (va8 < F->base_count || va8 < 3)) || plan == PLAN_DEFENSIVE) {
                        skip = 1;
                        break;
                    }
                }
                int grow = CB->state & 0x1000000;
                if (grow) {
                    int e = F->base_count - *game_turn / (hf ? 40 : 20) - pp - pg - 1;
                    if (e > 0) m += e;
                }
                score *= 5;
                if (CB->pop_size < 2 && (CB->surplus[0] < CB->surplus[1] || CB->surplus[0] < 1)) score *= 2;
                else if (plan > 2 && !grow) {
                    if (!hf) score /= *game_difficulty + 1;
                    else if (!(plan == PLAN_NAVAL_TRANSPORT && F->base_count >= *game_base_count / 5 && F->base_count >= 10)) {
                        if (!(CB->governor & GOV_ACTIVE)) score /= *game_difficulty + 1;
                        else if (gexp) score /= game_range(*game_difficulty + 1, 2, 99);
                        else if (gbld || gcon) score /= 2;
                    }
                }
                if (F->flags_ext & 1) score /= 2;
                if (F->region_flags[region] & 0x400) score *= 2, v78 = 1;
                if (*game_rules & 0x20000000) {
                    score /= 2;
                    if (plan == PLAN_COLONY) score /= 2;
                }
                int tail = 1;  // the bonuses for a faction's best bases
                if (tri == TRIAD_LAND) {
                    if (hf && *game_turn > 100) {
                        skip = 1;
                        break;
                    }
                    if (plan == PLAN_COLONY && ((units_region_pt[9] || plan_triad_region[9]) || !F->region_territory_pods[region])
                        && (units_region_pt[27] || plan_triad_region[27] || !has_tech(game_protos[1].preq_tech, f))
                        && !(F->region_flags[region] & 0x20))
                        score /= 2;
                    if (!units_all[3 * pl] && !plan_triad[3 * pl] && CB->surplus[0] >= 0 && plan != PLAN_DEFENSIVE
                        && (!(F->region_flags[region] & 0x401) || plan == PLAN_NAVAL_TRANSPORT))
                        score /= 2;
                    if (need < 3 && !units_region_pt[3 * pl] && !plan_triad_region[3 * pl]) {
                        int halve = 0;
                        if (plan == PLAN_COLONY) halve = 1;
                        else if (plan == PLAN_NAVAL_TRANSPORT) {
                            if ((CB->state & 0x1000000) || *game_ocean_coverage >= 1) tail = 2;
                            else halve = 1;
                        }
                        if (halve) {
                            score /= 2;
                            if (F->base_count == 1) score /= 4;
                        }
                    }
                } else {
                    if (ocean) {
                        score /= 2;
                        if (F->base_count < 4) score /= 2;
                        if (F->base_count < 8) score /= 2;
                        if (game_faction_defs[f].rule_flags & RULE_FLAG_AQUATIC) score /= 2;
                    }
                    if (*game_ocean_coverage > 1) {
                        score /= 2;
                        if (need < 3 && !plan_triad_region[3 * pl + tri] && !units_region_pt[3 * pl + tri] && !(CB->state & 0x1000000))
                            score /= 2;
                    }
                    if (CB->surplus[0] >= game_rules_basic->nutrient_intake_citizen && (plan == PLAN_NAVAL_TRANSPORT || ocean))
                        score /= 2;
                    if (!ocean && plan != PLAN_NAVAL_TRANSPORT && (hf || !(F->flags_ext & 4))) score *= 2;
                    if (!hf && (F->flags_ext & 4)) score /= 2;
                }
                if (tail == 1 && plan == PLAN_COLONY && F->region_total_bases[region] <= 1) tail = 0;
                if (tail) {
                    if (F->base_count >= 3 && min_score == F->best_mineral_output) score *= 2;
                    if (F->base_count >= 4 && CB->pop_size > v144) score *= 2;
                    if (hf && F->base_count >= (F->diff_level + 1) * *game_turn / 40) score *= 3;
                }
                if (!strcmp(game_faction_defs[f].filename, game_name_believers) && !hf && !(F->region_flags[region] & 1))
                    score /= 2;
                if (gexp) score /= 2;
                break;
            }
            case PLAN_TERRAFORM: {
                score *= threat ? 6 : 4;
                if (gbld) score /= 2;
                else if (gcon) score *= 2;
                if (F->flags_ext & 2) score /= 2;
                if (tri == TRIAD_SEA) {
                    if ((units_building_region[u] || units_all[28] > va8 / 2) && !ocean) skip = 1;
                    else if (CB->state & 0x80000) skip = 1;
                    else if (!ocean && (plan <= 2 || (plan != PLAN_NAVAL_TRANSPORT && units_all[28]))) skip = 1;
                    else if (war_plan7 && !ocean && units_all[28]) skip = 1;
                    if (skip) break;
                    if (!need) {
                        if (ocean && units_region[u] < va8) score /= 2;
                        if (units_region[u] < va8 / 2) score /= 2;
                        if (!units_all[28] && !war_plan7) score /= 2;
                    }
                    if (units_region_pt[28] >= va8) {
                        skip = 1;
                        break;
                    }
                    if (ocean) score /= 2;
                    break;
                }
                if (ocean) {
                    skip = 1;
                    break;
                }
                int c = units_region_pt[27 + tri];
                int check2;
                if (!c && !units_building_region[u] && *game_turn < 25 && !gcon && !gexp) {
                    need = 0;  // and so it stays for the items after this one
                    check2 = 1;
                } else
                    check2 = need < 3;
                int plan2_skip = 1;
                if (check2 && !c && !units_building_region[u]) {
                    if (!hf && (plan == PLAN_COLONY || *game_turn < 40)) score /= 16;
                    if (plan == PLAN_NAVAL_TRANSPORT) {
                        score /= 2;
                        plan2_skip = 0;
                    }
                }
                if (plan2_skip && plan == PLAN_DEFENSIVE) {
                    skip = 1;
                    break;
                }
                int rf = F->region_flags[region];
                if (rf & 0x400) score *= 4;
                if (!need && !gcon) {
                    uint32_t st = CB->state;
                    if (!(st & 0x20) && units_region_pt[3 * pl + tri] < F->region_total_bases[region]) {
                        if (plan != PLAN_DEFENSIVE && (pw || pt || (!frontier && plan > 2))) score /= 2;
                        if (plan == PLAN_NAVAL_TRANSPORT || (st & 0x2000000)) {
                            if (!pw && !pt && frontier) score /= 2;
                            else score /= 4;
                        }
                    }
                    if (plan > 2) {
                        int nb = F->region_total_bases[region], have_f = units_region_pt[3 * pl + tri];
                        if (have_f <= nb) score /= 2;
                        if (have_f <= (nb + 1) / 2 && !((rf & 0x20) && frontier) && (pw || pt || !frontier)) score /= 2;
                    }
                }
                if (CB->state & 0x20) score *= 3;
                int nb = F->region_total_bases[region];
                if (c >= nb) {
                    skip = 1;
                    break;
                }
                if (c >= 2 * nb / 3 && (plan == PLAN_OFFENSIVE || plan == PLAN_DEFENSIVE)) score = 3 * score / 2;
                if (!*game_cloud_cover) score /= 2;
                break;
            }
            case PLAN_SUPPLY: {
                if (hf || plan <= 2) {
                    skip = 1;
                    break;
                }
                int left = 3;
                for (int k = 0; k < *game_veh_count && !skip; k++) {
                    const struct veh *V = &game_vehs[k];
                    if (V->faction != f || game_protos[V->proto].plan != PLAN_SUPPLY) continue;
                    if (V->home_base == *game_current_base_id) score *= 2;
                    if (V->order != 3) skip = 1;
                    else {
                        if (left) left--;
                        score = 3 * score / 2;
                    }
                }
                if (skip || !left) {
                    skip = 1;
                    break;
                }
                if (!on_project && plan == PLAN_NAVAL_TRANSPORT && F->ai_tech) score /= 2;
                score = 3 * score * 256 / CB->pop_size;
                break;
            }
            case PLAN_PROBE: {
                int idx = 3 * pl + tri;
                if (units_region_pt[idx] >= F->region_total_bases[region] || (hf && plan_triad[idx] + units_all[idx] > 1)) {
                    skip = 1;
                    break;
                }
                int lim = 0;
                if (need < 2 && *game_difficulty > 2 && drones < 2) {
                    if (!strcmp(game_faction_defs[f].filename, game_name_peacekeepers)) lim = 1;
                    if (game_at_climax(f)) lim++;
                }
                if (F->flags_ext & 8) lim += 2;
                if (plan_triad_region[idx] > lim) {
                    skip = 1;
                    break;
                }
                int pick = -1, mind = 0, stolen = 0, comm = 0;
                for (int i = 1; i < FACTION_COUNT; i++) {
                    if (f == i) continue;
                    mind += F->diplo_mind_control[i];
                    stolen += F->diplo_stolen_techs[i];
                    const struct faction_data *I = &game_factions[i];
                    if (!I->region_total_bases[region]) continue;
                    if (has_project(SP_HUNTER_SEEKER, i)) continue;
                    if (!treaty(f, i, TREATY_COMMLINK)) continue;
                    comm++;
                    if (treaty(f, i, 7)) {
                        if (!human(i) && !hf) continue;
                        if (!lim) {
                            if (treaty(f, i, 0x1000000)) {
                                if (I->ranking < F->ranking) continue;
                                if ((I->tech_ranking - F->tech_ranking) / 2 < (I->ranking != 7 ? 3 : 6)) continue;
                            } else if (I->ranking < F->ranking && I->tech_ranking < F->tech_ranking)
                                continue;
                        }
                    }
                    pick = i;
                    if (human(i) && !*game_net_multiplayer) break;
                }
                if (pick < 0) {
                    if (((plan == PLAN_NAVAL_TRANSPORT && *game_difficulty > 1) || gdis) && !units_region_pt[idx])
                        pick = game_faction_rankings[7];
                    if (pick < 0) {
                        skip = 1;
                        break;
                    }
                }
                if (pick == f) {
                    skip = 1;
                    break;
                }
                const struct faction_data *K = &game_factions[pick];
                int rich = hf ? 0 : F->energy_credits / game_range(K->energy_credits, 100, 999999);
                int strong = hf ? 0 : K->mil_strength / game_range(F->mil_strength, F->base_count + 1, 999999);
                int level = (*game_rules & 0x40) ? 5 : K->diff_level;
                int div = hf ? 4 : 2;
                int val = F->tech_ranking / div - K->tech_ranking / div + K->se_pending[SE_PROBE] - level - strong - rich - lim
                        + units_region_pt[idx] + game_bit_count(treaty(f, pick, 7)) + 13;
                score = game_range(val, 1, 16) * score * 3 / (pt - pp + 3);
                if (!hf) {
                    int techs[7] = {game_chassis[5].preq_tech, game_chassis[8].preq_tech, game_weapons[16].preq_tech,
                                    *game_tech_unk_9a5e9c, game_reactors[1].preq_tech, game_reactors[2].preq_tech,
                                    game_reactors[3].preq_tech};
                    for (int i = 0; i < 7; i++)
                        if (has_tech(techs[i], pick) && !has_tech(techs[i], f)) score /= 2;
                    if (K->best_weapon_value > F->best_weapon_value || K->best_armor_value > F->best_armor_value) score /= 2;
                }
                if (plan != PLAN_NAVAL_TRANSPORT && (F->region_flags[region] & 0x20) && comm) {
                    if (gdis && !units_all[idx] && !plan_triad[idx]) score /= 2;
                } else {
                    if (!has_tech(game_chassis[3].preq_tech, f) || (units_region_pt[idx] && mind + stolen < 3)) {
                        skip = 1;
                        break;
                    }
                    if (hf) score *= 2;
                    if (plan_triad[idx] + units_all[idx]) score *= 2;
                }
                if (!hf && (F->flags_ext & 8)) score /= 4;
                if (threat == 1 && fight > 0) m *= 2;
                break;
            }
            }
            if (skip) continue;

            // A design not yet built costs a prototype.
            int s80 = 0;
            if (u >= 64 && !(P->flags & PROTO_FLAG_BUILT)) {
                int spd[TRIAD_COUNT] = {0}, ba = 0, bw = 0;
                for (int i = 0; i < 128; i++) {
                    int s = i < 64 ? i : f * 64 + i - 64;
                    if (s == u) continue;
                    const struct unit_proto *Q = &game_protos[s];
                    if (!(Q->flags & PROTO_FLAG_ACTIVE) || (s >= 64 && !(Q->flags & PROTO_FLAG_BUILT))) continue;
                    if (bw <= game_weap_val(s, f)) bw = game_weap_val(s, f);
                    if (ba <= game_arm_val(s, f)) ba = game_arm_val(s, f);
                    int t2 = game_chassis[Q->chassis].triad;
                    int was = spd[t2];
                    spd[t2] = was > game_speed_proto(s) ? was : game_speed_proto(s);
                }
                int wv = game_weap_val(u, f), av = game_arm_val(u, f), sp = game_speed_proto(u);
                if (pl == PLAN_DEFENSIVE) {
                    if (game_armor[P->armor].defense != 1 && av > ba && (av > ba + 1 || plan == PLAN_NAVAL_TRANSPORT)) {
                        score = 2 * score / 3;
                        s80 = 1;
                    }
                } else if (pl <= 1) {
                    if (wv > bw + ((plan == PLAN_NAVAL_TRANSPORT && seen) ? 0 : 1)) s80 = 1;
                }
                if (sp > spd[tri]) s80 = 1;
                if (!has_skunkworks) {
                    if (!(pl == PLAN_PLANET_BUSTER && !F->planet_busters)) {
                        if (!s80) m += game_range((plan > 2) - (gcon || gdis) + pg, 0, 2);
                        if (plan == PLAN_COLONY || (plan == PLAN_NAVAL_TRANSPORT && !(CB->state & 0x1000000))) {
                            if (pl == PLAN_COLONY) score /= 2;
                            else {
                                if (!gcon) {
                                    m++;
                                    if (!seen && !hf) score *= 2;
                                }
                                if (!s80) {
                                    if (*game_turn < 50) score *= 2;
                                    if (*game_turn < 100 && !hf) score *= 2;
                                }
                                if (!(CB->state & 0x1000000)) score *= 2;
                            }
                        }
                    }
                    if (need >= 3) score *= 8;
                }
                if (units_building[u]) {
                    if (!has_skunkworks) continue;
                } else {
                    int bm = F->best_mineral_output, lim = 3 * bm / 4;
                    if (bm - 1 < lim) lim = bm - 1;
                    int sw = 0;
                    if (min_score >= lim && CB->queue[0] > -FIRST_PROJECT && !has_skunkworks) {
                        if (vec < 2) score /= 2;
                        if (P->plan == F->region_base_plan[region]) score /= 2;
                        if (P->plan == PLAN_COMBAT && F->region_base_plan[region] <= 2) score /= 2;
                        if (s80 && F->region_base_plan[region] == PLAN_NAVAL_TRANSPORT && seen) score /= 2;
                        if (P->plan == PLAN_COMBAT && (pt || pw)) score /= 2;
                        if (P->plan == PLAN_OFFENSIVE && (pp || pg)) score /= 2;
                        if (vec) score = (vec + 5) * score / 5;
                    } else if (has_skunkworks) {
                        score = 2 * score / 3;
                        sw = 1;
                    } else if (vec)
                        score = (vec + 5) * score / 5;
                    if (sw || vec < F->base_count / 4) {
                        int rp = F->region_base_plan[region];
                        if (rp == 0 || rp == 2) {
                            if (P->plan == rp) score /= 2;
                            if (P->plan == PLAN_COMBAT) score = 3 * score / 4;
                        } else if (rp == PLAN_COLONY)
                            score = 3 * score / 4;
                        else if (rp == PLAN_NAVAL_TRANSPORT) {
                            if (P->plan == PLAN_NAVAL_TRANSPORT) score /= 2;
                            if (P->plan == PLAN_NAVAL_SUPERIORITY) score = 3 * score / 4;
                        }
                    }
                }
                if (game_weapons[P->weapon].mode > 2 && !has_skunkworks) score *= 2;
                int bm = F->best_mineral_output;
                if (min_score >= 2 * bm / 3 && plan != PLAN_COLONY) score = 2 * score / 3;
                if (min_score < bm / 2) {
                    if (!has_skunkworks && !(s80 && pl == PLAN_DEFENSIVE)) score *= 2;
                } else {
                    if (!v158 && plan != PLAN_COLONY) score = 3 * score / 4;
                    if (gdis && !has_skunkworks) score /= 2;
                }
            }

            // The base's own defence.
            if (need) {
                int set78 = 0;
                if (pl == PLAN_DEFENSIVE) {
                } else if (pl == PLAN_RECON) {
                    if (F->best_armor_value > 1 && need < 3) score *= 2;
                } else if (pl == PLAN_OFFENSIVE) {
                    if (game_has_abil(u, ABL_ARTILLERY) && (CB->state & 0x200000)) set78 = 1;
                } else if (pl == PLAN_COMBAT) {
                    if (need == 3 || (s5c + def >= gar && F->region_offensive[region] >= guard_check(f, region)))
                        if ((plan == PLAN_OFFENSIVE || plan == PLAN_NAVAL_TRANSPORT) && s5c + def >= gar) set78 = 1;
                } else
                    set78 = 1;
                if (set78) {
                    if (need == 3 && (!hf || (CB->governor & 0x1000))) v78 = 1;
                } else {
                    if (!s5c && def < gar && P->plan > 1) score *= 2;
                    if (game_has_abil(u, ABL_TRANCE) && game_stack_check(gv, 6, ABL_TRANCE, -1, -1) < def / 2 - 1) score /= 2;
                    if (game_has_abil(u, ABL_COMM_JAMMER) && game_stack_check(gv, 6, ABL_COMM_JAMMER, -1, -1) < def / 2 - 1)
                        score /= 2;
                    int zeroed = 0;
                    if (need == 3) {
                        score *= (P->cost + 1) / 2;
                        if ((pl == PLAN_DEFENSIVE || F->best_armor_value == 1) && (u < 64 || (P->flags & PROTO_FLAG_BUILT))) {
                            m = 0;
                            zeroed = 1;
                        }
                    }
                    if (!zeroed && drones < 2) {
                        int lim = 1 << (3 - need);
                        if (m >= lim) m = lim;
                    }
                }
            } else if (P->plan <= 9 && CB->intake_2[1] && !(P->plan == PLAN_NAVAL_TRANSPORT && plan == PLAN_NAVAL_TRANSPORT)) {
                int sup = F->se_pending[SE_SUPPORT], ed = CB->consumption[1], free, inc = 1;
                if (sup >= 3) free = CB->pop_size;
                else if (sup <= -4) free = 0, inc = 2;
                else if (sup <= -3) free = 0;
                else if (sup == -2) free = 1;
                else free = sup + 2;
                if (*game_base_forces_supported >= free) ed += inc;
                int k = (pw - (threat || war_plan7) - pp + 3) * ed;
                score = (k * k / 9 + CB->intake_2[1]) * score / CB->intake_2[1];
            }
            int rf = F->region_flags[region];
            if ((rf & 0x4200) && (P->plan == 0 || P->plan == 1 || P->plan == 4 || P->plan == 11 || P->plan == 5)) {
                score /= 2;
                if (plan == PLAN_DEFENSIVE && (rf & 0x4000)) score /= 2;
            }
            if (target && (plan == PLAN_OFFENSIVE || enemy_away > 0)) {
                if (tri) {
                    if (target > 1) score *= 2;
                } else if (P->plan < 2)
                    score = (4 - target) * score / 4;
            }
            if (game_weapons[P->weapon].mode <= 2 && has_fac_built(FAC_BIOENHANCEMENT)) score = (v134 + 2) * score / (v134 + 3);
            if (F->enemy_air_attackers && F->enemy_air_attackers > F->air_superiority && F->enemy_air_attackers > F->air_offense) {
                if (game_has_abil(u, ABL_AIR_SUPERIORITY | ABL_AAA)) score /= 2;
                else if (game_has_abil(u, ABL_ARTILLERY)) score *= 2;
                else if (!tri && !pl) score = 2 * score / 3;
            }
            if (game_has_abil(u, ABL_ARTILLERY)) {
                if (!(F->flags_ext & 0x40) && !game_at_goal(f, 16, CB->x, CB->y) && !(CB->state & 0x200000))
                    m += units_region_pt[3 * pl + tri] + plan_triad_region[3 * pl + tri];
                if (!game_stack_check(gv, 15, -1, -1, -1)) {
                    if (game_at_goal(f, 16, CB->x, CB->y) || (CB->state & 0x200000)) score /= 3;
                    else if (CB->event & 0x100) score = 2 * score / 3;
                    if (plan <= 2) score = 2 * score / 3;
                }
                if ((enemy_here >= 0 || enemy_away >= 0) && (!game_stack_check(gv, 15, -1, -1, -1) || !need)) score = 2 * score / 3;
            } else if (enemy_away >= 0 && !need) {
                if (pl == 0) score /= 2;
                else if (pl == 1) score = 2 * score / 3;
            } else if (enemy_here >= 0 && !need) {
                if (pl == 0) score = 2 * score / 3;
                else if (pl == 1) score = 4 * score / 5;
            }
            if (game_has_abil(u, ABL_AMPHIBIOUS) && (ocean || plan == PLAN_NAVAL_TRANSPORT)) score = 2 * score / 3;
            if (game_has_abil(u, ABL_DROP_POD)) {
                if (plan == PLAN_NAVAL_TRANSPORT || plan == PLAN_OFFENSIVE) {
                    score /= 2;
                    if (!F->units_active[u] && !F->units_queue[u]) score /= 2;
                } else
                    score = 2 * score / 3;
            }
            if (game_weapons[P->weapon].offense) {
                int wm = game_worm_mod(*game_current_base_id, f);
                int mm = game_morale_mod(*game_current_base_id, f, tri) + F->se_pending[SE_MORALE];
                if (pl == PLAN_DEFENSIVE && has_fac_built(FAC_CHILDREN_CRECHE)) {
                    int t = mm - F->se_pending[SE_MORALE] + 1;
                    if (mm <= t) mm = t;
                }
                if (game_weapons[P->weapon].offense < 0 && u < 64) {
                    m += 2 - game_range(game_worm_mod(*game_current_base_id, f), 0, 2);
                    if (mm > wm) m += mm - wm;
                    score = 8 * score / game_range(wm - mm + 6, 1, 16);
                    int psi = game_faction_defs[f].rule_psi;
                    if (psi) score = 100 * score / game_range(psi + 100, 50, 200);
                    if (hf && mm >= 0 && wm < 3) m++, score *= 2;
                } else {
                    if (tri != TRIAD_AIR && wm > mm + 1) m++;
                    score = 8 * score / game_range(wm - mm + 8, 1, 16);
                }
            }
            int as = game_arm_strat(P->armor, f);
            int ws = game_planet_buster2(u) ? 50 : game_weap_strat(P->weapon, f);
            if (need && (pl == PLAN_DEFENSIVE || pl == PLAN_RECON || (pl == PLAN_COMBAT && (plan == PLAN_DEFENSIVE || plan == PLAN_COLONY))))
                as *= 2;
            // The speed of the unit on top at the base; with none there, of
            // whatever lies just before the units table.
            int d = game_range(game_bit_count(P->abilities) + ws + as, 1, 999)
                  * (game_chassis[game_protos[game_vehs[gv].proto].chassis].speed / game_rules_basic->move_rate_roads + 1);
            int e = score / d;
            if (war_plan7 && plan == PLAN_NAVAL_TRANSPORT && P->plan <= 1 && !need) {
                if (fight == 0) e = 2 * e / 3;
                else if (fight == 1) e /= 2;
                if (!plan_triad_region[3 * pl + tri]) e /= 2;
            }
            if (my_units >= 256) e *= 2;
            if (gcon && game_weapons[P->weapon].offense && pl != PLAN_DEFENSIVE) e /= 2;
            if (gdis) e *= 2;
            if (gbld && P->plan != PLAN_TERRAFORM) e *= pl != PLAN_DEFENSIVE ? 8 : 2;
            if (gexp) {
                if (P->plan <= 1) e *= 2;
                if (P->plan >= 3) e /= 2;
            }
            int lm = game_base_lose_minerals(*game_current_base_id, u);
            if (lm > 0 && (!hf || (!score_out && (CB->governor & GOV_ACTIVE))) && plan != PLAN_DEFENSIVE) {
                int c = game_veh_cost(u, *game_current_base_id, 0) * cost_factor(f, 1, -1);
                e = (lm + c) * e / c;
            }
            if (v6c == 1 && plan != PLAN_NAVAL_TRANSPORT) v6c = 0;  // and so it stays
            else if (v6c) {
                if (plan > 2 && !(P->plan == PLAN_DEFENSIVE && !need)
                    && (P->plan <= 3 || P->plan == PLAN_COLONY || P->plan == PLAN_PROBE))
                    e /= v6c + 1;
                else if (P->plan == PLAN_NAVAL_TRANSPORT && v6c == 2)
                    e *= 2;
            }
            int total = m * e;
            v18 = total;
            if (!v78) {
                int key = total;
                if (hf) {
                    uint32_t mask = 0;
                    switch (P->plan) {
                    case 0: mask = 0x200; break;
                    case 1: mask = (CB->governor & 0x200) ? 0x200 : 0x1000; break;
                    case 2: mask = 0x1000; break;
                    case 3: mask = ((CB->governor & 0x1000) && !P->chassis) ? 0x1000 : 0x400000; break;
                    case 4: mask = 0x2000; break;
                    case 5: mask = 0x800; break;
                    case 6: mask = 0x400; break;
                    case 7: case 10: mask = 0x800000; break;
                    case 8: mask = 0x20000; break;
                    case 9: mask = 0x8000; break;
                    case 11: mask = 0x100000; break;
                    }
                    if (u >= 64 && !(P->flags & PROTO_FLAG_BUILT)) mask |= 0x80000;
                    if ((CB->governor & mask) != mask) continue;
                }
                if (score_out && base_claimed(u, total, gexp, gdis, gbld, gcon)) key = total + 100000;
                if (key < best[0] || (key == best[0] && (choice[0] >= 0 || need >= 3) && e < v9c)) {
                    v9c = e;
                    best[0] = key;
                    choice[0] = u;
                } else if (key == best[0]) {
                    total = 999999;
                    v18 = total;
                }
            } else {
                combat = 0;
                if (choice[1] < 90000 && pl != PLAN_TERRAFORM && pl != PLAN_COLONY) continue;
                if (P->plan == PLAN_OFFENSIVE || pl == PLAN_AIR_SUPERIORITY || pl == PLAN_NAVAL_SUPERIORITY) continue;
            }
            int s = combat ? 2 : 1;
            if (total <= best[s]) best[s] = total, choice[s] = u;
        }
    }
    // Facilities.
    for (int fac = 1; fac < FIRST_PROJECT; fac++) {
        if (!game_facility_avail(fac, f, *game_current_base_id, 0)) continue;
        CB = *game_current_base;
        int s14 = 0, s24 = 0x10000, s34 = 0x100, want = 999, pri = gbld;
        int spec = CB->specialist_total - CB->specialist_adjust;  // [-0x6c]
        const int pop = CB->pop_size;
        const int rf = F->region_flags[region];
        combat = 0;
        switch (fac) {
        case FAC_HEADQUARTERS:
            if (plan == PLAN_DEFENSIVE || F->se_pending[SE_EFFIC] <= -4) break;
            if (v14c && (v14c != 1 || CB->queue[0] != -1)) break;
            want = 3 - pop / 5;
            break;
        case FAC_CHILDREN_CRECHE: {
            int ineff = F->se_pending[SE_EFFIC] > -4 ? CB->inefficiency[2] : 0;
            int mor = F->se_pending[SE_MORALE], sg = mor > 0 ? 1 : mor < 0 ? -1 : 0;
            want = 12 - ineff - pop * ((plan <= 2) + 1) / game_range((mor + sg + 6) / 2, 1, 999);
            if (!has_project(SP_CLONING_VATS, f) && CB->surplus[1] > 2 && want > 4) want = 4;
            if (game_faction_defs[f].rule_morale + F->se_pending[SE_MORALE] < 0 && plan == PLAN_DEFENSIVE) want /= 2;
            want = game_range(want, 1, 12);
            if (!has_fac_built(FAC_RECYCLING_TANKS) && pop < 5) break;
            if (CB->inefficiency[2] < pop / 2 && CB->inefficiency[2] < CB->intake_2[2] / 2) break;
            pri = 2 * gbld;
            break;
        }
        case FAC_RECYCLING_TANKS: case FAC_AQUAFARM: case FAC_SUBSEA_TRUNKLINE: case FAC_THERMOCLINE_TRANSDUCER: {
            int v = pp - pw - pt + 4;
            if (fac != FAC_RECYCLING_TANKS) v -= (game_faction_defs[f].rule_flags >> 7) & 2;
            if (plan == PLAN_NAVAL_TRANSPORT) v--;
            else if (plan <= 2) v++;
            uint32_t st = CB->state;
            if (st & 0x1000) v -= game_res_info->recycling.energy;
            if (st & 0x2000) v -= game_res_info->recycling.mineral;
            if (ocean) {
                if ((st & 0x4000) && fac == FAC_AQUAFARM) v /= 2;
                if ((st & 0x2000) && fac == FAC_SUBSEA_TRUNKLINE) v /= 2;
                if ((st & 0x1000) && fac == FAC_THERMOCLINE_TRANSDUCER) v /= 2;
            }
            if (fac != FAC_RECYCLING_TANKS) {
                int n = 0;
                for (int i = 0; i < 21; i++) {
                    int x2 = xrange(CB->x + game_radius_x[i]), y2 = CB->y + game_radius_y[i];
                    if (!on_map(x2, y2) || !is_ocean(x2, y2)) continue;
                    uint32_t bits = bit_at(x2, y2);
                    if (fac == FAC_AQUAFARM ? (bits & MAP_FARM) : fac == FAC_SUBSEA_TRUNKLINE ? (bits & MAP_MINE) : (bits & MAP_SOLAR)) n++;
                }
                if (!n) {
                    want = 999;
                    break;
                }
                v -= n;
            }
            if (CB->pop_size < 5) v--;
            want = game_range(v, 1, 4);
            int g = CB->state & 0x1000000;
            if (!g && (plan == PLAN_COLONY || F->units_active[0] + F->base_count < 4)) want *= 2;
            if (!gdis) break;
            pri = (!g && plan == PLAN_COLONY) ? 1 : 2;
            break;
        }
        case FAC_PERIMETER_DEFENSE: case FAC_TACHYON_FIELD: case FAC_FLECHETTE_DEFENSE: {
            int b;
            if (fac == FAC_PERIMETER_DEFENSE) {
                if (has_project(SP_CITIZENS_DEFENSE, f)) break;
                combat = 1;
                b = game_range(8 - pop - (has_fac_built(FAC_HEADQUARTERS) ? 2 : 0), 1, 10);
                for (int i = 1; i < FACTION_COUNT; i++) {
                    if (treaty(f, i, TREATY_VENDETTA)) continue;
                    int t = treaty(f, i, 0x40020) | (game_factions[i].integrity_blemishes >= 6);
                    if (!t) continue;
                    b -= game_factions[i].region_total_bases[region] ? game_bit_count(t) : 1;
                    if (F->region_total_bases[region] == 1) b--;
                }
            } else {
                combat = 1;
                b = game_range(10 - pop / 2 - (has_fac_built(FAC_HEADQUARTERS) ? 2 : 0), 1, 10);
                for (int i = 1; i < FACTION_COUNT; i++) {
                    if (treaty(f, i, TREATY_VENDETTA)) continue;
                    int t = treaty(f, i, 0x40020);
                    if (t) b -= human(i) ? game_bit_count(t) : 1;
                }
            }
            if (F->clean_minerals >= 4 && fac != FAC_FLECHETTE_DEFENSE) b -= game_range(CB->eco_damage / 10, 1, 4);
            if (b < 1 && !need) s34 = 0x80;
            want = game_range(b, 1, 10);
            pri = (gcon && plan == PLAN_DEFENSIVE && !need) ? gbld | 1 : gbld;
            break;
        }
        case FAC_ENERGY_BANK:
            want = game_range(2 * (6 - pw) - CB->intake_2[2] / 2, 1, 10);
            if (!has_fac_built(FAC_RECYCLING_TANKS) && pop < 5) break;
            pri = 2 * gbld;
            break;
        case FAC_HAB_COMPLEX: case FAC_HABITATION_DOME: {
            int lim = (fac == FAC_HAB_COMPLEX ? game_rules_basic->pop_limit_hab_complex : game_rules_basic->pop_limit_hab_dome)
                    - game_faction_defs[f].rule_population;
            if (has_project(SP_ASCETIC, f)) lim += 2;
            int intake = game_rules_basic->nutrient_intake_citizen;
            if ((CB->surplus[0] + 1) / game_range(intake, 1, 10) + pop >= lim) {
                want = game_range(lim - (intake / 2 + CB->surplus[0]) / game_range(intake, 1, 10) - pop + 4, 1, 10);
                if (plan > 2) want /= 2;
            }
            if (has_project(SP_CLONING_VATS, f)) want /= 2;
            pri = gbld | gexp;
            if (pop < lim) break;
            if (!gcon) pri *= 2;
            if (game_aah_ooga(f, f)) want /= 2;
            break;
        }
        case FAC_RECREATION_COMMONS:
            if (drones >= 2) {
                s24 = 0, want = -10;
                break;
            }
            want = (has_project(SP_VIRTUAL_WORLD, f) ? 6 : 7) - (drones + 2) * spec;
            pri = gbld | gdis;
            if (F->diff_level > 3 && gdis) pri = 2;
            if (has_project(SP_CLONING_VATS, f)) want /= 2;
            break;
        case FAC_HOLOGRAM_THEATRE:
            if (drones >= 2) {
                s24 = 0, want = -8;
                break;
            }
            want = 8 - (drones + 2) * spec;
            if (has_project(SP_CLONING_VATS, f)) want /= 2;
            break;
        case FAC_RESEARCH_HOSPITAL:
            if (drones >= 2) {
                s24 = 0, want = -4;
                break;
            }
            want = game_range(2 * (5 - pt - game_stack_check(gv, 2, 12, f, -1)) - CB->intake_2[2] / 4, 1, 10);
            pri = gdis;
            break;
        case FAC_NANOHOSPITAL:
            if (drones >= 2) {
                s24 = 0, want = -2;
                break;
            }
            want = game_range(2 * (6 - pt - game_stack_check(gv, 2, 12, f, -1)) - CB->intake_2[2] / 5, 1, 10);
            pri = gdis;
            break;
        case FAC_PARADISE_GARDEN:
            if (drones >= 2) s24 = 0, want = -6;
            else want = 8 - spec;
            break;
        case FAC_NETWORK_NODE:
            want = game_range(2 * (5 - pt - game_stack_check(gv, 2, 12, f, -1)) - CB->intake_2[2] / 3, 1, 10);
            if (has_project(SP_VIRTUAL_WORLD, f)) {
                if (drones >= 2) s24 = 0, want = -10;
                else {
                    int t = 6 - (drones + 2) * spec;
                    if (want >= t) want = t;
                }
            }
            pri = (has_project(SP_VIRTUAL_WORLD, f) ? 3 : 1) * gdis;
            break;
        case FAC_BIOLOGY_LAB: case FAC_CENTAURI_PRESERVE: case FAC_TEMPLE_OF_PLANET: {
            int range_hi = 0;  // the preserve and the temple clamp it to 1..12
            if (fac == FAC_BIOLOGY_LAB) want = 2 * (5 - pt - pg);
            else want = 11 - CB->eco_damage, range_hi = 12;
            int psi = game_faction_defs[f].rule_psi;
            if (psi > 0) want = 100 * want / game_range(psi + 100, 50, 200), s14 = threat;
            int mor = F->se_pending[SE_MORALE];
            if (mor < 0) want = (mor - 1) / 2 + want / 2, s14 = threat;
            if (range_hi) {
                pri = (mor < 0 && gcon && !(rf & 0x4000)) ? gbld | 1 : gbld;
                want = game_range(want, 1, 12);
            } else if (gdis || (gcon && mor < 0 && plan != PLAN_DEFENSIVE && !(rf & 0x4000)))
                pri = 1;
            else
                pri = 0;
            break;
        }
        case FAC_SKUNKWORKS:
            want = 2 * (vec - pp) + 6 - (hf ? 0 : fight) + v120;
            s24 = 0x90000;
            if (has_fac_built(FAC_COMMAND_CENTER)) want--;
            if (has_fac_built(FAC_BIOENHANCEMENT)) want--;
            pri = gdis;
            s14 = 1;
            want = F->best_mineral_output * want / game_range(min_score, 1, 999);
            break;
        case FAC_FUSION_LAB:
            want = game_range(2 * (6 - pt) - CB->intake_2[2] / 3, 1, 12);
            pri = gbld | gdis;
            break;
        case FAC_QUANTUM_LAB:
            want = game_range(2 * (6 - pt) - CB->intake_2[2] / 4, 1, 12);
            pri = gbld | gdis;
            break;
        case FAC_TREE_FARM:
            if (game_aah_ooga(f, f)) forests *= 2;  // and so it stays
            want = game_range(12 - (forests + 2) / 4 - (CB->eco_damage + 1) / 2 - CB->intake_2[2] / 3, 1, 10);
            break;
        case FAC_HYBRID_FOREST:
            want = game_range(12 - (forests + 2) / 4 - (CB->eco_damage + 2) / 3 - CB->intake_2[2] / 4, 1, 10);
            break;
        case FAC_COVERT_OPS:
            want = 5 * (2 - ((F->flags_ext >> 3) & 1));
            break;
        case FAC_GEOSYNC_SURVEY_POD:
            want = 7;
            gv += (F->ai_growth ? 1 : 0) - (F->ai_tech ? 1 : 0) - (F->ai_wealth ? 3 : 0) - (F->ai_power ? 1 : 0);  // quirk
            break;
        case FAC_COMMAND_CENTER: case FAC_BIOENHANCEMENT: case FAC_BROOD_PIT: {
            s24 = 0x10200, combat = 1, s14 = 1;
            int have_r = fac == FAC_COMMAND_CENTER ? v11c : v134;
            want = v120 + 2 * (pt - pp) + 5 + (fac == FAC_BROOD_PIT ? 5 : have_r);
            if (!hf) want -= 2 * fight;
            int w = (fac_maint(fac, f) + 4) * want / 4;
            int psi = game_faction_defs[f].rule_psi;
            if (psi > 0) w = (psi + 100) * w / 100;
            if (fac == FAC_COMMAND_CENTER && !have_r && plan != PLAN_NAVAL_TRANSPORT && F->region_total_bases[region] >= 4) w /= 2;
            want = game_range(w, 1, 99);
            if (F->region_total_combat_units[region] < 2 * F->region_total_bases[region] || (rf & 0x4000)
                || (*game_turn >= 100 && F->region_total_bases[region] <= 8 && plan != PLAN_NAVAL_TRANSPORT))
                pri = 0;
            else
                pri = gcon;
            break;
        }
        case FAC_NAVAL_YARD:
            if (naval_base(*game_current_base_id)) {
                s24 = 0x10400, combat = 1;
                int c = 2 * (pt - fight - pp + pg) + 24 - game_bases[j].pop_size / 3 - F->enemy_naval_reactors - v15c / 2 + v118;
                if (plan == PLAN_NAVAL_TRANSPORT) c /= 2;
                want = game_range(c, 1, 24);
                if (game_faction_defs[f].rule_flags & RULE_FLAG_AQUATIC) want /= 3;
            }
            pri = (gcon && plan == PLAN_NAVAL_TRANSPORT) ? 1 : 0;
            break;
        case FAC_AEROSPACE_COMPLEX: {
            s24 = 0x10800, combat = 1, s14 = 1;
            int c = 2 * (pt - F->enemy_air_attackers - fight + pg) - game_bases[j].pop_size / 3 - F->enemy_missiles
                  + 3 * (8 - pp) - v164 / 3 + vac;
            if (plan == PLAN_NAVAL_TRANSPORT) c /= 2;
            if (CB->state & 0x100000) c /= 2;
            if (reactor < 2) c *= 2;
            want = game_range(c, 1, 24);
            pri = (!gcon || plan == PLAN_DEFENSIVE || (rf & 0x4000)) ? 0 : 1;
            break;
        }
        case FAC_PRESSURE_DOME: {
            if (alt_at(CB->x, CB->y) != 3) break;
            if (alt_detail_at(CB->x, CB->y) - *game_sea_level < *game_climate_change + *game_dome_depth_a) {
                want = -16;
                break;
            }
            if (F->flags & 0x80000) {
                want = 0;
                break;
            }
            int i = 0;
            while (i < 64 && game_secret_projects[i] != *game_current_base_id) i++;
            if (i == 64) break;
            if (alt_detail_at(CB->x, CB->y) - *game_sea_level < *game_climate_change + *game_dome_depth_b + *game_dome_depth_a)
                want = game_range(8 - pop / 3, hf ? 4 : 2, 8);
            else
                want = game_range(20 - pop / 2, hf ? 6 : 4, 12);
            break;
        }
        case FAC_SUBSPACE_GENERATOR:
            if (!(game_faction_defs[f].rule_flags & RULE_FLAG_ALIEN)) break;
            if (game_generators(f, 0) >= game_ascent_numbers[1]) break;
            if (game_bases[j].pop_size >= game_ascent_numbers[0]) want = 1;
            else if (game_bases[j].pop_size >= 4 * game_ascent_numbers[0] / 5) want = 6;
            break;
        case FAC_GENEJACK_FACTORY:
            if (hf) break;
            want = game_range(2 * (CB->drone_total - CB->talent_total - has_fac_built(FAC_BROOD_PIT)) + 16 - CB->intake[1]
                              - F->se_pending[SE_POLICE], 1, 16);
            pri = (!gcon || plan <= 2) ? 0 : 1;
            break;
        case FAC_PUNISHMENT_SPHERE:
            if (hf) break;
            if (F->se_pending[SE_POLICE] + 2 * has_fac_built(FAC_BROOD_PIT) <= 0 && CB->drone_total >= 2 && plan <= 2
                && F->se_pending[SE_ECONOMY] > 1 && (CB->assimilation_turns || !v140))
                want = game_range(2 * (8 - CB->drone_total) - pop / 2, 1, 16);
            pri = gcon;
            break;
        case FAC_ROBOTIC_ASSEMBLY_PLANT:
            want = game_range(10 - CB->intake[1], 1, 10);
            pri = (gcon && plan > 2) ? gbld | 1 : gbld;
            break;
        case FAC_NANOREPLICATOR: case FAC_QUANTUM_CONVERTER:
            want = game_range(10 - CB->intake[1] / 2, 1, 10);
            break;
        case FAC_SKY_HYDRO_LAB: case FAC_NESSUS_MINING: case FAC_ORBITAL_POWER: {
            int sat = F->satellites[fac - FAC_SKY_HYDRO_LAB];
            if (sat >= pop) break;
            want = game_range(2 * (sat + 2 * (4 - pw) + pp) - pop, 2, 16);
            if (fac == FAC_SKY_HYDRO_LAB && has_project(SP_CLONING_VATS, f)) want /= 2;
            break;
        }
        case FAC_ORBITAL_DEFENSE_POD: {
            int top = 1;
            s24 = 0x12000, combat = 1;
            for (int i = 1; i < FACTION_COUNT; i++) {
                if (i == f || treaty(f, i, 1)) continue;
                const struct faction_data *I = &game_factions[i];
                int v = I->major_atrocities ? I->planet_busters : (I->planet_busters + 1) / 2;
                if (top <= v) top = v;
                if ((treaty(f, i, 0x30) || human(i)) && top <= I->satellites[3]) top = I->satellites[3];
            }
            if (F->satellites[3] < top + 2) {
                want = game_range(2 * (F->satellites[3] - top) + 12 - pop, 1, 12);
                const struct faction_data *K = &game_factions[game_ranked_faction[0]];
                if (!K->planet_busters && !K->major_atrocities && K->satellites[3] <= F->satellites[3]) want *= 2;
            }
            s14 = 1;
            pri = (gcon && plan == PLAN_NAVAL_TRANSPORT) ? 1 : 0;
            break;
        }
        case FAC_PSI_GATE:
            want = game_range((game_climactic_battle() ? 2 : 4) - v154 - game_at_climax(f), 0, 4);
            break;
        default:
            break;
        }
        // The common scoring.
        int lm = game_base_lose_minerals(*game_current_base_id, -fac);
        if (lm > 0 && (!hf || ((CB->governor & GOV_ACTIVE) && !score_out))) {
            int c = cost_factor(f, 1, -1) * game_facilities[fac].cost;
            if (c) s34 = (lm + c) * s34 / c;
        }
        int t = want * s34 * 32 / ((pw - pp + 3) * (pri + 1));
        if ((s14 != 0) != (threat != 0) || (threat && need)) t *= 2;
        if (plan == PLAN_DEFENSIVE || (rf & 0x4000)) t *= 2;
        if (plan <= 2 && !combat) t *= 2;
        if (enemy_away >= 0 && !combat
            && (fight > 0 || (human(enemy_away) && (fight >= 0 || treaty(f, enemy_away, 0x20)))))
            t *= 2;
        v18 = t;
        for (int k = 0; k < 2; k++) {
            int key = t, slot = 0;
            if (k == 0) {
                if (hf) {
                    if (!(CB->governor & 0x10000)) continue;
                    if ((CB->governor & s24) != (uint32_t)s24) key += 100000;
                    if (score_out && base_claimed(-fac, key, gexp, gdis, gbld, gcon)) key += 100000;
                }
            } else
                slot = combat ? 2 : 1;
            if (key < best[slot]) best[slot] = key, choice[slot] = -fac;
        }
    }

    // Secret projects.
    int go = !neg;
    if (go && !(on_project && (plan > 2 || vd4 < (plan != PLAN_DEFENSIVE) + 1))
        && !(hf && (CB->governor & 0xdfbe00) == 0x40000)) {
        if (plan == PLAN_DEFENSIVE || CB->pop_size < 3 || need >= 2 || (need && have <= 1 && !hf)) go = 0;
        else if (min_score < 2 * CB->pop_size || CB->drone_total > CB->talent_total || (CB->state & 2)) go = 0;
        else if (CB->drone_total && CB->drone_total >= CB->talent_total && CB->specialist_total > CB->specialist_adjust
                 && CB->pop_size < 8)
            go = 0;
        else if (CB->pop_size <= (vd8 + 1) / 2 && min_score <= (F->best_mineral_output + 1) / 2)
            go = 0;
        else {
            int n = plan == PLAN_NAVAL_TRANSPORT ? F->base_count : F->region_total_bases[region];
            if (n - 3 * vd4 < (plan == PLAN_COLONY) - gdis - pt + pp + 3) go = 0;
        }
    }
    for (int p = 0; go && p < 64; p++) {
        const int fac = p + FIRST_PROJECT;
        if (!game_facility_avail(fac, f, *game_current_base_id, 0)) continue;
        const struct rules_facility *R = &game_facilities[fac];
        if (!hf && *game_difficulty < 3 && R->preq_tech >= 0 && !(game_tech_achieved[R->preq_tech] & *game_human_factions)) continue;
        const struct faction_def *D = &game_faction_defs[f];
        int s14 = 0;
        int own_elsewhere = proj_building[p] & fbit;
        int g = proj_group[p / 8];
        int v50 = 2 * vd4 + 8 - R->ai_fight * fight - R->ai_wealth * pw - R->ai_power * pp - R->ai_tech * pt - R->ai_growth * pg
                - (F->ranking >= 5) - CB->pop_size;
        int v = v50 - *game_turn / 40 + g + projects_here / 2 + enemies_near;
        int w = 256;
        int pri = gdis ? 2 : gbld;
        if ((p == 7 || p == 0) && plan == PLAN_COLONY) w = 170;
        int rf = F->region_flags[region];
        if (p == 0) {
            if (D->rule_talent || F->se_pending[SE_TALENT] > 0) w *= 2;
        } else if (p == 8 || p == 16 || p == 19 || p == 9)
            w = (D->rule_morale + F->se_pending[SE_MORALE] + 8) * w / 8;
        if (p == 0 || p == 6 || p == 14 || p == 26 || p == 30) {
            if (D->rule_talent) w = 3 * w / 2;
            if (D->rule_drone) w /= 2;
        }
        int region_check = 0;
        if (p == SP_CITIZENS_DEFENSE) {
            s14 = threat;
            int i = 0;
            while (i < D->bonus_count && !((D->bonus_id[i] == 2 || D->bonus_id[i] == 12) && D->bonus_val1[i] == 4)) i++;
            if (i < D->bonus_count) continue;
            region_check = 1;
        } else if (p == 1)
            region_check = 1;
        if (region_check) {
            if (rf & 0x40) w /= 2;
            else if (rf & 0x20) w = 2 * w / 3;
        }
        if ((p == 10 || p == 2) && plan == PLAN_NAVAL_TRANSPORT) w /= 2;
        if (p == SP_HUNTER_SEEKER) {
            if (!F->ai_power) w /= 2;
            if (F->ai_tech) w /= 2;
            int s = F->se_base[SE_PROBE];
            if (s < 0) w /= 2;
            else if (s > 0) w *= 2;
        } else if (p == 34) {
            if (F->flags_ext & 8) w /= 2;
        } else {
            if ((p == 36 || p == 33) && F->ai_wealth) w /= 2;
            if (p == SP_CLOUDBASE) {
                if (rf & 0x40) w /= 2;
                else if (rf & 0x20) w = 2 * w / 3;
            } else if (p == 11) {
                for (int i = 1; i < FACTION_COUNT; i++)
                    if (game_factions[i].tech_ranking >= F->tech_ranking) w = 7 * w / 8;
            } else if (p == 4) {
                for (int i = 1; i < FACTION_COUNT; i++)
                    if (i != f) w = (treaty(f, i, TREATY_COMMLINK) ? 9 : 7) * w / 8;
                if (*game_governor_faction == f) w /= 2;
                if (F->ranking == 7) w /= 2;
                for (int i = 0; i < D->bonus_count; i++)
                    if (D->bonus_id[i] == 11) {
                        w /= 2;
                        break;
                    }
            }
        }
        if (!on_project && p < 31 && (plan == PLAN_DEFENSIVE || (rf & 0x4000))) w *= 2;
        if (p == 31 && !vdc && (*game_rules & 0x800)) {
            w /= 2;
            if (R->preq_tech >= 0)
                for (int i = 1; i < FACTION_COUNT; i++) {
                    if (i == f || game_factions[i].ranking <= F->ranking) continue;
                    if (has_tech(R->preq_tech, i)) w *= 2;
                    else w /= 2;
                }
        }
        if (!g && !vd4) w /= 4;
        if (p == 32 && !vdc && min_score >= F->best_mineral_output / 2) w /= 4;
        game_str_buffer[0] = 0;
        strcat(game_str_buffer, D->filename);
        (*game_char_upper)(game_str_buffer);
        if (!strcmp(game_str_buffer, game_name_caretakers) && p == 32) v = 99, w = 0x400;
        v = game_range(v, 1, 99);
        int vacp = v * w * 32;
        int item = -fac;
        int lm = game_base_lose_minerals(*game_current_base_id, item);
        if (lm > 0 && (!hf || (!score_out && (CB->governor & GOV_ACTIVE)))) {
            int c = cost_factor(f, 1, -1) * R->cost;
            w = (lm + c) * w / c;
        }
        w = (F->best_mineral_output + 8) * v * w * 32 / ((pt - pp + 3) * (min_score + 8));
        if (s14 != threat) {
            int c = cost_factor(f, 1, -1) * R->cost + 1;
            w += (c - CB->minerals) * w / c;
        }
        w = w * (own_elsewhere ? 2 : vdc + 1) * 1024 / (F->energy_credits + 1024);
        int sc = w * 8 / (game_range(CB->surplus[1] / (*game_turn / 100 + 3), 0, 8) + 8);
        if (pri) sc /= pri + 1;
        if (has_fac_built(FAC_HEADQUARTERS) && !own_elsewhere) sc = 3 * sc / 4;
        if (!hf && CB->queue[0] == item) {
            vacp /= 4;
            if (CB->minerals >= cost_factor(f, 1, -1) * R->cost) {
                sc /= 2;
                if (need < 3) need = 0;
            }
        }
        int out_of_region = 0;
        for (int b = 0; b < *game_base_count; b++) {
            const struct base *B = &game_bases[b];
            if (B->faction != f || b == *game_current_base_id || B->queue[0] != item) continue;
            if (region_at(B->x, B->y) != region) {
                out_of_region = 1;
                break;
            }
            if (!own_elsewhere || on_project) sc *= 2;
        }
        if (out_of_region || sc < 0) continue;
        int lim = 9999;
        if (!has_fac_built(FAC_HAB_COMPLEX) && has_tech(game_facilities[FAC_HAB_COMPLEX].preq_tech, f))
            lim = game_rules_basic->pop_limit_hab_complex;
        else if (!has_fac_built(FAC_HABITATION_DOME) && has_tech(game_facilities[FAC_HABITATION_DOME].preq_tech, f))
            lim = game_rules_basic->pop_limit_hab_dome;
        lim -= D->rule_population;
        int full = CB->pop_size >= lim;
        if (full && !on_project) continue;
        if (on_project) {
            int rp = F->region_base_plan[region];
            if (rp == PLAN_DEFENSIVE && need) {
            } else if (rp <= 2) {
                if (!need) sc /= 2;
            } else {
                sc /= 3;
                if (need < 3 && drones < 2) {
                    sc /= 2;
                    if (!(CB->state & 8) && game_secret_projects[p] == -1 && choice[0] > -FIRST_PROJECT) {
                        best[0] = 9999999;
                        drones = 0;
                        need = 0;
                    }
                }
            }
        }
        if (own_elsewhere) {
            if (!vd4 || is_ocean(CB->x, CB->y) || plan_triad[30] + units_all[30] > 2) continue;
        }
        int pick = fac;
        if (!has_fac_built(FAC_RECREATION_COMMONS) && (!hf || (CB->governor & 0x10000))) {
            if (!has_tech(game_facilities[FAC_RECREATION_COMMONS].preq_tech, f)) continue;
            pick = FAC_RECREATION_COMMONS;
        }
        if (hf) {
            if (own_elsewhere) continue;
        } else if (own_elsewhere && !game_veh_avail(5, f, *game_current_base_id))
            own_elsewhere = 0;
        if (choice[0] <= -FIRST_PROJECT && !own_elsewhere) {  // a project leads: by the weight before its cost
            if (vacp >= v9c) continue;
            v9c = vacp;
            for (int s = 0; s < 2; s++) {
                if (hf && !(CB->governor & 0x40000)) continue;
                int key = sc;
                if (score_out && base_claimed(item, sc, gexp, gdis, gbld, gcon)) key = sc + 100000;
                choice[s] = -pick;  // whether or not it scores better (quirk)
                if (sc < best[s]) best[s] = key;
            }
            continue;
        }
        for (int s = 0; s < 2; s++) {
            if (hf && !(CB->governor & 0x40000)) continue;
            int key = sc;
            if (score_out && base_claimed(item, sc, gexp, gdis, gbld, gcon)) key = sc + 100000;
            if (key < best[s]) {
                best[s] = key;
                choice[s] = own_elsewhere ? 5 : -pick;
                v9c = vacp;
            }
        }
    }

    if (second) {
        *second = choice[1];
        game_parse_num(1, best[1]);
    }
    if (third) {
        *third = choice[2];
        game_parse_num(0, best[2]);
    }
    int pick = choice[0];
    if ((pick < 0 || !game_weapons[game_protos[pick].weapon].offense) && veh_who(CB->x, CB->y) < 0 && drones < 2
        && (!hf || (CB->governor & 0x1000))) {
        int cheapest = 99999;
        for (int u = 0; u < PROTO_COUNT; u++) {
            if (!game_veh_avail(u, f, *game_current_base_id)) continue;
            const struct unit_proto *P = &game_protos[u];
            if ((P->plan == PLAN_DEFENSIVE || P->plan == PLAN_RECON || P->plan == PLAN_COMBAT) && (P->flags & PROTO_FLAG_BUILT)
                && P->cost < cheapest)
                cheapest = P->cost, choice[0] = u;
        }
        pick = choice[0];
    }
    if (score_out) *score_out = best[0];
    return pick;
}

// Which class of item a base is making, for the retooling penalty: two
// items of the same class trade minerals freely. With strictness 0 there
// is one class; with 1, units, facilities and secret projects, with
// facilities 65 and up (satellites and the like) a class of their own;
// with 2 every item but the secret projects, which share one; above that
// every item. A Skunkworks lowers the strictness to 1. Whatever the item,
// it is -1 while the facility the base is set on is one it already has.
int base_making(int item, int base) {
    int strict = game_rules_basic->retool_strictness;
    if (built(base, FAC_SKUNKWORKS) && strict >= 1) strict = 1;
    int q = game_bases[base].queue[0];
    if (item < 0 && q < 0 && -q < 65 && built(base, -q)) return -1;
    if (!strict) return 0;
    if (strict == 2) return item <= -FIRST_PROJECT ? -FIRST_PROJECT : item;
    if (strict > 1) return item;
    if (item >= 0) return 0;
    if (item <= -FIRST_PROJECT) return 1;
    return (item > -65) + 2;
}

// bitmask(): a bit number as a byte index and a bit, as the game keeps
// its project news.
static int iabs(int v) { return v < 0 ? -v : v; }

// Sets what the base builds, keeping its faction's count of units being
// built. A human base changing to another class of item (base_making())
// loses part of the minerals over the exemption. When a faction other
// than the player's begins a secret project no one has built, and none
// of its bases is on it already, the player hears of it once; and when it
// moves every base off one project to begin another, the news changes
// with it. 99999 changes nothing.
void base_change(int base, int item) {
    if (item == 99999) return;
    struct base *B = &game_bases[base];
    int old = B->queue[0];
    B->queue[0] = item;
    if (item == old) return;
    int f = B->faction;
    struct faction_data *F = &game_factions[f];
    if (old >= 0) F->units_queue[old]--;
    if (item >= 0) F->units_queue[item]++;
    int penalty = 0;
    const struct rules_basic *R = game_rules_basic;
    if (R->retool_penalty && human(f) && base_making(B->production_last, base) != base_making(B->queue[0], base)) {
        int m = B->minerals_2, ex = R->retool_exemption;
        if (m > ex) penalty = m - (100 - R->retool_penalty) * (m - ex) / 100 - ex;
    }
    B->minerals = B->minerals_2 - penalty;
    game_draw_radius(B->x, B->y, 2, 2);
    if (f == *game_current_faction || *game_skip_project_news) return;

    int q = B->queue[0];
    int begun = q <= -FIRST_PROJECT && game_secret_projects[-q - FIRST_PROJECT] == -1;  // an unbuilt project
    // Whether it leaves a project that is unbuilt (or, when it begins one,
    // any project) and not its own.
    int left = 0;
    if (old <= -FIRST_PROJECT && (begun || game_secret_projects[-old - FIRST_PROJECT] == -1)) {
        int holder = game_secret_projects[iabs(old) - FIRST_PROJECT];
        left = holder < 0 || game_bases[holder].faction != f;
    }
    if (!begun && !left) return;
    if ((*game_state & GAME_STATE_DONE) && !(*game_state & GAME_STATE_FINAL_SCORE_DONE)) return;
    int project = iabs(q) - FIRST_PROJECT;
    int word;
    uint32_t mask;
    bitmask(begun ? project : iabs(old) - FIRST_PROJECT, &word, &mask);
    int on_old = 0, on_new = 0;  // the faction's other bases on each
    for (int i = 0; i < *game_base_count; i++) {
        const struct base *O = &game_bases[i];
        if (O->faction != B->faction || i == base) continue;
        if (O->queue[0] == old) on_old++;
        if (O->queue[0] == B->queue[0]) on_new++;
    }
    if (!begun || on_new) return;

    if (left && (!on_old || game_secret_projects[-old - FIRST_PROJECT] != -1)) {
        // It gives up the old project everywhere for the new one.
        for (int i = 0; i < *game_base_count; i++)
            if (game_bases[i].faction == f && game_bases[i].queue[0] == old) game_bases[i].queue[0] = B->queue[0];
        if (*game_sunspot_duration > 0) return;
        struct faction_def *D = &game_faction_defs[f];
        game_parse_says(0, D->adjective, -1, -1);
        game_parse_say(1, (int)(intptr_t)game_facilities[iabs(old)].name, -1, -1);
        *game_parse_gender = D->noun_gender;
        *game_parse_plural = D->noun_plural;
        game_parse_says(2, D->noun, -1, -1);
        game_parse_say(3, (int)(intptr_t)game_facilities[iabs(q)].name, -1, -1);
        game_popp(*game_script_file, text(0x689474), 0, text(0x689464), 0);
        F->project_intel[word] |= mask;
        bitmask(iabs(old) - FIRST_PROJECT, &word, &mask);
        F->project_intel[word] &= ~mask;
        return;
    }
    if (!(F->project_intel[word] & mask)) {
        if (*game_sunspot_duration > 0 && project != SP_VOICE_OF_PLANET) return;
        struct faction_def *D = &game_faction_defs[f];
        *game_parse_plural = 0;
        *game_parse_gender = D->leader_female;
        game_parse_says(0, D->leader_title, -1, -1);
        *game_parse_gender = D->leader_female;
        *game_parse_plural = 0;
        game_parse_says(1, D->leader_name, -1, -1);
        *game_parse_gender = D->noun_gender;
        *game_parse_plural = D->noun_plural;
        game_parse_says(2, D->noun, -1, -1);
        game_parse_say(3, (int)(intptr_t)game_facilities[iabs(q)].name, -1, -1);
        game_wave_it(treaty(*game_current_faction, f, TREATY_PACT) ? 0x24 : 0x26);
        game_popp(*game_script_file, text(0x689454), 0, text(0x689444), 0);
        F->project_intel[word] |= mask;
    }
    if (project == SP_VOICE_OF_PLANET) {
        game_interlude(11, 0, 1, 0);
        *game_interlude_state |= 0x400;
    }
}

// Picks the base's next item with base_build() and sets it. For a human
// base under a governor, `governor` first has the governor's four kinds
// of work (bits 24 to 27 of its settings) each claim an item, retrying up
// to 8 times in all while two claim the same; base_build() then passes
// over items claimed with better scores. Otherwise, when a human's
// governor turns from something else to a secret project, the player is
// asked whether to build it: no puts back the quick pick without
// projects; the third answer also turns off the governor's projects
// (bit 18), for the base being worked on and for the faction's new bases;
// help shows the project and asks again.
void base_reset(int base, int governor) {
    struct base *B = &game_bases[base];
    int f = B->faction;
    uint8_t bit = (uint8_t)(1u << f);
    int tries = 0;
    if ((*game_human_factions & bit) && (B->governor & 0x80000000u) && governor) {
        uint32_t saved = B->governor;
        for (int i = 0; i < 4; i++) game_gov_claims[i] = 0, game_gov_claim_scores[i] = 999999;
        if (saved & 0x0f000000u) {
            for (int k = 0; k < 4; k++) {
                B->governor = (B->governor & ~0x0f000000u) | (0x01000000u << k);
                int c = game_gov_claims[k] = base_build(base, NULL, NULL, &game_gov_claim_scores[k]);
                int same = 0;
                for (int j = 0; j < k; j++) same |= game_gov_claims[j] == c;
                if (same && ++tries < 8) k = -1;  // all four again
            }
            B->governor = saved;
        }
        base_change(base, base_build(base, NULL, NULL, &tries));
        return;
    }
    int was = B->queue[0];
    base_change(base, base_build(base, NULL, NULL, NULL));
    if (!(*game_human_factions & bit) || !(B->governor & 0x80000000u) || was <= -FIRST_PROJECT
        || B->queue[0] > -FIRST_PROJECT || *game_net_multiplayer || (*game_state & GAME_STATE_DONE))
        return;
    int fac = -B->queue[0];
    int answer;
    for (;;) {
        game_parse_says(0, B->name, -1, -1);
        game_parse_say(1, (int)(intptr_t)game_facilities[fac].name, -1, -1);
        answer = game_popp(*game_script_file, text(0x689494), 0, text(0x689484), 0);
        if (answer != 3) break;
        game_help_topic(11, fac);
    }
    if (!answer) return;
    base_change(base, base_build(-1 - base, NULL, NULL, NULL));
    if (answer > 1) {
        (*game_current_base)->governor &= ~0x40000u;
        game_factions[f].base_governor_adv &= ~0x40000u;
    }
}

// base_reset() for each of an AI faction's bases, those in one region if
// it isn't negative; with `undefended`, only those without a unit of any
// faction on them.
void bases_reset(int region, int faction, int undefended) {
    uint8_t bit = (uint8_t)(1u << faction);
    if (*game_human_factions & bit) return;
    for (int b = 0; b < *game_base_count; b++) {
        const struct base *B = &game_bases[b];
        if (B->faction != faction) continue;
        const struct map_tile *t = tile(B->x, B->y);
        if (region >= 0 && t->region != region) continue;
        if (undefended && (t->bits & MAP_UNIT) && (t->owner_site & 0xf) < 8) continue;
        if ((*game_human_factions & bit) && ((*game_current_base)->state & 0x80000000u)) continue;
        base_reset(b, 0);
    }
}

extern struct hook_module build_module;
#define BUILD (&build_module)

// It writes the game's state (through base_compute()) and its outputs,
// which base_reset() may keep on its own stack.
static void writes_outputs(struct hook_writes *w, int *second, int *third, int *score_out) {
    if (second) hooks_writes_add(w, second, sizeof *second);
    if (third) hooks_writes_add(w, third, sizeof *third);
    if (score_out) hooks_writes_add(w, score_out, sizeof *score_out);
}
SHADOW_WRITER(BUILD, int, base_build, (int a, int *b, int *c, int *d), (a, b, c, d),
              (hooks_writes_game(&w_), writes_outputs(&w_, b, c, d)), "%d", a)
SHADOW2(BUILD, int, base_making, int, int)
SHADOW_WRITER(BUILD, void, base_change, (int a, int b), (a, b), hooks_writes_game(&w_), "%d, %d", a, b)
SHADOW_WRITER(BUILD, void, bases_reset, (int a, int b, int c), (a, b, c), hooks_writes_game(&w_), "%d, %d, %d", a, b, c)

// A human base's reset may ask the player about a secret project and act
// on the answer, which a check can't give, so those run the original
// unchecked; the sweep checks them on a copy, where no question gets one.
SHADOW_WRITER_CHECK(void, base_reset, (int a, int b), (a, b), orig_base_reset(a, b), hooks_writes_game(&w_), "%d, %d", a, b)
static void shadow_base_reset(int a, int b) {
    if (hooks_nested) {
        orig_base_reset(a, b);
        return;
    }
    if (shadow_bypass()) {
        base_reset(a, b);
        return;
    }
    stats_base_reset.calls++;
    shadow_tick(BUILD);
    if (human(game_bases[a].faction)) orig_base_reset(a, b);
    else check_base_reset(a, b);
}

// A secret project no one has built, from the n'th on, as an item.
static int unbuilt_project(int n) {
    for (int i = 0; i < SP_COUNT; i++) {
        int sp = (n + i) % SP_COUNT;
        if (game_secret_projects[sp] == -1) return -FIRST_PROJECT - sp;
    }
    return -FIRST_PROJECT;
}

// base_change() to each kind of item, from a project or not, for an AI
// faction or, now and then, a human one (the retooling penalty).
static void sweep_change(int b) {
    struct base *B = &game_bases[b];
    int k = b + *game_turn;
    int items[] = {unbuilt_project(k), unbuilt_project(k + 7), 1 + (k & 1), -(1 + k % 64), B->queue[0], 99999};
    int was = B->queue[0];
    uint8_t humans = *game_human_factions;
    if (k % 3 == 0) B->queue[0] = unbuilt_project(k + 3);
    if (k % 5 == 0) *game_human_factions |= (uint8_t)(1u << B->faction);
    check_base_change(b, items[k % 6]);
    B->queue[0] = was;
    *game_human_factions = humans;
}

// base_reset(), now and then as a human's base under a governor doing all
// four kinds of work.
static void sweep_reset(int b) {
    struct base *B = &game_bases[b];
    if ((b + *game_turn) % 4) {
        check_base_reset(b, 0);
        return;
    }
    uint8_t humans = *game_human_factions;
    uint32_t governor = B->governor;
    *game_human_factions |= (uint8_t)(1u << B->faction);
    B->governor |= 0x8f000000u;
    check_base_reset(b, 1);
    *game_human_factions = humans;
    B->governor = governor;
}

// Every base's pick, the way base_reset() asks for it. On alternate turns
// the quick pick without projects is checked too, and otherwise the full
// pick at a difficulty of at least 3: below it the AI builds only secret
// projects a human faction has the tech for, which leaves none in a game
// the AI plays alone.
static void build_sweep(void) {
    if (!*game_map) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    int difficulty = *game_difficulty;
    for (int b = 0; b < *game_base_count; b++) {
        int second = 0, third = 0, score = 0;
        check_base_build(b, &second, &third, &score);
        if (*game_turn & 1) check_base_build(-1 - b, NULL, NULL, NULL);
        else if (difficulty < 3) {
            *game_difficulty = 3;
            check_base_build(b, &second, &third, &score);
            *game_difficulty = difficulty;
        }
        for (int item = -133; item < 520; item += 37) check_base_making(item, b);
        sweep_change(b);
        sweep_reset(b);
    }
    // One faction's bases, in all regions or in its first base's.
    int f = 1 + *game_turn % 7;
    for (int b = 0; b < *game_base_count; b++) {
        if (game_bases[b].faction != f) continue;
        int region = (*game_turn & 2) ? -1 : tile(game_bases[b].x, game_bases[b].y)->region;
        check_bases_reset(region, f, *game_turn & 1);
        break;
    }
    hooks_sweeping = sweeping;
}

static const struct hook build_hooks[] = {
    HOOK(0x4e4700, base_making, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x08),              // sub esp, 8
    HOOK(0x4e5a60, base_change, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x24),              // sub esp, 36
    HOOK(0x4e5fe0, base_reset, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x08),               // sub esp, 8
    HOOK(0x4e6300, bases_reset, 0x55, 0x8b, 0xec, 0x51, 0x8b, 0x4d, 0x0c),       // push ecx; mov ecx, [ebp+12]
    HOOK(0x4f81a0, base_build, 0x55, 0x8b, 0xec, 0xb8, 0x6c, 0x1f, 0x00, 0x00),  // mov eax, 0x1f6c (then the stack probe)
};
struct hook_module build_module = {"build", build_hooks, HOOK_COUNT(build_hooks), build_sweep, NULL, -1};
