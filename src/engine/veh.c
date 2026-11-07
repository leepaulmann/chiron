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
    ABL_DEEP_RADAR = 0x2, ABL_AIR_SUPERIORITY = 0x20, ABL_ARTILLERY = 0x8000, ABL_CARRIER = 0x80, ABL_ANTIGRAV_STRUTS = 0x400, ABL_SLOW = 0x800000,
    ABL_SOPORIFIC_GAS = 0x1000000, ABL_DISSOCIATIVE_WAVE = 0x2000000, ABL_MARINE_DETACHMENT = 0x4000000,
    ABL_FUEL_NANOCELLS = 0x8000000, ABL_ALGO_ENHANCEMENT = 0x10000000,
    ABL_HEAVY_TRANSPORT = 0x80000,
};
enum {
    BSC_LOCUSTS_OF_CHIRON = 10, BSC_SEALURK = 14, BSC_SPORE_LAUNCHER = 15, BSC_FUNGAL_TOWER = 19,
    BSC_UNITY_MINING_LASER = 20, BASIC_PROTOS = 64, BSC_MIND_WORMS = 8, BSC_ISLE_OF_THE_DEEP = 9,
    CHASSIS_HOVERTANK = 2, MAP_MAGTUBE = 0x8,
    ABL_EMPATH = 0x800, ABL_BLINK_DISPLACER = 0x20000, ABL_TRANCE = 0x40000, ABL_COMM_JAMMER = 0x200, ABL_AAA = 0x100,
    MAP_BUNKER = 0x800, RULE_FLAG_FANATIC = 0x400, BONUS_DEFENSE = 17, BONUS_OFFENSE = 18,
    FAC_PERIMETER_DEFENSE = 4, FAC_TACHYON_FIELD = 5, FAC_FLECHETTE_DEFENSE = 39, SP_CITIZENS_DEFENSE_FORCE = 5,
    WEAPON_RESONANCE_LASER = 12, WEAPON_RESONANCE_BOLT = 13, ARMOR_PULSE_3 = 10, ARMOR_RESONANCE_3 = 11,
    ARMOR_PULSE_8 = 12, ARMOR_RESONANCE_8 = 13, VEH_STATE_AIRDROPPED = 0x20, VEH_STATE_DESIGNATED_DEFENDER = 0x80,
    WEAPON_PROBE = 11, WEAPON_TECTONIC = 21, WMODE_TRANSPORT = 7, WMODE_PROBE = 11,
    MAP_AIRBASE = 0x40000, VEH_STATE_UNK_4000000 = 0x4000000, VEH_STATE_MONOLITH_USED = 0x100,
    VEH_STATE_UNK_200 = 0x200, VEH_STATE_UNK_10000 = 0x10000, VEH_STATE_UNK_20000 = 0x20000, VEH_STATE_UNK_40000 = 0x40000,
    PLAN_TERRAFORM = 9, ORDER_HOLD = 2, ORDER_NONE = 0, ORDER_MOVE_TO = 0x18,
    VEH_STATE_EXPLORE = 0x4000, VEH_STATE_UNK_1000000 = 0x1000000, TREATY_TRUCE = 0x4,
    PREF_WAKE_PACT = 0x100000, PREF_WAKE_TREATY = 0x200000, PREF_WAKE_TRUCE = 0x400000, PREF_WAKE_WAR = 0x800000,
    PREF_WAKE_SAME_TRIAD = 0x40000000,
    ABL_DROP_POD = 0x10, GAME_STATE_UNK_40000 = 0x40000,
    PLAN_PROBE = 11, PLAN_ARTIFACT = 12, BONUS_FREE_ABILITY = 15, TECH_FLAG_PROBE = 0x2,
    RULE_FLAG_FREE_PROTO = 0x200, RULE_FLAG_MORALE = 0x8000,
    RULES_NO_COLONY_PODS = 0x400000, MAP_UNK_40000000_ = 0x40000000, FACTION_FLAG_UNK_40000000 = 0x40000000,
};

static inline char *text(uint32_t addr) { return (char *)game_at(addr); }
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
    int p = (base_cost(proto) * prototype_factor(proto) + 50) / 100;
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

// Stacks. The units on a square are a list through next_in_stack and
// prev_in_stack; the map marks squares with units (MAP_UNIT, and
// MAP_UNIT_GROUND for one not in the air or native), and nothing else
// points at a list's head, which veh_at() finds by searching.
enum { MAP_UNIT_GROUND = 0x100000, ORDER_SENTRY = 1, GAME_STATE_UNK_40 = 0x40, GAME_STATE_UNK_1000 = 0x1000 };

static int on_map_(int x, int y) { return y >= 0 && y < *game_map_height && x >= 0 && x < *game_map_width; }
static int top(int veh) {
    while (game_vehs[veh].prev_in_stack >= 0) veh = game_vehs[veh].prev_in_stack;
    return veh;
}

// The head of the stack on a square, or -1. A square the map marks as
// having units but where none is found is logged and the map's unit bits
// rebuilt (in a network or some other games only once).
int veh_at(int x, int y) {
    if (on_map_(x, y) && !((*game_map)[(x >> 1) + y * *game_map_half_width].bits & MAP_UNIT)) return -1;
    for (int v = 0; v < *game_veh_count; v++)
        if (game_vehs[v].x == x && game_vehs[v].y == y) return top(v);
    if (!on_map_(x, y)) return -1;
    if (!*game_veh_at_rebuilt) game_log_say(text(0x691d70), x, y, 0);
    if ((*game_state & (GAME_STATE_UNK_40 | GAME_STATE_UNK_1000)) || *game_net_multiplayer) {
        if (*game_veh_at_rebuilt) return -1;
        *game_veh_at_rebuilt = 1;
    }
    rebuild_vehicle_bits();
    return -1;
}

// Takes a unit out of its stack (and off the map), noting where it was.
int veh_lift(int veh) {
    struct veh *V = &game_vehs[veh];
    int had_prev = 0;
    if (V->prev_in_stack >= 0) had_prev = 1, game_vehs[V->prev_in_stack].next_in_stack = V->next_in_stack;
    if (V->next_in_stack >= 0) game_vehs[V->next_in_stack].prev_in_stack = V->prev_in_stack;
    else if (!had_prev && on_map_(V->x, V->y)) bit_set(V->x, V->y, MAP_UNIT, 0);
    *game_veh_lift_x = V->x;
    *game_veh_lifted = veh;
    *game_veh_lift_y = V->y;
    V->x = V->y = V->next_in_stack = V->prev_in_stack = -1;
    return veh;
}

// Puts a unit at the head of the stack on a square; on an empty square
// without a base it takes the square's ownership.
int veh_drop(int veh, int x, int y) {
    int head = veh_at(x, y);
    struct veh *V = &game_vehs[veh];
    V->next_in_stack = (int16_t)head;
    V->prev_in_stack = -1;
    V->x = (int16_t)x;
    V->y = (int16_t)y;
    *game_veh_lifted = -1;
    if (head >= 0) game_vehs[head].prev_in_stack = (int16_t)veh;
    else {
        if (y < 0) return veh;
        if (on_map_(x, y) && !((*game_map)[(x >> 1) + y * *game_map_half_width].bits & MAP_BASE)) owner_set(x, y, V->faction);
    }
    if (on_map_(x, y)) {
        uint32_t bits = (V->faction && triad(V->proto) != TRIAD_AIR) ? MAP_UNIT_GROUND : 0;
        bit_set(x, y, bits | MAP_UNIT, 1);
    }
    return veh;
}

// Moves a unit to the head of its stack.
void veh_promote(int veh) {
    int head = veh >= 0 ? top(veh) : -1;
    if (head == veh) return;
    veh_lift(veh);
    veh_drop(veh, game_vehs[head].x, game_vehs[head].y);
}

// Moves a unit to the bottom of its stack.
void veh_demote(int veh) {
    if (veh < 0 || game_vehs[veh].next_in_stack < 0) return;
    int last = game_vehs[veh].next_in_stack;
    while (game_vehs[last].next_in_stack >= 0) last = game_vehs[last].next_in_stack;
    if (last == veh) return;
    veh_lift(veh);
    struct veh *V = &game_vehs[veh], *L = &game_vehs[last];
    L->next_in_stack = (int16_t)veh;
    V->prev_in_stack = (int16_t)last;
    V->next_in_stack = -1;
    V->x = L->x;
    V->y = L->y;
}

// Moves a whole stack to a square, one unit at a time from the head.
void stack_put(int veh, int x, int y) {
    if (veh < 0) return;
    for (int v = top(veh); v >= 0;) {
        int next = game_vehs[v].next_in_stack;
        veh_drop(veh_lift(v), x, y);
        v = next;
    }
}

// Moves the units collected off the map (at -3, -3) back onto the square,
// ending with them at the head.
static void restack(int last, int x, int y) {
    if (last < 0) return;
    for (int v = top(last); v >= 0;) {
        int next = game_vehs[v].next_in_stack;
        veh_drop(veh_lift(v), x, y);
        v = next;
    }
}

// Puts a stack's transports and carriers at its head.
void stack_sort(int veh) {
    if (veh < 0) return;
    int x = game_vehs[veh].x, y = game_vehs[veh].y, last = -1;
    for (int v = top(veh); v >= 0;) {
        int next = game_vehs[v].next_in_stack;
        if (veh_cargo(v) || has_abil(game_vehs[v].proto, ABL_CARRIER)) last = v, veh_drop(veh_lift(v), -3, -3);
        v = next;
    }
    restack(last, x, y);
}

// Puts a stack's colony pods, then its offensive and combat units, at its
// head. The unit is read for its square even when negative.
void stack_sort_2(int veh) {
    int x = game_vehs[veh].x, y = game_vehs[veh].y, last = -1;
    if (veh >= 0) {
        for (int v = top(veh); v >= 0;) {
            int next = game_vehs[v].next_in_stack;
            if (proto_of(game_vehs[v].proto)->plan == PLAN_COLONY) last = v, veh_drop(veh_lift(v), -3, -3);
            v = next;
        }
    }
    for (int v = veh_at(x, y); v >= 0;) {
        int next = game_vehs[v].next_in_stack;
        if (proto_of(game_vehs[v].proto)->plan <= PLAN_COMBAT) last = v, veh_drop(veh_lift(v), -3, -3);
        v = next;
    }
    restack(last, x, y);
}

// In a network game, puts a stack in order and returns its head. Its loop
// meant to visit the units on the unit's square but compares the unit
// with itself, so every unit in the game is promoted in turn, each time
// sorting the unit's stack.
int stack_fix(int veh) {
    if (veh < 0 || !*game_net_multiplayer) return veh;
    const struct veh *V = &game_vehs[veh];
    if (V->next_in_stack < 0 && V->prev_in_stack < 0) return veh;
    int x = V->x, y = V->y;
    for (int i = 0; i < *game_veh_count; i++)
        if (V->x == x && V->y == y) veh_promote(i), stack_sort(veh);
    return V->prev_in_stack >= 0 ? top(V->prev_in_stack) : veh;
}

// Starts a unit afresh, off the map: no orders, no home, and the morale
// the faction's bonus gives.
void veh_clear(int veh, int proto, int faction) {
    struct veh *V = &game_vehs[veh];
    V->x = V->y = -4;
    V->lurking = 0;
    V->unk_c = 0;
    V->flags = 0;
    V->faction = (uint8_t)faction;
    V->proto = (int16_t)proto;
    V->next_in_stack = V->prev_in_stack = -1;
    V->waypoint_count = V->patrol_point = 0;
    V->order = 0;
    for (int i = 0; i < 4; i++) V->waypoint_x[i] = V->waypoint_y[i] = -1;
    V->state = 0;
    V->iter_count = V->damage = V->order_auto_type = V->movement_turns = V->moves_spent = V->status_icon = 0;
    V->visibility = 0;
    V->home_base = -1;
    V->morale = (uint8_t)(game_faction_defs[faction].rule_morale + 1);
    V->unk_29 = 2;
    V->unk_2c[0] = V->unk_2c[1] = 0;
}

// Puts a unit on sentry with no end.
void veh_sleep(int veh) {
    struct veh *V = &game_vehs[veh];
    V->order = ORDER_SENTRY;
    V->waypoint_x[0] = -1;
    V->order_turns = 0;
}

// A unit's moves this turn, in thirds: its design's, two more squares for
// a ship with the Maritime Control Center, one for an elite unit (not
// native, unless asked for a straight line), cut by damage for all but
// air units (to at least one square, two for ships). A fungal tower has
// none.
int veh_speed(int veh, int straight) {
    const struct veh *V = &game_vehs[veh];
    int rate = game_rules_basic->move_rate_roads;
    int s = speed_proto(V->proto);
    const struct unit_proto *P = proto_of(V->proto);
    if (triad(V->proto) == TRIAD_SEA && game_secret_projects[SP_MARITIME_CONTROL_CENTER] >= 0
        && game_bases[game_secret_projects[SP_MARITIME_CONTROL_CENTER]].faction == V->faction)
        s += rate * 2;
    if (morale_veh(veh, 1, 0) == 6 && !straight && (V->proto >= BASIC_PROTOS || game_weapons[P->weapon].offense >= 0))
        s += rate;
    int damage = V->damage, tr = triad(V->proto);
    if (damage && tr != TRIAD_AIR) {
        int r = P->reactor < 1 ? 1 : P->reactor > 100 ? 100 : P->reactor;
        int hp = P->plan == PLAN_ARTIFACT ? 1 : r * 10;
        int full = P->plan == PLAN_ARTIFACT ? 1 : hp < 1 ? 1 : hp > 99 ? 99 : hp;
        int moves = s / rate;
        int left = hp - damage;
        left = left < 0 ? 0 : left > 9999 ? 9999 : left;
        int v = (left * moves + full - 1) / full;
        int least = tr == TRIAD_SEA ? 2 : 1;
        v = v < least ? least : v > 999 ? 999 : v;
        s = v * rate;
    }
    return V->proto == BSC_FUNGAL_TOWER ? 0 : s;
}

