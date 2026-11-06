// Units: what a design can do and cost, how fast it moves, how its weapon
// and armor rate, and the morale bonuses that bases, secret projects and
// social choices give it. Reimplemented from terranx.exe 2.0's
// disassembly; names follow Thinker (MIT) and OpenSMACX (Brendan Casey,
// GPL-3.0-or-later).
//
// Like the originals these don't check their arguments. arm_val() takes
// an armor, but the game's own callers hand it a prototype, which indexes
// the armor table past its end; Chiron's callers do the same.
#include "veh.h"

#include <string.h>

#include "base.h"
#include "game.h"
#include "map.h"
#include "shadow.h"
#include "tech.h"

enum {
    FAC_SKUNKWORKS = 10, FAC_COMMAND_CENTER = 27, FAC_NAVAL_YARD = 28, FAC_BIOENHANCEMENT = 30,
};
enum {  // secret projects, as indices into game_secret_projects
    SP_COMMAND_NEXUS = 1, SP_XENOEMPATHY_DOME = 8, SP_NEURAL_AMPLIFIER = 9, SP_MARITIME_CONTROL_CENTER = 10,
    SP_CYBORG_FACTORY = 17, SP_DREAM_TWISTER = 19, SP_TELEPATHIC_MATRIX = 30, SP_NETHACK_TERMINUS = 34,
};
enum {  // unit_proto.abilities
    ABL_DEEP_RADAR = 0x2, ABL_AIR_SUPERIORITY = 0x20, ABL_ARTILLERY = 0x8000, ABL_ANTIGRAV_STRUTS = 0x400, ABL_SLOW = 0x800000,
    ABL_SOPORIFIC_GAS = 0x1000000, ABL_DISSOCIATIVE_WAVE = 0x2000000, ABL_MARINE_DETACHMENT = 0x4000000,
    ABL_FUEL_NANOCELLS = 0x8000000, ABL_ALGO_ENHANCEMENT = 0x10000000,
};
enum {
    BSC_LOCUSTS_OF_CHIRON = 10, BSC_SEALURK = 14, BSC_SPORE_LAUNCHER = 15, BSC_FUNGAL_TOWER = 19,
    BSC_UNITY_MINING_LASER = 20, BASIC_PROTOS = 64,
    WEAPON_PROBE = 11, WEAPON_TECTONIC = 21, WMODE_TRANSPORT = 7,
    PLAN_PROBE = 11, BONUS_FREE_ABILITY = 15, TECH_FLAG_PROBE = 0x2,
    RULE_FLAG_FREE_PROTO = 0x200, RULE_FLAG_MORALE = 0x8000,
    RULES_NO_COLONY_PODS = 0x400000, MAP_UNK_40000000_ = 0x40000000, FACTION_FLAG_UNK_40000000 = 0x40000000,
};

static inline const struct unit_proto *proto_of(int proto) { return &game_protos[proto]; }
static inline int triad(int proto) { return game_chassis[proto_of(proto)->chassis].triad; }
static inline int built(int base, int fac) {  // the inline test: any base number, even negative
    return (game_bases[base].facilities_built[fac / 8] >> (fac & 7)) & 1;
}
static inline int owns_project(int sp, int faction) {
    int b = game_secret_projects[sp];
    return b >= 0 && game_bases[b].faction == faction;
}
// A native design: a basic one with a psi weapon, or the spore launcher.
static inline int native(int proto) {
    return proto < BASIC_PROTOS && (game_weapons[proto_of(proto)->weapon].offense < 0 || proto == BSC_SPORE_LAUNCHER);
}

// Whether the design has the ability, counting what the faction gets free
// once it has the tech (an aliens' deep radar, a faction bonus), and the
// algorithmic enhancement a tectonic payload gets from the Nethack
// Terminus.
int has_abil(int proto, uint32_t ability) {
    int f = proto / BASIC_PROTOS;
    if (proto_of(proto)->abilities & ability) return 1;
    if (!f) return 0;
    const struct faction_def *D = &game_faction_defs[f];
    if ((D->rule_flags & RULE_FLAG_ALIEN) && ability == ABL_DEEP_RADAR) return 1;
    for (int i = 0; i < D->bonus_count; i++) {
        if (D->bonus_id[i] != BONUS_FREE_ABILITY) continue;
        int a = D->bonus_val1[i];
        if (has_tech(game_abilities[a].preq_tech, f) && (ability & (1u << (a & 31)))) return 1;
    }
    return proto_of(proto)->weapon == WEAPON_TECTONIC && ability == ABL_ALGO_ENHANCEMENT
           && owns_project(SP_NETHACK_TERMINUS, f);
}

