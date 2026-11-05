// Bases: the rest of the end of a base's turn. Reimplemented from
// terranx.exe 2.0's disassembly; names follow OpenSMACX (Brendan Casey,
// GPL-3.0-or-later) and Thinker (MIT).
#include "base.h"
#include "game.h"
#include "shadow.h"
#include "tech.h"

static inline struct map_tile *tile(int x, int y) { return &(*game_map)[(x >> 1) + y * *game_map_half_width]; }
static inline int has_facility(int base, int facility) { return game_has_facility(&game_bases[base], facility); }
static inline int holds_project(int project, int faction) {
    int b = game_secret_projects[project];
    return b >= 0 && game_bases[b].faction == faction;
}
static inline int is_alien(int faction) { return (game_faction_defs[faction].rule_flags & RULE_FLAG_ALIEN) != 0; }
#define B (*game_current_base)
#define ID (*game_current_base_id)

enum {
    FAC_COMMAND_CENTER = 27,
    SP_SELF_AWARE = 25, SP_TELEPATHIC_MATRIX = 30,
    BASE_STATE_RIOTS = 0x2, GOLDEN = BASE_STATE_GOLDEN_AGE,
    RULES_NO_TECH_ADVANCES = 0x4000000,
};

static inline char *text(uint32_t addr) { return (char *)game_at(addr); }

// A base in energy deficit: its energy convoys sitting in bases stop.
void base_energy_costs(void) {
    if (B->surplus[RESOURCE_ENERGY] >= 0) return;
    int faction = B->faction;
    for (int v = 0; v < *game_veh_count; v++) {
        struct veh *u = &game_vehs[v];
        if (u->faction != faction || game_protos[u->proto].plan != PLAN_SUPPLY || u->order != ORDER_CONVOY ||
            u->order_auto_type != RESOURCE_ENERGY)
            continue;
        const struct map_tile *t = tile(u->x, u->y);
        if ((t->bits & MAP_BASE) && (t->owner_site & 0xf) < 8) u->order = 0;
    }
}

// The base's labs go to its faction's research: halved by a punishment
// sphere, none in the first turns under a poor RESEARCH (or at the lowest
// difficulties), then scaled by RESEARCH in tenths, the hundredths carried
// over from turn to turn.
void base_research(void) {
    if (*game_rules & RULES_NO_TECH_ADVANCES) return;
    int faction = B->faction, labs = B->labs_total;
    if (has_facility(ID, FAC_PUNISHMENT_SPHERE)) labs /= 2;
    struct faction_data *f = &game_factions[faction];
    int research = f->se_pending[SE_RESEARCH], turn = *game_current_turn;
    if (turn < -5 * research) labs = 0;
    if (turn < 5 && f->diff_level < 2) labs = 0;
    f->labs_total += labs;
    int scaled = (game_range(research, -5, 5) + 10) * labs * 10;
    f->net_random_event += scaled % 100;
    if (f->net_random_event >= 100) {
        f->net_random_event -= 100;
        scaled += 100;
    }
    game_tech_research(faction, scaled / 100);
}

// A facility's upkeep for the faction: none for one it gets free, and for
// a command center its best reactor, at most half its difficulty (rounded
// up).
int fac_maint(int facility, int faction) {
    if (facility == FAC_COMMAND_CENTER) {
        int half = (game_factions[faction].diff_level + 1) / 2, reactor = game_best_reactor(faction);
        if (reactor < 0 || half < 0) return 0;
        return reactor > half ? half : reactor;
    }
    const struct faction_def *d = &game_faction_defs[faction];
    for (int i = 0; i < d->bonus_count; i++) {
        int id = d->bonus_id[i];
        if (id == BONUS_FACILITY ||
            (id == BONUS_FREEFAC && has_tech(game_facilities[d->bonus_val1[i]].preq_tech, faction))) {
            if (d->bonus_val1[i] == facility) return 0;
        }
    }
    return game_facilities[facility].maint;
}

// The base's facilities' upkeep from the faction's energy reserves, half
// with the Self-Aware Colony (alternate odd units are let off). A faction
// that can't pay (above the two lowest difficulties) sells the facility,
// unless the base is building it, and gets its price back as reserves.
void base_maint(void) {
    int faction = B->faction;
    struct faction_data *f = &game_factions[faction];
    for (int fac = 1; fac < FAC_STOCKPILE_ENERGY + 1; fac++) {
        if (fac >= FAC_SKY_HYDRO_LAB || !has_facility(ID, fac)) continue;
        int maint = fac_maint(fac, faction);
        if (holds_project(SP_SELF_AWARE, faction)) {
            int sum = maint + (int)(f->flags >> 5 & 1);
            if (sum & 1) f->flags |= FACTION_FLAG_MAINT_HALF;
            else f->flags &= ~(uint32_t)FACTION_FLAG_MAINT_HALF;
            maint = sum / 2;
        }
        f->energy_credits -= maint;
        f->facility_maint_total += maint;
        if (f->energy_credits >= 0) continue;
        if (f->diff_level <= 1 || B->queue[0] == -fac) {
            f->energy_credits = 0;
            continue;
        }
        B->facilities_built[fac / 8] &= (uint8_t)~(1 << (fac % 8));
        f->energy_credits = cost_factor(B->faction, 1, -1) * game_facilities[fac].cost;
        game_parse_say(1, (int)(intptr_t)game_facilities[fac].name, -1, -1);
        game_popb(text(0x689db0), 0x10000, 14, text(0x689d9c), 0);
    }
}

