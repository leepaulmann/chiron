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

// ---- Doctors, unpaid units and rushing ----------------------------------------

#include <stdlib.h>
#include <string.h>

static inline int is_human(int faction) { return (*game_human_factions >> faction) & 1; }
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

// Distance as the game estimates it: the longer side and half the shorter.
static int vector_dist(int dx, int dy) {
    int a = abs(dx), b = abs(dy), hi = a > b ? a : b, lo = a < b ? a : b;
    return hi - ((((a + b) >> 1) - lo + 1) >> 1);
}

static int folded_dx(int dx) {
    dx = abs(dx);
    if (!(*game_map_flat & 1) && dx > *game_map_half_width) dx = *game_map_width - dx;
    return dx;
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
                score = vector_dist(folded_dx(u->x - B->x), u->y - B->y);
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
        int dist = (folded_dx(u->x - B->x) + abs(u->y - B->y)) >> 1;
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
            const char *name = (const char *)&game_faction_defs[faction] + 4;
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
    HOOK(0x4f5f30, base_doctors, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x18),          // sub esp, 24
    HOOK(0x4f4830, base_check_support, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x20),    // sub esp, 32
    HOOK(0x4f3fe0, base_hurry, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x1c),            // sub esp, 28
};
struct hook_module upkeep_module = {"upkeep", upkeep_hooks, HOOK_COUNT(upkeep_hooks), upkeep_sweep, NULL, -1};