// The best reactor the faction has the tech for, 1 to 4.
int best_reactor(int faction) {
    for (int r = 3; r >= 0; r--)
        if (has_tech(game_reactors[r].preq_tech, faction)) return r + 1;
    return 1;
}

// A planet buster's reactor, or 0 for any other design.
int planet_buster2(int proto) { return proto_of(proto)->plan == PLAN_PLANET_BUSTER ? proto_of(proto)->reactor : 0; }

// The units a unit can carry: a native one's morale plus one.
int veh_cargo(int veh) {
    int proto = game_vehs[veh].proto;
    int carry = proto_of(proto)->carry;
    if (carry && native(proto)) return game_vehs[veh].morale + 1;
    return carry;
}

// A psi strength, with the faction's PSI bonus, and half again with the
// Dream Twister for an attack, or the Neural Amplifier (and, if asked,
// fungus) for a defense.
int psi_factor(int value, int faction, int attack, int fungus) {
    int psi = game_faction_defs[faction].rule_psi;
    int v = psi ? (psi + 100) * value / 100 : value;
    if (attack) {
        if (game_secret_projects[SP_DREAM_TWISTER] < 0) return v;
        if (owns_project(SP_DREAM_TWISTER, faction)) v += v / 2;
        return v;
    }
    if (owns_project(SP_NEURAL_AMPLIFIER, faction)) v += v / 2;
    if (fungus) v += v / 2;
    return v;
}

static int smacx_weapon(int weapon) { return !*game_expansion && (weapon == 12 || weapon == 13 || weapon == 14); }

// A weapon's strength as the AI rates it; a psi weapon against the best
// armor the faction's enemies field.
int weap_strat(int weapon, int faction) {
    if (smacx_weapon(weapon)) return 1;
    int off = game_weapons[weapon].offense;
    if (off >= 0) return off;
    const struct rules_basic *R = game_rules_basic;
    return psi_factor(game_factions[faction].enemy_best_armor_value * R->psi_combat_ratio_atk[0] / R->psi_combat_ratio_def[0],
                      faction, 1, 0);
}

// The same for a design's weapon.
int weap_val(int proto, int faction) { return weap_strat(proto_of(proto)->weapon, faction); }

// An armor's strength as the AI rates it; psi armor against the best
// weapon the faction's enemies field.
int arm_strat(int armor, int faction) {
    if (!*game_expansion && armor > 9) return 1;
    int def = game_armor[armor].defense;
    if (def >= 0) return def;
    const struct rules_basic *R = game_rules_basic;
    return psi_factor(game_factions[faction].enemy_best_weapon_value * R->psi_combat_ratio_def[0] / R->psi_combat_ratio_atk[0],
                      faction, 0, 0);
}

// Twice that, and for no faction the armor's own value doubled.
int arm_val(int armor, int faction) {
    if (faction < 0) return game_armor[armor].defense * 2;
    return arm_strat(armor, faction) * 2;
}

// A design's moves a turn, in thirds of a square (road moves). A fungal
// tower doesn't move.
int speed_proto(int proto) {
    if (proto == BSC_FUNGAL_TOWER) return 0;
    const struct unit_proto *P = proto_of(proto);
    int air = triad(proto) == TRIAD_AIR;
    int s = game_chassis[P->chassis].speed;
    if (air) s += P->reactor * 2;
    if (game_weapons[P->weapon].mode == WMODE_TRANSPORT) s--;
    if (has_abil(proto, ABL_SLOW)) s--;
    if (has_abil(proto, ABL_ANTIGRAV_STRUTS)) s += air ? P->reactor * 2 : 1;
    if (air) {
        if (has_abil(proto, ABL_FUEL_NANOCELLS)) s += 2;
        if (owns_project(SP_CLOUDBASE_ACADEMY, proto / BASIC_PROTOS)) s += 2;
        if (has_abil(proto, ABL_AIR_SUPERIORITY)) s = s * 4 / 5;
        if (game_weapons[P->weapon].mode == WMODE_TRANSPORT) s /= 2;
    }
    if (s < 1) s = 1;
    if (s > 99) s = 99;
    return s * game_rules_basic->move_rate_roads;
}

// Morale a base gives the units it builds: 2 for the triad's facility or
// project, 2 more for the Bioenhancement Center or Cyborg Factory, halved
// under MORALE of -2 or less.
int morale_mod(int base, int faction, int triad_) {
    int m = 0;
    static const int fac[3] = {FAC_COMMAND_CENTER, FAC_NAVAL_YARD, FAC_AEROSPACE_COMPLEX};
    static const int sp[3] = {SP_COMMAND_NEXUS, SP_MARITIME_CONTROL_CENTER, SP_CLOUDBASE_ACADEMY};
    if (triad_ >= 0 && triad_ < 3 && (built(base, fac[triad_]) || owns_project(sp[triad_], faction))) m = 2;
    if (built(base, FAC_BIOENHANCEMENT) || owns_project(SP_CYBORG_FACTORY, faction)) m += 2;
    if (game_factions[faction].se_pending[SE_MORALE] < -1) m /= 2;
    return m;
}

