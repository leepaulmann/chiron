// Bases: the rest of the end of a base's turn. Reimplemented from
// terranx.exe 2.0's disassembly; names follow OpenSMACX (Brendan Casey,
// GPL-3.0-or-later) and Thinker (MIT).
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "mapgen.h"
#include "shadow.h"
#include "social.h"
#include "tech.h"

static inline struct map_tile *tile(int x, int y) { return &(*game_map)[(x >> 1) + y * *game_map_half_width]; }
static inline int has_facility(int base, int facility) { return game_has_facility(&game_bases[base], facility); }
static inline int holds_project(int project, int faction) {
    int b = game_secret_projects[project];
    return b >= 0 && game_bases[b].faction == faction;
}
static inline int is_alien(int faction) { return (game_faction_defs[faction].rule_flags & RULE_FLAG_ALIEN) != 0; }
static inline int is_human(int faction) { return (*game_human_factions >> faction) & 1; }
#define B (*game_current_base)
#define ID (*game_current_base_id)

enum {
    FAC_COMMAND_CENTER = 27,
    SP_SELF_AWARE = 25, SP_TELEPATHIC_MATRIX = 30,
    BASE_STATE_RIOTS = 0x2, GOLDEN = BASE_STATE_GOLDEN_AGE, BASE_STATE_COMBAT_LOSS_ = 0x8, PLAN_COLONY_ = 8,
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
//
// A riot goes on to drone_riot() as the original does, with a jump from
// where the game called (base_drones below), so it finds the stack as the
// original's would: base_drones_in() returns 1 for that.
int base_drones_in(void);
int base_drones_in(void) {
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
    if (b->drone_total > b->talent_total && !holds_project(SP_TELEPATHIC_MATRIX, b->faction) && !b->nerve_staple_turns)
        return 1;
    if (!(b->state & BASE_STATE_RIOTS)) return 0;
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
    return 0;
}

// The bytes below the entry are left alone (base_drones_in runs under them)
// for drone_riot.
__asm__(".text\n"
        ".globl base_drones\n"
        "base_drones:\n"
        "    subl $0x100, %esp\n"
        "    call base_drones_in\n"
        "    addl $0x100, %esp\n"
        "    testl %eax, %eax\n"
        "    jnz drone_riot\n"
        "    ret\n");

// ---- Riots --------------------------------------------------------------------

// Whether the faction gets the facility free at each base, as fac_maint()
// decides, but with `tech_faction`'s techs.
static int free_facility(int faction, int facility, int tech_faction) {
    const struct faction_def *d = &game_faction_defs[faction];
    for (int i = 0; i < d->bonus_count; i++) {
        int id = d->bonus_id[i];
        if ((id == BONUS_FACILITY ||
             (id == BONUS_FREEFAC && has_tech(game_facilities[d->bonus_val1[i]].preq_tech, tech_faction))) &&
            d->bonus_val1[i] == facility)
            return 1;
    }
    return 0;
}

// The faction's free facilities, at the current base.
static void add_free_facilities(int faction, int tech_faction) {
    const struct faction_def *d = &game_faction_defs[faction];
    for (int i = 0; i < d->bonus_count; i++) {
        int id = d->bonus_id[i];
        if (id != BONUS_FACILITY &&
            (id != BONUS_FREEFAC || !has_tech(game_facilities[d->bonus_val1[i]].preq_tech, tech_faction)))
            continue;
        int fac = d->bonus_val1[i];
        fac = fac < 1 ? 1 : fac > FAC_STOCKPILE_ENERGY ? FAC_STOCKPILE_ENERGY : fac;
        B->facilities_built[fac / 8] |= (uint8_t)(1 << (fac % 8));
    }
}

// Which faction a revolting base joins: the one that founded it, or else
// the nearest other by its bases (a pact makes one twice as far, a treaty
// half as far again, a truce half as far; the original weighs each base by
// its faction's standing with the nearest found so far, and with none
// found reads the word before the treaties). Then, with a chance that
// grows with each faction's REVOLT, one of those picked by its REVOLT.
//
// `weights` holds the chances, where the original keeps them: it sets those
// of factions with the bonus and reads the rest as the stack has them,
// skipping -1. With none set the pick divides by zero, as the original does.
static int revolt_target(int faction, int32_t *weights) {
    int target;
    if (B->faction_former != faction) {
        target = B->faction_former;
    } else {
        target = -1;
        int best = 9999;
        const uint8_t *status = (const uint8_t *)&game_factions[faction] + offsetof(struct faction_data, diplo_status);
        for (int i = 0; i < *game_base_count; i++) {
            const struct base *o = &game_bases[i];
            if (o->faction == faction) continue;
            int d = vector_dist_offset(x_dist(o->x, B->x), o->y - B->y);
            uint32_t treaty = *(const uint32_t *)(status + 4 * target);
            if (treaty & TREATY_PACT) d *= 2;
            if (treaty & TREATY_TREATY) d = d * 3 / 2;
            if (treaty & 0x4) d /= 2;  // truce
            if (d < best) best = d, target = o->faction;
        }
    }
    long double chance = 1.0L;
    int total = 0;
    for (int i = 1; i < FACTION_COUNT; i++) {
        const struct faction_def *d = &game_faction_defs[i];
        for (int j = 0; j < d->bonus_count; j++) {
            if (d->bonus_id[j] != BONUS_REVOLT) continue;
            int v = d->bonus_val1[j];
            v = v < 0 ? 0 : v > 100 ? 100 : v;
            weights[i - 1] = v;
            total += v;
            break;
        }
    }
    // In the x87's precision, as the original.
    for (int i = 0; i < FACTION_COUNT - 1; i++)
        if (weights[i] != -1) chance *= 1.0L - (long double)weights[i] * 0.01;
    volatile double kept = (double)chance;
    int32_t now = (int32_t)game_time_get_time();
    if ((int)((long double)kept * 100.0) >= now % 100) {
        int pick = now / 100 % total;
        for (int i = 1; i < FACTION_COUNT; i++) {
            int w = weights[i - 1];
            if (w == -1) continue;
            target = i;
            if (pick < w) break;
            pick -= w;
        }
    }
    return target;
}

// The current base goes over to another faction, with the units of its old
// owner beside it (and those it supports elsewhere disbanded). The player
// is told if it concerns them or they can see it.
static void defect(int faction, int target) {
    int x = B->x, y = B->y;
    B->faction = (uint8_t)target;
    for (int i = 1; i < FACTION_COUNT; i++)
        if (game_factions[i].attack_target_base == ID) game_factions[i].attack_target_base = -1;
    B->assimilation_turns = B->faction_former != target ? 50 : 0;
    struct faction_data *old = &game_factions[faction], *now = &game_factions[target];
    now->last_base_turn = *game_current_turn;
    B->minerals_2 = 0;
    B->minerals = 0;
    for (int i = 0; i <= B->queue_size; i++)
        if (B->queue[i] >= 0) old->units_queue[B->queue[i]]--;
    B->queue_size = 0;
    B->production_last = -FAC_STOCKPILE_ENERGY;
    B->queue[0] = -FAC_STOCKPILE_ENERGY;
    old->base_count--;
    now->base_count++;
    game_replay_base(1, x, y, target);
    // It takes the governor of its old owner's biggest base, a capital
    // counting twice.
    int best = 0, which = -1;
    for (int i = 0; i < *game_base_count; i++) {
        if (i == ID || game_bases[i].faction != faction) continue;
        int size = game_bases[i].pop_size * (has_facility(i, FAC_HEADQUARTERS) ? 2 : 1);
        if (size > best) best = size, which = i;
    }
    if (which >= 0) B->governor = game_bases[which].governor;
    B->state &= 0xffdef7b1;
    for (int k = 0; k < BASE_RADIUS_SQUARES; k++) {
        int sx = xrange(game_radius_x[k] + x), sy = game_radius_y[k] + y;
        if (!on_map(sx, sy)) continue;
        using_set(sx, sy, target);
        tile(sx, sy)->visibility |= (uint8_t)(1 << target);
        for (int v = *game_veh_count - 1; v >= 0; v--) {
            struct veh *u = &game_vehs[v];
            if (u->x != sx || u->y != sy) continue;
            if (u->faction == target) {
                u->state &= ~0x40000u;
                continue;
            }
            if (u->faction != faction || k >= 9) continue;  // the old owner's, on the base or next to it
            u->faction = (uint8_t)target;
            old->units_active[u->proto]--;
            if (u->proto > 64) {
                struct unit_proto *p = &game_protos[u->proto];
                int n = game_propose_proto(target, p->chassis, p->weapon, p->armor, p->abilities, p->reactor, -2,
                                           p->name);
                if (n >= 0) u->proto = (int16_t)n;
            }
            u->home_base = (int16_t)ID;
            now->units_active[u->proto]++;
            owner_set(sx, sy, target);
            if (game_chassis[game_protos[u->proto].chassis].range) u->movement_turns = 0;
            if (u->order != 2) u->order = 0;
            u->state &= 0xfcffbdffu;
        }
    }
    game_spot_base(ID, faction);
    game_reset_territory();
    for (int v = *game_veh_count - 1; v >= 0; v--)
        if (v < *game_veh_count && game_vehs[v].faction == faction && game_vehs[v].home_base == ID) game_kill(v);
    // Free facilities of both: the new owner's with the old owner's techs.
    add_free_facilities(faction, faction);
    add_free_facilities(target, faction);
    owner_set(x, y, target);
    *game_computed_base_id = ID;
    base_support();
    base_yield();
    base_nutrient();
    base_minerals();
    base_energy();
    int player = *game_current_faction;
    if (faction != player && target != player && B->faction != player && !((B->visibility >> player) & 1)) return;
    game_map_win_set_center(game_map_wins[0], x, y, 1);
    game_draw_map(1);
    game_parse_says(0, B->name, -1, -1);
    game_parse_says(2, game_faction_defs[target].adjective, -1, -1);
    if (!(*game_state & GAME_STATE_DONE) || (*game_state & GAME_STATE_FINAL_SCORE_DONE))
        game_popp(*game_script_file, text(0x689c6c), 0, text(0x689c5c), 0);
}

// A riot. Its first turn warns (one under a governor placing citizens says
// so), and the AI chooses what to build again. Later, under a poor POLICE
// (a brood pit counting two), sometimes, a base that isn't the capital, of
// a faction with others, not long assimilated: under size 4 only a third
// of the time. It loses a random facility, not a free one; the bigger the
// base and the lower its faction's ranking and difficulty, the more tries.
// With none lost (or the first try finding one when there are no tries to
// spare), a human's base under POLICE -3 or worse defects.
//
// `weights` is where the original keeps the revolt chances (revolt_target),
// at [ebp-0x58]; drone_riot below gives this the original's frame.
void drone_riot_in(int32_t *weights);
void drone_riot_in(int32_t *weights) {
    int faction = B->faction, start = !(B->state & BASE_STATE_RIOTS);
    B->state |= BASE_STATE_RIOTS;
    int alien = is_alien(faction);
    if (start) {
        game_draw_tile(B->x, B->y, 2);
        if ((B->governor & GOVERNOR_CITIZENS) && (B->governor & GOVERNOR_ACTIVE))
            game_popb(text(alien ? 0x689bac : 0x689bc8), 0, 0x11, text(alien ? 0x689b9c : 0x689bb8), 0);
        else
            game_popb(text(alien ? 0x689be4 : 0x689c00), 8, 0x11, text(alien ? 0x689bd4 : 0x689bf0), 0);
    } else {
        game_popb(text(alien ? 0x689be4 : 0x689c00), 8, 0x11, text(alien ? 0x689bd4 : 0x689bf0), 0);
    }
    if (!(B->state & BASE_STATE_RIOTS)) return;
    if (start) {
        if (!is_human(faction)) game_base_reset(ID, 0);
        return;
    }
    struct faction_data *f = &game_factions[faction];
    int police = f->se_pending[SE_POLICE] + 2 * has_facility(ID, FAC_BROOD_PIT);
    if (police + 6 > 0 && game_rand() % (police + 7)) return;
    if (has_facility(ID, FAC_HEADQUARTERS) || f->base_count <= 1 || B->assimilation_turns >= 45) return;
    if (B->pop_size < 4 && game_rand() % 3) return;
    int tries = B->pop_size - f->diff_level - f->ranking + 20;
    for (int n = 0;;) {
        int fac = game_rand() % 70, spare = !free_facility(faction, fac, faction);
        if (fac > FAC_HEADQUARTERS && fac < FAC_SKY_HYDRO_LAB && has_facility(ID, fac) && spare) {
            if (n > tries) break;
            B->facilities_built[fac / 8] &= (uint8_t)~(1 << (fac % 8));
            game_parse_say(1, (int)(intptr_t)game_facilities[fac].name, -1, -1);
            game_popb(text(alien ? 0x689c34 : 0x689c50), 8, -1, text(alien ? 0x689c24 : 0x689c40), 0);
            return;
        }
        if (++n > tries) break;
    }
    if (f->se_pending[SE_POLICE] + 2 * has_facility(ID, FAC_BROOD_PIT) > -3 || alien || !is_human(faction)) return;
    int target = revolt_target(faction, weights);
    if (target <= 0 || (!is_human(faction) && !is_human(target)) || is_alien(target)) return;
    defect(faction, target);
}

__asm__(".text\n"
        ".globl drone_riot\n"
        "drone_riot:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0x5c, %esp\n"
        "    leal -0x58(%ebp), %eax\n"
        "    pushl %eax\n"
        "    call drone_riot_in\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");

// ---- Doctors, unpaid units and rushing ----------------------------------------

enum { SP_VIRTUAL_WORLD_ = 6, FAC_PERIMETER_DEFENSE = 4, FAC_TACHYON_FIELD = 5, FAC_HURRY_33 = 33 };

// Whether the base will grow this turn, as base_growth() decides it.
static int grows_soon(int faction) {
    int id = ID;
    const struct base *b = &game_bases[id];
    int pop = b->pop_size - (holds_project(SP_ASCETIC_VIRTUES, b->faction) ? 2 : 0) +
              game_faction_defs[b->faction].rule_population;
    if (pop >= game_rules_basic->pop_limit_hab_complex && !has_fac(FAC_HAB_COMPLEX, id, 0)) return 0;
    if (pop >= game_rules_basic->pop_limit_hab_dome && !has_fac(FAC_HABITATION_DOME, id, 0)) return 0;
    int need = game_rules_basic->nutrient_intake_citizen;
    if ((holds_project(SP_CLONING_VATS, faction) || *game_base_growth >= 6) && need &&
        B->surplus[RESOURCE_NUTRIENT] >= need)
        return 1;
    int cost = cost_factor(faction, 0, ID) * (B->pop_size + 1);
    return B->surplus[RESOURCE_NUTRIENT] + B->nutrients >= cost;
}

// The specialist best at calming drones: psych three times over, with
// economy and labs at a size that allows them, of those the faction has
// the tech for.
static int doctor_kind(void) {
    int best = 0, score = -999;
    for (int k = 0; k < CITIZEN_KINDS; k++) {
        const struct citizen *c = &game_citizens[k];
        if (!has_tech(c->preq_tech, B->faction)) continue;
        int v = c->psych * 3;
        if (B->pop_size >= game_rules_basic->min_base_size_specialists) v += c->econ + c->labs;
        if (v > score) score = v, best = k;
    }
    return best;
}

static void add_doctor(int kind) {
    int n = B->specialist_total++;
    if (n >= 16) return;
    int shift = n % 8 * 4;
    B->specialist_types[n / 8] = (B->specialist_types[n / 8] & ~(0xfu << shift)) | (uint32_t)kind << shift;
}

// Riots under a poor POLICE: workers become doctors, up to ten, until the
// drones (and a citizen about to be born) no longer outnumber the talents.
// Otherwise doctors not needed go back to work. Humans only under a
// governor placing citizens. The tutorial points out doctors that energy
// for psych could replace.
void base_doctors(void) {
    int faction = B->faction, grow = 0;
    if (B->state & BASE_STATE_RIOTS) {
        int police = game_factions[faction].se_pending[SE_POLICE] + 2 * has_fac(FAC_BROOD_PIT, ID, 0);
        if (police < -1) grow = grows_soon(faction);
    }
    for (int tries = 1;;) {
        if (is_human(faction) && !((B->governor & GOVERNOR_CITIZENS) && (B->governor & GOVERNOR_ACTIVE))) return;
        if (*game_base_food_ok && B->drone_total + grow > B->talent_total &&
            !holds_project(SP_TELEPATHIC_MATRIX, faction)) {
            add_doctor(doctor_kind());
            base_compute(1);
            if (B->drone_total + grow <= B->talent_total) break;
        } else {
            int doctors = B->specialist_total;
            if (doctors - B->specialist_adjust <= 0 || (B->state & BASE_STATE_RIOTS)) return;
            if (B->drone_total && B->drone_total + grow >= B->talent_total &&
                !holds_project(SP_TELEPATHIC_MATRIX, faction))
                return;
            B->specialist_total = doctors - 1;
            base_compute(1);
            if (B->drone_total + grow > B->talent_total && !holds_project(SP_TELEPATHIC_MATRIX, faction)) {
                add_doctor(doctor_kind());
                return;
            }
            if (doctors <= B->specialist_total) return;
        }
        if (++tries >= 10) return;
    }
    if (!*game_control_upkeep_a || faction != *game_current_faction || game_factions[faction].alloc_psych >= 2 ||
        B->specialist_total - B->specialist_adjust <= 1)
        return;
    int item = B->queue[0];
    if (item == -FAC_RECREATION_COMMONS || item == -FAC_HOLOGRAM_THEATRE || item == -FAC_PARADISE_GARDEN ||
        item == -FAC_RESEARCH_HOSPITAL || item == -FAC_NANOHOSPITAL)
        return;
    if (item == 8 && holds_project(SP_VIRTUAL_WORLD_, faction)) return;  // 8, not -8 (a network node), as written
    int alien = is_alien(faction);
    game_popb(text(alien ? 0x689d6c : 0x689d8c), 0, -1, text(alien ? 0x689d60 : 0x689d7c), 0);
}

// Units the base can't pay for: its mineral convoys sitting in bases stop
// first, then the supported unit furthest from home is disbanded (a human
// is told), as long as upkeep is more than the base's minerals. Then a
// base rioting under POLICE -3 or worse disbands one of its units abroad
// causing drones, far, cheap and defensive ones first (for a human, only
// one on alert and not on its way).
void base_check_support(void) {
    int faction = B->faction;
    while (*game_base_forces_upkeep > B->intake_2[RESOURCE_MINERAL]) {
        int best = -1, which = -1;
        for (int v = 0; v < *game_veh_count; v++) {
            const struct veh *u = &game_vehs[v];
            if (u->faction != B->faction || u->home_base != ID) continue;
            int plan = game_protos[u->proto].plan, score;
            if (plan == PLAN_SUPPLY) {
                if (u->order != ORDER_CONVOY || u->order_auto_type != RESOURCE_MINERAL) continue;
                const struct map_tile *t = tile(u->x, u->y);
                if (!(t->bits & MAP_BASE) || (t->owner_site & 0xf) >= 8) continue;
                score = 99999;
            } else {
                if (!(u->state & VEH_STATE_SUPPORTED) || plan > 9) continue;
                score = vector_dist_offset(x_dist(u->x, B->x), u->y - B->y);
            }
            if (score > best) best = score, which = v;
        }
        if (best < 0) break;
        struct veh *u = &game_vehs[which];
        int plan = game_protos[u->proto].plan;
        if (plan == PLAN_SUPPLY) {
            u->order = 0;
            (*game_base_forces_upkeep)--;
            continue;
        }
        if (is_human(faction)) {
            game_parse_says(1, game_protos[u->proto].name, -1, -1);
            game_popb(text(0x689b7c), 0x8000, 0xd, text(0x689b68), 0);
        } else if (plan == 8) {
            // A colony pod's upkeep comes off twice, as the original has it.
            u->state &= ~(uint32_t)VEH_STATE_SUPPORTED;
            int support = game_factions[faction].se_pending[SE_SUPPORT];
            *game_base_forces_upkeep -= game_has_abil(u->proto, ABILITY_CLEAN_REACTOR) ? 0 : (support <= -4) + 1;
        }
        int support = game_factions[faction].se_pending[SE_SUPPORT];
        if (game_has_abil(u->proto, ABILITY_CLEAN_REACTOR)) break;
        *game_base_forces_upkeep -= (support <= -4) + 1;
        game_kill(which);
    }
    if (is_human(faction) && !((B->governor & GOVERNOR_CITIZENS) && (B->governor & GOVERNOR_ACTIVE))) return;
    if (!(B->state & BASE_STATE_RIOTS)) return;
    struct faction_data *f = &game_factions[faction];
    if (f->se_pending[SE_POLICE] + 2 * has_facility(ID, FAC_BROOD_PIT) > -3) return;
    if (B->drone_total <= B->talent_total && B->surplus[RESOURCE_NUTRIENT] >= 0) return;
    int best = -1, which = -1;
    for (int v = 0; v < *game_veh_count; v++) {
        const struct veh *u = &game_vehs[v];
        if (u->faction != faction || u->home_base != ID) continue;
        uint32_t st = u->state;
        if (!(st & VEH_STATE_PACIFISM_DRONE)) continue;
        if (is_human(faction)) {
            const uint8_t *raw = (const uint8_t *)u;
            if (!(st & 0x200) || (st & 0x1000200) == 0x1000200) continue;
            if ((st & 0x2000200) == 0x2000200 && raw[0x13] < raw[0x12]) continue;
            if (u->order_auto_type) continue;
        }
        int n = f->ai_fight + 2;
        if (n - 1 > 0 && game_rand() % n) continue;
        int dist = (x_dist(u->x, B->x) + abs(u->y - B->y)) >> 1;
        int cost = game_protos[u->proto].cost;
        cost = cost < 1 ? 1 : cost > 99 ? 99 : cost;
        int score = dist * 100 / cost, plan = game_protos[u->proto].plan;
        if (plan == 2) score *= 2;
        if (plan == 1) score /= 2;
        if (score > best) best = score, which = v;
    }
    if (which < 0) return;
    game_kill(which);
    B->state &= ~(uint32_t)BASE_STATE_RIOTS;
}

// The AI (or a human's governor allowed to) spends energy reserves to
// rush what the base is building: more for what its plans call for, for
// threats, projects and defenses, and the richer it is; scaled by the
// faction's rate and how much is left to build, with part given back by
// difficulty and ranking. Then the player hears of an AI project that
// will be done next turn.
void base_hurry(void) {
    int faction = B->faction, human = is_human(faction);
    if (human && (!(B->governor & 0x20) || !(B->governor & GOVERNOR_ACTIVE))) return;
    if (!B->minerals) return;
    struct faction_data *f = &game_factions[faction];
    int item = B->queue[0], fac = item < 0 ? -item : 0, hurry = 0;
    int rows = fac ? game_facilities[fac].cost : game_veh_cost(item, ID, 0);
    int cost = cost_factor(faction, 1, -1) * rows;
    int x = B->x, y = B->y;
    const struct map_tile *t = tile(x, y);
    int region = t->region;
    if (item >= 0) {
        const struct unit_proto *p = &game_protos[item];
        if (p->plan == PLAN_COLONY_ && B->pop_size < 2) return;
        int garrison = (t->bits & MAP_UNIT) && (t->owner_site & 0xf) < 8;
        if (!garrison || (B->state & BASE_STATE_COMBAT_LOSS_) || game_at_goal(faction, 0x10, x, y)) {
            hurry = f->energy_credits / 10;
        } else if (item >= 64 && !(p->flags & PROTO_FLAG_BUILT)) {
            hurry = f->energy_credits / ((f->ai_wealth + 2) * 10);
        } else {
            int plan = f->region_base_plan[region], want = p->plan;
            if (plan == want || (want == 9 && plan == 8) || (want == 1 && plan <= 2)) {
                hurry = f->energy_credits / ((f->ai_wealth + 2) * 25 - f->ai_power * 10);
                if (plan == 8) {
                    hurry = (f->ai_growth + 5) * hurry / 4;
                    if (f->energy_credits > *game_current_turn) hurry *= 2;
                }
            }
        }
        if (game_planet_buster2(item) && !f->planet_busters)
            for (int i = 1; i < FACTION_COUNT; i++) {
                int theirs = game_factions[i].major_atrocities;
                if (i != faction && (f->diplo_status[i] & 0x10) && theirs && f->major_atrocities <= theirs)
                    hurry = f->energy_credits / 10;
            }
    } else if ((B->state & BASE_STATE_RIOTS) && fac) {
        hurry = f->energy_credits / ((f->ai_power + f->ai_wealth + 1) * 5);
        if (B->drone_total > B->talent_total &&
            f->se_pending[SE_POLICE] + 2 * has_facility(ID, FAC_BROOD_PIT) <= -2 && fac < FAC_FIRST_PROJECT) {
            int most = f->energy_credits * 3 / 4, need = (cost - B->minerals) * (human ? 2 : 0);
            hurry = need < 0 || most < 0 ? 0 : need > most ? most : need;
        }
    } else if (fac == FAC_HEADQUARTERS) {
        hurry = f->energy_credits / 10;
    } else if (fac == FAC_PERIMETER_DEFENSE || fac == FAC_TACHYON_FIELD) {
        hurry = f->energy_credits / 50;
        // The original looks up each faction's bases in the region of its
        // own number.
        for (int i = 1; i < FACTION_COUNT; i++)
            if (i != faction && (f->diplo_status[i] & 0x40030) &&
                (game_factions[i].region_total_bases[i] || is_human(i)))
                hurry = f->energy_credits / 10;
    } else if (fac >= FAC_FIRST_PROJECT) {
        hurry = f->energy_credits / 50;
        if (f->energy_credits >= 2000) hurry *= 4;
        if (fac == FAC_ASCENT) hurry *= 4;
    } else if (fac == FAC_HURRY_33) {
        hurry = f->energy_credits / 50;
        if (game_at_climax(faction)) hurry *= 2;
    }
    int credits = f->energy_credits;
    if (credits >= 4800) hurry += credits / 25;
    else if (credits > 3200) hurry += credits / 100;
    else if (credits > 2000) hurry += credits / 250;
    hurry = hurry < 0 || credits < 0 ? 0 : hurry > credits ? credits : hurry;
    int need = cost - B->minerals;
    if (hurry >= 0 && need >= 0 && hurry > need) hurry = need;
    if (hurry > 0 && need >= 0) {
        if (game_secret_projects[SP_VOICE_OF_PLANET] >= 0) {
            hurry /= 2;
        } else {
            const char *name = game_faction_defs[faction].filename;
            if (!strcmp(name, text(0x689b3c))) hurry = hurry * 2 / 3;
            else if (!strcmp(name, text(0x689b44))) hurry = hurry * 3 / 2;
        }
        int rate = game_faction_defs[faction].rule_hurry;
        if (rate != 100 && hurry < f->energy_credits / 10) hurry = hurry * 100 / rate;
        int minerals = B->minerals, left = cost - minerals;
        int weight = left * left / 20 + 2 * left;
        if (B->queue[0] <= -FAC_FIRST_PROJECT) weight *= 2;
        if (game_secret_projects[SP_VOICE_OF_PLANET] >= 0) weight *= 2;
        int added = weight ? left * hurry / weight : 0;
        B->minerals = added * 100 / rate + minerals;
        int back = human ? 0 : *game_difficulty * hurry / ((8 - game_factions[*game_ranked_faction].ranking) * 10);
        f->energy_credits += back - hurry;
    }
    if (human || fac < FAC_FIRST_PROJECT || game_secret_projects[fac - FAC_FIRST_PROJECT] != -1 ||
        *game_sunspot_duration > 0 || (*game_state & GAME_STATE_DONE))
        return;
    int price = cost_factor(B->faction, 1, -1) * game_facilities[fac].cost;
    if (B->surplus[RESOURCE_MINERAL] + B->minerals < price) return;
    game_parse_says(0, game_faction_defs[B->faction].adjective, -1, -1);
    game_parse_say(1, (int)(intptr_t)game_facilities[fac].name, -1, -1);
    game_popp(*game_script_file, text(0x689b5c), 0, text(0x689b4c), 0);
}

// ---- Ecology ------------------------------------------------------------------

enum { UNIT_MIND_WORMS = 8, UNIT_ISLE_OF_THE_DEEP = 9, UNIT_LOCUSTS = 10, UNIT_SPORE_LAUNCHER = 15 };
enum { RULES_NO_NATIVE_LIFE = 0x200000, ALT_SHELF = 0x40, ALT_LAND = 0x60, MAP2_VOLCANO = 0x2, MAP2_DISABLED = 0x80000000 };

static int altitude(const struct map_tile *t) { return t->climate & 0xe0; }
static int is_fungus(const struct map_tile *t) { return (t->bits & MAP_FUNGUS) && altitude(t) >= ALT_SHELF; }
// The faction with a base, a unit or either on the square, or -1.
static int owner_with(const struct map_tile *t, uint32_t bits) {
    int owner = t->owner_site & 0xf;
    return (t->bits & bits) && owner < 8 ? owner : -1;
}
static int bit_count(uint32_t v) {
    int n = 0;
    for (; v; v >>= 1) n += v & 1;
    return n;
}

// The base nearest the square (the last of equals), as the game's
// base_find() has it, noting the distance.
static int nearest_base(int x, int y) {
    int best = 9999, which = -1;
    for (int i = 0; i < *game_base_count; i++) {
        int d = vector_dist_offset(x_dist(x, game_bases[i].x), abs(y - game_bases[i].y));
        if (d <= best) best = d, which = i;
    }
    if (which >= 0) *game_base_find_dist = best;
    return which;
}

// Where ecological damage grows fungus: of the base's squares out to
// radius 3 (beyond its own 24 only one it is nearest, and only while nothing
// nearer was chosen), without fungus or a base, next to fungus, by chance,
// more for more fungus around, improvements it would destroy and the inner
// squares, half at sea.
static int fungus_square(void) {
    int best = 0, which = -1;
    for (int i = 1; i <= 48; i++) {
        int x = xrange(B->x + game_radius_x[i]), y = B->y + game_radius_y[i];
        if (!on_map(x, y)) continue;
        const struct map_tile *t = tile(x, y);
        if (is_fungus(t) || owner_with(t, MAP_BASE) >= 0) continue;
        int score = 0;
        if (i >= 25) {
            if (best || nearest_base(x, y) != ID) continue;
            score = 1;
        }
        for (int j = 0; j < 4; j++) {
            int ax = xrange(x + game_adjacent_x[j]), ay = y + game_adjacent_y[j];
            if (on_map(ax, ay) && is_fungus(tile(ax, ay))) score += 2;
        }
        if (!score) continue;
        if (score > 1) score += bit_count(t->bits & *game_fungus_clears) + (i <= 8);
        if (altitude(t) < ALT_LAND) score = (score + 1) / 2;
        int roll = score * 10 - 1 > 0 ? game_rand() % (score * 10) : 0;
        if (roll > best) best = roll, which = i;
    }
    return which;
}

// Fungus on the square, and on up to eight more around it at sea when
// the damage has gone on long enough (mostly at lower difficulties): as the
// original has it, the first square fungus takes ends the spreading, as
// the search for the improvement it destroys reuses the counter. Whoever
// sees it is shown. Returns whether the player saw it, and the improvement.
static int grow_fungus(int sx, int sy, int *improvement) {
    int faction = B->faction;
    struct faction_data *f = &game_factions[faction];
    int count = 1, seen = 0;
    if (altitude(tile(sx, sy)) < ALT_LAND && f->clean_minerals >= 4) {
        int n = 10 - f->clean_minerals / 8 - f->diff_level;
        if (n - 1 <= 0 || !(game_rand() % n)) count = 9;
    }
    int player = *game_current_faction;
    for (int i = 0; i < count; i++) {
        int x = xrange(sx + game_radius_x[i]), y = sy + game_radius_y[i];
        if (!on_map(x, y)) continue;
        struct map_tile *t = tile(x, y);
        if (i && altitude(t) >= ALT_LAND) continue;
        uint32_t bits = t->bits;
        if (is_fungus(t) || owner_with(t, MAP_BASE) >= 0 || (t->bits2 & (MAP2_DISABLED | MAP2_VOLCANO)) == MAP2_VOLCANO)
            continue;
        if (*improvement < 0) {
            for (i = 0; i < TERRAFORM_COUNT; i++)
                if (bits & game_terraform[i].bit & *game_fungus_clears) *improvement = i;
        }
        bit_set(x, y, MAP_FUNGUS, 1);
        bit_set(x, y, *game_fungus_clears, 0);
        if (altitude(tile(x, y)) < ALT_SHELF) world_alt_set(x, y, ALT_SHELF >> 5, 0);
        player = *game_current_faction;
        if (!(game_factions[player].flags & FACTION_FLAG_MAP_REVEALED) && !((tile(x, y)->visibility >> player) & 1) &&
            !(*game_state & GAME_STATE_OMNISCIENT))
            continue;
        seen = 1;
        if (faction == player) {
            game_console_focus(game_console, x, y, faction);
            if (f->clean_minerals < 2) game_interlude(3, 0, 1, 0);
            game_boom(x, y, 0x80);
        }
        synch_bit(x, y, *game_current_faction);
        game_draw_tile(x, y, 2);
    }
    return seen;
}

// Native life where the damage was: mind worms beside the faction's own
// fungus (with bases in the region; else locusts, and at sea isles of the
// deep, every third locusts), all of the worms and a third of the damage of
// the others, at most two thirds of the faction's bases; a few spore
// launchers among the worms. Then on the square itself, unless occupied, up
// to a third of the damage, at most the base's size, less those.
static void native_life(int sx, int sy) {
    int faction = B->faction;
    struct faction_data *f = &game_factions[faction];
    int counted = 0, made = 0;
    for (int i = 0; i < (f->clean_minerals < 9 ? f->clean_minerals : 9); i++) {
        int x = xrange(sx + game_radius_x[i]), y = sy + game_radius_y[i];
        if (!on_map(x, y) || !is_fungus(tile(x, y))) continue;
        int b = nearest_base(x, y);
        if (b < 0 || game_bases[b].faction != faction) continue;
        const struct map_tile *t = tile(x, y);
        if (owner_with(t, MAP_UNIT) > 0 || owner_with(t, MAP_BASE) > 0) continue;
        int unit;
        if (t->region >= LAND_REGION_COUNT) unit = counted % 3 ? UNIT_ISLE_OF_THE_DEEP : UNIT_LOCUSTS;
        else unit = f->region_total_bases[t->region] ? UNIT_MIND_WORMS : UNIT_LOCUSTS;
        if (unit != UNIT_MIND_WORMS && (++counted >= 2 * f->base_count / 3 || counted >= f->clean_minerals / 3))
            continue;
        made++;
        if (unit == UNIT_MIND_WORMS && !(game_rand() % 5) && *game_expansion) unit = UNIT_SPORE_LAUNCHER;
        int v = game_veh_init(unit, 0, x, y);
        if (v >= 0) game_vehs[v].order_auto_type = (uint8_t)faction;
        game_draw_tile(x, y, 2);
    }
    const struct map_tile *t = tile(sx, sy);
    if (owner_with(t, MAP_BASE | MAP_UNIT) > 0) return;
    int unit = !f->region_total_bases[t->region]      ? UNIT_LOCUSTS
               : t->region >= LAND_REGION_COUNT ? UNIT_ISLE_OF_THE_DEEP
                                                : UNIT_MIND_WORMS;
    for (int n = made; n < f->clean_minerals / 3 && n < B->pop_size; n++) {
        if (unit == UNIT_MIND_WORMS && !(game_rand() % 5) && *game_expansion) unit = UNIT_SPORE_LAUNCHER;
        game_veh_init(unit, 0, sx, sy);
        if (unit == UNIT_SPORE_LAUNCHER) unit = UNIT_MIND_WORMS;
    }
}

// Warming, for a human's base or at the higher difficulties: once enough
// has built up, the sea rises by one to three, more for more damage, and
// everyone hears of it. Returns whether it did.
static int global_warming(void) {
    int faction = B->faction;
    struct faction_data *f = &game_factions[faction];
    (*game_climate_level)++;
    int sea = *game_sea_level, rises = game_worldbuilder->sea_level_rises;
    sea = sea < 0 ? 0 : sea > 100 ? 100 : sea;
    int rate = rises < 1 ? 1 : rises > 99 ? 99 : rises;
    if (game_rules_basic->warming_num * *game_climate_level / game_rules_basic->warming_den < 3 * sea / rate + 6)
        return 0;
    *game_climate_level = 0;
    int rise = f->clean_minerals / 6 - 1;
    rise = rise < 1 ? 1 : rise > 3 ? 3 : rise;
    *game_sea_level_council += rise;
    *game_climate_change += rises * rise / 3;
    game_climate_shown[0] = abs(*game_climate_change);
    game_climate_shown[1] = 20;
    game_climate_shown[2] = 0;
    game_parse_num(0, 20);
    game_parse_num(1, 1000 * abs(*game_climate_change) / 15);
    if (faction == *game_current_faction) {
        game_popp(*game_script_file, text(0x689e48), 0, text(0x689e38), 0);
    } else {
        const struct faction_def *d = &game_faction_defs[faction];
        *game_parse_gender = d->noun_gender;
        *game_parse_plural = d->noun_plural;
        game_parse_says(0, (char *)d->noun, -1, -1);
        game_popp(*game_script_file, text(0x689e68), 0, text(0x689e58), 0);
    }
    return 1;
}

// Once in a game, with enough damage: a volcano rises at a random sea
// square with no one within three squares.
static void volcano(void) {
    int w = *game_map_width, h = *game_map_height;
    int rx = w - 1 > 0 ? game_rand() % w : 0;
    int y = h - 1 > 0 ? game_rand() % h : 0;
    int x = rx;
    if (rx & 1) x = rx - 1;
    if (y & 1) x++;
    if (!on_map(x, y) || altitude(tile(x, y)) >= ALT_LAND) return;
    for (int j = 0; j < 49; j++) {
        int nx = xrange(x + game_radius_x[j]), ny = y + game_radius_y[j];
        if (on_map(nx, ny) && owner_with(tile(nx, ny), MAP_BASE | MAP_UNIT) >= 0) return;
    }
    int player = *game_current_faction;
    if ((game_factions[player].flags & FACTION_FLAG_MAP_REVEALED) || ((tile(x, y)->visibility >> player) & 1))
        game_console_focus(game_console, x, y, player);
    *game_state |= GAME_STATE_VOLCANO;
    world_volcano(x, y, 1);
    world_climate();
    game_draw_map(1);
    game_clock_wait(1000);
    const struct faction_def *d = &game_faction_defs[*game_current_faction];
    *game_parse_plural = 0;
    *game_parse_gender = d->leader_female;
    game_parse_says(0, (char *)d->leader_title, -1, -1);
    d = &game_faction_defs[*game_current_faction];
    *game_parse_plural = 0;
    *game_parse_gender = d->leader_female;
    game_parse_says(1, (char *)d->leader_name, -1, -1);
    player = *game_current_faction;
    if ((game_factions[player].flags & FACTION_FLAG_MAP_REVEALED) || ((tile(x, y)->visibility >> player) & 1))
        game_popp(*game_script_file, text(0x689e94), 0, text(0x689e88), 0);
}

// The base's ecological damage, weighed by PLANET, adds to its faction's,
// and may, by chance (twice the chance at perihelion), grow fungus near it;
// the player is shown. Then, unless the AI is let off (more often at the
// lower difficulties) or the faction was spared this turn, after enough
// damage and while the Voice of Planet is unbuilt, native life comes,
// planet warms, and a volcano may rise.
void base_ecology(void) {
    int faction = B->faction;
    struct faction_data *f = &game_factions[faction];
    int planet = 4 - f->se_pending[SE_PLANET];
    planet = planet < 1 ? 1 : planet > 99 ? 99 : planet;
    f->eco_damage_total += 4 * B->eco_damage / planet;
    int roll = game_rand() % 100;
    if (roll >= B->eco_damage / ((*game_state & GAME_STATE_PERIHELION) ? 2 : 1)) return;
    if ((*game_rules & RULES_NO_NATIVE_LIFE) || ((*game_state & GAME_STATE_DONE) && !(*game_state & GAME_STATE_FINAL_SCORE_DONE)))
        return;
    int which = fungus_square();
    if (which <= 0) return;
    int sx = xrange(B->x + game_radius_x[which]), sy = B->y + game_radius_y[which];
    f->clean_minerals++;
    int improvement = -1;
    if (grow_fungus(sx, sy, &improvement)) {
        if (!(game_get_async_key_state(0x10) & 0xff00) && !*game_net_multiplayer) game_clock_wait(20);
        int player = *game_current_faction;
        if (faction == player || owner_with(tile(sx, sy), MAP_UNIT) == player) {
            game_map_win_set_center(game_map_wins[0], sx, sy, 1);
            game_console_focus(game_console, sx, sy, faction);
            game_parse_says(0, B->name, -1, -1);
            if (improvement >= 0 && faction == *game_current_faction) {
                const struct rules_terraform *r = &game_terraform[improvement];
                game_parse_say(1, (int)(intptr_t)(altitude(tile(sx, sy)) < ALT_LAND ? r->name_sea : r->name), -1, -1);
                game_pop2(text(0x689df0), text(0x689de4), ID);
            } else {
                game_pop2(text(0x689e0c), text(0x689e00), -1);
            }
            player = *game_current_faction;
        }
        if (faction == player && f->clean_minerals >= 2) game_interlude(5, B->name, 1, 0);
    }
    if (!is_human(faction) && !(game_rand() % 2)) {
        int n = *game_difficulty + 1;
        if (n - 1 > 0 && game_rand() % n) {
            f->clean_minerals--;
            return;
        }
    }
    if (f->flags & FACTION_FLAG_ECO_SPARED) {
        f->flags &= ~(uint32_t)FACTION_FLAG_ECO_SPARED;
        return;
    }
    if (f->clean_minerals < 7 || game_secret_projects[SP_VOICE_OF_PLANET] != -1) return;
    if (faction == *game_current_faction) game_interlude(8, 0, 1, 0);
    if (!(game_rand() % 4) && *game_expansion && altitude(tile(sx, sy)) >= ALT_LAND)
        game_veh_init(UNIT_FUNGAL_TOWER, 0, sx, sy);
    native_life(sx, sy);
    if ((is_human(faction) || *game_difficulty >= 4) && global_warming()) return;
    if (f->clean_minerals >= 10 && !(*game_state & GAME_STATE_VOLCANO)) volcano();
}

// ---- Base names -------------------------------------------------------------

#include <stdio.h>
#include <strings.h>

// Counts the section's lines, up to `want`, stopping at an empty line or
// the one that ends it.
static int count_lines(int n, int want, uint32_t end) {
    while (n < want) {
        game_text_get();
        if (!strlen(*game_text_line) || !strncasecmp(*game_text_line, text(end), 4)) break;
        n++;
    }
    return n;
}

static void take_name(char *name) {
    strncpy(name, game_text_item(), 24);
    name[23] = 0;
}

// A new base's name from its faction's file: for a sea base the next of its
// sea names, while they last; otherwise the next of its land names (after
// the first, in an order shuffled by the map's seed), then the shared list,
// and failing those its noun and a number. `save` counts the name as used.
// As the original has it, a section that runs out leaves the file open.
void name_base(int faction, char *name, int save, int sea) {
    struct faction_data *f = &game_factions[faction];
    struct faction_def *d = &game_faction_defs[faction];
    if (sea && !game_text_open(d->filename, text(0x6893b0))) {
        int want = f->base_sea_name_offset + 1;
        if (count_lines(0, want, 0x6893bc) == want) {
            take_name(name);
            if (save) f->base_sea_name_offset = want;
            game_text_close();
            return;
        }
    }
    int used = f->base_name_offset + 1;
    name[0] = 0;
    *game_parse_gender = d->noun_gender;
    *game_parse_plural = d->noun_plural;
    strcat(name, d->noun);
    strcat(name, text(0x682820));
    char number[16];
    snprintf(number, sizeof number, "%d", used);
    strcat(name, number);
    if (game_text_open(d->filename, text(0x6893c4))) return;
    int total = count_lines(0, 0x7fffffff, 0x6893cc), want = used;
    if (used > 1 && used <= total) {
        int v = ((*game_map_seed + faction) & 0xfe) | 1;
        for (int i = 1; i != used; i++) {
            do {
                if (v & 1) v ^= 0x170;
                v >>= 1;
            } while (v >= total);
        }
        want = v + 1;
    }
    if (game_text_open(d->filename, text(0x6893d4))) return;
    int n = count_lines(0, want, 0x6893dc);
    if (n != want) {
        if (game_text_open(text(0x6893ec), text(0x6893e4))) return;
        for (; n < want; n++) {
            game_text_get();
            if (!strlen(*game_text_line) || !strncasecmp(*game_text_line, text(0x6893f8), 4)) return;
        }
    }
    take_name(name);
    if (save) f->base_name_offset = used;
    game_text_close();
}

// ---- The AI's terraforming plans --------------------------------------------

enum {
    BASE_STATE_WANTS_ENERGY = 0x1000, BASE_STATE_WANTS_MINERALS = 0x2000, BASE_STATE_WANTS_NUTRIENTS = 0x4000,
    MAP_SPECIAL = 0x1c00000,
    MAP_IMPROVED = 0x1e00050, MAP_BOREHOLE = 0x1000000, MAP_MONOLITH_ = 0x2000,
    GOAL_CONDENSER = 0x49, GOAL_BOREHOLE = 0x69, GOAL_FOREST = 0x79,
    GOAL_SEA = 25, TERRAFORM_ROAD_TECH = 1, TERRAFORM_LEVEL_TECH = 6, TERRAFORM_FUNGUS_TECH = 10,
};

static int rockiness(const struct map_tile *t) { return t->rocky_lock >> 6; }
static int rainfall(const struct map_tile *t) { return (t->climate >> 3) & 3; }
static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// How much a square suits a borehole or condenser goal: dry, rocky, or flat
// and not wet.
static int dry_or_rocky(const struct map_tile *t) {
    int rocky = t->rocky_lock & 0xc0, rain = t->climate & 0x18;
    return !rain + (rocky == 0x80) + (!rocky && rain < 0x10);
}

// For the AI (and a human's governor): what the base needs most, energy,
// minerals or nutrients, from its priorities and size; then the squares it
// works or could, each scored for a forest, a farm, a mine, a solar
// collector or removing fungus, with goals set for its formers (and, when
// `instant`, the improvement made at once); a raised square, a borehole or
// a condenser where they would help most; and roads to its other bases.
//
// As the original has it: a sea shelf square that can't be terraformed ends
// it at once; the scores share one variable, which a mine on rockiness 3
// takes as it was left; and nothing comes of what follows the target set
// last (the original jumps over it always).
void base_terraform(int instant) {
    struct base *b = B;
    int faction = b->faction, human = is_human(faction);
    struct faction_data *f = &game_factions[faction];
    int aquatic = (game_faction_defs[faction].rule_flags & RULE_FLAG_AQUATIC) != 0;
    int tech = 0, wealth = 0, power = 0;
    if (human) {
        uint32_t g = b->governor;
        if (g & GOVERNOR_EXPLORE) {
        } else if (g & GOVERNOR_DISCOVER) {
            tech = 1;
        } else if (g & GOVERNOR_BUILD) {
            wealth = 1;
        } else if (g & GOVERNOR_CONQUER) {
            power = 1;
        }
    } else {
        tech = f->ai_tech, wealth = f->ai_wealth, power = f->ai_power;
    }
    int pop = b->pop_size;
    int need_e = clamp(((wealth - power + tech + 4) * pop + 3) / 5 - b->intake[RESOURCE_ENERGY] + *game_terraform_energy, 0, 99);
    int need_m = clamp(2 * power - b->intake[RESOURCE_MINERAL] + pop + 1, 0, 99);
    if (need_e && need_e + 1 > need_m) b->state |= BASE_STATE_WANTS_ENERGY;
    else b->state &= ~(uint32_t)BASE_STATE_WANTS_ENERGY;
    if (need_m && need_m + 1 > need_e) b->state |= BASE_STATE_WANTS_MINERALS;
    else b->state &= ~(uint32_t)BASE_STATE_WANTS_MINERALS;
    int food = 0;
    if (b->surplus[RESOURCE_NUTRIENT] < clamp(3 - clamp(need_e, 0, 2) - clamp(need_m, 0, 2), 0, 3)) {
        b->state |= BASE_STATE_WANTS_NUTRIENTS;
        food = b->surplus[RESOURCE_NUTRIENT] < 0 ? 2 : 1;
    } else {
        b->state &= ~(uint32_t)BASE_STATE_WANTS_NUTRIENTS;
    }

    // Its own land: sensors, the squares it works that could yield well,
    // and good ones no base of the faction works.
    int sensors = 0, open_rich = 0, worked_good = 0, open = 0;
    for (int i = 0; i < BASE_RADIUS_SQUARES; i++) {
        int x = xrange(b->x + game_radius_x[i]), y = b->y + game_radius_y[i];
        if (!on_map(x, y) || altitude(tile(x, y)) < ALT_LAND) continue;
        if (whose_territory(faction, x, y, 0, 0) != faction) continue;
        if (is_sensor(x, y)) sensors++;
        const struct map_tile *t = tile(x, y);
        uint32_t bits = t->bits;
        if ((bits & MAP_FOREST) || is_fungus(t)) continue;
        if (b->worked_tiles & (1u << i)) {
            if ((bits & MAP_FARM) || rockiness(t) >= 2) continue;
            if ((t->climate & 0x18) >= 0x10 && !has_tech(game_rules_basic->tech_three_nutrients, faction) &&
                bonus_at(x, y, 0) != 1)
                continue;
            worked_good++;
            continue;
        }
        if (rainfall(t) + (int)(bits >> 15 & 1) < 2) continue;
        int free = 1;
        for (int j = 0; j < *game_base_count; j++) {
            const struct base *o = &game_bases[j];
            if (o->faction != faction || vector_dist_offset(x_dist(x, o->x), abs(y - o->y)) > 2) continue;
            if (o->worked_tiles & (1u << (radius_move(o->x, o->y, x, y, 21) & 31))) free = 0;
        }
        if (!free) continue;
        open++;
        if ((t->rocky_lock & 0xc0) || bonus_at(x, y, 0)) open_rich++;
    }

    // The sea: a shelf square to work, in the biggest sea region about it.
    int s = 0, seas = 0, best = 0, bx = -1, by = -1, sea_region = -1;
    for (int i = 0; i < BASE_RADIUS_SQUARES; i++) {
        int x = xrange(b->x + game_radius_x[i]), y = b->y + game_radius_y[i];
        if (!on_map(x, y)) continue;
        const struct map_tile *t = tile(x, y);
        int a = altitude(t);
        if (a >= ALT_LAND) continue;
        seas++;
        if (!i) continue;
        int r = t->region;
        if (sea_region < 0) {
            sea_region = r;
        } else if (r != sea_region) {
            if (game_continents[r].tile_count <= game_continents[sea_region].tile_count) continue;
            sea_region = r;
        }
        if (a != ALT_SHELF) continue;
        if (!game_can_terraform(faction, x, y, 1, ID)) return;
        s = 21 - i + (bonus_at(x, y, 0) ? 21 : 0);
        if (s > best) best = s, bx = x, by = y;
    }
    if (on_map(bx, by) && tile(bx, by)->region == sea_region) game_add_goal(faction, GOAL_SEA, 2, bx, by, ID);

    // How good its land is: squares that could yield well, those improved
    // already (or, with enough fungus bonuses, fungus), and the rain.
    b->terraform_x = b->terraform_y = -1;
    int sat = f->tech_fungus[0] + f->tech_fungus[1] + f->tech_fungus[2];
    int good = 0, improvable = 0, good_worked = 0, improved_worked = 0, special = 0, rains = 0;
    for (int i = 0; i < BASE_RADIUS_SQUARES; i++) {
        int x = xrange(b->x + game_radius_x[i]), y = b->y + game_radius_y[i];
        if (!on_map(x, y)) {
            rains++;
            continue;
        }
        const struct map_tile *t = tile(x, y);
        int a = altitude(t);
        if (a < ALT_LAND && (!instant || !aquatic)) continue;
        int rocky = rockiness(t), rain = rainfall(t), alt2 = (t->climate >> 5) - 2;
        alt2 = alt2 < 0 ? 0 : alt2;
        uint32_t bits = t->bits;
        int forest = bits & MAP_FOREST, fungus = (bits & MAP_FUNGUS) && a >= ALT_SHELF;
        if (bits & MAP_SPECIAL) special++;
        if (i && (alt2 + rocky + rain >= 4 || forest || (fungus && sat >= 4))) {
            good++;
            int w = (b->worked_tiles >> i) & 1;
            good_worked += w;
            if (fungus) {
                if (sat >= 4) improvable++;
                else if (!has_tech(game_terraform[TERRAFORM_FUNGUS_TECH].preq_tech, faction)) good--;
            } else if (forest || (((bits & MAP_FARM) || (rocky > 1 && (bits & MAP_MINE))) &&
                                  (rocky > 1 || (bits & MAP_SOLAR) || ((bits & MAP_MINE) && (bits & MAP_FARM))))) {
                improvable++;
                improved_worked += w;
            }
        }
        rains += rain;
    }
    if (!human && b->pop_size >= 4) good--;
    int wet = (rains >= 15) + (rains > 25);

    // Each square of the base's land region, scored for each improvement.
    int home = tile(b->x, b->y)->region;
    int bsq = -1, bact = 0, highs = 0, has_bore = 0, has_cond = 0;
    int raise_best = -1, raise_x = -1, raise_y = -1, bore_best = -1, bore_x = 0, bore_y = 0;
    int cond_best = -1, cond_x = 0, cond_y = 0;
    best = 0;
    for (int i = 0; i < BASE_RADIUS_SQUARES; i++) {
        int x = xrange(b->x + game_radius_x[i]), y = b->y + game_radius_y[i];
        if (!on_map(x, y)) continue;
        const struct map_tile *t = tile(x, y);
        uint32_t bits = t->bits;
        if (bits & MAP_MONOLITH_) continue;
        if (bits & MAP_BOREHOLE) has_bore = 1;
        if (bits & MAP_CONDENSER) has_cond = 1;
        if (!i) continue;
        if (is_fungus(t) && !has_tech(game_terraform[TERRAFORM_FUNGUS_TECH].preq_tech, faction)) continue;
        if (altitude(t) < ALT_LAND && (!instant || !aquatic)) continue;
        if (t->region != home) continue;
        int z = zoc_move(x, y, faction);
        if (z >= 0) {
            if (z == 1 || (f->diplo_status[z - 1] & 0xc30) || owner_with(tile(x, y), MAP_BASE | MAP_UNIT) >= 0) continue;
        }
        int owner = whose_territory(faction, x, y, 0, 0);
        if (owner >= 0 && owner != faction) continue;
        t = tile(x, y);
        bits = t->bits;
        int altlev = t->climate >> 5, farmed = bits & MAP_FARM;
        int solared = (bits & MAP_SOLAR) || (bits & MAP_BOREHOLE), mined = (bits & MAP_MINE) || (bits & MAP_BOREHOLE);
        uint32_t impr = bits & MAP_IMPROVED;
        int crop = crop_yield(faction, -1, x, y, 1);
        int rocky = rockiness(tile(x, y));
        int bonus = bonus_at(x, y, 0);
        int rain = rainfall(tile(x, y));
        int sensor = is_sensor(x, y);
        t = tile(x, y);
        bits = t->bits;
        int cond = (bits & MAP_SPECIAL) != 0, forest = bits & MAP_FOREST, fung = is_fungus(t) ? 4 : 0;
        int worked = (B->worked_tiles & (1u << i)) != 0;
        int alt2 = altlev - 2 < 0 ? 0 : altlev - 2;
        if (!worked && improved_worked == good_worked && !open_rich) {
            if (forest) goto forest_check;
            if (fung) goto fungus_check;
            if (rain && rocky < 2) worked = 1;
            goto plant_forest;
        }
        if (!forest) goto fungus_maybe;
    forest_check:
        s = (sensor ? -1 : 1) + alt2 + rocky + rain;
        if (has_tech(game_terraform[TERRAFORM_ROAD_TECH].preq_tech, faction)) s++;
        if (has_facility(ID, FAC_TREE_FARM)) s--;
        if (has_facility(ID, FAC_HYBRID_FOREST)) s--;
        if (s > 4) forest = 0;
    fungus_maybe:
        if (!fung) goto plant_forest;
    fungus_check: {
        int v = !sensor + alt2 + rocky + rain;
        if (v < sat) continue;
        if (v == sat) fung = 1;
    }
    plant_forest:
        if (!sensors && !sensor && !bonus && !(impr && B->pop_size < 8) && !(human && !(*game_more_preferences & 0x20)) &&
            !(open_rich && worked_good < B->pop_size - 1 && B->pop_size < 2) &&
            !(fung && !has_tech(game_rules_basic->tech_improve_fungus, faction)) &&
            !(rocky <= 1 && !forest && rain && (!fung || !has_tech(game_rules_basic->tech_improve_fungus, faction)))) {
            s = (B->pop_size + 2 * f->ai_fight + 2) * 4;
            if (forest || fung || !rain) s *= 2;
            int n = 1, k;
            for (k = 1; k < BASE_RADIUS_SQUARES; k++, n++) {
                int x2 = xrange(x + game_radius_x[k]), y2 = y + game_radius_y[k];
                if (on_map(x2, y2) && altitude(tile(x2, y2)) < ALT_LAND) break;
            }
            if (k == BASE_RADIUS_SQUARES || !n || n >= 9) s *= 2;
            if (s > best) best = s, bsq = i, bact = 9;
        }
        // Energy: a solar collector (or a farm).
        if ((need_e || (B->state & 0x8000)) && !forest && !cond && !(sensor && sensors <= (worked ? 1 : 2)) &&
            !(mined && (need_m || alt2 < 2)) && crop >= (farmed ? 1 : 0) + 1 - (need_e > 1 && alt2 > 1) && alt2) {
            s = alt2 * 3;
            if (bonus == 2 || rocky == 1) s += 2;
            if (bonus == 1) s += 2;
            else if (bonus == 3) s += farmed ? -1 : 1;
            if (farmed) s++;
            if (rocky == 2) s -= 3;
            if (tile(x, y)->bits & MAP_FOREST) s /= 2;
            s *= clamp(2 * need_e, 1, 99);
            if (worked) s *= 2;
            if (fung) s += B->eco_damage * fung * 2;
            if (worked || need_e > 1) {
                if (!farmed && (bonus == 3 || !need_e || food) && rocky < 2) {
                    if (food) s *= 2;
                    if (s > best) best = s, bsq = i, bact = 0;
                } else if (!solared) {
                    if (s > best) best = s, bsq = i, bact = 3;
                } else if (!farmed && crop < 3 && rocky < 2 && s > best) {
                    best = s, bsq = i, bact = 0;
                }
            }
        }
        // Raising the land, where it would help the most squares nearby, away
        // from bases' own squares, while the land isn't high or wet already.
        if (altlev >= 5 && ++highs > 1) raise_best = -99999;
        if (!wet && raise_best > -99999 && (raise_best > -9999 || bonus == 1)) {
            s = game_radius_x[i] * game_radius_x[i] * 2;
            if (altlev == 3) {
                if (rocky == 1) s += 3;
                if (bonus == 1) s += 6;
            } else {
                if (bonus != 1) s -= 3 * crop;
                if (rocky == 1) s -= 4;
            }
            int left = seas;
            for (int j = 1, level = altlev; j < altlev + 1; j++, level--) {
                int found = 0;
                for (int k = game_radius_count[j - 1]; k < game_radius_count[j]; k++) {
                    int x2 = xrange(x + game_radius_x[k]), y2 = y + game_radius_y[k];
                    if (!on_map(x2, y2) || tile(x2, y2)->climate >> 5 >= level) continue;
                    found = 1;
                    int d = vector_dist_offset(x_dist(x2, B->x), abs(y2 - B->y));
                    if (d > 2) continue;
                    s++;
                    if (d <= 1 && altitude(tile(x2, y2)) == ALT_SHELF) left--;
                }
                if (!found) break;
            }
            if (!seas || left > 0) {
                if (mined) s -= 4;
                if (solared) s += 2;
                if (bonus == 1) s += 8;
                if (i < 8) s /= 2;
                if (s > raise_best) raise_best = s, raise_x = x, raise_y = y;
            }
        }
        // Minerals: a mine (or a road, or a solar collector).
        if (!need_m && ((rain < 1 - game_rules_basic->nutrient_effect_mine && rain) || rocky)) goto farm_entry;
        if ((sensor && sensors <= (worked ? 1 : 2)) || (solared && (need_e || alt2 >= 2)) || food || forest ||
            (rocky == 1 && alt2 >= 2 && need_m <= 1) || (crop + mined < (farmed ? 1 : 0) + 2 && rocky == 1))
            goto farm_entry;
        if (rocky == 0) s = crop + 1;
        else if (rocky == 1) s = 3 - crop;
        else if (rocky == 2) s = 6;
        if (bonus == 1) s += 4;
        else if (bonus == 2) s *= 2;
        else if (bonus == 3) s += 2;
        if (farmed) s++;
        bits = tile(x, y)->bits;
        if (bits & MAP_ROAD) s++;
        if (bits & MAP_FOREST) s--;
        s = s * need_m * 3;
        if (worked) s *= 2;
        if (fung) s += B->eco_damage * fung * 2;
        if (!worked && need_m <= 1 && (!rain || rocky)) goto farm_entry;
        if (!mined && !cond) {
            if (s > best) best = s, bsq = i, bact = 2;
            goto farm_entry;
        }
        if (!(bits & MAP_ROAD)) {
            if (!mined && !(bits & MAP_BOREHOLE)) goto farm_entry;
            if (!human || !(*game_more_preferences & 0x400000)) {
                if (s > best) best = s, bsq = i, bact = 5;
                goto farm_entry;
            }
        }
        if (!mined || cond) goto farm_entry;
        if (farmed || rocky >= 2) goto fungus_removal;
        if (((tile(x, y)->climate & 0x18) || bonus == 1) && s > best) best = s, bsq = i, bact = 0;
        goto farm;
    farm_entry:
        if (farmed) goto fungus_removal;
    farm:
        // Nutrients: a farm.
        if (rocky >= 2 || forest) goto fungus_removal;
        if (food <= 1 && ((rocky && crop) ? open_rich : open) && !worked) goto fungus_removal;
        if (!food && !(need_m && rocky && crop) &&
            (!rain || !(rocky == 1 || rain == 2 || mined || solared)))
            goto fungus_removal;
        s = crop * 3 + 3 + (bonus > 1 ? 3 : 0);
        bits = tile(x, y)->bits;
        if (bits & MAP_RIVER) s += need_e + 1;
        if (rocky == 1) s += need_m + 1;
        if (bits & MAP_FOREST) s--;
        s = clamp(food + 1, 1, 99) * s * 2;
        if (need_m && rocky) s *= 2;
        if (worked) s *= 2;
        if (rain > 1 && !has_tech(game_rules_basic->tech_three_nutrients, faction)) s /= 3;
        if (fung) s = s / 2 + B->eco_damage * fung * 2;
        if (s > best) best = s, bsq = i, bact = 0;
    fungus_removal:
        if (is_fungus(tile(x, y))) {
            s = B->eco_damage * fung * 2;
            if (s > best) best = s, bsq = i, bact = 10;
        }
        // A borehole: rocky or dry, far out, unimproved, not below its
        // neighbours nor beside another; a condenser: dry and rocky,
        // unimproved, twice as wanted on a planet without clouds.
        if (!has_bore) {
            s = 21 - i + (rocky == 2 ? 12 : 0) + (rocky == 2 || !rain ? 12 : 0);
            if (impr) s -= 8;
            for (int k = 0; k < 8; k++) {
                int x2 = xrange(x + game_radius_near_x[k]), y2 = y + game_radius_near_y[k];
                if (!on_map(x2, y2) || altitude(tile(x2, y2)) < ALT_LAND) continue;
                if (tile(x2, y2)->climate >> 5 < altlev) s = -1;
                if (tile(x2, y2)->bits & MAP_BOREHOLE) s = -1;
            }
            if (s > bore_best) bore_best = s, bore_x = x, bore_y = y;
        }
        if (!has_cond) {
            s = 21 - i + (!impr ? 8 : 0) + (!farmed ? 2 : 0) + ((2 - rain) * (2 - rain) + rocky * 2) * 2;
            if (!*game_cloud_cover && rain < 2) s *= 2;
            if (s > cond_best) cond_best = s, cond_x = x, cond_y = y;
        }
    }

    // Goals for the best square, and roads.
    int level = has_tech(game_terraform[TERRAFORM_LEVEL_TECH].preq_tech, faction) ? 2 : 1;
    int connect = !human || !(*game_more_preferences & 0x400000);
    if (bsq > 0) {
        int x = xrange(B->x + game_radius_x[bsq]), y = B->y + game_radius_y[bsq];
        if (bact == 9) game_add_goal(faction, GOAL_FOREST, 2, x, y, ID);
        else if (bact != 5 && !game_can_terraform(faction, x, y, 1, ID)) level = 2;
        else game_add_goal(faction, GOAL_TERRAFORM, 2, x, y, ID);
        if (connect) game_base_connect(level);
        if (instant) {
            struct map_tile *t = tile(x, y);
            t->bits &= ~(uint32_t)MAP_FUNGUS;
            int v = game_can_terraform(faction, x, y, 1, ID);
            if (*game_expansion && aquatic) {
                int r = ((int)game_time_get_time() + ID + y + x) & 3;
                v = r ? r + 5 : 4;
            }
            t = tile(x, y);
            switch ((unsigned)(v - 4)) {
            case 0:
                t->bits |= MAP_FARM | MAP_ROAD;
                t->bits |= (t->climate & 7) || altitude(t) > ALT_LAND ? MAP_SOLAR : MAP_MINE;
                break;
            case 2:
                t->bits |= MAP_MINE | MAP_ROAD;
                if ((t->rocky_lock & 0xc0) < 0x80) t->bits |= MAP_FARM;
                break;
            case 3:
                t->bits |= MAP_SOLAR | MAP_ROAD;
                break;
            case 4:
                t->bits |= MAP_FOREST;
                break;
            }
        }
    } else if (connect) {
        game_base_connect(2);
    }

    // Then one of a borehole, a condenser or raised land, for a base whose
    // good squares are mostly improved.
    int pop_now = B->pop_size;
    int none = improvable < pop_now + 1 && good > improvable;
    if (!none) {
        if (!has_bore && bore_best > 0 && !B->eco_damage && dry_or_rocky(tile(bore_x, bore_y)) >= 1 &&
            (pop_now >= 8 || need_e > 1 ||
             has_tech(game_abilities[game_abil_index(1)].preq_tech, faction) || *game_current_turn > 200) &&
            game_terrain_avail(0xe, 0, faction) &&
            (!human || (faction == *game_current_faction && (*game_preferences & 0x2000000)))) {
            game_add_goal(faction, GOAL_BOREHOLE, 1, bore_x, bore_y, ID);
            goto target;
        }
        if (!has_cond && cond_best > 0 && !B->eco_damage) {
            int ok = !wet;
            if (wet == 1 && !special)
                ok = food || (dry_or_rocky(tile(cond_x, cond_y)) >= 1 &&
                              (has_tech(game_abilities[game_abil_index(1)].preq_tech, faction) || *game_current_turn > 200));
            if (ok && game_terrain_avail(0xc, 0, faction) &&
                (!human || (faction == *game_current_faction && (*game_preferences & 0x2000000)))) {
                game_add_goal(faction, GOAL_CONDENSER, 1, cond_x, cond_y, ID);
                goto target;
            }
        }
    }
    if (!(wet && need_e <= 2) && raise_best > 0 && !none &&
        (!human || (faction == *game_current_faction && (*game_preferences & 0x20000))) &&
        game_terrain_avail(0x10, 0, faction))
        game_add_goal(faction, GOAL_RAISE_LAND, 1, raise_x, raise_y, ID);
target: {
    // A square 8 west of the base: its terraforming target while wet.
    int x = xrange(B->x - 8), y = B->y;
    if (on_map(x, y) && altitude(tile(x, y)) >= ALT_LAND && wet && altitude(tile(x, y)) < 0xc0)
        B->terraform_x = (int16_t)x, B->terraform_y = (int16_t)y;
    else
        B->terraform_x = B->terraform_y = -1;
}
}

// ---- A base's turn ----------------------------------------------------------

enum { BASE_STATE_RENAME = 0x10000, BASE_STATE_RENAMED = 0x80, PREF_TUTORIAL_MESSAGES = 0x20 };

// A captured base that hasn't been renamed: a third of the time, until the
// game is over, its new owner names it, and the player is told if it is
// theirs or they can see it (Thinker has its former owner).
static void rename_base(int faction) {
    if (!(B->state & BASE_STATE_RENAME)) return;
    if (game_rand() % 3 || (*game_state & GAME_STATE_DONE)) return;
    const struct base *b = &game_bases[ID];
    int player = *game_current_faction;
    if ((b->faction == player || ((b->visibility >> player) & 1)) &&
        !game_console_focus(game_console, B->x, B->y, player))
        game_draw_tiles(B->x, B->y, 2);
    game_parse_says(1, B->name, -1, -1);
    const struct faction_def *d = &game_faction_defs[faction];
    *game_parse_gender = d->noun_gender;
    *game_parse_plural = d->noun_plural;
    game_parse_says(0, (char *)d->noun, -1, -1);
    name_base(faction, B->name, 1, (tile(B->x, B->y)->climate & 0xe0) < ALT_LAND);
    game_parse_says(2, B->name, -1, -1);
    player = *game_current_faction;
    b = &game_bases[ID];
    if (b->faction == player || ((b->visibility >> player) & 1)) {
        game_draw_map(1);
        if (is_alien(faction))
            game_popp(*game_script_file, text(0x689eb4), 0, text(0x689ea4), 0);
        else
            game_popp(*game_script_file, text(0x689ed0), 0, text(0x689ec0), 0);
    }
    B->state &= ~(uint32_t)BASE_STATE_RENAME;
    B->state |= BASE_STATE_RENAMED;
}

// A base's turn, at the end of its faction's: production, rushing,
// growth, support, drones, research, ecology, its energy and trade into
// the faction's reserves, maintenance and the AI's terraforming; then the
// countdowns. Returns 1 if the base was lost (to growth's starvation or a
// revolt) part way.
//
// The original works out the base again by calling base_compute()'s five
// parts itself. Unlike Thinker, it clears only two state bits at the start
// and starts drones and research from the second turn on, not the first.
int base_upkeep(int base) {
    struct base *start = &game_bases[base];
    ID = base;
    B = start;
    int faction = start->faction;
    *game_upkeep_flag = 0;
    start->state &= 0xdffffdffu;
    base_compute(1);
    base_production();
    base_hurry();
    B->minerals_2 = B->minerals;
    B->production_last = B->queue[0];
    if (base_growth()) return 1;
    if (*game_current_turn > 1) base_check_support();
    *game_base_upkeep_state = 1;
    *game_computed_base_id = ID;
    base_support();
    base_yield();
    base_nutrient();
    base_minerals();
    base_energy();
    // The AI's count of worked squares, weighted by its plan for the
    // base's region.
    struct faction_data *f = &game_factions[faction];
    int plan = f->region_base_plan[tile(B->x, B->y)->region];
    int weight = plan == PLAN_NAVAL_TRANSPORT && f->ai_fight <= 0 ? 4 : (plan != PLAN_DEFENSIVE) + 1;
    f->unk_7f4 += bit_count(B->worked_tiles) * weight;
    *game_base_upkeep_state = 2;
    if (*game_current_turn > 1) {
        base_drones();
        base_doctors();
        if (B->faction != faction) return 1;
        base_energy_costs();
        if (*game_current_turn > 5 || *game_net_multiplayer || !(*game_preferences & PREF_TUTORIAL_MESSAGES))
            base_research();
    }
    ID = base;
    B = start;
    base_compute(0);
    base_ecology();
    f->energy_credits += B->economy_total;
    // Trade with each other faction neither under sanctions, and for the
    // Progenitors their energy grid: one for each facility, five for each
    // secret project, halved.
    const struct faction_data *other = &game_factions[1];
    for (int i = 1; i < FACTION_COUNT; i++, other++) {
        if (i == faction || is_alien(faction) || is_alien(i)) continue;
        f->turn_commerce_income += !f->sanction_turns && !other->sanction_turns ? game_base_commerce_import[i] : 0;
    }
    if (is_alien(faction)) {
        int grid = 0;
        for (int i = 0; i < FAC_FIRST_PROJECT; i++) grid += (B->facilities_built[i / 8] >> (i % 8)) & 1;
        for (int i = 0; i < SP_COUNT; i++)
            if (game_secret_projects[i] == base) grid += 5;
        f->turn_commerce_income += f->sanction_turns ? 0 : grid / 2;
    }
    if (*game_current_turn > 1) base_maint();
    base_terraform(0);
    rename_base(faction);
    *game_computed_base_id = -1;
    if (ID == *game_base_win_redraw_base) game_base_win_on_redraw(game_base_win);
    if (*game_upkeep_flag && (!(*game_state & GAME_STATE_DONE) || (*game_state & GAME_STATE_FINAL_SCORE_DONE))) {
        game_log_say(text(0x689edc), base, 0, 0);
        game_base_win_zoom(game_base_win, base, 1);
        game_log_say(text(0x689ef0), base, 0, 0);
    }
    *game_base_upkeep_state = 0;
    if (B->assimilation_turns) {
        B->assimilation_turns--;
        if (B->faction_former == B->faction && B->assimilation_turns) B->assimilation_turns--;
        if (!B->assimilation_turns) B->faction_former = B->faction;
    }
    if (B->nerve_staple_turns) B->nerve_staple_turns--;
    B->state &= 0xffdffff7u;
    if (!((*game_current_turn + ID) & 7)) B->state &= ~0x100000u;
    return 0;
}

// ---- A faction's turn --------------------------------------------------------

enum { FACTION_FLAG_SELF_AWARE_MAINT = 0x20, DIFF_THINKER = 4 };

// A faction's loans: each of the other factions in the game, not under
// sanctions, that it owes gets the payment, or what it has of it; what is
// short (all of it, at war) is added to the debt. Unlike Thinker, a
// faction in debt pays a negative amount.
static void pay_loans(int faction) {
    struct faction_data *f = &game_factions[faction];
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (i == faction || !((*game_factions_alive >> i) & 1) || !((*game_factions_alive >> faction) & 1)) continue;
        if (!f->loan_balance[i] || game_factions[i].sanction_turns) continue;
        if (f->diplo_status[i] & TREATY_VENDETTA) {
            f->loan_balance[i] += f->loan_payment[i];
            continue;
        }
        int pay = f->loan_payment[i] < f->energy_credits ? f->loan_payment[i] : f->energy_credits;
        game_factions[i].energy_credits += pay;
        f->loan_balance[i] -= pay;
        f->energy_credits -= pay;
        if (pay < f->loan_payment[i]) f->loan_balance[i] += f->loan_payment[i] - pay;
    }
}