// Golden ages, starting (no drones, at least size 3, half talents) and
// ending; riots while drones outnumber talents (unless the faction has the
// Telepathic Matrix or the base is nerve stapled), and their end: the AI
// then chooses what to build again.
void base_drones(void) {
    struct base *b = B;
    int alien = is_alien(b->faction);
    if (!b->drone_total && b->pop_size > 2 && b->talent_total >= (b->pop_size + 1) / 2) {
        if (!(b->state & GOLDEN))
            game_popb(text(alien ? 0x689c84 : 0x689ca0), 0x20, -1, text(alien ? 0x689c78 : 0x689c90), 0);
        B->state |= GOLDEN;
    } else if (b->state & GOLDEN) {
        b->state &= ~(uint32_t)GOLDEN;
        if (B->drone_total <= B->talent_total)
            game_popb(text(alien ? 0x689cb8 : 0x689cd8), 0x40, -1, text(alien ? 0x689cac : 0x689cc8), 0);
    }
    b = B;
    if (b->drone_total > b->talent_total && !holds_project(SP_TELEPATHIC_MATRIX, b->faction) && !b->nerve_staple_turns) {
        game_drone_riot();
        return;
    }
    if (!(b->state & BASE_STATE_RIOTS)) return;
    b->state &= ~(uint32_t)BASE_STATE_RIOTS;
    b = B;
    if (!(b->state & GOLDEN)) {
        int manage = (b->governor & GOVERNOR_CITIZENS) && (b->governor & GOVERNOR_ACTIVE);
        if (is_alien(b->faction))
            game_popb(text(manage ? 0x689cf4 : 0x689d10), manage ? 0 : 0x10, 0x33, text(manage ? 0x689ce8 : 0x689d04), 0);
        else
            game_popb(text(manage ? 0x689d30 : 0x689d50), manage ? 0 : 0x10, 0x33, text(manage ? 0x689d20 : 0x689d40), 0);
        b = B;
    }
    if (!((*game_human_factions >> b->faction) & 1)) {
        game_base_reset(ID, 0);
        b = B;
    }
    game_draw_tile(b->x, b->y, 2);
}

// ---- Hooks and shadow checks ------------------------------------------------

extern struct hook_module upkeep_module;
#define UPKEEP (&upkeep_module)
#define WRITES_BASE WRITES(B, sizeof(struct base)); WRITES(&game_factions[B->faction], sizeof(struct faction_data))
SHADOW_WRITER(UPKEEP, void, base_energy_costs, (void), (), WRITES(game_vehs, *game_veh_count * sizeof(struct veh)), "%s", "")
SHADOW_WRITER(UPKEEP, void, base_research, (void), (), WRITES_BASE, "%s", "")
SHADOW2(UPKEEP, int, fac_maint, int, int)
SHADOW_WRITER(UPKEEP, void, base_maint, (void), (), WRITES_BASE, "%s", "")
// Riots, and the AI choosing again, write widely.
#define WRITES_DRONES                                                                          \
    if (B->drone_total > B->talent_total || (B->state & BASE_STATE_RIOTS)) hooks_writes_game(&w_); \
    else WRITES(B, sizeof(struct base))
SHADOW_WRITER(UPKEEP, void, base_drones, (void), (), WRITES_DRONES, "%s", "")

static int slice;

// Each base as the current one, as it is and made up: an energy deficit,
// labs, maintenance it can't pay, and drones and talents.
static void upkeep_sweep(void) {
    slice++;
    if (!*game_map || *game_base_count <= 0) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    struct base *current = B;
    int current_id = ID;
    for (int f = 0; f < FACTION_COUNT; f++)
        for (int fac = 0; fac <= FAC_STOCKPILE_ENERGY; fac++) check_fac_maint(fac, f);
    for (int b = 0; b < *game_base_count; b++) {
        struct base *base = &game_bases[b], saved = *base;
        struct faction_data *f = &game_factions[base->faction], saved_f = *f;
        B = base;
        ID = b;
        check_base_energy_costs();
        check_base_research();
        check_base_maint();
        base->surplus[RESOURCE_ENERGY] = -1 - (b + slice) % 5;
        check_base_energy_costs();
        base->labs_total = (b * 7 + slice * 3) % 60;
        f->net_random_event = (b + slice * 11) % 100;
        check_base_research();
        f->energy_credits = (b + slice) % 3 - 1;
        check_base_maint();
        // Drones and talents, but no riots: drone_riot is still the game's
        // and writes widely; riots are checked as the game has them.
        base->drone_total = (b + slice) % 2 ? 0 : base->talent_total;
        base->talent_total = (b + slice) % 3 ? base->pop_size / 2 + 1 : 0;
        if (base->drone_total > base->talent_total) base->drone_total = base->talent_total;
        base->state ^= (uint32_t)((b + slice) % 4 == 0) * GOLDEN;
        check_base_drones();
        *base = saved;
        *f = saved_f;
    }
    B = current;
    ID = current_id;
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook upkeep_hooks[] = {
    HOOK(0x4f4dc0, base_energy_costs, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),     // sub esp, 8
    HOOK(0x4f4e80, base_research, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),
    HOOK(0x4f6510, fac_maint, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x55, 0x08),             // mov edx, [ebp+8]
    HOOK(0x4f65f0, base_maint, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x20),            // sub esp, 32
    HOOK(0x4f5ce0, base_drones, 0x8b, 0x0d, 0x30, 0xea, 0x90, 0x00),               // mov ecx, [0x90ea30]
};
struct hook_module upkeep_module = {"upkeep", upkeep_hooks, HOOK_COUNT(upkeep_hooks), upkeep_sweep, NULL, -1};