// Morale for native life a base breeds.
int breed_mod(int base, int faction) {
    int n = owns_project(SP_XENOEMPATHY_DOME, faction);
    n += owns_project(SP_PHOLUS_MUTAGEN, faction);
    n += owns_project(SP_VOICE_OF_PLANET, faction);
    n += built(base, FAC_CENTAURI_PRESERVE);
    n += built(base, FAC_TEMPLE_OF_PLANET);
    n += built(base, FAC_BIOLOGY_LAB);
    n += built(base, FAC_BIOENHANCEMENT) || owns_project(SP_CYBORG_FACTORY, faction);
    return n;
}

// A base's strength against native life: breed_mod()'s list (asking
// has_fac() about the facilities), plus a PSI bonus, the Dream Twister and
// the Neural Amplifier.
int worm_mod(int base, int faction) {
    int n = owns_project(SP_XENOEMPATHY_DOME, faction);
    n += owns_project(SP_PHOLUS_MUTAGEN, faction);
    n += owns_project(SP_VOICE_OF_PLANET, faction);
    n += has_fac(FAC_CENTAURI_PRESERVE, base, 0) != 0;
    n += has_fac(FAC_TEMPLE_OF_PLANET, base, 0) != 0;
    n += has_fac(FAC_BIOLOGY_LAB, base, 0) != 0;
    n += has_fac(FAC_BIOENHANCEMENT, base, 0) || owns_project(SP_CYBORG_FACTORY, faction);
    if (game_faction_defs[faction].rule_psi) n++;
    n += owns_project(SP_DREAM_TWISTER, faction);
    n += owns_project(SP_NEURAL_AMPLIFIER, faction);
    return n;
}

// Native life's morale: growing with the turn, more against a faction
// that has committed atrocities or set off tectonic payloads, and for a
// unit that is bred, a locust swarm or hardened. A fungal tower's is the
// fungus on high ground around it, less 2. The unit is read even when
// negative.
int morale_alien(int veh, int faction_vs) {
    int t = *game_turn;
    int m = t < 45 ? 0 : t < 90 ? 1 : t < 170 ? 2 : t < 250 ? 3 : t >= 330 ? 6 : 4;
    if (faction_vs > 0)
        m += (game_factions[faction_vs].major_atrocities != 0) + (game_tectonic_detonations[faction_vs] != 0);
    const struct veh *V = &game_vehs[veh];
    if (veh >= 0) {
        if (V->state & 0x100) m++;
        if (V->proto == BSC_LOCUSTS_OF_CHIRON) m++;
        m += (V->flags >> 3) & 3;
    }
    if (V->proto == BSC_FUNGAL_TOWER) {
        int x0 = V->x, y0 = V->y, n = 0, w = *game_map_width;
        for (int i = 1; i <= 8; i++) {
            int x = game_radius_x[i] + x0, y = game_radius_y[i] + y0;
            if (!(*game_map_flat & 1)) {
                if (x < 0) x += w;
                else if (x >= w) x -= w;
            }
            if (y < 0 || y >= *game_map_height || x < 0 || x >= w) continue;
            const struct map_tile *T = &(*game_map)[(x >> 1) + y * *game_map_half_width];
            if ((T->bits & MAP_FUNGUS) && (T->climate & 0xe0) >= 0x40) n++;
        }
        m = n - 2;
    }
    return m < 0 ? 0 : m > 6 ? 6 : m;
}

