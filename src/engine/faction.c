// Factions: probes and the energy market. Reimplemented from terranx.exe
// 2.0's disassembly; names follow OpenSMACX (Brendan Casey,
// GPL-3.0-or-later) and Thinker (MIT).
#include <stdlib.h>

#include "base.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "shadow.h"

enum {
    TREATY_ATROCITY_VICTIM = 0x40000, TREATY_SURRENDERED = 0x2000000,
    STACK_PLAN = 2, STACK_ABILITY = 6, ABL_POLY_ENCRYPTION = 0x1000, BASE_STATE_RIOTS_ = 0x2,
};

static inline int is_human(int faction) { return (*game_human_factions >> faction) & 1; }

// How hard the base is to reach from its owner's headquarters: the
// distance to the first base with one, 12 with none. OpenSMACX calls this
// vulnerable(); the original has it inline.
static int vulnerability(const struct base *b) {
    for (int i = 0; i < *game_base_count; i++) {
        const struct base *hq = &game_bases[i];
        if (hq->faction != b->faction || !has_fac(FAC_HEADQUARTERS, i, 0)) continue;
        int dx = abs(b->x - hq->x);
        if (!(*game_map_flat & 1) && dx > *game_map_half_width) dx = *game_map_width - dx;
        return vector_dist_offset(dx, abs(b->y - hq->y));
    }
    return 12;
}

// The cost grows with the defenders on the base's square (more with Polymorphic
// Encryption), the probing faction's past mind control, the base's size and
// its owner's reserves, and falls with its distance from headquarters. A
// Genejack Factory doubles it, and a Children's Creche, a Punishment Sphere
// or nerve stapling halves it. Against a human, an AI pays less on the
// harder levels. For the market, a pact or treaty with the owner halves it
// and it is scaled by the two factions' commerce; for a probe, a pact
// doubles it. A former owner, riots and the owner's grudges change it too.
int mind_control(int base, int faction, int market) {
    const struct base *b = &game_bases[base];
    int owner = b->faction;
    int dist = vulnerability(b);
    if (dist <= 0) {
        if (!market) return -1;
        dist = 1;
    }
    if (game_has_facility(b, FAC_GENEJACK_FACTORY)) dist *= 2;
    if (game_has_facility(b, FAC_CHILDREN_CRECHE)) dist /= 2;
    if (game_has_facility(b, FAC_PUNISHMENT_SPHERE)) dist /= 2;
    if (b->nerve_staple_turns) dist /= 2;
    int v = game_stack_fix(game_veh_at(b->x, b->y));
    int defense = game_stack_check(v, STACK_PLAN, PLAN_COMBAT, -1, -1) + game_stack_check(v, STACK_PLAN, PLAN_OFFENSIVE, -1, -1);
    defense *= game_stack_check(v, STACK_ABILITY, ABL_POLY_ENCRYPTION, -1, -1) + 1;
    const struct faction_data *f = &game_factions[faction], *o = &game_factions[owner];
    int cost = defense + f->mind_control_total / 4 + b->pop_size;
    cost *= (o->corner_market_cost + o->energy_credits + 1200) / (dist + 4);
    if (!is_human(faction) && is_human(owner) && o->diff_level > 3) cost = cost * 3 / o->diff_level;
    uint32_t ties = f->diplo_status[owner];
    if (market) {
        if (ties & TREATY_PACT) cost /= 2;
        if (ties & TREATY_TREATY) cost /= 2;
        cost = cost * (o->tech_commerce_bonus * o->tech_commerce_bonus + 1) /
               (f->tech_commerce_bonus * f->tech_commerce_bonus + 1);
    } else if (ties & TREATY_PACT) {
        cost *= 2;
    }
    if (b->faction_former == faction) cost /= 2;
    if (b->state & BASE_STATE_RIOTS_) cost /= 2;
    if (b->state & BASE_STATE_GOLDEN_AGE) cost *= 2;
    uint32_t grudge = o->diplo_status[faction];
    if (grudge & TREATY_ATROCITY_VICTIM) return cost * 2;
    if (grudge & TREATY_WANT_REVENGE) cost += cost / 2;
    return cost;
}

int corner_market(int faction) {
    int cost = 0;
    for (int i = 0; i < *game_base_count; i++) {
        int owner = game_bases[i].faction;
        if (owner == faction) continue;
        uint32_t ties = game_factions[owner].diplo_status[faction];
        if ((ties & TREATY_PACT) && (ties & TREATY_SURRENDERED)) continue;
        cost += mind_control(i, faction, 1);
    }
    return cost < 1000 ? 1000 : cost;
}

// ---- Hooks and shadow checks ------------------------------------------------

// stack_fix(), called by mind_control(), changes the stack only in a
// network game, so both are checked as reading only.
extern struct hook_module faction_module;
#define FACTION (&faction_module)
SHADOW3(FACTION, int, mind_control, int, int, int)
SHADOW1(FACTION, int, corner_market, int)

// Every base against every faction, for a probe and the market; and each
// faction's market cost.
static void faction_sweep(void) {
    if (!*game_map || *game_base_count <= 0) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    for (int f = 0; f < FACTION_COUNT; f++) {
        for (int b = 0; b < *game_base_count; b++) {
            check_mind_control(b, f, 0);
            check_mind_control(b, f, 1);
        }
        check_corner_market(f);
    }
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook faction_hooks[] = {
    HOOK(0x59ea80, mind_control, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),  // sub esp, 12
    HOOK(0x59ee50, corner_market, PUSH_EBP_MOV_EBP_ESP, 0x51, 0xa1, 0xcc, 0x64, 0x9a, 0x00),  // push ecx; mov eax, [0x9a64cc]
};
struct hook_module faction_module = {"faction", faction_hooks, HOOK_COUNT(faction_hooks), faction_sweep, NULL, -1};