// Uses up a unit's moves for the turn.
void veh_skip(int veh) { game_vehs[veh].moves_spent = (uint8_t)veh_speed(veh, 0); }

// Combat strengths of a design's weapon and armor, against (or from) a
// unit if one is given, when they are multiplied by 8: probe against probe
// is 16; psi combat, a psi weapon or armor on either side (unless
// bombarding) or a spore launcher, uses the rules' psi odds, for the
// defender's triad; a missile's weapon counts half again. The other unit is
// read for the spore launcher test even when there is none.
int offense_proto(int proto, int veh_def, int bombard) {
    const struct unit_proto *P = proto_of(proto);
    const struct rules_weapon *W = &game_weapons[P->weapon];
    int c = veh_def;
    if (W->mode == WMODE_PROBE && c >= 0 && proto_of(game_vehs[c].proto)->plan == PLAN_PROBE) return 16;
    int psi = 0;
    if (!bombard && (W->offense < 0 || (c >= 0 && game_armor[proto_of(game_vehs[c].proto)->armor].defense < 0))) psi = 1;
    if (!psi && (game_vehs[c].proto == BSC_SPORE_LAUNCHER || proto == BSC_SPORE_LAUNCHER)) psi = 1;
    if (psi) {
        if (c < 0) return game_rules_basic->psi_combat_ratio_atk[0];
        return game_rules_basic->psi_combat_ratio_atk[triad(game_vehs[c].proto)] * 8;
    }
    int v = W->offense < 0 ? -W->offense : W->offense;
    if (game_chassis[P->chassis].missile && W->offense < 99) v = v * 3 / 2;
    return c >= 0 ? v * 8 : v;
}

int armor_proto(int proto, int veh_att, int bombard) {
    const struct unit_proto *P = proto_of(proto);
    int c = veh_att;
    if (game_weapons[P->weapon].mode == WMODE_PROBE && c >= 0 && proto_of(game_vehs[c].proto)->plan == PLAN_PROBE) return 16;
    int psi = 0;
    if (!bombard || game_vehs[c].proto == BSC_SPORE_LAUNCHER || proto == BSC_SPORE_LAUNCHER)
        psi = game_armor[P->armor].defense < 0
              || (c >= 0 && game_weapons[proto_of(game_vehs[c].proto)->weapon].offense < 0);
    if (psi) {  // by its own triad, where offense_proto() takes the other unit's
        if (c < 0) return game_rules_basic->psi_combat_ratio_def[0];
        return game_rules_basic->psi_combat_ratio_def[triad(proto)] * 8;
    }
    int v = game_armor[P->armor].defense;
    v = v < 1 ? 1 : v > 9999 ? 9999 : v;
    return c >= 0 ? v * 8 : v;
}

// The extra percent a new design's first unit costs, by triad: none for a
// faction that builds prototypes free or plays at the two lowest levels.
int prototype_factor(int proto) {
    int f = proto / BASIC_PROTOS;
    if ((game_faction_defs[f].rule_flags & RULE_FLAG_FREE_PROTO) || game_factions[f].diff_level <= 1) return 0;
    switch (triad(proto)) {
    case TRIAD_SEA: return game_rules_basic->extra_cost_prototype[1];
    case TRIAD_AIR: return game_rules_basic->extra_cost_prototype[2];
    default: return game_rules_basic->extra_cost_prototype[0];
    }
}

// The nearest unit (of a faction, unless negative) other than `skip`, by
// the game's map distance, the last of equals; its distance goes to
// game_veh_find_dist.
int veh_find(int x, int y, int faction, int skip) {
    int best = 9999, found = -1;
    *game_veh_find_dist = 9999;
    for (int i = 0; i < *game_veh_count; i++) {
        const struct veh *V = &game_vehs[i];
        if ((faction >= 0 && V->faction != faction) || i == skip) continue;
        int dx = x - V->x;
        dx = dx < 0 ? -dx : dx;
        if (!(*game_map_flat & 1) && dx > *game_map_half_width) dx = *game_map_width - dx;
        int dy = y - V->y;
        dy = dy < 0 ? -dy : dy;
        dx = dx < 0 ? -dx : dx;  // again, as the original does
        int big = dx > dy ? dx : dy, small = dx < dy ? dx : dy;
        int d = big - ((((dx + dy) >> 1) - small + 1) >> 1);
        if (d <= best) best = d, found = i;
    }
    if (found >= 0) *game_veh_find_dist = best;
    return found;
}

// A land unit on sentry aboard an air transport, away from a base of its
// own faction (or any but native life's) and from an airbase: it can't
// leave.
int veh_jail(int veh) {
    const struct veh *V = &game_vehs[veh];
    if (triad(V->proto) != TRIAD_LAND || V->order != ORDER_SENTRY) return 0;
    int t = V->waypoint_x[0];
    if (t < 0 || triad(game_vehs[t].proto) != TRIAD_AIR) return 0;
    const struct map_tile *T = &(*game_map)[(V->x >> 1) + V->y * *game_map_half_width];
    if (T->bits & MAP_BASE) {
        int owner = T->owner_site & 0xf;
        if (owner < 8 && owner > 0) return 0;
    }
    return !(T->bits & MAP_AIRBASE);
}

// The player's unit on the map, not held back (flags 0x40 with 0x400) or
// jailed.
static int playable(int veh) {
    const struct veh *V = &game_vehs[veh];
    if (!on_map_(V->x, V->y) || V->faction != *game_current_faction) return 0;
    if ((V->flags & 0x40) && (V->flags & 0x400)) return 0;
    return !veh_jail(veh);
}

int veh_selectable(int veh) { return veh >= 0 && playable(veh); }

static int moves_left(int veh) {
    int r = veh_speed(veh, 0) - game_vehs[veh].moves_spent;
    return r > 0;
}

// The player's unit with moves left, or on a terraforming order.
int veh_unmoved(int veh) {
    if (veh < 0 || !playable(veh)) return 0;
    const struct veh *V = &game_vehs[veh];
    if (!(V->state & VEH_STATE_UNK_4000000) && (int8_t)V->order >= 4 && (int8_t)V->order < 24) return 1;
    return moves_left(veh);
}

// The player's unit waiting for orders: moves left and not on sentry,
// hold or fortified (unless its state says otherwise).
int veh_ready(int veh) {
    if (veh < 0 || !playable(veh)) return 0;
    const struct veh *V = &game_vehs[veh];
    if (!(V->state & 0x4200) && (V->order == 1 || V->order == 2 || V->order == 3)) return 0;
    return moves_left(veh);
}

// The spare unit slot past the table, cleared for a design: for working
// out what a unit would do.
int veh_fake(int proto, int faction) {
    veh_clear(VEH_COUNT, proto, faction);
    return VEH_COUNT;
}

// Ends a unit's order. A former stopped mid-terraforming loses a move
// and the turns its work already counted for; an automated convoy
// (state 0x200, type 11) loses its moves unless state 4 says otherwise.
int veh_wake(int veh) {
    struct veh *V = &game_vehs[veh];
    int8_t order = (int8_t)V->order;
    if (order >= 4 && order < 24 && !(V->state & VEH_STATE_UNK_4000000)) {
        V->moves_spent = (uint8_t)(veh_speed(veh, 0) - game_rules_basic->move_rate_roads);
        int turns = V->movement_turns;
        if (turns) {
            turns -= game_contribution(veh, order - 4);
            V->movement_turns = (uint8_t)(turns < 0 ? 0 : turns);
        }
    }
    if ((V->state & 0x200) && V->order_auto_type == 11 && !(V->state & 4)) V->moves_spent = 0;
    V->order = 0;
    V->state &= 0xf4ffbdffu;
    return veh;
}

// How far the faction's units can air drop: the rule's range, or across
// the whole map with the orbital insertion tech or the Space Elevator.
int drop_range(int faction) {
    if (!has_tech(game_rules_basic->tech_orbital_insert, faction) && !owns_project(SP_SPACE_ELEVATOR, faction))
        return game_rules_basic->max_airdrop_range;
    return *game_map_width > *game_map_height ? *game_map_width : *game_map_height;
}

// The unit's reactor if it's a planet buster, else 0.
int planet_buster(int veh) { return planet_buster2(game_vehs[veh].proto); }

// The head of the unit's stack, or -1 for no unit.
int veh_top(int veh) {
    if (veh < 0) return -1;
    while (game_vehs[veh].prev_in_stack >= 0) veh = game_vehs[veh].prev_in_stack;
    return veh;
}

// The moves the unit has left this turn, from 0 to 999.
int veh_moves(int veh) {
    int r = veh_speed(veh, 0) - game_vehs[veh].moves_spent;
    return r < 0 ? 0 : r > 999 ? 999 : r;
}

// The unit's full health: ten per reactor level (1 to 100), or 1 for an
// alien artifact.
int proto_power(int veh) {
    const struct unit_proto *P = proto_of(game_vehs[veh].proto);
    if (P->plan == PLAN_ARTIFACT) return 1;
    int r = P->reactor < 1 ? 1 : P->reactor > 100 ? 100 : P->reactor;
    return r * 10;
}

// The design's armor rating doubled, as arm_val().
int armor_val(int proto, int faction) { return arm_val(proto_of(proto)->armor, faction); }

// The units a transport of the chassis carries: times the reactor at sea,
// halved when slow, and half again more for heavy transport.
int transport_val(int chassis, uint32_t ability, int reactor) {
    int cargo = game_chassis[chassis].cargo;
    if (game_chassis[chassis].triad == TRIAD_SEA) cargo *= reactor;
    if (ability & ABL_SLOW) cargo /= 2;
    if (ability & ABL_HEAVY_TRANSPORT) cargo = (cargo * 3 + 1) / 2;
    return cargo;
}

// The number of the lowest bit set: an ability's index from its flag. Like
// the original it never returns for 0.
int abil_index(uint32_t ability) {
    int i = 0;
    int32_t a = (int32_t)ability;
    while (!(a & 1)) a >>= 1, i++;
    return i;
}

// Moves the unit alone to the square.
int veh_put(int veh, int x, int y) { return veh_drop(veh_lift(veh), x, y); }

// The unit's health left, from 0 to 9999.
int veh_health(int veh) {
    int r = proto_power(veh) - game_vehs[veh].damage;
    return r < 0 ? 0 : r > 9999 ? 9999 : r;
}

// The faction's first design with the plan that it can build: of the
// basic designs, those whose tech it has, then its own; -1 if none.
int get_plan(int faction, int plan) {
    for (int i = 0; i < 2 * BASIC_PROTOS; i++) {
        int p = i < BASIC_PROTOS ? i : faction * BASIC_PROTOS + i - BASIC_PROTOS;
        const struct unit_proto *P = proto_of(p);
        if (!(P->flags & PROTO_FLAG_ACTIVE)) continue;
        if ((uint8_t)(1u << (faction & 31)) & P->obsolete_factions) continue;
        if (p < BASIC_PROTOS && !has_tech(P->preq_tech, faction)) continue;
        if (P->plan == plan) return p;
    }
    return -1;
}

// Whether the AI wants the unit to visit a monolith: an armed unit below
// elite morale (by its own count and morale_veh()'s) that hasn't had its
// upgrade there yet.
int want_monolith(int veh) {
    const struct veh *V = &game_vehs[veh];
    if (V->state & VEH_STATE_MONOLITH_USED) return 0;
    if (morale_veh(veh, 1, 0) >= 6 || V->morale >= 6) return 0;
    return game_weapons[proto_of(V->proto)->weapon].offense != 0;
}


static struct map_tile *square(int x, int y) { return &(*game_map)[(x >> 1) + y * *game_map_half_width]; }
// A base of a faction (0 to 7, or 1 to 7 with natives left out) on the
// square.
static int faction_base(const struct map_tile *t, int first) {
    int owner = t->owner_site & 0xf;
    return (t->bits & MAP_BASE) && owner < 8 && owner >= first;
}

// Whether the faction's unit spotting another (or -1) should stop its
// move. An air unit on its way to a base of its own or of a pact partner
// (the first waypoint), when aloft, never stops. Otherwise it stops by the
// player's preferences for the spotter's treaty with the faction, whose
// flag is returned as it is, not as 1 (in a network game always, except
// with a pact); with "only the same triad" set, or in a network game,
// it doesn't stop for a unit of another triad, unless it is flagged
// (state bits 0x200 and 0x1000000) and the other isn't a land unit.
int want_to_wake(int faction, int veh, int spotted) {
    const struct veh *V = &game_vehs[veh];
    int tr = triad(V->proto);
    if (tr == TRIAD_AIR && V->movement_turns && V->order == ORDER_MOVE_TO) {
        const struct map_tile *t = square(V->waypoint_x[0], V->waypoint_y[0]);
        if (faction_base(t, 0)) {
            int owner = t->owner_site & 0xf;
            if (owner == V->faction || game_has_treaty(V->faction, owner, TREATY_PACT)) return 0;
        }
    }
    uint32_t d = game_factions[V->faction].diplo_status[faction];
    uint32_t pref = (uint32_t)*game_preferences;
    int net = *game_net_multiplayer, want;
    if (d & TREATY_PACT) want = net ? 0 : (int)(pref & PREF_WAKE_PACT);
    else if (d & TREATY_TREATY) want = net ? 1 : (int)(pref & PREF_WAKE_TREATY);
    else if (d & TREATY_TRUCE) want = net ? 1 : (int)(pref & PREF_WAKE_TRUCE);
    else want = net ? 1 : (int)(pref & PREF_WAKE_WAR);
    if (!net && !(pref & PREF_WAKE_SAME_TRIAD)) return want;
    if (veh < 0 || spotted < 0) return want;
    int other = game_veh_triad(spotted);
    if (other == tr) return want;
    const uint32_t flagged = VEH_STATE_UNK_200 | VEH_STATE_UNK_1000000;
    if ((V->state & flagged) == flagged && other != TRIAD_LAND) return want;
    return 0;
}