// A unit's morale level, 0 (very green) to 6 (elite). A probe team's
// counts PROBE, the Telepathic Matrix and probe techs, from at least 2;
// native life's, native rules; otherwise MORALE and the faction's own
// bonus, softened by a Children's Creche (and a Brood Pit for native
// units) at home, and less one for a home base in riot when asked.
int morale_veh(int veh, int riot, int faction_vs) {
    const struct veh *V = &game_vehs[veh];
    int f = V->faction;
    if (!f) return morale_alien(veh, faction_vs);
    const struct unit_proto *P = proto_of(V->proto);
    const struct faction_data *F = &game_factions[f];
    if (P->plan == PLAN_PROBE) {
        int m = F->se[SE_PROBE];
        m = m < 0 ? 0 : m > 3 ? 3 : m;
        if (owns_project(SP_TELEPATHIC_MATRIX, f)) m += 2;
        for (int t = 0; t < TECH_COUNT; t++)
            if ((game_tech[t].flags & TECH_FLAG_PROBE) && has_tech(t, f)) m++;
        int r = V->morale + m;
        return r < 2 ? 2 : r > 6 ? 6 : r;
    }
    if (V->proto < BASIC_PROTOS && game_weapons[P->weapon].offense < 0) return V->morale > 6 ? 6 : V->morale;
    int m = F->se[SE_MORALE];
    m = m < -4 ? -4 : m > 4 ? 4 : m;
    if (m <= -2) m++;
    else if (m >= 2) m--;
    const struct faction_def *D = &game_faction_defs[f];
    if (D->rule_morale < 0) m += D->rule_morale;
    int home = V->home_base;
    if (home >= 0) {
        if (built(home, FAC_CHILDREN_CRECHE) && m < 0) m /= 2;
        if (built(home, FAC_BROOD_PIT) && native(V->proto) && m < 0) m /= 2;
    }
    if (D->rule_morale > 0) m += D->rule_morale;
    int steady = D->rule_flags & RULE_FLAG_MORALE;
    if (steady && m < 0) m = 0;
    if (riot && home >= 0 && (game_bases[home].state & BASE_STATE_DRONE_RIOT) && !steady) {
        m--;
        m = m < 0 ? 0 : m > 6 ? 6 : m;
    }
    int r = m + V->morale;
    return r < 0 ? 0 : r > 6 ? 6 : r;
}

// A design's cost without the prototype extra.
int base_cost(int proto) {
    const struct unit_proto *P = proto_of(proto);
    return game_proto_cost(P->chassis, P->weapon, P->armor, 0, P->reactor);
}

// What a design costs at a base: a quarter off native units with a Brood
// Pit; colony pods at least 1; and the first unit of a new design costs
// extra by triad, unless the faction builds prototypes free, plays at the
// two lowest levels, or the base has a Skunkworks. *prototype, if given,
// says whether the extra was charged.
int veh_cost(int proto, int base, int *prototype) {
    const struct unit_proto *P = proto_of(proto);
    int cost = P->cost;
    if (native(proto) && built(base, FAC_BROOD_PIT)) cost = cost * 3 / 4;
    if (P->plan == PLAN_COLONY && base >= 0) cost = cost < 1 ? 1 : cost > 999 ? 999 : cost;
    if (proto < BASIC_PROTOS || (P->flags & PROTO_FLAG_BUILT)) {
        if (prototype) *prototype = 0;
        return cost;
    }
    int f = proto / BASIC_PROTOS, extra;
    if ((game_faction_defs[f].rule_flags & RULE_FLAG_FREE_PROTO) || game_factions[f].diff_level <= 1) extra = 0;
    else extra = game_rules_basic->extra_cost_prototype[triad(proto) == TRIAD_SEA ? 1 : triad(proto) == TRIAD_AIR ? 2 : 0];
    int p = (base_cost(proto) * extra + 50) / 100;
    if (base >= 0 && built(base, FAC_SKUNKWORKS)) p = 0;
    if (prototype) *prototype = p != 0;
    return cost + p;
}

// The defenders the AI wants in a base: more for its size, its secret
// projects, headquarters or an objective, and an untrustworthy neighbour
// at sea, fewer in a region set on colonizing or under low SUPPORT; 1 to 10.
int garrison_check(int base) {
    const struct base *B = &game_bases[base];
    int f = B->faction;
    const struct map_tile *T = &(*game_map)[(B->x >> 1) + B->y * *game_map_half_width];
    int region = T->region;
    int projects = 0;
    for (int i = 0; i < SP_COUNT; i++) projects += game_secret_projects[i] == base;
    int g = (projects + 2) / 3 + (B->pop_size + 1) / 4 + 1;
    if (built(base, FAC_HEADQUARTERS) || (T->bits & MAP_UNK_40000000_) || game_is_objective(base)) g++;
    if (game_is_objective(base) && (game_factions[f].flags & FACTION_FLAG_UNK_40000000)) g++;
    if (game_factions[f].region_base_plan[region] == PLAN_COLONY) g--;
    int z = zoc_sea(B->x, B->y, f) - 1;
    if (z > 0 && ((game_factions[f].diplo_status[z] & 0x30) || (game_factions[z].diplo_status[f] & 0x20)
                  || game_factions[z].integrity_blemishes >= 4))
        g++;
    int support = game_factions[f].se_pending[SE_SUPPORT];
    if (support <= -4) g--;
    if (support <= -3 && g > 2) g--;
    return g < 1 ? 1 : g > 10 ? 10 : g;
}

