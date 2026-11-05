// Bases: the rest of the end of a base's turn. Reimplemented from
// terranx.exe 2.0's disassembly; names follow OpenSMACX (Brendan Casey,
// GPL-3.0-or-later) and Thinker (MIT).
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "game.h"
#include "map.h"
#include "mapgen.h"
#include "shadow.h"
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
            if (game_chassis[game_protos[u->proto].chassis].range) u->unk_25 = 0;
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
// Fungus, native life, the sea and volcanoes write widely.
SHADOW_WRITER(UPKEEP, void, base_ecology, (void), (), hooks_writes_game(&w_), "%s", "")

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
};
struct hook_module upkeep_module = {"upkeep", upkeep_hooks, HOOK_COUNT(upkeep_hooks), upkeep_sweep, NULL, -1};