// A faction's turn for its bases: the totals they add up reset, loans
// paid, each base's turn (the next base taken from where one was lost),
// the AI's discount on maintenance above Specialist (a third, or two
// thirds above Thinker), interest on the reserves (or one energy a base),
// the turn's balance, the faction's free techs on the first turn, and the
// end of sanctions (the player told).
void production_phase(int faction) {
    struct faction_data *f = &game_factions[faction];
    const struct faction_def *d = &game_faction_defs[faction];
    f->best_mineral_output = 0;
    f->energy_surplus_total = 0;
    f->facility_maint_total = 0;
    f->turn_commerce_income = 0;
    tech_effects(faction);
    pay_loans(faction);
    f->flags &= ~(uint32_t)FACTION_FLAG_SELF_AWARE_MAINT;
    *game_turn_start_credits = f->energy_credits;
    memset(f->social_effic, 0, sizeof f->social_effic);
    memset(f->social_support, 0, sizeof f->social_support);
    memset(f->social_psych, 0, sizeof f->social_psych);
    f->unk_7f4 = f->armed_units = f->eco_damage_total = f->nutrient_surplus_total = f->labs_total = 0;
    for (int b = 0; b < *game_base_count; b++) {
        if (game_bases[b].faction != faction) continue;
        if (base_upkeep(b)) b--;
        game_do_all_non_input();
    }
    if (!is_human(faction) && *game_difficulty > 3 && f->facility_maint_total) {
        int off = *game_difficulty == DIFF_THINKER ? f->facility_maint_total / 3 : f->facility_maint_total * 2 / 3;
        f->energy_credits += off;
        f->facility_maint_total -= off;
    }
    if (d->rule_flags & RULE_FLAG_INTEREST) {
        if (d->rule_interest) {
            f->energy_credits += f->energy_credits * d->rule_interest / 100;
        } else {
            for (int b = 0; b < *game_base_count; b++)
                if (game_bases[b].faction == faction) f->energy_credits++, f->energy_surplus_total++;
        }
    }
    f->unk_310 = f->energy_surplus_total;
    f->unk_30c = f->turn_commerce_income - f->facility_maint_total + f->energy_surplus_total;
    if (*game_current_turn == 1) {
        *game_skip_tech_screen = 1;
        for (int i = 0; i < d->rule_tech_selected; i++) {
            game_log_say5(text(0x68b9d8), (char *)d->leader_name, faction, 0, 0);
            game_tech_advance(faction);
        }
        *game_skip_tech_screen = 0;
    }
    if (((*game_state & GAME_STATE_DONE) && !(*game_state & GAME_STATE_FINAL_SCORE_DONE)) || !f->sanction_turns) return;
    if (--f->sanction_turns || faction != *game_current_faction) return;
    if (is_alien(faction))
        game_popp(*game_script_file, text(0x68b9f4), 0, text(0x68b9e8), 0);
    else
        game_popp(*game_script_file, text(0x68ba18), 0, text(0x68ba08), 0);
}