// Wakes the units of the stack sentried on board: all but land units, and
// those only on land, and from an air transport only at a base or an
// airbase (or with no transport).
void wake_stack(int veh) {
    for (int v = veh_top(veh); v >= 0; v = game_vehs[v].next_in_stack) {
        struct veh *U = &game_vehs[v];
        if (U->order != ORDER_SENTRY_BOARD) continue;
        if (game_veh_triad(v) == TRIAD_LAND) {
            const struct map_tile *t = square(U->x, U->y);
            if (t->climate >> 5 < ALT_SHORE_LINE) continue;
            int tr = U->waypoint_x[0];
            if (tr >= 0 && game_veh_triad(tr) == TRIAD_AIR && !faction_base(t, 0) && !(t->bits & MAP_AIRBASE)) continue;
        }
        U->order = ORDER_NONE;
        U->state &= ~(uint32_t)VEH_STATE_EXPLORE;
    }
}

// Whether the faction wants the finished design, i.e. none of its own
// finished designs with the same plan and triad is as good: a weapon of
// the same mode (for modes past missiles), or the same psi rating, or at
// least the offense; the same armor if it has no rating, or at least the
// armor value; and at least the speed. OpenSMACX compares the design's
// offense by get_proto_offense_rating(); the original takes the weapon's.
int wants_prototype(int proto, int faction) {
    const struct unit_proto *P = proto_of(proto);
    if (!(P->flags & PROTO_FLAG_ACTIVE) || !(P->flags & PROTO_FLAG_BUILT)) return 0;
    const struct rules_weapon *wp = &game_weapons[P->weapon];
    for (int i = 0; i < BASIC_PROTOS; i++) {
        int c = faction * BASIC_PROTOS + i;
        const struct unit_proto *C = proto_of(c);
        if (!(C->flags & PROTO_FLAG_ACTIVE) || !(C->flags & PROTO_FLAG_BUILT) || C->plan != P->plan) continue;
        if (game_chassis[C->chassis].triad != game_chassis[P->chassis].triad) continue;
        const struct rules_weapon *wc = &game_weapons[C->weapon];
        if (wc->mode > 2) {
            if (wc->mode != wp->mode) continue;
        } else if (wc->offense < 0) {
            if (wc->offense != wp->offense) continue;
        } else if (wc->offense < wp->offense) {
            continue;
        }
        if (game_armor[C->armor].defense <= 0) {
            if (C->armor != P->armor) continue;
        } else if (armor_val(c, faction) < armor_val(proto, faction)) {
            continue;
        }
        if (game_chassis[C->chassis].speed >= game_chassis[P->chassis].speed) return 0;
    }
    return 1;
}

// ---- Combat strengths and moves ------------------------------------------------

// Clears a battle report: the modifiers listed for each side, and the two
// flags after them (game_battle_state, which keeps messages back while a
// battle runs, among them).
void battle_init(void) {
    game_battle_mod_count[0] = game_battle_mod_count[1] = 0;
    game_battle_state[0] = game_battle_state[1] = 0;
}

// Lists a non-zero modifier with its label on one side of the battle
// report, up to 4 for each side. Each label has 80 bytes, where OpenSMACX
// counts 20.
void add_bat(int side, int value, const char *label) {
    int n = game_battle_mod_count[side];
    if (!value || n >= 4) return;
    game_battle_mod_count[side] = n + 1;
    game_battle_mod[side * 4 + n] = value;
    strcpy(game_battle_mod_label[side * 4 + n], label);
}

// Whether the unit's design is a basic psi unit, read even when negative.
static int psi_basic(int proto) { return game_weapons[proto_of(proto)->weapon].offense < 0 && proto < BASIC_PROTOS; }

// The defender's terrain in combat, in halves: 2 at sea, in a base, and in
// fungus on land against native life, a faction with the Pholus Mutagen or
// a basic psi unit; otherwise 3 on rocky ground; in fungus on land, for a
// unit that doesn't fly, 3, or 4 for a basic psi unit or a faction with the
// Pholus Mutagen; and 3 in a forest against a land unit or none. The
// terrain's label for the battle report is left in game_battle_terrain
// ("Rocky" unless fungus or forest counted).
int defense_value(int faction, int x, int y, int veh_def, int veh_att) {
    const struct map_tile *t = square(x, y);
    int alt = t->climate & 0xe0;
    if (alt < ALT_SHORE_LINE << 5) return 2;
    if ((t->bits & MAP_BASE) && (t->owner_site & 0xf) < FACTION_COUNT) return 2;
    const struct veh *A = &game_vehs[veh_att];
    int fungus = (t->bits & MAP_FUNGUS) && alt >= 0x40;
    if (fungus && veh_att >= 0 && (!A->faction || owns_project(SP_PHOLUS_MUTAGEN, A->faction) || psi_basic(A->proto)))
        return 2;
    *game_battle_terrain = (*game_labels)[91];
    int d = (t->rocky_lock & 0xc0) > 0x40;
    if (fungus && !d && triad(game_vehs[veh_def].proto) != TRIAD_AIR
        && !(veh_att >= 0 && (psi_basic(A->proto) || owns_project(SP_PHOLUS_MUTAGEN, A->faction)))) {
        *game_battle_terrain = (*game_labels)[338];
        d = owns_project(SP_PHOLUS_MUTAGEN, faction) || psi_basic(game_vehs[veh_def].proto) ? 2 : 1;
    }
    if ((t->bits & MAP_FOREST) && !d && (veh_att < 0 || triad(A->proto) == TRIAD_LAND)) {
        *game_battle_terrain = (*game_labels)[291];
        d = 1;
    }
    return d + 2;
}

// MORALE as a Children's Creche or Brood Pit softens it, between -4 and
// `most`, less one when it is -2 or below.
static int morale_softened(int faction, int most) {
    int m = game_factions[faction].se[SE_MORALE];
    m = m < -4 ? -4 : m > most ? most : m;
    return m <= -2 ? m + 1 : m;
}

// A unit's morale in its base: a Children's Creche adds one and takes
// MORALE (softened) off, as does a Brood Pit for a native unit; with both,
// a native unit only gets one more.
static int morale_at_home(int m, int base, int faction, int proto, int creche_most) {
    if (built(base, FAC_CHILDREN_CRECHE)) {
        m += 1 - morale_softened(faction, creche_most);
        if (built(base, FAC_BROOD_PIT) && native(proto)) m++;
    } else if (built(base, FAC_BROOD_PIT) && native(proto)) {
        m += 1 - morale_softened(faction, 4);
    }
    return m;
}

// An attacker's strength: its weapon's (with PSI and the Dream Twister in
// psi combat) times its morale plus 6, times 4. In a battle (`battle`)
// morale counts a pending MORALE of 2 or 3 and native defenders as one
// more and soporific gas as two less when the attacker is in a base, and
// is held between 1 and 6. The morale is left in game_battle_morale.
int get_basic_offense(int veh_att, int veh_def, int psi, int bombard, int battle) {
    const struct veh *A = &game_vehs[veh_att];
    int f = A->faction;
    int m = f ? morale_veh(veh_att, 1, 0) : morale_alien(veh_att, veh_def >= 0 ? game_vehs[veh_def].faction : -1);
    int b = game_base_at(A->x, A->y);
    if (b >= 0) {
        m = morale_at_home(m, b, f, A->proto, 4);
        if (battle) {
            int p = game_factions[f].se_pending[SE_MORALE];
            if (p >= 2 && p <= 3) m++;
            if (veh_def >= 0) {
                const struct veh *D = &game_vehs[veh_def];
                if (!D->faction)
                    m++;
                else if (!native(A->proto) && !has_abil(A->proto, ABL_DISSOCIATIVE_WAVE)
                         && has_abil(D->proto, ABL_SOPORIFIC_GAS))
                    m -= 2;
            }
        }
    }
    if (battle) m = m < 1 ? 1 : m > 6 ? 6 : m;
    game_battle_morale[battle != 0] = m;
    int v = offense_proto(A->proto, veh_def, bombard);
    if (psi) v = psi_factor(v, f, 1, 0);
    return v * (m + 6) * 4;
}

// A defender's strength: its armor's (with PSI, the Neural Amplifier and,
// for a fungal tower, half again in psi combat) times its morale plus 6.
// Morale in a base counts a pending MORALE of 2 or 3 and a native attacker
// as one more (OpenSMACX adds a headquarters, which the original doesn't);
// soporific gas takes two off anywhere; it is held between 1 and 6 and left
// in game_battle_morale. An alien artifact, and a probe team with armor 1
// against anything but a probe team, defend at 1; that test reads the
// attacker even when there is none.
int get_basic_defense(int veh_def, int veh_att, int psi, int bombard) {
    const struct veh *D = &game_vehs[veh_def], *A = &game_vehs[veh_att];
    int f = D->faction;
    int m = f ? morale_veh(veh_def, 1, 0) : morale_alien(veh_def, veh_att >= 0 ? A->faction : -1);
    int b = game_base_at(D->x, D->y);
    if (b >= 0) {
        m = morale_at_home(m, b, f, D->proto, 0);
        int p = game_factions[f].se_pending[SE_MORALE];
        if (p >= 2 && p <= 3) m++;
        if (veh_att >= 0 && !A->faction) m++;
    }
    if (veh_att >= 0 && A->faction && !native(D->proto) && !has_abil(D->proto, ABL_DISSOCIATIVE_WAVE)
        && has_abil(A->proto, ABL_SOPORIFIC_GAS))
        m -= 2;
    m = m < 1 ? 1 : m > 6 ? 6 : m;
    game_battle_morale[1] = m;
    const struct unit_proto *P = proto_of(D->proto);
    if (P->plan == PLAN_ARTIFACT) return 1;
    if (P->plan == PLAN_PROBE && game_armor[P->armor].defense == 1 && proto_of(A->proto)->plan != PLAN_PROBE) return 1;
    int v = armor_proto(D->proto, veh_att, bombard);
    if (psi) v = psi_factor(v, f, 0, D->proto == BSC_FUNGAL_TOWER);
    return v * (m + 6);
}

// What moving from one square to the next costs a design (or none, -1),
// in the rules' road moves. Ships pay three in fungus on the shelf, but
// not an isle of the deep (OpenSMACX also spares the sealurk) or with the
// Xenoempathy Dome; anything else at sea, from the sea, or not on land
// pays one move. On land, for a faction other than native life: none
// between bases and mag tubes; a road's cost along roads and bases (and
// from fungus with the Dome); and along a river diagonally. Fungus costs
// the road's with the Dome, for native life and for mind worms and spore
// launchers. Hovertanks and antigrav units pay one move; others one more
// each for rocks and forest unless `fast`, and in fungus, unless they are
// basic psi units, two more, or (for formers, artifacts or with PLANET
// above 0) at least their speed.
int hex_cost(int proto, int faction, int xs, int ys, int xd, int yd, int fast) {
    const struct map_tile *s = square(xs, ys), *t = square(xd, yd);
    uint32_t from = s->bits, to = t->bits;
    int mr = game_rules_basic->move_rate_roads;
    int alt = t->climate & 0xe0;
    if (alt < ALT_SHORE_LINE << 5) {
        if ((to & MAP_FUNGUS) && alt == 0x40 && triad(proto) == TRIAD_SEA && proto != BSC_ISLE_OF_THE_DEEP
            && !owns_project(SP_XENOEMPATHY_DOME, faction))
            return mr * 3;
        return mr;
    }
    if ((s->climate & 0xe0) < ALT_SHORE_LINE << 5) return mr;
    if (proto >= 0 && triad(proto) != TRIAD_LAND) return mr;
    if ((from & (MAP_BASE | MAP_MAGTUBE)) && (to & (MAP_BASE | MAP_MAGTUBE)) && faction) return 0;
    if (((from & (MAP_BASE | MAP_ROAD)) || ((from & MAP_FUNGUS) && faction > 0 && owns_project(SP_XENOEMPATHY_DOME, faction)))
        && (to & (MAP_BASE | MAP_ROAD)) && faction)
        return 1;
    if (faction >= 0
        && (owns_project(SP_XENOEMPATHY_DOME, faction) || !faction || proto == BSC_MIND_WORMS || proto == BSC_SPORE_LAUNCHER)
        && (to & MAP_FUNGUS))
        return 1;
    if ((from & MAP_RIVER) && (to & MAP_RIVER) && x_dist(xs, xd) == 1 && (ys - yd == 1 || yd - ys == 1) && faction)
        return 1;
    const struct unit_proto *P = proto_of(proto);
    if (P->chassis == CHASSIS_HOVERTANK || has_abil(proto, ABL_ANTIGRAV_STRUTS)) return mr;
    int cost = mr;
    if ((t->rocky_lock & 0xc0) > 0x40 && !fast) cost += mr;
    if ((to & MAP_FOREST) && !fast) cost += mr;
    if (faction && (to & MAP_FUNGUS) && !psi_basic(proto)) {
        if (P->plan != PLAN_TERRAFORM && P->plan != PLAN_ARTIFACT && game_factions[faction].se[SE_PLANET] <= 0)
            return cost + 2 * mr;
        int sp = speed_proto(proto);
        return cost > sp ? cost : sp;
    }
    return cost;
}