// Whether the faction may build the design (at the base, if given): an
// active design it hasn't made obsolete, whose tech it has, a colony pod
// only if the rules allow them, a ship only at a coastal base, and no
// Alien Crossfire parts without it.
int veh_avail(int proto, int faction, int base) {
    const struct unit_proto *P = proto_of(proto);
    if (!(P->flags & PROTO_FLAG_ACTIVE)) return 0;
    uint8_t bit = (uint8_t)(1u << (faction & 31));
    if (P->obsolete_factions & bit) return 0;
    if (proto < BASIC_PROTOS) {
        int t = P->preq_tech;
        if (faction <= 0) return 0;
        if (t != TECH_NONE) {
            if (t < 0 || t >= TECH_COUNT - 1) return 0;
            int p1 = game_tech[t].preq1, p2 = game_tech[t].preq2;
            if (p1 < -1 || (p2 < -1 && p1 != -1)) return 0;
            if (!(game_tech_achieved[t] & bit)) return 0;
        }
    }
    if (P->plan == PLAN_COLONY && (*game_rules & RULES_NO_COLONY_PODS)) return 0;
    if (base >= 0 && triad(proto) == TRIAD_SEA && !is_coast(game_bases[base].x, game_bases[base].y, 0)) return 0;
    if (!*game_expansion) {
        if (P->armor > 9) return 0;
        int w = P->weapon;
        if (smacx_weapon(w) || w == 24 || w == 25) return 0;
        if (P->abilities & (ABL_SOPORIFIC_GAS | ABL_DISSOCIATIVE_WAVE | ABL_MARINE_DETACHMENT | ABL_FUEL_NANOCELLS
                            | ABL_ALGO_ENHANCEMENT))
            return 0;
        if (proto >= BSC_SEALURK && proto <= BSC_UNITY_MINING_LASER) return 0;
    }
    return proto < BASIC_PROTOS || proto / BASIC_PROTOS == faction;
}

// Whether a design can bombard: one with a weapon and no psi armor (or
// the spore launcher), on land if it has artillery, at sea only if asked,
// never in the air.
int can_arty(int proto, int sea) {
    const struct unit_proto *P = proto_of(proto);
    if (game_weapons[P->weapon].offense <= 0 && proto != BSC_SPORE_LAUNCHER) return 0;
    if (game_armor[P->armor].defense < 0 && proto != BSC_SPORE_LAUNCHER) return 0;
    switch (triad(proto)) {
    case TRIAD_SEA: return sea;
    case TRIAD_AIR: return 0;
    default: return has_abil(proto, ABL_ARTILLERY);
    }
}

// Counts (or adds up) something over the units of the stack the unit is
// in, from its head. `type` picks what (STACK_*), and for most kinds one of
// the conditions, when not negative, keeps to one faction's units.
int stack_check(int veh, int type, int c1, int c2, int c3) {
    int n = 0;
    if (veh < 0) return 0;
    while (game_vehs[veh].prev_in_stack >= 0) veh = game_vehs[veh].prev_in_stack;
    for (int v = veh; v >= 0; v = game_vehs[v].next_in_stack) {
        const struct veh *V = &game_vehs[v];
        const struct unit_proto *P = proto_of(V->proto);
        int f = V->faction, tr = triad(V->proto);
#define BY(c) ((c) < 0 || f == (c))
        switch (type) {
        case 0: n += BY(c2) && V->proto == c1; break;  // of a design
        case 1: n += BY(c1); break;                       // units
        case 2: n += BY(c2) && P->plan == c1; break;     // of a plan
        case 3: n += BY(c2) && tr == c1; break;          // of a triad
        case 4: if (BY(c1)) n += game_weapons[P->weapon].offense; break;
        case 5: if (BY(c1)) n += game_armor[P->armor].defense; break;
        case 6: n += BY(c2) && has_abil(V->proto, c1); break;
        case 7: if (BY(c1)) n += P->cost; break;
        case 8:  // room aboard: ships' cargo, less a place for each land unit
            if (!BY(c1)) break;
            if (tr == TRIAD_LAND) n--;
            else if (tr == TRIAD_SEA) n += veh_cargo(v);
            break;
        case 9: n += BY(c2) && (int8_t)V->order == c1; break;
        case 10: n += f == c1; break;
        case 11: n += BY(c3) && (int)(V->state & c1) == c2; break;
        case 12: n += BY(c1) && arm_strat(P->armor, f) > weap_strat(P->weapon, f); break;  // defenders
        case 13: n += BY(c1) && game_chassis[P->chassis].missile != 0; break;
        case 14: n += BY(c1) && (P->plan == PLAN_DEFENSIVE || P->plan == PLAN_COMBAT); break;
        case 15: n += BY(c1) && can_arty(V->proto, 1); break;
        case 16: n += BY(c1) && (P->plan == PLAN_DEFENSIVE || P->plan == PLAN_RECON || P->plan == PLAN_COMBAT); break;
        case 17: n += BY(c2) && P->group == c1; break;
        case 18: n += BY(c1) && tr == TRIAD_AIR && game_chassis[P->chassis].range > 1; break;
        case 19: n += BY(c1) && game_weapons[P->weapon].offense == 0; break;
        default: break;
        }
#undef BY
    }
    return n;
}