// ---- Energy allocation ------------------------------------------------------

enum { TECH_TRANSCENDENT_THOUGHT = 88 };

// The most of a faction's energy, in tenths, that may go to economy alone
// over labs and psych: all of it, in this version.
int energy_limit(int faction) {
    (void)faction;
    return 10;
}

// The player warned of a falling energy reserve at a higher difficulty:
// with half or more going to economy, every fifth turn once formers are
// known, to build infrastructure; otherwise asked whether to open the
// SOCIAL screen.
static void warn_energy(int faction) {
    const struct faction_data *f = &game_factions[faction];
    if (f->diff_level <= 1 || faction != *game_current_faction || f->base_count <= 1) return;
    if (f->energy_credits >= 100 || f->energy_credits * 11 - *game_turn_start_credits * 10 >= 0) return;
    if (10 - f->alloc_labs - f->alloc_psych < 5) {
        if (game_popp(*game_script_file, text(0x68b9a4), 0, text(0x68b990), 0)) game_social_select(faction);
        return;
    }
    if (has_tech(game_protos[PROTO_FORMERS].preq_tech, faction) && *game_current_turn % 5 == 0)
        game_popp(*game_script_file, text(0x68b9c8), 0, text(0x68b9b4), 0);
}

// The AI's psych allocation, from its POLICE. With a base in riots it is
// raised by one, and under POLICE -3 or worse once more for each base that
// riots when worked out again, starting over after each. Otherwise it may
// fall, unless the current base (not each of the faction's: a quirk of the
// original) has specialists to spare.
static void allocate_psych(int faction) {
    struct faction_data *f = &game_factions[faction];
    int police = f->se_pending[SE_POLICE];
    int want = police < -2 ? 4 : police == -2 ? 2 : police <= 1;
    int calm = 1, riot = 0;
    for (int b = 0; b < *game_base_count && !riot; b++) {
        if (game_bases[b].faction != faction) continue;
        if (B->state & BASE_STATE_RIOTS) riot = 1;
        else if (B->specialist_total > B->specialist_adjust) calm = 0;
    }
    if (!riot) {
        if (calm && f->alloc_psych > want / 2) f->alloc_psych--;
        return;
    }
    if (f->alloc_psych >= want || ++f->alloc_psych >= want) return;
    for (int b = 0; police < -2 && b < *game_base_count; b++) {
        if (game_bases[b].faction != faction) continue;
        ID = b;
        B = &game_bases[b];
        base_compute(1);
        if (B->talent_total < B->drone_total && (B->state & BASE_STATE_RIOTS)) {
            if (++f->alloc_psych >= want) return;
            b = -1;
        }
    }
}