// One line of the battle report, when its side has room (the original's
// inline add_bat, which doesn't skip a value of 0).
static void report(int side, int value, const char *text) {
    int n = game_battle_mod_count[side];
    if (n >= 4) return;
    game_battle_mod_count[side] = n + 1;
    game_battle_mod[side * 4 + n] = value;
    strcpy(game_battle_mod_label[side * 4 + n], text);
}

// A strength times a percentage, wrapping as the original's 32-bit product.
static int percent(int v, int pct) { return (int)((uint32_t)v * (uint32_t)pct) / 100; }

static int alt_under(const struct veh *V) { return square(V->x, V->y)->climate >> 5; }
static int chassis_of(int proto) { return proto_of(proto)->chassis; }

// The squares within two of (x, y) that are on the map, wrapping x unless
// the map is flat; -1 once there are no more.
static int ring_square(int x0, int y0, int i, int *x, int *y) {
    int w = *game_map_width;
    *x = game_radius_x[i] + x0, *y = game_radius_y[i] + y0;
    if (!(*game_map_flat & 1)) {
        if (*x < 0) *x += w;
        else if (*x >= w) *x -= w;
    }
    return *y >= 0 && *y < *game_map_height && *x >= 0 && *x < w;
}

// The attacker's and defender's strengths in a battle (or the defender
// against nothing, or no defender), with every modifier listed in the
// battle report. combat_type: 1 artillery, 2 weapon against weapon (air
// combat, an artillery duel), 8 and 0x10 air combat that ends with the
// weapons. Psi combat counts only the faction's PSI and the projects (in
// get_basic_offense and _defense) plus PLANET, a base's own defense,
// trance and resonance. Otherwise the defender's terrain, bunkers, base
// defenses (perimeter, naval yard, aerospace complex or their projects, the
// tachyon field), artillery against the open, sensors, and the attacker's
// altitude, mobility and road count. Where it differs from OpenSMACX: a
// probe team attacking (not defending) quadruples the defense; a mobile
// attacker in the open needs the defender to be no faster; the air drop
// penalty is shown as negative; a missile meets a flechette defense from
// each of the defender's bases within two squares; and with no attacker,
// a base's facilities don't count. Units that aren't there are still read,
// as the original reads them.
void battle_compute(int veh_att, int veh_def, int *off_out, int *def_out, int combat) {
    const struct rules_basic *R = game_rules_basic;
    const struct veh *A = &game_vehs[veh_att], *D = &game_vehs[veh_def];
    int off = 8, def = 8, psi = 0, base = -1;
    battle_init();
    int art = combat & 1;
    int bombard = art || game_chassis[chassis_of(A->proto)].missile;
    if (!bombard || A->proto == BSC_SPORE_LAUNCHER || D->proto == BSC_SPORE_LAUNCHER) {
        if (veh_att >= 0 && game_weapons[proto_of(A->proto)->weapon].offense < 0) psi = 1;
        if (veh_def >= 0 && game_armor[proto_of(D->proto)->armor].defense < 0) psi |= 2;
        if (psi) {
            int fa = A->faction, fd = D->faction;
            if (game_faction_defs[fa].rule_psi) report(0, game_faction_defs[fa].rule_psi, game_label(342));
            if (owns_project(SP_DREAM_TWISTER, fa)) report(0, 50, game_label(343));
            if (game_faction_defs[fd].rule_psi) report(1, game_faction_defs[fd].rule_psi, game_label(342));
            if (owns_project(SP_NEURAL_AMPLIFIER, fd)) report(1, 50, game_label(344));
            if (veh_def >= 0 && D->proto == BSC_FUNGAL_TOWER) report(1, 50, game_protos[BSC_FUNGAL_TOWER].name);
        }
    }
    if (veh_att >= 0) {
        off = get_basic_offense(veh_att, veh_def, psi, bombard, 0);
        int pa = A->proto;
        if (proto_of(pa)->plan != PLAN_PROBE) {
            if (veh_def >= 0 && !combat) {
                const struct map_tile *t = square(D->x, D->y);
                if ((t->bits & MAP_FUNGUS) && (t->climate & 0xe0) >= 0x40
                    && (psi_basic(pa) || owns_project(SP_PHOLUS_MUTAGEN, A->faction))
                    && game_weapons[proto_of(D->proto)->weapon].offense >= 0) {
                    off += off / 2;
                    report(0, 50, game_label(338));
                }
                int w = proto_of(pa)->weapon;
                if ((psi & 2) && (w == WEAPON_RESONANCE_LASER || w == WEAPON_RESONANCE_BOLT)) {
                    off = percent(off, 125);
                    report(0, 25, game_label(1110));
                }
                if (!has_abil(D->proto, ABL_DISSOCIATIVE_WAVE) && (psi & 2) && has_abil(pa, ABL_EMPATH)
                    && R->combat_empath) {
                    off = percent(off, R->combat_empath + 100);
                    report(0, R->combat_empath,
                           game_strings_get(game_strings, game_abilities[abil_index(ABL_EMPATH)].name));
                }
            }
            if ((A->state & VEH_STATE_AIRDROPPED) && has_abil(pa, ABL_DROP_POD) && R->combat_pen_airdrop) {
                off = percent(off, 100 - R->combat_pen_airdrop);
                int fa = A->faction, range = R->max_airdrop_range;
                if (has_tech(R->tech_orbital_insert, fa) || owns_project(SP_SPACE_ELEVATOR, fa))
                    range = *game_map_width > *game_map_height ? *game_map_width : *game_map_height;
                char *text = game_label(range > R->max_airdrop_range ? 438 : 437);
                if (R->combat_pen_airdrop) report(0, -R->combat_pen_airdrop, text);
            }
            const struct faction_def *F = &game_faction_defs[A->faction];
            if ((F->rule_flags & RULE_FLAG_FANATIC) && R->combat_fanatic && !combat && !psi) {
                off = percent(off, R->combat_fanatic + 100);
                report(0, R->combat_fanatic, game_label(528));
            }
            for (int i = 0; i < F->bonus_count; i++) {
                if (F->bonus_id[i] != BONUS_OFFENSE) continue;
                off = percent(off, F->bonus_val1[i]);
                if (F->bonus_val1[i] != 100) report(0, F->bonus_val1[i] - 100, game_label(1108));
            }
            if (psi && A->faction) {
                int p = game_factions[A->faction].se[SE_PLANET];
                if (p && R->combat_psi_planet) {
                    int v = p * R->combat_psi_planet;
                    if (v) report(0, v, game_label(625));
                    off = percent(off, v + 100);
                }
            }
        }
    }
    if (combat & 2) {  // weapon against weapon
        if (veh_def < 0) goto out;
        def = get_basic_offense(veh_def, veh_att, psi, bombard, 1);
        if (combat & 0x18) goto out;
        int td = triad(D->proto), ta = triad(A->proto);
        if (td == TRIAD_SEA && ta == TRIAD_LAND) {
            if (!R->combat_art_vs_ship) goto out;
            off = percent(off, R->combat_art_vs_ship + 100);
            report(0, R->combat_art_vs_ship, game_label(435));
            goto out;
        }
        if (ta == TRIAD_SEA && td == TRIAD_LAND) {
            if (!R->combat_art_vs_ship) goto out;
            def = percent(def, R->combat_art_vs_ship + 100);
            report(1, R->combat_art_vs_ship, game_label(435));
            goto out;
        }
        int r = R->combat_art_altitude;
        if (!r) goto out;
        int aa = alt_under(A), ad = alt_under(D);
        if (ad < aa) {
            int v = (aa - ad) * r;
            off = percent(off, v + 100);
            if (v != 0) report(0, v, game_label(576));
        } else if (ad > aa) {
            int v = (ad - aa) * r;
            def = percent(def, v + 100);
            if (v != 0) report(1, v, game_label(576));
        }
        goto out;
    }
    if (veh_def < 0) goto out;
    def = get_basic_defense(veh_def, veh_att, psi, bombard);
    for (int i = 0; i < game_faction_defs[D->faction].bonus_count; i++) {
        const struct faction_def *F = &game_faction_defs[D->faction];
        if (F->bonus_id[i] != BONUS_DEFENSE) continue;
        def = percent(def, F->bonus_val1[i]);
        if (F->bonus_val1[i] != 100) report(1, F->bonus_val1[i] - 100, game_label(1109));
    }
    if (veh_att >= 0 && proto_of(A->proto)->plan == PLAN_PROBE) {
        def *= 4;
        goto out;
    }
    int dx = D->x, dy = D->y;
    base = game_base_at(dx, dy);
    if (psi) {
        if (base >= 0 && R->combat_base_def) {
            report(1, R->combat_base_def, game_label(332));
            def = percent(def, R->combat_base_def + 100);
        }
        def *= 4;
    } else {
        int terrain = triad(D->proto) == TRIAD_AIR ? 2 : defense_value(D->faction, dx, dy, veh_def, veh_att);
        if (veh_att >= 0 && triad(A->proto) == TRIAD_LAND) {
            int r = R->combat_art_altitude;
            if (combat && r && triad(D->proto) == TRIAD_LAND) {
                int aa = alt_under(A), ad = alt_under(D);
                if (aa > ad) {
                    int v = (aa - ad) * r;
                    off = percent(off, v + 100);
                    if (v != 0) report(0, v, game_label(576));
                }
            }
            if (R->combat_mobile_open && !combat && base < 0 && terrain == 2
                && (square(dx, dy)->rocky_lock & 0xc0) < 0x80 && speed_proto(A->proto) > R->move_rate_roads
                && speed_proto(D->proto) <= speed_proto(A->proto)) {
                off = percent(off, R->combat_mobile_open + 100);
                report(0, R->combat_mobile_open, game_label(611));
            }
            if (R->combat_def_vs_mobile && !combat && (terrain > 2 || base >= 0)
                && speed_proto(A->proto) > R->move_rate_roads) {
                def = percent(def, R->combat_def_vs_mobile + 100);
                report(1, R->combat_def_vs_mobile, game_label(base >= 0 ? 612 : 548));
            }
            if (R->combat_atk_road && !combat) {  // along a road or not
                off = percent(off, R->combat_atk_road + 100);
                report(0, R->combat_atk_road, game_label(606));
            }
            if (R->combat_pen_atk_lower && !combat && alt_under(D) > alt_under(A)
                && !has_abil(A->proto, ABL_ANTIGRAV_STRUTS)) {
                report(1, R->combat_pen_atk_lower, game_label(441));
                def = percent(def, R->combat_pen_atk_lower + 100);
            }
            if (R->combat_atk_higher && !combat && alt_under(A) > alt_under(D)
                && !has_abil(D->proto, ABL_ANTIGRAV_STRUTS)) {
                report(0, R->combat_atk_higher, game_label(330));
                off = percent(off, R->combat_atk_higher + 100);
            }
        }
        def *= terrain;
        if (terrain > 2) {
            char *text = game_battle_text;
            text[0] = 0;
            strcat(text, game_label(331));
            strcat(text, " ");
            strcat(text, "(");
            strcat(text, game_strings_get(game_strings, *game_battle_terrain));
            strcat(text, ")");
            int v = (terrain * 5 - 10) * 10;
            if (v != 0) report(1, v, text);
        }
        const struct map_tile *t = square(dx, dy);
        int mult = 2;
        char *text = NULL;
        if ((t->bits & MAP_BUNKER) && (veh_att < 0 || triad(A->proto) != TRIAD_AIR)) {
            text = (*game_labels)[358];
            mult = 3;
        }
        if (base >= 0) {
            text = (*game_labels)[332];
            mult = 2;
            int m = 2;
            if (veh_att < 0) {
                m = veh_att;  // the original's register still holds the attacker
            } else {
                switch (triad(A->proto)) {
                case TRIAD_LAND:
                    if (built(base, FAC_PERIMETER_DEFENSE) || owns_project(SP_CITIZENS_DEFENSE_FORCE, D->faction))
                        m = 4, text = (*game_labels)[354];
                    break;
                case TRIAD_SEA:
                    if (built(base, FAC_NAVAL_YARD) || owns_project(SP_MARITIME_CONTROL_CENTER, game_bases[base].faction))
                        m = 4, text = game_facilities[FAC_NAVAL_YARD].name;
                    break;
                case TRIAD_AIR:
                    if (built(base, FAC_AEROSPACE_COMPLEX) || owns_project(SP_CLOUDBASE_ACADEMY, game_bases[base].faction))
                        m = 4, text = game_facilities[FAC_AEROSPACE_COMPLEX].name;
                    break;
                }
            }
            if (built(base, FAC_TACHYON_FIELD)) m += 2, text = (*game_labels)[357];
            if (m >= 2) mult = m;
            if (m <= 2 && !(veh_att >= 0 && has_abil(A->proto, ABL_BLINK_DISPLACER)) && R->combat_base_def) {
                report(1, R->combat_base_def, game_label(332));
                def = percent(def, R->combat_base_def + 100);
            }
        }
        if (veh_att >= 0 && has_abil(A->proto, ABL_BLINK_DISPLACER)) {
            if (mult > 2) mult = 2;
            text = (*game_labels)[428];
        }
        if (R->combat_infantry_vs_base && !combat && base >= 0 && A->faction && !native(A->proto)
            && triad(A->proto) == TRIAD_LAND && game_chassis[chassis_of(A->proto)].speed == 1) {
            off = percent(off, R->combat_infantry_vs_base + 100);
            report(0, R->combat_infantry_vs_base, game_label(547));
        }
        if (art && mult <= 2 && base < 0) {
            t = square(dx, dy);
            if ((t->rocky_lock & 0xc0) < 0x80 && !(t->bits & MAP_FOREST)
                && !((t->bits & MAP_FUNGUS) && (t->climate & 0xe0) >= 0x40))
                mult = 3, text = (*game_labels)[525];
        }
        def *= mult;
        if (mult > 2) {
            char *shown = game_strings_get(game_strings, text);
            int v = (mult * 5 - 10) * 10;
            if (v != 0) report(1, v, shown);
        }
    }
    // A missile against the flechette defenses of the defender's bases
    // nearby, each counting; sensors on land.
    if (D->faction && veh_att >= 0 && game_chassis[chassis_of(A->proto)].missile) {
        for (int i = 0; i < 25; i++) {
            int x, y;
            if (!ring_square(dx, dy, i, &x, &y)) continue;
            int b = game_base_at(x, y);
            if (b >= 0 && game_bases[b].faction == D->faction && built(b, FAC_FLECHETTE_DEFENSE)) {
                def = def * 3 / 2;
                report(1, 50, game_label(1113));
            }
        }
    }
    if ((square(dx, dy)->climate & 0xe0) >= ALT_SHORE_LINE << 5) {
        int sensed = 0;
        if (D->faction) {
            for (int i = 0; i < 25; i++) {
                int x, y;
                if (!ring_square(dx, dy, i, &x, &y) || !is_sensor(x, y)) continue;
                if ((square(x, y)->climate & 0xe0) < ALT_SHORE_LINE << 5) {
                    if (game_bases[game_base_find(x, y)].faction != D->faction) continue;
                } else {
                    int owner = whose_territory(D->faction, x, y, NULL, 0);
                    if (owner >= 0 && owner != D->faction) continue;
                }
                sensed |= 1;
                if (is_sensor(x, y) > 1) sensed |= 2;
            }
        }
        if (sensed & 1) {
            def = percent(def, R->combat_sensor + 100);
            if (R->combat_sensor) report(1, R->combat_sensor, game_label(613));
        }
        if (sensed & 2) {
            def = percent(def, R->combat_sensor + 100);
            if (R->combat_sensor) report(1, R->combat_sensor, game_label(1123));
        }
    }
    if (!has_abil(A->proto, ABL_DISSOCIATIVE_WAVE) && R->combat_trance && veh_att >= 0 && (psi & 1)
        && has_abil(D->proto, ABL_TRANCE)) {
        def = percent(def, R->combat_trance + 100);
        report(1, R->combat_trance, game_label(329));
    }
    if (psi & 1) {
        if (veh_att < 0) goto out;
        int ar = proto_of(D->proto)->armor;
        if (ar == ARMOR_RESONANCE_3 || ar == ARMOR_RESONANCE_8) {
            def = percent(def, 125);
            report(1, 25, game_label(1111));
        }
    }
    if (veh_att < 0) goto out;
    {
        int ta = triad(A->proto), td = triad(D->proto);
        if (ta == TRIAD_AIR && has_abil(A->proto, ABL_AIR_SUPERIORITY) && !psi) {
            if (td != TRIAD_AIR) {
                if (R->combat_pen_air_vs_ground) {
                    off = percent(off, 100 - R->combat_pen_air_vs_ground);
                    report(0, -R->combat_pen_air_vs_ground, game_label(448));
                }
            } else if (R->combat_air_vs_air && !has_abil(D->proto, ABL_AIR_SUPERIORITY)) {
                off = percent(off, R->combat_air_vs_air + 100);
                report(0, R->combat_air_vs_air, game_label(449));
            }
        }
        if (ta == TRIAD_AIR && td == TRIAD_AIR && has_abil(D->proto, ABL_AIR_SUPERIORITY)
            && !has_abil(A->proto, ABL_AIR_SUPERIORITY) && !psi && !game_chassis[chassis_of(A->proto)].missile
            && !game_chassis[chassis_of(D->proto)].missile && R->combat_air_vs_air) {
            def = percent(def, R->combat_air_vs_air + 100);
            report(1, R->combat_air_vs_air, game_label(449));
        }
        const struct unit_proto *P = proto_of(D->proto);
        if (game_weapons[P->weapon].offense == 0 && game_armor[P->armor].defense == 1 && (A->faction || base < 0)
            && R->combat_pen_noncombat) {
            def = percent(def, 100 - R->combat_pen_noncombat);
            report(1, -R->combat_pen_noncombat, game_label(439));
        }
        if (ta != TRIAD_SEA && td == TRIAD_SEA && base >= 0 && R->combat_vs_ship_port) {
            off = percent(off, R->combat_vs_ship_port + 100);
            report(0, R->combat_vs_ship_port, game_label(335));
        }
        int fast = ta == TRIAD_LAND && game_chassis[chassis_of(A->proto)].speed > 1;
        if ((P->armor == ARMOR_PULSE_3 || P->armor == ARMOR_PULSE_8) && fast) {
            def = percent(def, 125);
            report(1, 25, game_label(1112));
        }
        if (!has_abil(A->proto, ABL_DISSOCIATIVE_WAVE) && has_abil(D->proto, ABL_COMM_JAMMER) && fast
            && R->combat_comm_jammer) {
            def = percent(def, R->combat_comm_jammer + 100);
            report(1, R->combat_comm_jammer, game_label(336));
        }
        if (!has_abil(A->proto, ABL_DISSOCIATIVE_WAVE) && has_abil(D->proto, ABL_AAA) && ta == TRIAD_AIR && R->combat_aaa) {
            def = percent(def, R->combat_aaa + 100);
            report(1, R->combat_aaa, game_label(337));
        }
    }
out:
    if (off_out) *off_out = off;
    if (def_out) *def_out = def;
}