extern struct hook_module veh_module;
#define VEH (&veh_module)

SHADOW2(VEH, int, has_abil, int, uint32_t)
SHADOW1(VEH, int, best_reactor, int)
SHADOW1(VEH, int, planet_buster2, int)
SHADOW1(VEH, int, veh_cargo, int)
SHADOW4(VEH, int, psi_factor, int, int, int, int)
SHADOW2(VEH, int, weap_strat, int, int)
SHADOW2(VEH, int, weap_val, int, int)
SHADOW2(VEH, int, arm_strat, int, int)
SHADOW2(VEH, int, arm_val, int, int)
SHADOW1(VEH, int, speed_proto, int)
SHADOW3(VEH, int, morale_mod, int, int, int)
SHADOW2(VEH, int, breed_mod, int, int)
SHADOW2(VEH, int, worm_mod, int, int)
SHADOW2(VEH, int, morale_alien, int, int)
SHADOW3(VEH, int, morale_veh, int, int, int)
SHADOW1(VEH, int, base_cost, int)
SHADOW1(VEH, int, garrison_check, int)
SHADOW3(VEH, int, veh_avail, int, int, int)
SHADOW2(VEH, int, can_arty, int, int)
SHADOW5(VEH, int, stack_check, int, int, int, int, int)

// veh_cost writes whether the extra was charged.
SHADOW_DECLARE(int, veh_cost, (int a, int b, int *c))
static int check_veh_cost(int a, int b, int *c) {
    int want_p = -1, got_p = -1;
    hooks_nested++;
    int want = orig_veh_cost(a, b, c ? &want_p : NULL);
    hooks_nested--;
    int got = veh_cost(a, b, c ? &got_p : NULL);
    if (got != want || got_p != want_p)
        hooks_mismatch(&stats_veh_cost, "(%d, %d) = %d, %d, original %d, %d", a, b, got, got_p, want, want_p);
    if (c) *c = want_p;
    return want;
}
SHADOW_ENTRY(VEH, int, veh_cost, (int a, int b, int *c), (a, b, c))

// The checks again with the settings these functions read varied: the
// factions' PSI and MORALE bonuses, social engineering, who holds the
// secret projects that count, some of the bases' facilities and some
// units' designs. They are put back afterwards; nothing else
// changes, as these functions only read.
static const int varied_projects[] = {SP_COMMAND_NEXUS, SP_XENOEMPATHY_DOME, SP_NEURAL_AMPLIFIER,
                                      SP_MARITIME_CONTROL_CENTER, SP_PHOLUS_MUTAGEN, SP_CYBORG_FACTORY,
                                      SP_DREAM_TWISTER, SP_TELEPATHIC_MATRIX, SP_VOICE_OF_PLANET,
                                      SP_NETHACK_TERMINUS, SP_CLOUDBASE_ACADEMY};