// What a faction spends its energy on, for the AI (the player is only
// warned): psych as above, kept from 0 (2 under POLICE -2 or worse) to 4,
// none with the Telepathic Matrix; labs by its priorities, more early on
// and with reserves to spare, less with regions to defend or attack, then
// moved against economy by the bases' takings less their maintenance, one
// less while researching Transcendent Thought; then kept within the
// limits, and economy kept within EFFICIENCY's reach of labs.
void allocate_energy(int faction) {
    if (is_human(faction)) {
        warn_energy(faction);
        return;
    }
    struct faction_data *f = &game_factions[faction];
    const struct faction_def *d = &game_faction_defs[faction];
    int economy = 10 - f->alloc_psych - f->alloc_labs;
    allocate_psych(faction);
    int police = f->se_pending[SE_POLICE];
    f->alloc_psych = game_range(f->alloc_psych, police >= -1 ? 0 : 2, 4);
    if (holds_project(SP_TELEPATHIC_MATRIX, faction)) f->alloc_psych = 0;
    f->alloc_labs = (f->ai_tech - f->ai_power - f->alloc_psych + 11) / 2 - f->ai_wealth;
    if (*game_current_turn < (f->ai_tech - f->ai_power - f->ai_wealth + 3) * 25) f->alloc_labs++;
    f->alloc_labs += f->energy_credits / (2 * (d->rule_techcost * (f->ai_power + f->ai_wealth + 1) + *game_current_turn));
    int r = 1;
    for (; r < REGION_COUNT; r++) {
        if (!f->region_total_bases[r]) continue;
        if (f->region_base_plan[r] == PLAN_DEFENSIVE) {
            f->alloc_labs--;
            break;
        }
        if (f->region_base_plan[r] == PLAN_OFFENSIVE && (f->ai_wealth || f->ai_power)) f->alloc_labs--;
    }
    if (f->ai_tech && !f->ai_wealth && !f->ai_power && r >= REGION_COUNT) f->alloc_labs++;
    if (economy && 10 - f->alloc_labs - f->alloc_psych >= economy) {
        int sum = 0;
        for (int b = 0; b < *game_base_count; b++) {
            const struct base *base = &game_bases[b];
            if (base->faction != faction) continue;
            sum += base->unk_total;
            for (int fac = 0; fac <= FAC_SKY_HYDRO_LAB - 1; fac++)
                if ((base->facilities_built[fac / 8] >> (fac % 8)) & 1) sum -= fac_maint(fac, faction);
        }
        int enough = (f->ai_wealth * 2 + 4 - f->ai_tech) * f->base_count / 4;
        if (sum > enough) {
            while (10 - f->alloc_labs - f->alloc_psych >= economy) f->alloc_labs++;
        } else if (sum < enough / 2) {
            f->alloc_labs--;
        }
    }
    if (f->tech_research_id == TECH_TRANSCENDENT_THOUGHT) f->alloc_labs--;
    int most = 10 - f->alloc_psych < energy_limit(faction) ? 10 - f->alloc_psych : energy_limit(faction);
    f->alloc_labs = f->alloc_labs < 0 || most < 0 ? 0 : f->alloc_labs > most ? most : f->alloc_labs;
    while (10 - f->alloc_labs - f->alloc_psych > energy_limit(faction)) f->alloc_labs++;
    int effic = game_range(f->se_2[SE_EFFIC], 0, 99) + game_range(3 - f->base_count, 0, 3) + 1;
    f->alloc_labs = game_range(f->alloc_labs, 0, 10);
    while (f->alloc_labs > effic - f->alloc_psych - f->alloc_labs + 10) f->alloc_labs--;
    while (10 - f->alloc_labs - f->alloc_psych > f->alloc_labs + effic) f->alloc_labs++;
}