// The unit of a stack that defends it: the one with the best score, its
// strength against the attacker (8 without one) after damage, less its own
// weapon, sixteen times over for combat units and formers (the plans below
// 8, and 9), and well ahead for an air superiority unit against an air
// attacker (or an air unit away from a base, airbase or carrier against
// one with air superiority), artillery when asked for, or otherwise a unit
// told to defend when the stack has no air units or is at a base, an
// airbase or a carrier. Ties go to the earlier unit; land units don't
// defend at sea outside a base, and against a probe team only probe teams
// do. The search stops at the first unit the attacker can't harm. Unlike
// OpenSMACX, an artillery duel needs both units to be artillery, and air
// superiority counts against an air attacker; the attacker is read even
// when there is none.
int best_defender(int veh_def, int veh_att, int artillery) {
    const struct veh *A = &game_vehs[veh_att], *D = &game_vehs[veh_def];
    int offense = veh_att >= 0 ? get_basic_offense(veh_att, veh_def, 0, 0, 0) : 8;
    const struct map_tile *t = square(D->x, D->y);
    int ocean = (t->climate & 0xe0) < ALT_SHORE_LINE << 5;
    int base = game_base_at(D->x, D->y);
    int best_score = -999, best = veh_def;
    if (veh_def < 0) return best;
    int i = veh_def;
    while (game_vehs[i].prev_in_stack >= 0) i = game_vehs[i].prev_in_stack;
    for (; i >= 0; i = game_vehs[i].next_in_stack) {
        const struct veh *V = &game_vehs[i];
        int cp = V->proto;
        const struct unit_proto *P = proto_of(cp);
        if (triad(cp) == TRIAD_LAND && ocean && base < 0) continue;
        if (proto_of(A->proto)->plan == PLAN_PROBE && P->plan != PLAN_PROBE) continue;
        int ct = can_arty(cp, 1) && can_arty(A->proto, 1) ? 2 : 0;
        if (veh_att >= 0 && triad(A->proto) == TRIAD_AIR && has_abil(cp, ABL_AIR_SUPERIORITY)
            && !game_chassis[P->chassis].missile && !game_chassis[chassis_of(A->proto)].missile
            && game_weapons[proto_of(A->proto)->weapon].offense > 0 && game_armor[proto_of(A->proto)->armor].defense > 0
            && game_weapons[P->weapon].offense > 0) {
            ct |= 0xa;
            if (triad(cp) == TRIAD_AIR) ct |= 0x10;
        }
        int o, d;
        battle_compute(veh_att, i, &o, &d, ct);
        if (!o) break;
        int power = P->plan == PLAN_ARTIFACT ? 1 : (P->reactor < 1 ? 1 : P->reactor > 100 ? 100 : P->reactor) * 10;
        int hp = power - V->damage;
        hp = hp < 0 ? 0 : hp > 9999 ? 9999 : hp;
        int v = (int)((uint32_t)hp * (uint32_t)d) / power;
        v = (int)((uint32_t)v * (uint32_t)offense) / o;
        int score = v / 8 - game_weapons[P->weapon].offense;
        if (P->plan < 8 || P->plan == PLAN_TERRAFORM) score = (int)((uint32_t)score << 4);
        int ahead = 0;
        if (veh_att >= 0 && triad(A->proto) == TRIAD_AIR && has_abil(cp, ABL_AIR_SUPERIORITY) && triad(cp) == TRIAD_AIR
            && !game_chassis[P->chassis].missile && !game_chassis[chassis_of(A->proto)].missile) {
            ahead = 1;
        } else if (veh_att >= 0 && triad(A->proto) == TRIAD_AIR && has_abil(A->proto, ABL_AIR_SUPERIORITY)
                   && triad(cp) == TRIAD_AIR && base < 0 && !(t->bits & MAP_AIRBASE)
                   && !stack_check(i, 6, ABL_CARRIER, -1, -1) && !game_chassis[P->chassis].missile
                   && !game_chassis[chassis_of(A->proto)].missile) {
            ahead = 1;
        } else if (artillery) {
            ahead = can_arty(cp, 1);
        } else if (!stack_check(i, 3, TRIAD_AIR, -1, -1) || base >= 0 || (t->bits & MAP_AIRBASE)
                   || stack_check(i, 6, ABL_CARRIER, -1, -1)) {
            ahead = (V->state & VEH_STATE_DESIGNATED_DEFENDER) != 0;
        }
        if (ahead) score += 0x80000;
        score = (int)((uint32_t)score << 11) + i;
        if (score > best_score) best_score = score, best = i;
    }
    return best;
}

// A transport's cargo: the units of its faction on its square that it can
// carry (land units that aren't transports themselves, and for a carrier
// air units that aren't carriers), up to its capacity (any number for the
// natives).
//
// Type 0 counts: units already on board this transport count first, then
// sentried ones without a transport, or for the AI, units it would load;
// they're marked (state bit 1), and the room left is returned. Type 1 also
// takes them off the map, to a stack at (-2, -2), and then the transport,
// returning veh_drop()'s answer. Type 2, for a transport about to be
// killed, takes only the units on board, and in a base none.
//
// The AI loads a unit at sea, or one its goal says should go by sea, or
// one in a region the AI isn't defending; a unit on hold, an artifact or
// a former only at sea. An air transport loads only at a base of a
// faction (not the natives') or an airbase.
int stack_veh(int veh, int type) {
    if (veh < 0) return type ? veh : 0;
    if (!veh_cargo(veh) && !has_abil(game_vehs[veh].proto, ABL_CARRIER)) {
        if (!type) return 0;
        return veh_drop(veh_lift(veh), -2, -2);
    }
    if (*game_net_multiplayer) stack_fix(veh);
    if (type) {
        const struct veh *T = &game_vehs[veh];
        if (type > 1 && faction_base(square(T->x, T->y), 0)) return veh_drop(veh_lift(veh), -2, -2);
        stack_sort(veh);
    } else {
        for (int v = veh_top(veh); v >= 0; v = game_vehs[v].next_in_stack) game_vehs[v].state &= ~1u;
    }
    const struct veh *T = &game_vehs[veh];
    int tx = T->x, ty = T->y, at_sea = 0;
    if (on_map_(tx, ty) && is_ocean(tx, ty) && !faction_base(square(tx, ty), 0)) at_sea = 1;
    int faction = T->faction, air = triad(T->proto) == TRIAD_AIR;
    int room = veh_cargo(veh), air_room = 0;
    if (has_abil(T->proto, ABL_CARRIER)) air_room = veh_cargo(veh) < 1 ? 1 : veh_cargo(veh);
    if (!faction) room = 99;
    int passes = type <= 1 ? 2 : 1;
    for (int pass = 0; pass < passes; pass++) {
        int v = type ? veh : veh_top(veh);
        while (v >= 0 && (room || air_room)) {
            int next = game_vehs[v].next_in_stack;
            struct veh *U = &game_vehs[v];
            int load = 0;
            if (U->faction != faction) goto skip;
            int fits = room && triad(U->proto) == TRIAD_LAND && !veh_cargo(v);
            if (!fits && !(air_room && triad(U->proto) == TRIAD_AIR && !has_abil(U->proto, ABL_CARRIER))) goto skip;
            if (pass == 0) {
                load = U->order == ORDER_SENTRY_BOARD && U->waypoint_x[0] == veh;
            } else {
                if (air) {
                    const struct map_tile *t = square(tx, ty);
                    if (!faction_base(t, 1) && !(t->bits & MAP_AIRBASE)) goto skip;
                }
                int sentried = 0;
                if (((uint8_t)(1u << U->faction) & *game_human_factions) &&
                    !((T->state & VEH_STATE_UNK_200) && (U->state & VEH_STATE_UNK_200))) {
                    if (U->order == ORDER_SENTRY_BOARD) load = U->waypoint_x[0] < 0;
                    else sentried = 1;
                } else if (U->state & VEH_STATE_UNK_40000) {
                    if (!(U->state & VEH_STATE_UNK_20000)) goto skip;
                    int f = U->faction;
                    load = game_factions[f].region_base_plan[square(U->x, U->y)->region] == PLAN_NAVAL_TRANSPORT;
                } else {
                    int plan = proto_of(U->proto)->plan;
                    if (U->order == ORDER_HOLD || plan == PLAN_ARTIFACT || plan == PLAN_TERRAFORM) {
                        sentried = 1;
                    } else if (at_sea || !on_map_(U->x, U->y) ||
                               game_factions[U->faction].region_base_plan[square(U->x, U->y)->region] != PLAN_DEFENSIVE ||
                               plan >= PLAN_COLONY) {
                        load = 1;
                    }
                }
                if (sentried) load = at_sea;
            }
            if (!load) goto skip;
            if (!type && (U->state & 1)) goto skip;
            veh_sleep(v);
            U->waypoint_x[0] = (int16_t)veh;
            if (type) {
                veh_drop(veh_lift(v), -2, -2);
                U->state &= ~VEH_STATE_UNK_10000;
            } else {
                U->state |= 1;
            }
            if (air_room > 0) air_room--;
            if (room > 0) room--;
        skip:
            v = next;
        }
    }
    if (type) return veh_drop(veh_lift(veh), -2, -2);
    return veh_cargo(veh) ? room : air_room;
}