enum { VARIED = sizeof varied_projects / sizeof varied_projects[0] };
static void sweep_varied(void) {
    static const int psi[5] = {0, 10, 25, -20, 50};
    struct saved {
        int32_t psi, morale, se_morale, se_probe, pending_morale, pending_support;
        uint32_t flags;
    } saved[FACTION_COUNT];
    int32_t held[VARIED];
    int t = *game_turn, bases = *game_base_count;
    for (int f = 0; f < FACTION_COUNT; f++) {
        struct faction_def *D = &game_faction_defs[f];
        struct faction_data *F = &game_factions[f];
        saved[f] = (struct saved){D->rule_psi, D->rule_morale, F->se[SE_MORALE], F->se[SE_PROBE],
                                  F->se_pending[SE_MORALE], F->se_pending[SE_SUPPORT], D->rule_flags};
        int k = f + t;
        D->rule_psi = psi[k % 5];
        D->rule_morale = k % 5 - 2;
        if (k & 8) D->rule_flags ^= RULE_FLAG_MORALE;
        F->se[SE_MORALE] = k % 11 - 5;
        F->se[SE_PROBE] = k % 6 - 1;
        F->se_pending[SE_MORALE] = (f + 3 * t) % 7 - 3;
        F->se_pending[SE_SUPPORT] = k % 7 - 5;
    }
    static uint8_t facilities[BASE_COUNT][12];
    static const int toggled[] = {FAC_HEADQUARTERS, FAC_CHILDREN_CRECHE, FAC_BIOLOGY_LAB, FAC_SKUNKWORKS, FAC_COMMAND_CENTER,
                                  FAC_NAVAL_YARD, FAC_AEROSPACE_COMPLEX, FAC_BIOENHANCEMENT, FAC_CENTAURI_PRESERVE,
                                  FAC_TEMPLE_OF_PLANET, FAC_BROOD_PIT};
    for (int b = 0; b < bases; b++) {
        uint8_t *built_ = game_bases[b].facilities_built;
        memcpy(facilities[b], built_, sizeof facilities[b]);
        for (unsigned i = 0; i < sizeof toggled / sizeof toggled[0]; i++)
            if ((b + t + i) % 3 == 0) built_[toggled[i] / 8] ^= (uint8_t)(1u << (toggled[i] & 7));
    }
    for (int i = 0; i < VARIED; i++) {
        int32_t *sp = &game_secret_projects[varied_projects[i]];
        held[i] = *sp;
        if (bases) *sp = ((i + t) & 1) ? (i * 7 + t) % bases : -1;
    }
    for (int f = 0; f < FACTION_COUNT; f++) {
        for (int v = -7; v < 30; v += 3) check_psi_factor(v, f, v & 1, (v >> 1) & 1);
        for (int a = 0; a < ARMOR_COUNT; a++) check_arm_strat(a, f);
        for (int w = 0; w < WEAPON_COUNT; w++) check_weap_strat(w, f);
    }
    for (int p = 0; p < PROTO_COUNT; p++)
        if (game_protos[p].flags & PROTO_FLAG_ACTIVE) check_speed_proto(p), check_has_abil(p, ABL_ALGO_ENHANCEMENT);
    for (int p = 0; p < PROTO_COUNT; p++)
        if (game_protos[p].flags & PROTO_FLAG_ACTIVE) check_veh_cost(p, bases ? p % bases : -1, NULL);
    for (int b = 0; b < bases; b++) {
        int f = game_bases[b].faction;
        for (int tr = 0; tr < 3; tr++) check_morale_mod(b, f, tr);
        check_breed_mod(b, f);
        check_worm_mod(b, f);
        check_garrison_check(b);
    }
    for (int v = 0; v < *game_veh_count; v++) check_morale_veh(v, 1, (v + t) % FACTION_COUNT);
    // A few units given other designs, so that every kind of stack is
    // counted, air units early in a game too.
    int designs[PROTO_COUNT], count = 0;
    for (int p = 0; p < PROTO_COUNT; p++)
        if (game_protos[p].flags & PROTO_FLAG_ACTIVE) designs[count++] = p;
    for (int v = 0; v < *game_veh_count && v < 24 && count; v++) {
        struct veh *V = &game_vehs[v];
        int16_t proto = V->proto;
        V->proto = (int16_t)designs[(v * 13 + t) % count];
        for (int type = 0; type < 20; type++)
            check_stack_check(v, type, type == 6 ? (int)(1u << (v % 32)) : (v + t) % 4, -1, -1);
        check_can_arty(V->proto, v & 1);
        check_veh_cargo(v);
        check_morale_veh(v, 0, 0);
        V->proto = proto;
    }
    for (int i = 0; i < VARIED; i++) game_secret_projects[varied_projects[i]] = held[i];
    for (int b = 0; b < bases; b++) memcpy(game_bases[b].facilities_built, facilities[b], sizeof facilities[b]);
    for (int f = 0; f < FACTION_COUNT; f++) {
        struct faction_def *D = &game_faction_defs[f];
        struct faction_data *F = &game_factions[f];
        D->rule_psi = saved[f].psi, D->rule_morale = saved[f].morale, D->rule_flags = saved[f].flags;
        F->se[SE_MORALE] = saved[f].se_morale, F->se[SE_PROBE] = saved[f].se_probe;
        F->se_pending[SE_MORALE] = saved[f].pending_morale, F->se_pending[SE_SUPPORT] = saved[f].pending_support;
    }
}