// ---- Repairs ----------------------------------------------------------------

enum {
    PROTO_BATTLE_OGRE_MK1 = 16, PROTO_BATTLE_OGRE_MK3 = 18, PROTO_FUNGAL_TOWER = 19, PROTO_FIRST_CUSTOM = 64,
    VEH_STATE_HAS_MOVED = 0x4, VEH_STATE_AWAY = 0x8, ORDER_SENTRY = 1, ORDER_HOLD = 2,
    MAP_BUNKER = 0x800, MAP_AIRBASE = 0x40000, ABILITY_REPAIR = 0x200000, FAC_NAVAL_YARD = 28,
    SP_COMMAND_NEXUS = 1, SP_XENOEMPATHY_DOME = 8, SP_MARITIME_CONTROL_CENTER = 10, SP_NANO_FACTORY = 22,
};

static int has_base(const struct map_tile *t) { return (t->bits & MAP_BASE) && (t->owner_site & 0xf) < 8; }
static int proto_triad(int proto) { return game_chassis[game_protos[proto].chassis].triad; }
// Native life: a psi unit of the game's own.
static int is_native(int proto) { return game_weapons[game_protos[proto].weapon].offense < 0 && proto < PROTO_FIRST_CUSTOM; }

// One unit's repair, from a value of 1 a turn by its reactor: 2 for native
// life in fungus; 1 more in its faction's territory, at an airbase (air)
// or bunker (land); 1 more at a base not in riots, where the right
// facility or project (Command Center or Nexus, Naval Yard or Maritime
// Control Center, Aerospace Complex or Cloudbase Academy, for its
// faction), or a base that breeds native life for native units, repairs
// all of it. A land unit carrying others that is on sentry or at sea
// heals twice as fast with a repair unit in its stack; the Nano Factory
// repairs all. Away from bases a unit of a faction stays at twice its
// reactor's damage, unless it is native life in fungus or the faction has
// the Xenoempathy Dome (which repairs a reactor more). A human's unit on
// sentry wakes when fully repaired, unless it is a land unit at sea.
//
// Thinker adds fixed amounts where the original repairs fully.
static void repair_veh(int faction, int v) {
    struct veh *u = &game_vehs[v];
    int x = u->x, y = u->y, damage = u->damage;
    u->moves_spent = 0;
    u->iter_count = 0;
    u->state &= 0xfbffdffdu;
    u->flags &= (uint16_t)~0x1000;
    if (!((*game_current_turn + v) & 3)) {
        u->state &= ~0x800u;
        u->flags &= (uint16_t)~(u->flags & 2 ? 2 : 1);
    }
    if ((u->order == ORDER_SENTRY || u->order == ORDER_HOLD) && u->order_turns && !--u->order_turns) u->order = 0;
    const struct map_tile *t = tile(x, y);
    if (u->state & VEH_STATE_AWAY) {
        int triad = proto_triad(u->proto);
        if (has_base(t) || (triad == TRIAD_LAND && (t->bits & MAP_BUNKER)) || (triad == TRIAD_AIR && (t->bits & MAP_AIRBASE)))
            u->state &= ~(uint32_t)VEH_STATE_AWAY;
    }
    if (u->proto == PROTO_FUNGAL_TOWER) u->faction = 0;
    int p = u->proto;
    if (p >= PROTO_BATTLE_OGRE_MK1 && p <= PROTO_BATTLE_OGRE_MK3) return;
    if (p != PROTO_FUNGAL_TOWER && (u->state & VEH_STATE_HAS_MOVED)) return;
    if (!u->damage) return;
    int value = 1;
    if (p != PROTO_FUNGAL_TOWER) {
        if (is_native(p) && (t->bits & MAP_FUNGUS) && altitude(t) >= ALT_SHELF) value = 2;
        if (whose_territory(faction, x, y, NULL, 0) == faction) value++;
        int triad = proto_triad(p);
        if (triad == TRIAD_AIR && (t->bits & MAP_AIRBASE)) value++;
        if (triad == TRIAD_LAND && (t->bits & MAP_BUNKER)) value++;
    }
    int base = game_base_at(x, y);
    if (base >= 0 && !(game_bases[base].state & BASE_STATE_RIOTS)) {
        value++;
        if (is_native(p)) {
            if (game_breed_mod(base, game_bases[base].faction)) value = u->damage;
        } else {
            int triad = proto_triad(p);
            int fac = triad == TRIAD_SEA ? FAC_NAVAL_YARD : triad == TRIAD_AIR ? FAC_AEROSPACE_COMPLEX : FAC_COMMAND_CENTER;
            int sp = triad == TRIAD_SEA ? SP_MARITIME_CONTROL_CENTER : triad == TRIAD_AIR ? SP_CLOUDBASE_ACADEMY : SP_COMMAND_NEXUS;
            if (has_facility(base, fac) || holds_project(sp, faction)) value = u->damage;
        }
    }
    int repair_unit = 0;
    if (proto_triad(p) == TRIAD_LAND && game_veh_cargo(v) && (u->order == ORDER_SENTRY || altitude(t) < ALT_LAND)) {
        int top = v;
        while (game_vehs[top].prev_in_stack >= 0) top = game_vehs[top].prev_in_stack;
        for (int j = top; j >= 0; j = game_vehs[j].next_in_stack)
            if (j != v && game_has_abil(game_vehs[j].proto, ABILITY_REPAIR)) repair_unit = 1;
        if (repair_unit) value *= 2;
    }
    if (holds_project(SP_NANO_FACTORY, faction)) value = u->damage;
    int reactor = game_protos[p].reactor, repair = reactor * value, floor = 0;
    if (faction && !repair_unit && base < 0 && !holds_project(SP_NANO_FACTORY, faction)) {
        floor = reactor * 2;
        if (floor && (t->bits & MAP_FUNGUS) && altitude(t) >= ALT_SHELF) {
            if (is_native(p)) floor = 0;
            if (holds_project(SP_XENOEMPATHY_DOME, faction)) repair += reactor, floor = 0;
        }
    }
    int left = game_range(u->damage - repair, floor, 999);
    if (left >= u->damage) left = u->damage;
    u->damage = (uint8_t)left;
    left &= 0xff;
    if (left > floor || !is_human(faction) || u->order != ORDER_SENTRY || left == damage) return;
    if (proto_triad(p) != TRIAD_LAND || altitude(t) >= ALT_LAND) u->order = 0;
}