// A new unit of the design for the faction at the square, or -1 when the
// table is full. Its home is the nearest base if that's the faction's (not
// for an alien artifact, which some games flag instead), and it is spotted
// at once.
//
// The AI may then send a new land unit (other than drop-pod units) to the
// faction's target, if it was made on land in the target's region, is a
// combat or defensive unit or a probe, and the target is no further than
// the nearest base of an enemy in the region (the first faction it has a
// vendetta with, once it has bases there itself). It sends up to
// two thirds of its bases in the region (3 to 8, and no more than twice
// its transports' cargo when the region is a naval one), one probe, and
// defenders (armor above weapon) only while none has gone. An attacker
// stays home one time in that number plus two.
int veh_init(int proto, int faction, int x, int y) {
    if (*game_veh_count >= VEH_COUNT) return -1;
    int veh = (*game_veh_count)++;
    game_log_say(text(0x691d98), veh, faction, proto);
    struct faction_data *F = &game_factions[faction];
    if (proto_of(proto)->plan <= PLAN_NAVAL_TRANSPORT) F->total_combat_units++;
    F->units_active[proto]++;
    veh_clear(veh, proto, faction);
    int base = game_base_find(x, y);
    struct veh *V = &game_vehs[veh];
    if (proto_of(proto)->plan == PLAN_ARTIFACT) {
        if (*game_state & GAME_STATE_UNK_40000) V->flags |= VEH_FLAG_UNK_20;
    } else if (base >= 0 && game_bases[base].faction == faction) {
        V->home_base = (int16_t)base;
        if (*game_computed_base_id == base) *game_computed_base_id = -1;
    }
    veh_drop(veh, x, y);
    game_spot_all(veh, 1);
    if (F->target_x < 0) return veh;
    const struct map_tile *here = square(x, y);
    if (here->climate >> 5 < ALT_SHORE_LINE) return veh;
    int plan = proto_of(V->proto)->plan;
    if (plan > PLAN_DEFENSIVE && plan != PLAN_PROBE) return veh;
    int region = here->region;
    if (square(F->target_x, F->target_y)->region != region) return veh;
    int enemy = -1, best = 0;
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (i == faction || !(F->diplo_status[i] & TREATY_VENDETTA)) continue;
        if (F->region_total_bases[region] > best) best = F->region_total_bases[region], enemy = i;
    }
    if (enemy >= 0) {
        game_base_find3(x, y, enemy, region, -1, -1);
        if (vector_dist(x, y, F->target_x, F->target_y) > *game_base_find_dist) return veh;
    }
    int limit = F->region_total_bases[region] * 2 / 3;
    limit = limit < 3 ? 3 : limit > 8 ? 8 : limit;
    int naval = F->region_base_plan[region] == PLAN_NAVAL_TRANSPORT;
    if (naval) {
        int cargo = (int32_t)((uint32_t)F->naval << 1) / 0x400000;
        if (limit >= cargo) limit = cargo;
    }
    if (F->target_units - game_bit_count(F->target_kinds) >= limit) return veh;
    int weapon = weap_strat(proto_of(V->proto)->weapon, faction);
    int armor = arm_strat(proto_of(V->proto)->armor, faction);
    if (triad(V->proto) != TRIAD_LAND || has_abil(V->proto, ABL_DROP_POD)) return veh;
    if (plan == PLAN_PROBE) {
        if (F->target_kinds & 2) return veh;
        V->state |= VEH_STATE_UNK_40000;
        F->target_units++;
        F->target_kinds |= 2;
        return veh;
    }
    if (weapon < armor) {
        if ((F->target_kinds & 1) || naval) return veh;
    } else if (!naval) {
        int n = F->region_total_bases[region] * 2 / 3;
        n = n < 3 ? 3 : n > 8 ? 8 : n;
        if (n + 1 <= 0 || game_rand() % (n + 2) == 0) {
            V->state |= VEH_STATE_UNK_20000;
            return veh;
        }
    }
    V->state |= VEH_STATE_UNK_40000;
    F->target_units++;
    if (weapon < armor) F->target_kinds |= 1;
    return veh;
}

// Removes the unit: off its stack and its faction's counts, and every
// later unit moves down a slot, so the units' stack links, the transport
// a unit on board names, and the unit numbers kept elsewhere are
// renumbered; a unit on board this one is left without orders. Removing
// the spare slot (veh_fake) leaves the count alone. The base window is
// told about the square and the home base.
void veh_kill(int veh) {
    if (veh < 0) return;
    if (*game_net_multiplayer) game_deletion_add(game_deletion_list, veh);
    struct veh *V = &game_vehs[veh];
    game_log_say(text(0x691dc0), veh, V->faction, V->proto);
    int x = V->x, y = V->y, home = V->home_base;
    struct faction_data *F = &game_factions[V->faction];
    if (proto_of(V->proto)->plan <= PLAN_NAVAL_TRANSPORT) F->total_combat_units--;
    F->units_active[V->proto]--;
    veh_lift(veh);
    int n = *game_veh_count;
    if (veh < n - 1) memmove(V, V + 1, (size_t)(n - veh - 1) * sizeof *V);
    if (veh < VEH_COUNT) *game_veh_count = --n;
    for (int i = 0; i < n; i++) {
        struct veh *U = &game_vehs[i];
        if (U->prev_in_stack > veh) U->prev_in_stack--;
        if (U->next_in_stack > veh) U->next_in_stack--;
        if (U->order == ORDER_SENTRY_BOARD) {
            if (U->waypoint_x[0] == veh) U->order = 0;
            else if (U->waypoint_x[0] > veh) U->waypoint_x[0]--;
        }
    }
    int32_t *refs[] = {game_veh_ref_map_1, game_veh_ref_map_2, game_veh_ref_3, game_veh_ref_4};
    for (unsigned i = 0; i < sizeof refs / sizeof refs[0]; i++) {
        if (*refs[i] == veh) *refs[i] = -1;
        else if (*refs[i] > veh) (*refs[i])--;
    }
    if (*game_current_veh && *game_current_veh >= veh) (*game_current_veh)--;
    int32_t *p = *game_net_veh_ref;
    if (*game_net_multiplayer && p && p != game_veh_ref_map_1 && p != game_current_veh && p != game_veh_ref_map_2 &&
        *p >= veh)
        (*p)--;
    game_base_win_check_loc(game_base_win, x, y, 0);
    game_base_win_check_base(game_base_win, home);
}

// Kills every unit in the unit's stack, from its head.
void stack_kill(int veh) {
    if (veh < 0) return;
    for (int v = veh_top(veh); v >= 0;) {
        int next = game_vehs[v].next_in_stack;
        veh_kill(v);
        if (next > v) next--;
        v = next;
    }
}