// Every design against every faction and a spread of bases, units and
// values.
static void veh_sweep(void) {
    if (!*game_map) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    int bases = *game_base_count, vehs = *game_veh_count, turn = *game_turn;
    for (int p = 0; p < PROTO_COUNT; p++) {
        if (!(game_protos[p].flags & PROTO_FLAG_ACTIVE) && p % 16) continue;
        int f = (p + turn) % FACTION_COUNT, b = bases ? (p + turn) % bases : -1;
        check_has_abil(p, 1u << ((p + turn) % 32));
        check_has_abil(p, ABL_DEEP_RADAR);
        check_has_abil(p, ABL_ALGO_ENHANCEMENT);
        check_planet_buster2(p);
        check_weap_val(p, f);
        check_arm_val(p, f);
        check_arm_val(p % ARMOR_COUNT, -1);
        check_speed_proto(p);
        check_base_cost(p);
        int charged;
        check_veh_cost(p, b, &charged);
        check_veh_cost(p, -1, NULL);
        for (int g = 0; g < FACTION_COUNT; g++) check_veh_avail(p, g, (g & 1) ? b : -1);
    }
    for (int f = -1; f <= FACTION_COUNT; f++) {
        if (f >= 0 && f < FACTION_COUNT) {
            check_best_reactor(f);
            for (int w = 0; w < WEAPON_COUNT; w++) check_weap_strat(w, f);
            for (int a = 0; a < ARMOR_COUNT; a++) check_arm_strat(a, f);
            for (int v = -7; v < 30; v += 3) check_psi_factor(v, f, v & 1, (v >> 1) & 1);
        }
    }
    for (int b = 0; b < bases; b++) {
        int f = game_bases[b].faction;
        for (int t = -1; t <= 3; t++) check_morale_mod(b, f, t);
        check_morale_mod(b, (f + 1) % FACTION_COUNT, b % 3);
        check_breed_mod(b, f);
        check_worm_mod(b, f);
        check_garrison_check(b);
    }
    for (int v = 0; v < vehs; v++) {
        const struct veh *V = &game_vehs[v];
        int type = (v + turn) % 21;
        const int conds[] = {-1, 0, 1, 2, 3, V->proto, V->faction, 0x100, 0x2000000, 0x8000};
        for (unsigned i = 0; i < sizeof conds / sizeof conds[0]; i++) {
            check_stack_check(v, type, conds[i], -1, -1);
            check_stack_check(v, type, conds[i], V->faction, (v & 1) ? V->faction : -1);
            check_stack_check(v, type, conds[i], 0, -1);
        }
        check_can_arty(V->proto, v & 1);
        check_veh_cargo(v);
        check_morale_veh(v, v & 1, (v + turn) % FACTION_COUNT);
        check_morale_alien(v, (v + turn) % FACTION_COUNT);
    }
    check_morale_alien(-1, 1);
    sweep_varied();
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook veh_hooks[] = {
    HOOK(0x5bf1f0, has_abil, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x4d, 0x08),           // push ecx; mov ecx, [ebp+8]
    HOOK(0x57efa0, best_reactor, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x08),       // push ebx; mov ebx, [ebp+8]
    HOOK(0x5004f0, planet_buster2, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),           // mov eax, [ebp+8]
    HOOK(0x5c1760, veh_cargo, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                // mov eax, [ebp+8]
    HOOK(0x501500, psi_factor, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x8b, 0x75, 0x0c),         // push esi; mov esi, [ebp+12]
    HOOK(0x57d2e0, weap_strat, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x88, 0x64, 0x9a, 0x00),   // mov eax, [expansion]
    HOOK(0x57d360, weap_val, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                 // mov eax, [ebp+8]
    HOOK(0x57d270, arm_strat, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x88, 0x64, 0x9a, 0x00),    // mov eax, [expansion]
    HOOK(0x57d3f0, arm_val, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x0c),                  // mov eax, [ebp+12]
    HOOK(0x5c13b0, speed_proto, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x08),        // push ebx; mov ebx, [ebp+8]
    HOOK(0x4e6400, morale_mod, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x10),               // mov eax, [ebp+16]
    HOOK(0x4e65c0, breed_mod, PUSH_EBP_MOV_EBP_ESP, 0x51, 0xa1, 0x34, 0x65, 0x9a, 0x00),  // push ecx; mov eax, [...]
    HOOK(0x4e6740, worm_mod, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x34, 0x65, 0x9a, 0x00),     // mov eax, [...]
    HOOK(0x501350, morale_alien, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),             // sub esp, 8
    HOOK(0x5c0e40, morale_veh, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),               // sub esp, 8
    HOOK(0x5a5d00, base_cost, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                // mov eax, [ebp+8]
    HOOK(0x5c1850, veh_cost, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x53, 0x56),                 // push ecx; push ebx; push esi
    HOOK(0x560b30, garrison_check, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),           // sub esp, 12
    HOOK(0x5c0db0, can_arty, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x4d, 0x08),                 // mov ecx, [ebp+8]
    HOOK(0x5b9580, stack_check, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x56, 0x8b, 0x75, 0x08),  // push ecx; push esi; mov esi, [ebp+8]
    HOOK(0x5ba910, veh_avail, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56, 0x57),                // push ebx; push esi; push edi
};
struct hook_module veh_module = {"veh", veh_hooks, HOOK_COUNT(veh_hooks), veh_sweep, NULL, -1};