// A faction's units at the start of its turn: moves restored, some orders
// run down, and repairs; then the player's view of the units it can see
// outside bases redrawn.
void repair_phase(int faction) {
    game_factions[faction].odp_deployed = 0;
    for (int v = 0; v < *game_veh_count; v++)
        if (game_vehs[v].faction == faction) repair_veh(faction, v);
    for (int v = 0; v < *game_veh_count; v++) {
        const struct veh *u = &game_vehs[v];
        if (u->faction != faction || has_base(tile(u->x, u->y))) continue;
        int player = *game_current_faction;
        if (u->faction == player || ((u->visibility >> player) & 1)) game_draw_tile(u->x, u->y, -1);
    }
    game_do_all_draws();
}

// ---- A faction's whole turn --------------------------------------------------

enum {
    GAME_STATE_COUNCIL_SHOWN = 0x4000, GAME_STATE_COUNCIL_CONVENED = 0x10000,
    RULES_VICTORY_ECONOMIC = 0x4, PREF_AUTOSAVE = 0x2,
    BASE_STATE_HURRY_ = 0x40000000, BASE_STATE_UNK_1 = 0x1,
    PROTO_COLONY_POD = 0, PROTO_SEA_ESCAPE_POD = 21,
};

// A full turn: not one of the turns of a game played by e-mail where a
// faction only answers (GAME_STATE_DONE set without FINAL_SCORE_DONE).
static int full_turn(uint32_t state) { return !(state & GAME_STATE_DONE) || (state & GAME_STATE_FINAL_SCORE_DONE); }