// Kills the unit and, for a transport at sea (or off the map, but for a
// unit at x -2, which takes no redraw), the units it carries; it then
// redraws the square. The player's square is redrawn with its
// surroundings when no unit of theirs is left there, or the unit had
// deep radar.
void kill(int veh) {
    const struct veh *V = &game_vehs[veh];
    int x = V->x, y = V->y, faction = V->faction, home = V->home_base;
    int radar = has_abil(V->proto, ABL_DEEP_RADAR);
    int on_map = on_map_(x, y);
    if (!veh_cargo(veh) || (on_map && !is_ocean(x, y))) {
        veh_kill(veh);
    } else if (!on_map && x == -2) {
        stack_kill(veh);
        return;
    } else {
        stack_veh(veh, 2);
        stack_kill(veh);
    }
    game_base_win_check_base(game_base_win, home);
    if (*game_battle_state || !on_map) return;
    if (faction == *game_current_faction && (!stack_check(veh_at(x, y), 1, faction, -1, -1) || radar))
        game_draw_radius(x, y, 2, 2);
    else
        game_draw_tile(x, y, 2);
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

// The stack functions write units, the map and the lifted unit's globals.
static void writes_stacks(struct hook_writes *w) {
    hooks_writes_add(w, game_vehs, (VEH_COUNT + 1) * sizeof(struct veh));
    hooks_writes_add(w, game_veh_lift_x, 0x18);  // to game_veh_at_rebuilt
    hooks_writes_add(w, *game_map, (size_t)*game_map_half_width * *game_map_height * sizeof(struct map_tile));
}
#define WRITES_STACKS writes_stacks(&w_)
SHADOW_WRITER(VEH, int, veh_lift, (int a), (a), WRITES_STACKS, "%d", a)
SHADOW_WRITER(VEH, int, veh_drop, (int a, int b, int c), (a, b, c), WRITES_STACKS, "%d, %d, %d", a, b, c)
SHADOW_WRITER(VEH, void, veh_promote, (int a), (a), WRITES_STACKS, "%d", a)
SHADOW_WRITER(VEH, void, veh_demote, (int a), (a), WRITES_STACKS, "%d", a)
SHADOW_WRITER(VEH, void, stack_put, (int a, int b, int c), (a, b, c), WRITES_STACKS, "%d, %d, %d", a, b, c)
SHADOW_WRITER(VEH, void, stack_sort, (int a), (a), WRITES_STACKS, "%d", a)
SHADOW_WRITER(VEH, void, stack_sort_2, (int a), (a), WRITES_STACKS, "%d", a)
// stack_fix() is called constantly and acts only in a network game: it is
// checked as a query otherwise.
SHADOW_WRITER_CHECK(int, stack_fix, (int a), (a), orig_stack_fix(a), WRITES_STACKS, "%d", a)
static int shadow_stack_fix(int a) {
    if (hooks_nested) return orig_stack_fix(a);
    if (shadow_bypass()) return stack_fix(a);
    stats_stack_fix.calls++;
    shadow_tick(VEH);
    if (*game_net_multiplayer) return check_stack_fix(a);
    hooks_nested++;
    int want = orig_stack_fix(a);
    hooks_nested--;
    int got = stack_fix(a);
    if (got != want) hooks_mismatch(&stats_stack_fix, "(%d) = %d, original %d", a, got, want);
    return want;
}
// veh_init() calls veh_clear() for a slot that still holds a dead unit's
// stack links, so it never starts the sweep, which would walk them.
SHADOW_WRITER_CHECK(void, veh_clear, (int a, int b, int c), (a, b, c), orig_veh_clear(a, b, c), WRITES_STACKS, "%d, %d, %d",
                    a, b, c)
static void shadow_veh_clear(int a, int b, int c) {
    if (hooks_nested) {
        orig_veh_clear(a, b, c);
        return;
    }
    if (shadow_bypass()) {
        veh_clear(a, b, c);
        return;
    }
    stats_veh_clear.calls++;
    check_veh_clear(a, b, c);
}
SHADOW_WRITER(VEH, void, veh_sleep, (int a), (a), WRITES_STACKS, "%d", a)
SHADOW_WRITER(VEH, void, veh_skip, (int a), (a), WRITES_STACKS, "%d", a)
SHADOW2(VEH, int, veh_speed, int, int)
SHADOW3(VEH, int, offense_proto, int, int, int)
SHADOW3(VEH, int, armor_proto, int, int, int)
SHADOW1(VEH, int, prototype_factor, int)
SHADOW1(VEH, int, veh_jail, int)
SHADOW1(VEH, int, veh_selectable, int)
SHADOW1(VEH, int, veh_unmoved, int)
SHADOW1(VEH, int, veh_ready, int)
SHADOW_WRITER(VEH, int, veh_find, (int a, int b, int c, int d), (a, b, c, d), WRITES(game_veh_find_dist, 4), "%d, %d, %d, %d", a, b, c, d)
SHADOW_WRITER(VEH, int, veh_fake, (int a, int b), (a, b), WRITES(&game_vehs[VEH_COUNT], sizeof(struct veh)), "%d, %d", a, b)
SHADOW_WRITER(VEH, int, veh_wake, (int a), (a), WRITES(&game_vehs[a], sizeof(struct veh)), "%d", a)
SHADOW1(VEH, int, drop_range, int)
SHADOW1(VEH, int, planet_buster, int)
SHADOW1(VEH, int, veh_top, int)
SHADOW1(VEH, int, veh_moves, int)
SHADOW1(VEH, int, proto_power, int)
SHADOW2(VEH, int, armor_val, int, int)
SHADOW3(VEH, int, transport_val, int, uint32_t, int)
SHADOW1(VEH, int, abil_index, uint32_t)
SHADOW_WRITER(VEH, int, veh_put, (int a, int b, int c), (a, b, c), WRITES_STACKS, "%d, %d, %d", a, b, c)
SHADOW1(VEH, int, veh_health, int)
SHADOW2(VEH, int, get_plan, int, int)
SHADOW1(VEH, int, want_monolith, int)
SHADOW3(VEH, int, want_to_wake, int, int, int)
SHADOW_WRITER(VEH, void, wake_stack, (int a), (a), WRITES_STACKS, "%d", a)
SHADOW2(VEH, int, wants_prototype, int, int)
SHADOW_WRITER(VEH, void, battle_init, (void), (), WRITES(game_battle_mod_count, 16), "%s", "")
SHADOW_WRITER(VEH, void, add_bat, (int a, int b, const char *c), (a, b, c),
              (WRITES(game_battle_mod_count, 8), WRITES(game_battle_mod, 8 * 4), WRITES(game_battle_mod_label, 8 * 80)),
              "%d, %d, \"%s\"", a, b, c)
SHADOW_WRITER(VEH, int, defense_value, (int a, int b, int c, int d, int e), (a, b, c, d, e), WRITES(game_battle_terrain, 4),
              "%d, %d, %d, %d, %d", a, b, c, d, e)
SHADOW_WRITER(VEH, int, get_basic_offense, (int a, int b, int c, int d, int e), (a, b, c, d, e),
              WRITES(game_battle_morale, 8), "%d, %d, %d, %d, %d", a, b, c, d, e)
SHADOW_WRITER(VEH, int, get_basic_defense, (int a, int b, int c, int d), (a, b, c, d), WRITES(game_battle_morale, 8),
              "%d, %d, %d, %d", a, b, c, d)
SHADOW_FN(VEH, int, hex_cost, (int a, int b, int c, int d, int e, int f, int g), (a, b, c, d, e, f, g),
          "%d, %d, %d, %d, %d, %d, %d", a, b, c, d, e, f, g)

// A battle writes its report, the morale and terrain it found, the text it
// put together, base_find()'s distance and its two results; a check gives
// both runs the same place for the results and hands the original's back.
static int32_t battle_out[2];
static void writes_battle(struct hook_writes *w) {
    hooks_writes_add(w, game_battle_mod_count, 16);
    hooks_writes_add(w, game_battle_mod, 8 * 4);
    hooks_writes_add(w, game_battle_mod_label, 8 * 80);
    hooks_writes_add(w, game_battle_terrain, 4);
    hooks_writes_add(w, game_battle_morale, 8);
    hooks_writes_add(w, game_battle_text, 256);
    hooks_writes_add(w, game_base_find_dist, 4);
    hooks_writes_add(w, battle_out, sizeof battle_out);
}
SHADOW_DECLARE(void, battle_compute, (int a, int b, int *c, int *d, int e))
static void check_battle_compute(int a, int b, int *c, int *d, int e) {
    struct hook_writes w_ = {0};
    writes_battle(&w_);
    int *oc = c ? &battle_out[0] : NULL, *od = d ? &battle_out[1] : NULL;
    hooks_writes_begin(&w_);
    hooks_nested++;
    orig_battle_compute(a, b, oc, od, e);
    hooks_nested--;
    int want[2] = {battle_out[0], battle_out[1]};
    hooks_writes_middle(&w_);
    battle_compute(a, b, oc, od, e);
    const char *diff = hooks_writes_end(&w_, 0, 0);
    if (diff) hooks_mismatch(&stats_battle_compute, "(%d, %d, %d, %d, %d) %s", a, b, c != NULL, d != NULL, e, diff);
    hooks_ui_release();
    if (c) *c = want[0];
    if (d) *d = want[1];
}
SHADOW_WRITER_ENTRY(VEH, void, battle_compute, (int a, int b, int *c, int *d, int e), (a, b, c, d, e))
SHADOW_WRITER(VEH, int, best_defender, (int a, int b, int c), (a, b, c), writes_battle(&w_), "%d, %d, %d", a, b, c)
// Killing a unit renumbers units all over the game's memory.
SHADOW_WRITER(VEH, int, veh_init, (int a, int b, int c, int d), (a, b, c, d), hooks_writes_game(&w_), "%d, %d, %d, %d", a,
              b, c, d)
SHADOW_WRITER(VEH, int, stack_veh, (int a, int b), (a, b), WRITES_STACKS, "%d, %d", a, b)
SHADOW_WRITER(VEH, void, veh_kill, (int a), (a), hooks_writes_game(&w_), "%d", a)
SHADOW_WRITER(VEH, void, stack_kill, (int a), (a), hooks_writes_game(&w_), "%d", a)
SHADOW_WRITER(VEH, void, kill, (int a), (a), hooks_writes_game(&w_), "%d", a)

// veh_at() is called constantly, so it's checked as a query; a square the
// map marks with units but where the original found none (it then logs
// and rebuilds the map's bits) isn't compared, as ours would do it again.
SHADOW_DECLARE(int, veh_at, (int a, int b))
static int check_veh_at(int x, int y) {
    hooks_nested++;
    int want = orig_veh_at(x, y);
    hooks_nested--;
    if (want < 0 && on_map_(x, y) && ((*game_map)[(x >> 1) + y * *game_map_half_width].bits & MAP_UNIT)) return want;
    int got = veh_at(x, y);
    if (got != want) hooks_mismatch(&stats_veh_at, "(%d, %d) = %d, original %d", x, y, got, want);
    return want;
}
SHADOW_ENTRY(VEH, int, veh_at, (int a, int b), (a, b))

// Whether a unit's stack is a proper list both ways, without loops: the
// stack functions would walk forever otherwise.
static int stack_sound(int veh) {
    int n = *game_veh_count, steps = 0;
    for (int v = veh; game_vehs[v].prev_in_stack >= 0; v = game_vehs[v].prev_in_stack) {
        int p = game_vehs[v].prev_in_stack;
        if (p >= n || game_vehs[p].next_in_stack != v || ++steps > n) return 0;
    }
    for (int v = veh; game_vehs[v].next_in_stack >= 0; v = game_vehs[v].next_in_stack) {
        int q = game_vehs[v].next_in_stack;
        if (q >= n || game_vehs[q].prev_in_stack != v || ++steps > 2 * n) return 0;
    }
    return 1;
}

// Stacks moved, sorted and cleared for a few units each turn, and every
// square's head. stack_fix() is checked only as games other than network
// ones use it: made to act, the original promoted unit after unit until
// it tied a stack into a loop and its veh_at() never returned.
static void sweep_stacks(void) {
    int vehs = *game_veh_count, t = *game_turn, w = *game_map_width, h = *game_map_height;
    for (int y = 0; y < h; y++)
        for (int x = y & 1; x < w; x += 2) check_veh_at(x, y);
    check_veh_at(-3, -3);
    check_veh_at(w, 0);
    for (int v = 0; v < vehs; v++) {
        const struct veh *V = &game_vehs[v];
        check_veh_speed(v, v & 1);
        check_veh_jail(v);
        check_veh_selectable(v);
        check_veh_unmoved(v);
        check_veh_ready(v);
        int other = (v * 7 + t) % vehs;
        for (int b = 0; b < 2; b++) {
            check_offense_proto(V->proto, other, b);
            check_armor_proto(V->proto, other, b);
        }
        check_offense_proto(V->proto, -1, v & 1);
        check_armor_proto(V->proto, -1, v & 1);
    }
    for (int p = 0; p < PROTO_COUNT; p++)
        if (game_protos[p].flags & PROTO_FLAG_ACTIVE) check_prototype_factor(p);
    for (int k = 0; k < 16; k++) {
        int x = (k * 13 + t) % w, y = (k * 7 + t) % h;
        check_veh_find(x, y, k % 9 - 1, k * 5 % (vehs + 1) - 1);
    }
    for (int k = 0; k < 12 && vehs; k++) {
        int v = (k * 37 + t) % vehs;
        if (!stack_sound(v)) continue;
        const struct veh *V = &game_vehs[v];
        int x = V->x, y = V->y, nx = (x + 2) % w;
        check_veh_lift(v);
        check_veh_drop(v, x, y);
        check_veh_promote(v);
        check_veh_demote(v);
        check_stack_put(v, nx, y);
        check_veh_put(v, nx, (y + 2) % h);
        check_stack_sort(v);
        check_stack_sort_2(v);
        check_stack_fix(v);
        check_veh_clear(v, V->proto, (V->faction + k) % FACTION_COUNT);
        check_veh_sleep(v);
        check_veh_skip(v);
        check_veh_wake(v);
        check_veh_fake(V->proto, V->faction);
    }
    // Loading: every transport, each way, and again as a human's (soaks
    // hand every faction to the AI).
    for (int v = 0; v < vehs; v++) {
        if (!stack_sound(v) || (!veh_cargo(v) && !has_abil(game_vehs[v].proto, ABL_CARRIER) && v % 16)) continue;
        for (int type = 0; type < 3; type++) check_stack_veh(v, type);
        uint8_t humans = *game_human_factions;
        *game_human_factions ^= (uint8_t)(1u << game_vehs[v].faction);
        for (int type = 0; type < 2; type++) check_stack_veh(v, type);
        // With the next unit on board this transport, or another one.
        int n = game_vehs[v].next_in_stack;
        if (n >= 0) {
            struct veh saved = game_vehs[n];
            game_vehs[n].order = ORDER_SENTRY_BOARD;
            for (int k = 0; k < 2; k++) {
                game_vehs[n].waypoint_x[0] = (int16_t)(k ? (v + 1) % vehs : v);
                for (int type = 0; type < 3; type++) check_stack_veh(v, type);
                check_wake_stack(v);
                *game_human_factions = humans;
                for (int type = 0; type < 3; type++) check_stack_veh(v, type);
                *game_human_factions ^= (uint8_t)(1u << game_vehs[v].faction);
            }
            game_vehs[n] = saved;
        }
        *game_human_factions = humans;
    }
    check_stack_veh(-1, 1);
    check_stack_veh(-1, 0);
    // New units: each faction's designs at its bases and its target, and
    // where a unit stands. (Like the original, veh_init() reads the map at
    // its square unchecked, so it isn't given one off the map.)
    for (int f = 0; f < FACTION_COUNT; f++) {
        const struct faction_data *F = &game_factions[f];
        for (int k = 0; k < 3; k++) {
            int p = (k + t + f) % (2 * BASIC_PROTOS);
            p = p < BASIC_PROTOS ? p : f * BASIC_PROTOS + p - BASIC_PROTOS;
            if (!(game_protos[p].flags & PROTO_FLAG_ACTIVE)) continue;
            int b = *game_base_count ? (f * 7 + k + t) % *game_base_count : -1;
            if (b >= 0) check_veh_init(p, f, game_bases[b].x, game_bases[b].y);
            if (F->target_x >= 0) check_veh_init(p, f, F->target_x, F->target_y);
        }
    }
    for (int k = 0; k < 4 && vehs; k++) {
        const struct veh *V = &game_vehs[(k * 97 + t) % vehs];
        if (on_map_(V->x, V->y)) check_veh_init(V->proto, V->faction, V->x, V->y);
    }
    // Killing: a few units, a stack, and every transport (some at sea).
    for (int k = 0; k < 4 && vehs; k++) {
        int v = (k * 53 + t * 5) % vehs;
        if (!stack_sound(v)) continue;
        check_veh_kill(v);
        if (k == 0) check_stack_kill(v);
        check_kill(v);
    }
    for (int v = 0; v < vehs; v++)
        if (veh_cargo(v) && stack_sound(v)) check_kill(v);
    check_veh_kill(-1);
}

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
// secret projects that count, some of the bases' facilities, and some
// units' designs, damage, owner and orders, and their chassis as a missile. They are put back afterwards; nothing else
// changes, as these functions only read.
static const int varied_projects[] = {SP_COMMAND_NEXUS, SP_XENOEMPATHY_DOME, SP_NEURAL_AMPLIFIER,
                                      SP_MARITIME_CONTROL_CENTER, SP_PHOLUS_MUTAGEN, SP_CYBORG_FACTORY,
                                      SP_DREAM_TWISTER, SP_TELEPATHIC_MATRIX, SP_VOICE_OF_PLANET,
                                      SP_NETHACK_TERMINUS, SP_CLOUDBASE_ACADEMY, SP_SPACE_ELEVATOR};
enum { VARIED = sizeof varied_projects / sizeof varied_projects[0] };
static void sweep_battle(int v, int o, int k);
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
        check_drop_range(f);
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
        if (!stack_sound(v)) continue;
        struct veh *V = &game_vehs[v];
        int16_t proto = V->proto;
        V->proto = (int16_t)designs[(v * 13 + t) % count];
        for (int type = 0; type < 20; type++)
            check_stack_check(v, type, type == 6 ? (int)(1u << (v % 32)) : (v + t) % 4, -1, -1);
        check_can_arty(V->proto, v & 1);
        check_veh_cargo(v);
        check_morale_veh(v, 0, 0);
        uint8_t damage = V->damage;
        V->damage = (uint8_t)((v * 7 + t) % 30);
        check_veh_speed(v, v & 1);
        V->damage = damage;
        check_veh_lift(v);
        check_veh_drop(v, V->x, V->y);
        check_stack_sort(v);
        // As the player's, under each order; and its design as a missile.
        uint8_t faction = V->faction, order = V->order;
        V->faction = (uint8_t)*game_current_faction;
        V->order = (uint8_t)((v + t) % 26);
        check_veh_selectable(v);
        check_veh_unmoved(v);
        check_veh_ready(v);
        check_veh_wake(v);
        V->faction = faction, V->order = order;
        struct rules_chassis *C = &game_chassis[proto_of(V->proto)->chassis];
        uint8_t missile = C->missile;
        C->missile = 1;
        check_offense_proto(V->proto, (v * 3 + t) % *game_veh_count, 0);
        check_offense_proto(V->proto, -1, 1);
        C->missile = missile;
        V->proto = proto;
    }
    // Units in battle, some of them as native life's designs, for the
    // Brood Pit.
    for (int v = (t & 3); v < *game_veh_count; v += 4) {
        if (!stack_sound(v)) continue;
        struct veh *V = &game_vehs[v];
        int o = (v * 7 + t) % *game_veh_count;
        sweep_battle(v, o, v + t);
        // Air dropped, and told to defend.
        uint32_t state = V->state;
        V->state ^= VEH_STATE_AIRDROPPED | VEH_STATE_DESIGNATED_DEFENDER;
        int att, def;
        check_battle_compute(v, o, &att, &def, 0);
        check_best_defender(o, v, 0);
        check_best_defender(v, o, 0);
        V->state = state;
        int16_t proto = V->proto;
        V->proto = (v & 4) ? BSC_SPORE_LAUNCHER : BSC_MIND_WORMS;
        check_get_basic_offense(v, o, 1, 0, v & 8);
        check_get_basic_defense(v, o, 1, 0);
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

// (No battle is checked without an attacker: the original then reads the
// design of the unit before the first, whatever memory that is, and can
// fault.) A unit's strengths against another unit and none, its terrain's defense
// there and around it, and its moves out of its square (also as native
// life and with no design).
static void sweep_battle(int v, int o, int k) {
    const struct veh *V = &game_vehs[v];
    check_get_basic_offense(v, o, k & 1, (k >> 1) & 1, 1);
    check_get_basic_offense(v, o, 0, 0, 0);
    check_get_basic_offense(v, -1, (k >> 2) & 1, 0, k & 2);
    check_get_basic_defense(v, o, k & 1, (k >> 1) & 1);
    check_get_basic_defense(v, -1, (k >> 2) & 1, 0);
    check_defense_value(V->faction, V->x, V->y, v, o);
    check_defense_value(V->faction, V->x, V->y, v, -1);
    static const int types[] = {0, 1, 2, 3, 0xa, 0x1a};
    int att, def;
    check_battle_compute(o, v, &att, &def, types[k % 6]);
    check_battle_compute(o, v, NULL, &def, 0);
    check_best_defender(v, o, k & 1);
    for (int i = 1; i <= 8; i++) {
        int x = xrange(V->x + game_radius_x[i]), y = V->y + game_radius_y[i];
        if (!on_map_(x, y)) continue;
        check_hex_cost(V->proto, V->faction, V->x, V->y, x, y, (k + i) & 1);
        check_hex_cost(game_vehs[o].proto, (k + i) % FACTION_COUNT, V->x, V->y, x, y, 0);
        if (i & 1) check_hex_cost(i & 2 ? -1 : V->proto, 0, x, y, V->x, V->y, 0);
        if (!(i & 3)) check_defense_value((k + i) % FACTION_COUNT, x, y, v, i & 4 ? o : -1);
    }
}

// The battle report: each side's modifiers from empty to full.
static void sweep_battle_report(void) {
    int32_t held[2] = {game_battle_mod_count[0], game_battle_mod_count[1]};
    check_battle_init();
    for (int n = 0; n <= 5; n++)
        for (int side = 0; side < 2; side++) {
            game_battle_mod_count[side] = n;
            check_add_bat(side, n - 2, "Chiron");
        }
    game_battle_mod_count[0] = held[0], game_battle_mod_count[1] = held[1];
}

// Waking: some units spotting others for each faction, under each of the
// player's preferences and in a network game, with each treaty, and air units as if on their
// way to a base. Stacks with units sentried on board are woken in the
// loading checks.
static void sweep_wake(void) {
    int vehs = *game_veh_count, t = *game_turn, bases = *game_base_count;
    static const uint32_t prefs[] = {0, PREF_WAKE_PACT | PREF_WAKE_TRUCE, PREF_WAKE_TREATY | PREF_WAKE_WAR,
                                     0xffffffffu, PREF_WAKE_SAME_TRIAD | PREF_WAKE_WAR | PREF_WAKE_PACT};
    int32_t pref = *game_preferences, net = *game_net_multiplayer;
    for (int k = 0; k < 24 && vehs; k++) {
        int v = (k * 41 + t) % vehs;
        struct veh *V = &game_vehs[v];
        struct veh saved = *V;
        for (int round = 0; round < 2; round++) {
            if (round) {
                if (!bases) break;
                const struct base *B = &game_bases[(k + t) % bases];
                V->order = ORDER_MOVE_TO, V->movement_turns = (uint8_t)(k % 3);
                V->waypoint_x[0] = B->x, V->waypoint_y[0] = B->y;
                if (k & 1) V->state |= VEH_STATE_UNK_200 | VEH_STATE_UNK_1000000;
            }
            for (unsigned i = 0; i <= sizeof prefs / sizeof prefs[0]; i++) {
                *game_net_multiplayer = i == sizeof prefs / sizeof prefs[0];
                *game_preferences = (int32_t)(*game_net_multiplayer ? (uint32_t)pref : prefs[i]);
                for (int f = 0; f < FACTION_COUNT; f++) {
                    uint32_t *d = &game_factions[V->faction].diplo_status[f], kept = *d;
                    static const uint32_t treaties[] = {TREATY_PACT, TREATY_TREATY, TREATY_TRUCE, 0};
                    *d = (kept & ~(uint32_t)(TREATY_PACT | TREATY_TREATY | TREATY_TRUCE)) | treaties[(f + k + i) % 4];
                    check_want_to_wake(f, v, (v + f * 11 + 1) % vehs);
                    *d = kept;
                    check_want_to_wake(f, v, (v + f * 11 + 1) % vehs);
                }
                check_want_to_wake(V->faction, v, -1);
            }
            *game_preferences = pref, *game_net_multiplayer = net;
            check_want_to_wake((k + t) % FACTION_COUNT, v, v);
        }
        *V = saved;
    }
    for (int k = 0; k < 8 && vehs; k++) {
        int v = (k * 23 + t) % vehs;
        if (stack_sound(v)) check_wake_stack(v);
    }
    check_wake_stack(-1);
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
        check_armor_val(p, f);
        check_armor_val(p, -1);
        check_wants_prototype(p, f);
        check_wants_prototype(p, p / BASIC_PROTOS % FACTION_COUNT);
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
            check_drop_range(f);
            for (int plan = 0; plan < 16; plan++) check_get_plan(f, plan);
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
        if (!stack_sound(v)) continue;
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
        check_planet_buster(v);
        check_veh_top(v);
        check_veh_moves(v);
        check_proto_power(v);
        check_veh_health(v);
        check_want_monolith(v);
    }
    check_veh_top(-1);
    check_morale_alien(-1, 1);
    for (int c = 0; c < CHASSIS_COUNT; c++)
        for (int r = 0; r < 5; r++) {
            static const uint32_t abls[] = {0, ABL_SLOW, ABL_HEAVY_TRANSPORT, ABL_SLOW | ABL_HEAVY_TRANSPORT, 0xffffffffu};
            for (int a = 0; a < 5; a++) check_transport_val(c, abls[a], r * 3 - 1);
        }
    for (int i = 0; i < 32; i++) {
        check_abil_index(1u << i);
        check_abil_index((1u << i) | 0x80000000u | (uint32_t)turn << 16);
    }
    for (int v = ((turn + 2) & 3); v < vehs; v += 4)
        if (stack_sound(v)) sweep_battle(v, (v * 5 + turn) % vehs, v * 3 + turn);
    sweep_battle_report();
    sweep_wake();
    sweep_varied();
    sweep_stacks();
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
    HOOK(0x5bfe90, veh_at, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x55, 0x0c),                   // mov edx, [ebp+12]
    HOOK(0x5bffa0, veh_lift, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x57),                       // push esi; push edi
    HOOK(0x5c0080, veh_drop, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x0c),           // push ebx; mov ebx, [ebp+12]
    HOOK(0x5c01a0, veh_sleep, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                // mov eax, [ebp+8]
    HOOK(0x5c01d0, veh_demote, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x08),         // push ebx; mov ebx, [ebp+8]
    HOOK(0x5c0260, veh_promote, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x8b, 0x75, 0x08),        // push esi; mov esi, [ebp+8]
    HOOK(0x5c02d0, veh_clear, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                // mov eax, [ebp+8]
    HOOK(0x5b8af0, stack_put, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                // mov eax, [ebp+8]
    HOOK(0x5b8b60, stack_sort, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x53),                     // push ecx; push ebx
    HOOK(0x5b8c90, stack_sort_2, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x45, 0x08),       // push ecx; mov eax, [ebp+8]
    HOOK(0x5b8e10, stack_fix, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x53),                      // push ecx; push ebx
    HOOK(0x5c1540, veh_speed, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56),                      // push ebx; push esi
    HOOK(0x5c1d20, veh_skip, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x8b, 0x75, 0x08),           // push esi; mov esi, [ebp+8]
    HOOK(0x5c1150, offense_proto, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56),                 // push ebx; push esi
    HOOK(0x5c1290, armor_proto, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x55, 0x08),              // mov edx, [ebp+8]
    HOOK(0x5c17d0, prototype_factor, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x4d, 0x08),         // mov ecx, [ebp+8]
    HOOK(0x5c0cb0, veh_find, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),                 // sub esp, 12
    HOOK(0x5c1a20, veh_selectable, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x4d, 0x08),           // mov ecx, [ebp+8]
    HOOK(0x5c1ab0, veh_unmoved, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x57),                    // push esi; push edi
    HOOK(0x5c1b70, veh_ready, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x57),                      // push esi; push edi
    HOOK(0x5c1c40, veh_jail, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                 // mov eax, [ebp+8]
    HOOK(0x5c1d50, veh_fake, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x0c),                 // mov eax, [ebp+12]
    HOOK(0x5c1d70, veh_wake, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56),                       // push ebx; push esi
    HOOK(0x5ba910, veh_avail, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56, 0x57),                // push ebx; push esi; push edi
    HOOK(0x500320, drop_range, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0xc8, 0x97, 0x94, 0x00),   // mov eax, [tech_orbital_insert]
    HOOK(0x500520, planet_buster, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),            // mov eax, [ebp+8]
    HOOK(0x579920, veh_top, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                  // mov eax, [ebp+8]
    HOOK(0x579960, veh_moves, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x8b, 0x75, 0x08),          // push esi; mov esi, [ebp+8]
    HOOK(0x5799a0, proto_power, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),              // mov eax, [ebp+8]
    HOOK(0x57d480, armor_val, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                // mov eax, [ebp+8]
    HOOK(0x57d510, transport_val, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),            // mov eax, [ebp+8]
    HOOK(0x581170, abil_index, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x4d, 0x08),               // mov ecx, [ebp+8]
    HOOK(0x5a59b0, veh_put, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x10),                  // mov eax, [ebp+16]
    HOOK(0x5a59e0, veh_health, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),               // mov eax, [ebp+8]
    HOOK(0x5aed50, get_plan, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56, 0x57),                 // push ebx; push esi; push edi
    HOOK(0x579f80, want_monolith, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),            // mov eax, [ebp+8]
    HOOK(0x5b5ea0, want_to_wake, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x57),                   // push esi; push edi
    HOOK(0x5b6060, wake_stack, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),               // mov eax, [ebp+8]
    HOOK(0x5be100, wants_prototype, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),          // sub esp, 12
    HOOK(0x501d30, battle_init, 0x33, 0xc0, 0xa3, 0x14, 0x56, 0x91, 0x00),             // xor eax, eax; mov [...], eax
    HOOK(0x501d50, add_bat, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x55, 0x0c),                 // mov edx, [ebp+12]
    HOOK(0x5010c0, defense_value, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0xf0, 0xfa, 0x68, 0x00),  // mov eax, [map_half_width]
    HOOK(0x5015b0, get_basic_offense, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),        // sub esp, 12
    HOOK(0x501940, get_basic_defense, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),        // sub esp, 8
    HOOK(0x501da0, battle_compute, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x2c),          // sub esp, 44
    HOOK(0x5044d0, best_defender, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x2c),           // sub esp, 44
    HOOK(0x593510, hex_cost, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x8b, 0x0d, 0xf0, 0xfa, 0x68, 0x00),  // push ecx; mov ecx, [...]
    HOOK(0x5c03d0, veh_init, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),                 // sub esp, 8
    HOOK(0x5b8ee0, stack_veh, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x28),                // sub esp, 40
    HOOK(0x5c08c0, veh_kill, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),                 // sub esp, 8
    HOOK(0x5b9510, stack_kill, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x8b, 0x75, 0x08),         // push esi; mov esi, [ebp+8]
    HOOK(0x5c0b00, kill, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),                     // sub esp, 16
};
struct hook_module veh_module = {"veh", veh_hooks, HOOK_COUNT(veh_hooks), veh_sweep, NULL, -1};