// The AI corners the energy market when it can pay: the win comes the
// rules' number of turns later, and every faction is told. Unlike Thinker,
// the original neither waits for an earlier attempt to end nor checks that
// the game is still undecided.
static void corner_energy_market(int faction) {
    struct faction_data *f = &game_factions[faction];
    if (f->energy_credits <= corner_market(faction)) return;
    if (!has_tech(game_rules_basic->tech_economic_victory, faction) || !(*game_rules & RULES_VICTORY_ECONOMIC)) return;
    f->corner_market_turn = *game_current_turn + game_rules_basic->turns_corner_market;
    f->corner_market_cost = corner_market(faction);
    f->energy_credits -= corner_market(faction);
    struct faction_def *d = &game_faction_defs[faction];
    *game_parse_gender = d->leader_female, *game_parse_plural = 0;
    game_parse_says(0, d->leader_title, -1, -1);
    *game_parse_gender = d->leader_female, *game_parse_plural = 0;
    game_parse_says(1, d->leader_name, -1, -1);
    *game_parse_gender = d->noun_gender, *game_parse_plural = d->noun_plural;
    game_parse_says(2, d->noun, -1, -1);
    game_parse_says(3, d->adjective, -1, -1);
    game_parse_num(0, game_game_year(f->corner_market_turn));
    game_popp(*game_script_file, text(0x68ba38), 0, text(0x68ba28), 0);
}

// A faction's turn: its social models, repairs and bases, and on a full
// turn its energy, the AI's diplomacy, strategy and social engineering
// and its try at the energy market. Then its bases' rush flags are
// cleared and what it spent rushing paid; a faction with neither bases
// nor colony pods is out. On a full turn the player is told once that
// the Planetary Council can be called, and the AI may call it; and the
// player's game is saved when the preferences say so. Unlike Thinker,
// the original picks no research here.
void faction_upkeep(int faction) {
    *game_control_upkeep_a = 1;
    social_upkeep(faction);
    game_do_all_non_input();
    repair_phase(faction);
    game_do_all_non_input();
    production_phase(faction);
    game_do_all_non_input();
    if (full_turn(*game_state)) {
        allocate_energy(faction);
        game_do_all_non_input();
        game_enemy_diplomacy(faction);
        game_do_all_non_input();
        game_enemy_strategy(faction);
        game_do_all_non_input();
        game_social_ai(faction, -1, -1, -1, -1, 0);
        game_do_all_non_input();
        if (!is_human(faction)) corner_energy_market(faction);
    }
    for (int b = 0; b < *game_base_count; b++)
        if (game_bases[b].faction == faction) game_bases[b].state &= ~(uint32_t)(BASE_STATE_HURRY_ | BASE_STATE_UNK_1);
    struct faction_data *f = &game_factions[faction];
    f->energy_credits -= f->hurry_cost_total;
    f->hurry_cost_total = 0;
    if (f->energy_credits < 0) f->energy_credits = 0;
    if (!f->base_count && !f->units_active[PROTO_COLONY_POD] && !f->units_active[PROTO_SEA_ESCAPE_POD])
        game_eliminate_player(faction, 0);
    *game_control_upkeep_a = 0;
    int32_t *path = game_path;
    path[6] = path[7] = -1;  // the path finder's destination
    uint32_t state = *game_state;
    if (full_turn(state)) {
        if (faction == *game_current_faction && !(state & (GAME_STATE_COUNCIL_SHOWN | GAME_STATE_COUNCIL_CONVENED)) &&
            game_can_call_council(faction, 0) && !(*game_state & GAME_STATE_DONE)) {
            *game_state |= GAME_STATE_COUNCIL_SHOWN;
            game_popp(*game_script_file, text(0x68ba58), 0, text(0x68ba48), 0);
        }
        if (!is_human(faction)) game_call_council(faction);
    }
    if (!*game_net_multiplayer && (*game_preferences & PREF_AUTOSAVE) && faction == *game_current_faction)
        game_auto_save();
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
SHADOW_WRITER_STALE(UPKEEP, base_drones, WRITES_DRONES)
SHADOW_WRITER_STALE(UPKEEP, drone_riot, hooks_writes_game(&w_))
#define WRITES_DOCTORS                                                                              \
    WRITES_BASE; WRITES(game_base_globals, BASE_GLOBALS_END - BASE_GLOBALS_START);                    \
        WRITES(game_vehs, *game_veh_count * sizeof(struct veh));                                     \
        WRITES(game_computed_base_id, sizeof(*game_computed_base_id))
SHADOW_WRITER(UPKEEP, void, base_doctors, (void), (), WRITES_DOCTORS, "%s", "")
// Disbanding units writes widely.
#define WRITES_SUPPORT_CHECK                                                                                  \
    if (*game_base_forces_upkeep > B->intake_2[RESOURCE_MINERAL] || (B->state & BASE_STATE_RIOTS)) hooks_writes_game(&w_); \
    else WRITES(B, sizeof(struct base))
SHADOW_WRITER(UPKEEP, void, base_check_support, (void), (), WRITES_SUPPORT_CHECK, "%s", "")
SHADOW_WRITER(UPKEEP, void, base_hurry, (void), (), WRITES_BASE, "%s", "")
// Naming reads a file, and the C runtime's file buffers come from the heap,
// which a check can't put back. But all it writes is the name and the
// faction's two counters, so its check runs both with those alone kept
// apart, unprotected.
static void (*orig_name_base)(int faction, char *name, int save, int sea);
static struct hook_stats stats_name_base = {"name_base"};
static void check_name_base(int faction, char *name, int save, int sea) {
    struct faction_data *f = &game_factions[faction];
    int32_t land = f->base_name_offset, seas = f->base_sea_name_offset;
    char want[25], got[25];
    memcpy(want, name, sizeof want);
    memcpy(got, name, sizeof got);
    hooks_nested++;
    orig_name_base(faction, want, save, sea);
    hooks_nested--;
    int32_t want_land = f->base_name_offset, want_seas = f->base_sea_name_offset;
    f->base_name_offset = land, f->base_sea_name_offset = seas;
    name_base(faction, got, save, sea);
    if (memcmp(want, got, sizeof want) || f->base_name_offset != want_land || f->base_sea_name_offset != want_seas)
        hooks_mismatch(&stats_name_base, "(%d, %d, %d) = \"%.24s\" %d %d, original \"%.24s\" %d %d", faction, save, sea,
                       got, f->base_name_offset, f->base_sea_name_offset, want, want_land, want_seas);
    f->base_name_offset = want_land, f->base_sea_name_offset = want_seas;
    memcpy(name, want, sizeof want);
}
static void shadow_name_base(int faction, char *name, int save, int sea) {
    if (hooks_nested) {
        orig_name_base(faction, name, save, sea);
        return;
    }
    if (shadow_bypass()) {
        name_base(faction, name, save, sea);
        return;
    }
    stats_name_base.calls++;
    shadow_tick(UPKEEP);
    check_name_base(faction, name, save, sea);
}
// Goals, roads and (instantly) the map: all of the game's state, and the
// tables of the game's path finder (from the heap, sized by the map), which
// base_connect() leaves its last search in and reads again.
static void writes_path(struct hook_writes *w) {
    struct { int32_t *map; int16_t *x, *y; } *p = game_path;
    size_t area = (size_t)*game_map_area;
    if (p->map) hooks_writes_add(w, p->map, area * 4);
    if (p->x) hooks_writes_add(w, p->x, area * 2);
    if (p->y) hooks_writes_add(w, p->y, area * 2);
}
SHADOW_WRITER(UPKEEP, void, base_terraform, (int a), (a), (hooks_writes_game(&w_), writes_path(&w_)), "%d", a)
// Fungus, native life, the sea and volcanoes write widely.
SHADOW_WRITER(UPKEEP, void, base_ecology, (void), (), hooks_writes_game(&w_), "%s", "")
// A base's turn writes all over the game, the path finder's tables too
// (base_terraform). A human's base runs the original unchecked, as its
// production may ask the player (see base_production), and so does a base
// about to be renamed, as naming reads a file (see name_base). Only a
// human's base can revolt, so a check never reaches drone_riot's stale
// revolt chances, which would differ between the two runs.
SHADOW_WRITER_CHECK(int, base_upkeep, (int a), (a), orig_base_upkeep(a), (hooks_writes_game(&w_), writes_path(&w_)),
                    "%d", a)
static int shadow_base_upkeep(int a) {
    if (hooks_nested) return orig_base_upkeep(a);
    if (shadow_bypass()) return base_upkeep(a);
    stats_base_upkeep.calls++;
    shadow_tick(UPKEEP);
    const struct base *b = &game_bases[a];
    if (is_human(b->faction) || (b->state & BASE_STATE_RENAME)) return orig_base_upkeep(a);
    return check_base_upkeep(a);
}
// A faction's turn: the same, for all its bases. A human's runs the
// original unchecked, and so does one with a base about to be renamed.
// Its check covers base_upkeep() for AI bases, which the sweep also checks
// on its own.
// Energy allocation: the AI's writes the faction and, with bases in riots,
// works them out again. The player's warning uses the answer to a popup,
// so a human's runs the original unchecked.
SHADOW1(UPKEEP, int, energy_limit, int)
SHADOW_WRITER_CHECK(void, allocate_energy, (int a), (a), orig_allocate_energy(a), hooks_writes_game(&w_), "%d", a)
static void shadow_allocate_energy(int a) {
    if (hooks_nested) {
        orig_allocate_energy(a);
        return;
    }
    if (shadow_bypass()) {
        allocate_energy(a);
        return;
    }
    stats_allocate_energy.calls++;
    shadow_tick(UPKEEP);
    if (is_human(a)) orig_allocate_energy(a);
    else check_allocate_energy(a);
}
// Repairs write the faction's units.
SHADOW_WRITER(UPKEEP, void, repair_phase, (int a), (a), hooks_writes_game(&w_), "%d", a)
static int renames_base(int faction) {
    for (int b = 0; b < *game_base_count; b++)
        if (game_bases[b].faction == faction && (game_bases[b].state & BASE_STATE_RENAME)) return 1;
    return 0;
}
SHADOW_WRITER_CHECK(void, production_phase, (int a), (a), orig_production_phase(a),
                    (hooks_writes_game(&w_), writes_path(&w_)), "%d", a)
static void shadow_production_phase(int a) {
    if (hooks_nested) {
        orig_production_phase(a);
        return;
    }
    if (shadow_bypass()) {
        production_phase(a);
        return;
    }
    stats_production_phase.calls++;
    shadow_tick(UPKEEP);
    if (is_human(a) || renames_base(a)) orig_production_phase(a);
    else check_production_phase(a);
}

// A faction's whole turn is checked with all it calls, the original's run
// calling the original's phases and the AI's code twice. As for
// production_phase(), the original runs unchecked for a human faction or
// one about to rename a base; and also while any faction is human, since
// the AI's diplomacy and the Council would put questions to the player
// that a check holds back unanswered, and before an autosave, which
// writes a file.
SHADOW_WRITER_CHECK(void, faction_upkeep, (int a), (a), orig_faction_upkeep(a),
                    (hooks_writes_game(&w_), writes_path(&w_)), "%d", a)
static void shadow_faction_upkeep(int a) {
    if (hooks_nested) {
        orig_faction_upkeep(a);
        return;
    }
    if (shadow_bypass()) {
        faction_upkeep(a);
        return;
    }
    stats_faction_upkeep.calls++;
    shadow_tick(UPKEEP);
    int autosave = !*game_net_multiplayer && (*game_preferences & PREF_AUTOSAVE) && a == *game_current_faction;
    if (*game_human_factions || renames_base(a) || autosave) orig_faction_upkeep(a);
    else check_faction_upkeep(a);
}


static int slice;

// Riots: a first turn, a later one (which may take a facility), and one
// that has to end in a revolt, the faction made human, without facilities
// and under a POLICE that allows it, with made-up stale revolt chances: a
// few for factions in play, or, when no faction has REVOLT (whose chances
// would then be divided by zero), one that makes them negative.
static void sweep_riot(int b, struct base saved) {
    struct base *base = &game_bases[b];
    int faction = base->faction;
    struct faction_data *f = &game_factions[faction], saved_f = *f;
    uint8_t human = *game_human_factions;
    struct hook_stale stale = {{0}};
    for (int k = 0; k < FACTION_COUNT - 1; k++) HOOK_STALE_AT(&stale, 0x5c - 4 * k) = (uint32_t)-1;
    base->state &= ~(uint32_t)BASE_STATE_RIOTS;
    check_drone_riot(&stale);
    *base = saved;
    base->state |= BASE_STATE_RIOTS;
    f->se_pending[SE_POLICE] = -1 - slice % 3;
    check_drone_riot(&stale);
    *base = saved;
    *f = saved_f;
    if (f->base_count > 1 && !is_alien(faction) && !has_facility(b, FAC_HEADQUARTERS)) {
        int revolt = 0;
        for (int i = 1; i < FACTION_COUNT; i++)
            for (int j = 0; j < game_faction_defs[i].bonus_count; j++)
                revolt |= game_faction_defs[i].bonus_id[j] == BONUS_REVOLT;
        for (int k = 0; k < FACTION_COUNT - 1; k++) {
            int i = k + 1, alive = game_factions[i].base_count > 0;
            int32_t w = revolt ? (alive && (k + slice) % 3 == 0 ? 10 + 5 * k : -1) : (k == slice % 7 ? 200 : -1);
            HOOK_STALE_AT(&stale, 0x5c - 4 * k) = (uint32_t)w;
        }
        *game_human_factions |= (uint8_t)(1 << faction);
        f->se_pending[SE_POLICE] = -6;
        memset(base->facilities_built, 0, sizeof base->facilities_built);
        base->state |= BASE_STATE_RIOTS;
        base->assimilation_turns = 0;
        if (slice & 1) base->faction_former = (uint8_t)(1 + (faction + slice / 2) % (FACTION_COUNT - 1));
        check_drone_riot(&stale);
        *game_human_factions = human;
        *base = saved;
        *f = saved_f;
    }
}

// Ecological damage at a few bases a turn: enough for fungus, and as much
// before as native life, warming (also for a human) and the volcano need,
// with the AI let off or the faction spared at times.
static void sweep_ecology(int b, struct base saved) {
    struct base *base = &game_bases[b];
    struct faction_data *f = &game_factions[base->faction], saved_f = *f;
    uint8_t human = *game_human_factions;
    uint32_t state = *game_state;
    for (int k = 0; k < 3; k++) {
        base->eco_damage = 30 + (b * 7 + slice * 3 + k * 40) % 90;
        f->clean_minerals = (b + slice + k * 5) % 14;
        f->flags ^= (uint32_t)((b + slice + k) % 5 == 0) * FACTION_FLAG_ECO_SPARED;
        if ((b + k) % 3 == 0) *game_human_factions |= (uint8_t)(1 << base->faction);
        if ((slice + k) % 4 == 0) *game_state &= ~(uint32_t)GAME_STATE_VOLCANO;
        check_base_ecology();
        *game_human_factions = human;
        *game_state = state;
        *base = saved;
        *f = saved_f;
    }
}

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
    // Names for each faction, land and sea, the next and some way down
    // the lists (and past their end).
    for (int f = 1; f < FACTION_COUNT; f++) {
        struct faction_data *fd = &game_factions[f];
        int32_t land = fd->base_name_offset, seas = fd->base_sea_name_offset;
        char name[25] = {0};
        for (int k = 0; k < 4; k++) {
            if (k >= 2) fd->base_name_offset = land + 7 * k + slice % 40, fd->base_sea_name_offset = seas + k + slice % 12;
            check_name_base(f, name, k & 1, (k + slice) & 1);
        }
        fd->base_name_offset = land, fd->base_sea_name_offset = seas;
    }
    // Each AI faction's energy allocation under made-up POLICE, allocations
    // and priorities, with a base in riots at times; and each faction's
    // repairs with made-up damage, orders and moves.
    for (int i = 1; i < FACTION_COUNT; i++) {
        struct faction_data *fd = &game_factions[i], saved_f = *fd;
        if (!fd->base_count) continue;
        // The original reads the current base as it loops over the
        // faction's; in the game production_phase() has set one.
        for (int b = 0; b < *game_base_count; b++)
            if (game_bases[b].faction == i) B = &game_bases[b], ID = b;
        if (!is_human(i) && B) {
            check_allocate_energy(i);
            for (int k = 0; k < 3; k++) {
                fd->se_pending[SE_POLICE] = (i + slice + k) % 7 - 4;
                fd->alloc_psych = (i + k + slice) % 5, fd->alloc_labs = (i * 3 + k + slice) % 7;
                fd->ai_tech = (k + slice) & 1, fd->ai_wealth = (i + k) % 3 == 0, fd->ai_power = (i + slice + k) % 4 == 0;
                fd->energy_credits = (i * 131 + slice * 17 + k * 400) % 1500;
                fd->tech_research_id = k == 2 ? 88 : fd->tech_research_id;
                int riot = -1;
                for (int b = 0; b < *game_base_count && k == 1; b++)
                    if (game_bases[b].faction == i) riot = b;
                uint32_t state = riot >= 0 ? game_bases[riot].state : 0;
                if (riot >= 0) game_bases[riot].state |= BASE_STATE_RIOTS;
                check_allocate_energy(i);
                if (riot >= 0) game_bases[riot].state = state;
                *fd = saved_f;
            }
        }
        check_repair_phase(i);
        size_t vehs_size = (size_t)*game_veh_count * sizeof(struct veh);
        struct veh *saved_vehs = malloc(vehs_size);
        if (!saved_vehs) continue;
        memcpy(saved_vehs, game_vehs, vehs_size);
        for (int v = 0; v < *game_veh_count; v++) {
            struct veh *u = &game_vehs[v];
            if (u->faction != i) continue;
            u->damage = (uint8_t)((v * 7 + slice * 3) % 30);
            if ((v + slice) % 3 == 0) u->state ^= VEH_STATE_HAS_MOVED;
            if ((v + slice) % 4 == 0) u->order = ORDER_SENTRY, u->order_turns = (int16_t)((v + slice) % 3);
            if ((v + slice) % 5 == 0) u->state |= VEH_STATE_AWAY;
        }
        uint8_t human = *game_human_factions;
        if (slice & 1) *game_human_factions |= (uint8_t)(1 << i);
        check_repair_phase(i);
        *game_human_factions = human;
        memcpy(game_vehs, saved_vehs, vehs_size);
        free(saved_vehs);
    }
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
        struct hook_stale stale = {{0}};
        check_base_drones(&stale);
        *base = saved;
        *f = saved_f;
        // Rushing, with more reserves and less built; and doctors and
        // unpaid units in riots and short of minerals, for a few bases a
        // turn, as those checks cover the whole game.
        check_base_hurry();
        f->energy_credits = (b * 97 + slice * 31) % 6000;
        base->minerals = 1 + (b + slice) % 20;
        check_base_hurry();
        *base = saved;
        *f = saved_f;
        check_base_doctors();
        // Doctors no longer needed, and riots with workers to spare.
        base->specialist_total = 2 + (b + slice) % 3, base->specialist_adjust = 0;
        base->state &= ~(uint32_t)BASE_STATE_RIOTS;
        check_base_doctors();
        int food_ok = *game_base_food_ok;
        *game_base_food_ok = 1;
        base->state |= BASE_STATE_RIOTS;
        base->drone_total = base->talent_total + 1;
        check_base_doctors();
        *game_base_food_ok = food_ok;
        *base = saved;
        *f = saved_f;
        if ((b + slice) % 8 == 0) {
            check_base_check_support();
            base->state |= BASE_STATE_RIOTS;
            base->drone_total = base->talent_total + 1 + (b + slice) % 2;
            check_base_doctors();
            check_base_check_support();
            *base = saved;
            int upkeep = *game_base_forces_upkeep;
            *game_base_forces_upkeep = base->intake_2[RESOURCE_MINERAL] + 1 + slice % 3;
            check_base_check_support();
            *game_base_forces_upkeep = upkeep;
            *base = saved;
        }
        if ((b + slice) % 6 == 0) sweep_riot(b, saved);
        if ((b + slice) % 5 == 0) sweep_ecology(b, saved);
        // A whole base's turn, for a few AI bases a turn: the game's calls
        // are checked as part of their faction's.
        if ((b + slice) % 16 == 0 && !is_human(base->faction) && !(base->state & BASE_STATE_RENAME)) {
            check_base_upkeep(b);
            *base = saved;
            *f = saved_f;
        }
        // Terraforming plans, for a human's governor too, and made at once.
        if ((b + slice) % 4 == 0) {
            uint8_t human = *game_human_factions;
            int32_t prefs = *game_preferences, more = *game_more_preferences;
            check_base_terraform(0);
            check_base_terraform(1);
            *game_human_factions |= (uint8_t)(1 << base->faction);
            base->governor = (base->governor & ~0x0f000000u) | (0x1000000u << (slice % 5)) | GOVERNOR_ACTIVE;
            *game_preferences ^= 0x2020000 * (slice & 1), *game_more_preferences ^= 0x400020 * ((slice >> 1) & 1);
            check_base_terraform(0);
            *game_human_factions = human, *game_preferences = prefs, *game_more_preferences = more;
            *base = saved;
        }
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
    HOOK(0x4f4fb0, drone_riot, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x5c),           // sub esp, 92
    HOOK(0x4f5f30, base_doctors, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x18),          // sub esp, 24
    HOOK(0x4f4830, base_check_support, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x20),    // sub esp, 32
    HOOK(0x4f3fe0, base_hurry, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x1c),            // sub esp, 28
    HOOK(0x4f67f0, base_ecology, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x3c),          // sub esp, 60
    HOOK(0x4e4090, name_base, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x54),             // sub esp, 84
    HOOK(0x4ecbf0, base_terraform, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0x04, 0x01, 0x00, 0x00),  // sub esp, 0x104
    HOOK(0x4f79c0, base_upkeep, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x0c),           // sub esp, 12
    HOOK(0x526e70, production_phase, PUSH_EBP_MOV_EBP_ESP, 0x51, 0x53, 0x8b, 0x5d, 0x08),  // push ecx; push ebx; mov ebx, [ebp+8]
    HOOK(0x445440, energy_limit, 0xb8, 0x0a, 0x00, 0x00, 0x00),                    // mov eax, 10
    HOOK(0x5267b0, allocate_energy, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x18),       // sub esp, 24
    HOOK(0x526030, repair_phase, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x28),          // sub esp, 40
    HOOK(0x527290, faction_upkeep, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56, 0x57),         // push ebx; push esi; push edi
};
// The game calls base_production() only from base_upkeep(), whose check
// covers it, so for AI bases it is never checked on its own: its sweep
// runs with this one.
extern struct hook_module production_module;
struct hook_module upkeep_module = {"upkeep", upkeep_hooks, HOOK_COUNT(upkeep_hooks), upkeep_sweep, &production_module, -1};
