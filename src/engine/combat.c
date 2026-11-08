// Combat and probe team helpers: units killed or captured in battle, the
// diplomatic cost of an attack, what a probe team steals and its odds,
// interceptors, orbital and flechette defenses, and planet busters.
// Reimplemented from terranx.exe 2.0's disassembly; names follow Thinker
// (MIT) and OpenSMACX (Brendan Casey, GPL-3.0-or-later).
#include "combat.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "base.h"
#include "build.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "mapgen.h"
#include "score.h"
#include "shadow.h"
#include "spot.h"
#include "strategy.h"
#include "tech.h"
#include "veh.h"

enum {
    PLAN_PLANET_BUSTER_ = 5, PLAN_ARTIFACT = 12,
    ABL_AIR_SUPERIORITY = 0x20, ABL_CARRIER = 0x80,
    FAC_HEADQUARTERS_ = 1, FAC_COVERT_OPS_CENTER = 34, FAC_FLECHETTE_DEFENSE = 39,
    MAP_AIRBASE = 0x40000,
    PREF_NO_INTERLUDES = 0x40000,  // game_preferences: no story interludes
    // Treaty bits an attack breaks, and those it sets.
    TREATY_TRUCE = 0x4, TREATY_BROKEN_BY_ATTACK = TREATY_PACT | TREATY_TREATY | TREATY_TRUCE,
    // What a unit forgets when it changes hands or is moved by the game.
    VEH_STATE_MOVED_OFF = 0x2000000 | 0x1000000 | 0x4000 | 0x200,
    // Squares around a missile's target whose bases' flechette defenses
    // count: the first 25 of the radius table.
    SHOOT_SQUARES = 25,
};

static inline uint32_t bit(int n) { return 1u << (n & 31); }
static inline const struct unit_proto *proto_of(int proto) { return &game_protos[proto]; }
static inline int map_flat_(void) { return *game_map_flat & 1; }
static inline int iabs_(int v) { return v < 0 ? -v : v; }
static struct map_tile *tile_at(int x, int y) { return &(*game_map)[*game_map_half_width * y + (x >> 1)]; }

// x brought back onto a wrapping map, from up to a width outside it.
static int wrap_x(int x) {
    if (!map_flat_()) {
        if (x < 0) x += *game_map_width;
        else if (x >= *game_map_width) x -= *game_map_width;
    }
    return x;
}
static int on_the_map(int x, int y) { return y >= 0 && y < *game_map_height && x >= 0 && x < *game_map_width; }

// Base b's name, also for b = -1 as the originals read it (the bytes
// before the base table).
static char *base_name(int b) { return (char *)game_bases + (intptr_t)b * (intptr_t)sizeof(struct base) + offsetof(struct base, name); }
// A unit's faction, also for veh = -1.
static int veh_faction_any(int veh) {
    return ((uint8_t *)game_vehs)[(intptr_t)veh * (intptr_t)sizeof(struct veh) + offsetof(struct veh, faction)];
}
static int has_facility(int base, int facility) {
    int offset;
    uint32_t mask;
    bitmask(facility, &offset, &mask);
    return (game_bases[base].facilities_built[offset] & (uint8_t)mask) != 0;
}
static char *at_text(uint32_t addr) { return (char *)game_at(addr); }
static void say(int slot, char *text) { game_parse_says(slot, text, -1, -1); }
static void say_adjective(int slot, int faction) { say(slot, game_faction_defs[faction].adjective); }
static void say_noun(int slot, int faction) {
    const struct faction_def *d = &game_faction_defs[faction];
    *game_default_gender = d->noun_gender;
    *game_default_plural = d->noun_plural;
    say(slot, (char *)d->noun);
}
static void pop(uint32_t label, uint32_t picture) { game_popp(*game_script_file, at_text(label), 0, at_text(picture), 0); }

// A unit's hit points: one for an artifact, else ten for each level of
// reactor (1 to 100).
static int max_hp(const struct veh *V) {
    const struct unit_proto *P = proto_of(V->proto);
    return P->plan == PLAN_ARTIFACT ? 1 : game_range(P->reactor, 1, 100) * 10;
}

// ---- Units killed in combat ---------------------------------------------------

// The unit is counted lost to its faction. An artifact whose attacker
// (not a missile) is a faction's unit on the map is taken: on land, or at
// sea when the attacker is on a transport or the artifact is an
// objective; it changes hands where the attacker stands. Otherwise it
// dies: an objective counts for the attacking faction; the Assistant Worm
// killed by a unit with a base (its home, or the nearest of its faction)
// marks the base and the faction and plays an interlude, unless they are
// turned off; a native unit killed by a faction's unit earns ten times
// one more than its morale. The original reads the attacker's faction
// for that last test even when there is no attacker (veh_att -1); Thinker
// checks it.
int battle_kill(int veh, int *killed, int *captured, int *credits, int veh_att, int faction_att) {
    struct veh *V = &game_vehs[veh];
    int faction = V->faction, proto = V->proto;
    game_factions[faction].units_lost[proto]++;
    if (veh_att >= 0 && veh_att != veh) {
        const struct veh *A = &game_vehs[veh_att];
        if (A->faction && on_the_map(A->x, A->y) && proto_of(proto)->plan == PLAN_ARTIFACT &&
            !game_chassis[proto_of(A->proto)->chassis].missile &&
            ((tile_at(A->x, A->y)->climate & 0xe0) >= ALT_SHORE_LINE << 5 || stack_check(veh_att, 8, -1, -1, -1) > 0 ||
             (V->flags & VEH_FLAG_UNK_20))) {
            veh_lift(veh);
            int taker = A->faction;
            V->damage = 0;
            V->faction = (uint8_t)taker;
            V->order = 0;
            game_factions[faction].units_active[proto]--;
            V->state &= ~(uint32_t)VEH_STATE_MOVED_OFF;
            game_factions[taker].units_active[proto]++;
            veh_drop(veh, A->x, A->y);
            veh_skip(veh);
            if (captured) ++*captured;
            return 0;
        }
    }
    if ((V->flags & VEH_FLAG_UNK_20) && faction_att >= 0) game_factions[faction_att].objective_bonus++;
    if ((V->state & VEH_STATE_UNK_800000) && veh_att >= 0 && !(*game_preferences & PREF_NO_INTERLUDES)) {
        const struct veh *A = &game_vehs[veh_att];
        int b = A->home_base;
        if (b < 0) b = base_find_2(A->x, A->y, A->faction);
        if (b >= 0) {
            int f = A->faction;
            const struct faction_def *d = &game_faction_defs[f];
            *game_default_plural = 0;
            game_bases[b].state |= BASE_STATE_UNK_40000;
            game_factions[f].flags |= FACTION_FLAG_UNK_1000;
            *game_default_gender = d->leader_female;
            say(6, (char *)d->leader_name);
            say_noun(7, f);
            game_interlude(6, game_bases[b].name, 1, 0);
        }
    }
    if (!V->faction && veh_faction_any(veh_att) && credits) *credits += 10 * (morale_veh(veh, 1, 0) + 1);
    if (killed) ++*killed;
    veh_kill(veh);
    return 1;
}

// The units of a stack from its head; a unit killed moves the later units
// (and the attacker) down a slot.
static void kill_stack(int veh, int *killed, int *captured, int *credits, int veh_att, int faction_att, int chaff) {
    if (veh < 0) return;
    while (game_vehs[veh].prev_in_stack >= 0) veh = game_vehs[veh].prev_in_stack;
    while (veh >= 0) {
        const struct veh *V = &game_vehs[veh];
        int next = V->next_in_stack;
        if (!chaff || max_hp(V) - V->damage <= 0 || !V->faction) {
            if (battle_kill(veh, killed, captured, credits, veh_att, faction_att)) {
                if (veh_att > veh) veh_att--;
                if (next > veh) next--;
            }
        }
        veh = next;
    }
}

void battle_kill_stack(int veh, int *killed, int *captured, int *credits, int veh_att, int faction_att) {
    kill_stack(veh, killed, captured, credits, veh_att, faction_att, 0);
}

// The stack's native units and those with no hit points left.
void battle_kill_chaff(int veh, int *killed, int *captured, int *credits, int veh_att, int faction_att) {
    kill_stack(veh, killed, captured, credits, veh_att, faction_att, 1);
}

// ---- Diplomacy and probe teams --------------------------------------------------

// The victim's truce, treaty or pact with the attacker (as the victim
// holds it) is broken, and both are set at vendetta.
int act_of_aggression(int faction, int victim) {
    int broken = 0;
    if (game_factions[victim].diplo_status[faction] & TREATY_BROKEN_BY_ATTACK) {
        game_double_cross(faction, victim, -1);
        broken = 1;
    }
    treaty_on(faction, victim, TREATY_UNK_8000 | TREATY_VENDETTA);
    return broken;
}

// A tech the faction's probe team takes from the victim (or, without
// `steal`, acquires another way): tech_pick() chooses, 9999 for none. In a
// network game the faction playing here only tells the others, and a
// human elsewhere is left alone. The player robbed is told; an AI that
// gained a tech rethinks its bases' builds. Returns 1 when there was
// nothing to take.
int steal_tech(int faction, int victim, int steal) {
    enum { NO_TECH = 9999 };
    if (!faction || !victim) return 0;
    if (*game_net_multiplayer && (*game_human_factions & bit(faction)) && faction != *game_current_faction) return 0;
    int tech = game_tech_pick(faction, 0, victim, at_text(steal ? 0x68a334 : 0x68a340));
    if (tech < 0) tech = NO_TECH;
    if (*game_net_multiplayer && faction == *game_current_faction) {
        game_message_data(0x244c, 0, faction, tech, victim, 0);
        return tech == NO_TECH;
    }
    if (victim == *game_current_faction && tech != NO_TECH) {
        say_noun(0, faction);
        game_battle_text[0] = 0;
        say_tech(game_battle_text, tech, 0);
        say(1, game_battle_text);
        game_netmsg_pop(game_netmsg, at_text(0x68a34c), 5000, 0, NULL);
    }
    game_tech_achieve(faction, tech, victim, 0);
    if (!(*game_human_factions & bit(faction)) && tech != NO_TECH) bases_reset(-1, faction, 0);
    return tech == NO_TECH;
}

// Its share, by citizens, of its faction's energy reserves: credits times
// the base's size over one more than the faction's population.
int steal_energy(int base) {
    const struct base *B = &game_bases[base];
    const struct faction_data *F = &game_factions[B->faction];
    int credits = F->energy_credits;
    if (credits <= 0) return 0;
    return B->pop_size * credits / (F->pop_total + 1);
}

// The first base of the faction with headquarters.
int vulnerable(int faction, int x, int y) {
    for (int b = 0; b < *game_base_count; b++)
        if (game_bases[b].faction == faction && has_facility(b, FAC_HEADQUARTERS_))
            return vector_dist(x, y, game_bases[b].x, game_bases[b].y);
    return 12;
}

// The target's PROBE, with 2 for a Covert Ops Center at the base, kept
// between -2 and 0.
static int probe_defense(int base) {
    int covert = base != -1 && has_facility(base, FAC_COVERT_OPS_CENTER) ? 2 : 0;
    int d = game_factions[*game_probe_target].se[SE_PROBE] + covert;
    return d < -2 ? -2 : d > 0 ? 0 : d;
}

// A probe action of the given difficulty (negative: sure to succeed, and
// that number is returned) by a team of the given morale (at least 1)
// against the target faction and base: the chance of success, 100 less
// difficulty in hundreds over half the morale less the defense plus one,
// and of survival, 100 less one more than the difficulty in hundreds over
// the morale less the defense. The algorithmic enhancement halves the
// losses but against the Hunter-Seeker Algorithm, which halves the
// chances. The original halves the chance of success a second time
// rather than that of survival (as OpenSMACX notes). Both go in parse
// slot `slot` as "s%" or "s%, v%" (the strings from the game's own).
int success_rates(int slot, int morale, int difficulty, int base) {
    char num[16];
    game_battle_text[0] = 0;
    if (morale < 1) morale = 1;
    int success = difficulty;
    if (difficulty < 0) {
        strcat(game_battle_text, at_text(0x6902b8));
    } else {
        int algo = *game_probe_algo, hsa = *game_probe_target_hsa;
        int fail = difficulty * 100 / (morale / 2 - probe_defense(base) + 1);
        if (algo && !hsa) fail /= 2;
        success = 100 - fail;
        if (hsa) success /= 2;
        snprintf(num, sizeof num, "%d", success);
        strcat(game_battle_text, num);
        strcat(game_battle_text, at_text(0x6902c0));
        int loss = (difficulty + 1) * 100 / (morale - probe_defense(base));
        if (algo && !hsa) loss /= 2;
        int survive = 100 - loss;
        if (hsa) success /= 2;
        if (survive != success) {
            strcat(game_battle_text, at_text(0x6902c4));
            snprintf(num, sizeof num, "%d", survive);
            strcat(game_battle_text, num);
            strcat(game_battle_text, at_text(0x6902c8));
        }
    }
    say(slot, game_battle_text);
    return success;
}

// ---- Interceptors -----------------------------------------------------------

// The x distance plus the y distance, as these functions measure.
static int xy_dist(int x1, int y1, int x2, int y2) { return x_dist(x1, x2) + iabs_(y1 - y2); }

// The defender's air superiority unit at a base (of any faction), an
// airbase or on a carrier, within two squares (by x plus y distance
// halved) of the target, with the most weapon (at least 1) times hit
// points left: the last such of the units. It flies to the target. When
// the player is either side, they are told, and the flight is shown a
// square at a time, the step that most nears the target each time.
// Thinker's differs: it rates units otherwise and checks the distance as
// the game's map_range does.
int interceptor(int faction_def, int faction_att, int x, int y) {
    int best = -1, best_score = 0;
    for (int v = 0; v < *game_veh_count; v++) {
        const struct veh *V = &game_vehs[v];
        if (V->faction != faction_def) continue;
        const struct unit_proto *P = proto_of(V->proto);
        if (game_chassis[P->chassis].triad != TRIAD_AIR || !has_abil(V->proto, ABL_AIR_SUPERIORITY)) continue;
        const struct map_tile *t = tile_at(V->x, V->y);
        int at_base = (t->bits & MAP_BASE) && (t->owner_site & 0xf) < 8;
        if (!at_base && !(t->bits & MAP_AIRBASE) && !stack_check(v, 6, ABL_CARRIER, -1, -1)) continue;
        if ((xy_dist(V->x, V->y, x, y) & ~1) > 4) continue;
        int weapon = game_weapons[P->weapon].offense;
        if (weapon < 1) weapon = 1;
        int hp = max_hp(V) - V->damage;
        hp = hp < 0 ? 0 : hp > 9999 ? 9999 : hp;
        if (weapon * hp >= best_score) best_score = weapon * hp, best = v;
    }
    if (best < 0) return 0;
    if (faction_def == *game_current_faction || faction_att == *game_current_faction) {
        say_adjective(0, faction_def);
        int vx = game_vehs[best].x, vy = game_vehs[best].y;
        say(1, base_name(base_find(vx, vy)));
        game_console_focus(game_console, vx, vy, faction_def);
        game_netmsg_pop(game_netmsg, at_text(0x68a0c8), 5000, 0, NULL);
        while (xy_dist(vx, vy, x, y) & ~1) {
            int dir = -1, best_d = 999, bx = 0, by = 0;
            for (int k = 0; k < 8; k++) {
                int nx = wrap_x(vx + game_radius_near_x[k]), ny = vy + game_radius_near_y[k];
                if (!on_the_map(nx, ny)) continue;
                int d = xy_dist(nx, ny, x, y) >> 1;
                if (d < best_d) best_d = d, dir = k, bx = nx, by = ny;
            }
            if (dir < 0) break;
            game_veh_scoot(best, vx, vy, dir, 0);
            vx = bx, vy = by;
        }
    }
    veh_drop(veh_lift(best), x, y);
    struct veh *V = &game_vehs[best];
    V->order = 0;
    V->state &= ~(uint32_t)VEH_STATE_MOVED_OFF;
    return 1;
}

// ---- Orbital defenses and planet busters -----------------------------------------

// Whether a base of a faction other than the attacker near the target
// has flechette defenses, and with `roll` one that brings the missile
// down (an even chance each, in order): that base, or -1.
static int flechette(int faction_att, int x, int y, int roll) {
    for (int i = 0; i < SHOOT_SQUARES; i++) {
        int nx = wrap_x(x + game_radius_x[i]), ny = y + game_radius_y[i];
        if (!on_the_map(nx, ny)) continue;
        int b = base_at(nx, ny);
        if (b < 0 || game_bases[b].faction == faction_att || !has_facility(b, FAC_FLECHETTE_DEFENSE)) continue;
        if (!roll || game_rand() % 100 < 50) return b;
    }
    return -1;
}

// The defender's orbital defense pods not yet used this turn fire at the
// missile, each an even chance of a hit. Missing that, flechette
// defenses near the target may. Missing those, one of its pods is given
// up to bring it down: but a human defender playing here (not in a
// network game) is asked first. Messages tell the player of the outcome
// when they are either side. Returns 1 when the missile is down.
int shoot(int faction_att, int faction_def, int x, int y) {
    int has_flechette = flechette(faction_att, x, y, 0) >= 0;
    struct faction_data *D = &game_factions[faction_def];
    int pods = D->satellites[3];
    if (!pods && !has_flechette) return 0;
    int hit = 0;
    for (int k = 0, n = pods - D->odp_deployed; k < n; k++) {
        D->odp_deployed++;
        if (game_rand() % 100 >= 50) {
            hit = 1;
            break;
        }
    }
    int left = game_range(D->satellites[3] - D->odp_deployed, 0, 9999);
    int current = *game_current_faction;
    if (!hit) {
        int b = flechette(faction_att, x, y, 1);
        if (b >= 0) {
            say(0, game_bases[b].name);
            if (faction_def == current) {
                say_adjective(1, faction_att);
                pop(0x689f5c, 0x689f4c);
            } else if (faction_att == current) {
                say_adjective(1, game_bases[b].faction);
                pop(0x689f78, 0x689f68);
            }
            return 1;
        }
        int sacrifice = 0;
        if (D->satellites[3]) {
            if (!*game_net_multiplayer && (*game_human_factions & bit(faction_def))) {
                if (faction_def == current) {
                    game_parse_num(0, D->satellites[3]);
                    say(0, base_name(base_find(x, y)));
                    say_adjective(1, faction_att);
                    sacrifice = !game_popp(*game_script_file, at_text(0x689fcc), 0, at_text(0x689fbc), 0);
                } else {
                    if (faction_att == current) {
                        say_noun(0, faction_def);
                        pop(0x689fb0, 0x689fa0);
                    }
                    sacrifice = 1;
                }
            } else {
                if (faction_def == current) pop(0x689f94, 0x689f84);
                else if (faction_att == current) {
                    say_noun(0, faction_def);
                    pop(0x689fb0, 0x689fa0);
                }
                sacrifice = 1;
            }
        }
        if (!sacrifice) {
            if (has_flechette) {
                if (faction_def == current) {
                    say_adjective(1, faction_att);
                    pop(0x689fe8, 0x689fd8);
                } else if (faction_att == current) {
                    say_adjective(1, faction_def);
                    pop(0x68a00c, 0x689ffc);
                }
            }
            return 0;
        }
        D->satellites[3]--;
        if (D->satellites[3] - D->odp_deployed < 0) D->odp_deployed = D->satellites[3];
    }
    int b = base_find_2(x, y, faction_def);
    if (b < 0) b = base_find(x, y);
    say(0, base_name(b));
    game_parse_num(0, D->satellites[3]);
    game_parse_num(1, left);
    if (faction_def == current) {
        say_adjective(1, faction_att);
        pop(0x68a030, 0x68a020);
    } else if (faction_att == current) {
        say_adjective(1, faction_def);
        pop(0x68a04c, 0x68a03c);
    }
    return 1;
}

static void *fx(int id) { return (char *)game_fx + id * FX_SIZE; }
static int fx_playing(int id) {
    void *f = fx(id);
    int(__attribute__((thiscall)) * playing)(void *) = (*(void ***)f)[0x5c / 4];
    return playing(f);
}

// The missile's owner is told of where it is aimed (the player is warned,
// unless they fired it). Each faction with a base, or an unallied unit,
// in the blast (its reactor's radius) may shoot it down, at the square
// for a base and at the target for a unit. The owner of the target's
// territory may then try too, unless it already did and missed: as
// Thinker notes, the original asks it even when the missile is already
// down, and keeps that answer. The missile is gone. If it got through,
// the land falls a level for each level of reactor, the climate changes,
// and every base and unit in the blast is destroyed, with explosions and
// sound; the first faction other than the owner to lose a base (or else
// a unit) is the victim of a major atrocity.
void planet_busting(int veh, int x, int y) {
    const struct veh *V = &game_vehs[veh];
    int faction = V->faction, current = *game_current_faction;
    game_console_focus(game_console, x, y, faction);
    const struct unit_proto *P = proto_of(V->proto);
    int reactor = P->plan == PLAN_PLANET_BUSTER_ ? P->reactor : 0;
    int squares = game_radius_count[reactor];
    int b = base_find(x, y);
    if (faction != current && b >= 0) {
        const struct faction_def *d = &game_faction_defs[faction];
        *game_default_plural = 0;
        *game_default_gender = d->leader_female;
        say(0, (char *)d->leader_title);
        *game_default_gender = d->leader_female;
        *game_default_plural = 0;
        say(1, (char *)d->leader_name);
        say_noun(2, faction);
        say(3, game_bases[b].name);
        int dist = xy_dist(game_bases[b].x, game_bases[b].y, x, y) >> 1;
        game_popp(*game_script_file, at_text(0x68a074), 0, at_text(dist > reactor ? 0x68a068 : 0x68a058), 0);
    }
    int tried[FACTION_COUNT] = {0}, down = 0;
    for (int i = 0; i < squares; i++) {
        int nx = wrap_x(x + game_radius_x[i]), ny = y + game_radius_y[i];
        if (!on_the_map(nx, ny)) continue;
        int nb = base_at(nx, ny);
        int other;
        if (nb >= 0) {
            other = game_bases[nb].faction;
            if (other == faction || tried[other]) continue;
            down = shoot(faction, other, nx, ny);
        } else {
            int v = stack_fix(veh_at(nx, ny));
            if (v < 0) continue;
            other = game_vehs[v].faction;
            if (other == faction || (game_factions[faction].diplo_status[other] & TREATY_PACT) || tried[other]) continue;
            down = shoot(faction, other, x, y);
        }
        if (down) break;
        tried[other] = 1;
    }
    int owner = whose_territory(faction, x, y, NULL, 0);
    if (owner >= 0 && owner != faction && !tried[owner]) down = shoot(faction, owner, x, y);
    kill(veh);
    game_draw_radius(x, y, 0, 2);
    if (down) return;
    *game_battle_state = 1;
    int victim_base = -1, victim_veh = -1;
    for (int k = reactor; k > 0; k--) world_lower_alt(x, y);
    world_climate();
    *game_rain_base = -1;
    game_fx_play(game_fx, FX_PB_BLAST);
    *game_pb_reactor = reactor;
    game_boom(x, y, 0x20);
    for (int i = 0; i < squares; i++) {
        int nx = wrap_x(x + game_radius_x[i]), ny = y + game_radius_y[i];
        if (!on_the_map(nx, ny)) continue;
        int nb = base_at(nx, ny);
        if (nb >= 0) {
            if (victim_base < 0 && game_bases[nb].faction != faction) victim_base = game_bases[nb].faction;
            base_kill(nb);
        }
        int v = stack_fix(veh_at(nx, ny));
        if (v >= 0) {
            if (victim_veh < 0 && game_vehs[v].faction != faction) victim_veh = game_vehs[v].faction;
            stack_kill(v);
        }
    }
    *game_battle_state = 0;
    if (victim_base < 0) victim_base = victim_veh;
    game_draw_map(0);
    game_boom(x, y, 0x40);
    while (fx_playing(FX_PB_BLAST)) game_do_task();
    game_fx_play(game_fx, FX_PB_AFTER);
    game_boom(x, y, 0x11);
    game_boom(x, y, 0x12);
    game_major_atrocity(faction, victim_base);
}

// ---- The battle -----------------------------------------------------------------

enum {
    PLAN_NAVAL_SUPERIORITY_ = 6, PLAN_COLONY_ = 8, PLAN_TERRAFORM = 9, PLAN_SUPPLY_ = 10, PLAN_PROBE = 11,
    ABL_COMM_JAMMER = 0x200, ABL_NERVE_GAS = 0x100000, ABL_DISSOCIATIVE_WAVE = 0x2000000,
    ABL_MARINE_DETACHMENT = 0x4000000,
    PROTO_MIND_WORMS = 8, PROTO_SPORE_LAUNCHER = 15, BASIC_PROTOS = 64,
    // The fight's kind (as battle_compute() takes it): artillery, weapon
    // against weapon (artillery duels, ships against land), and an
    // interception between air units.
    COMBAT_ARTILLERY = 0x1, COMBAT_WEAPONS = 0x2, COMBAT_AIR = 0x8 | 0x10 | COMBAT_WEAPONS,
    ATTACK_ASK = 0x1, ATTACK_MADE = 0x2,  // game_veh_attack_flags
    VEH_STATE_MOVED = 0x4, VEH_STATE_REPAIR = 0x8, VEH_STATE_ATTACKED = 0x40, VEH_STATE_ON_ALERT = 0x200,
    VEH_STATE_NO_HASTE = 0x1000, VEH_STATE_EXPLORE = 0x4000, VEH_STATE_HELD_FIRE = 0x40000,
    VEH_STATE_NERVE_GAS = 0x80000,
    VEH_STATE_ORDERS = 0x2000000 | 0x1000000 | 0x4000 | 0x200,  // what a unit's orders leave behind
    ORDER_HOLD_ = 2, ORDER_MOVE_TO = 24,
    BASE_STATE_COMBAT_LOSS = 0x8, BASE_STATE_AIR_ATTACKED = 0x100000, BASE_STATE_SHELLED = 0x200000,
    BASE_EVENT_SHELLED = 0x100,
    MAP_BUNKER = 0x800,
    FAC_PERIMETER_DEFENSE = 4, FAC_TACHYON_FIELD = 5, SP_CITIZENS_DEFENSE_FORCE = 5,
    PREF_FAST_BATTLES = 0x8, PREF_SHOW_ENEMY_MOVES = 0x4, PREF_SHOW_ALLY_MOVES = 0x8000,
    MPREF_PAUSE_AFTER_BATTLES = 0x10, MPREF_CONFIRM_ODDS = 0x8000,
    // Proposals to repeal and to reinstate the U.N. Charter, as counted in
    // game_proposal_passes.
    PROPOSAL_REPEAL_CHARTER = 8, PROPOSAL_REINSTATE_CHARTER = 9,
};

// The state of a battle, as battle_fight_2() keeps it in its frame.
struct battle {
    int veh_att, offset, tx, ty, table_offset, option, *def_id;
    int veh_def, faction_att, faction_def, x, y, base;
    int combat;              // COMBAT_*
    int off, def;            // the strengths battle_compute() gives
    int cost;                // the moves the attack costs
    int hp_att, hp_def;      // hit points at the start
    int dmg_att, dmg_def;    // damage each takes a round lost: its own reactor in psi combat, else 1
    int intercepted, nerve, report, interlude, render, render_tile, render_base;
    int proto_att, proto_def;  // the designs that met, for their combat bits
    int single, damage_mod, combat_value, retreat, detach, marine, moved, nx, ny;
    int hp_att_left;
    int killed, captured, credits, damaged;
    int survivor;            // the defending stack's unit the base's losses look at
    char base_name[80];      // the base gassed
};

static inline struct veh *veh_(int v) { return &game_vehs[v]; }
static inline const struct unit_proto *veh_proto(int v) { return proto_of(veh_(v)->proto); }
static inline int veh_triad_(int v) { return game_chassis[veh_proto(v)->chassis].triad; }
static inline int veh_missile(int v) { return game_chassis[veh_proto(v)->chassis].missile; }
static inline int veh_hp(int v) { return game_range(max_hp(veh_(v)) - veh_(v)->damage, 0, 9999); }
static inline int veh_psi_weapon(int v) { return game_weapons[veh_proto(v)->weapon].offense < 0; }
static inline int veh_psi_armor(int v) { return game_armor[veh_proto(v)->armor].defense < 0; }
// A faction's data, also for the -1 the originals read as an owner.
static inline struct faction_data *faction_any(int f) {
    return (struct faction_data *)((char *)game_factions + (intptr_t)f * (intptr_t)sizeof(struct faction_data));
}
static inline struct base *base_any(int b) {
    return (struct base *)((char *)game_bases + (intptr_t)b * (intptr_t)sizeof(struct base));
}
static int is_sea(int x, int y) { return (tile_at(x, y)->climate & 0xe0) < ALT_SHORE_LINE << 5; }
static int top_of(int v) {
    while (game_vehs[v].prev_in_stack >= 0) v = game_vehs[v].prev_in_stack;
    return v;
}
static int shift_down(void) { return (game_get_async_key_state(0x10) & 0xff00) != 0; }
static void say_title(int slot, int faction) {
    const struct faction_def *d = &game_faction_defs[faction];
    *game_default_plural = 0;
    *game_default_gender = d->leader_female;
    say(slot, (char *)d->leader_title);
}
static void say_leader(int slot, int faction) {
    const struct faction_def *d = &game_faction_defs[faction];
    *game_default_gender = d->leader_female;
    *game_default_plural = 0;
    say(slot, (char *)d->leader_name);
}
static void notify(char *label) { game_netmsg_pop(game_netmsg, label, 5000, 0, NULL); }
static void status_draw(struct battle *b, int combat) {
    game_status_win_draw(game_status_win, b->veh_att, b->veh_def, b->off, b->def, combat);
}
static void show_battle(struct battle *b, int combat) {
    game_set_iface_mode(game_battle_win);
    game_battle_win_clear(game_battle_win);
    status_draw(b, combat);
    *game_status_win_battle = 1;
}
// The attacker's moves taken back and its orders cleared, as when the
// player calls the attack off.
static int called_off(struct battle *b) {
    struct veh *A = veh_(b->veh_att);
    game_battle_win_pulse_timer(game_battle_win);
    if (*game_veh_attack_flags & ATTACK_MADE) A->moves_spent -= (uint8_t)b->cost;
    A->order = 0;
    A->state &= ~(uint32_t)VEH_STATE_ORDERS;
    game_status_win_reset(game_status_win);
    game_status_win_redraw(game_status_win);
    return 1;
}

// Who defends, the kind of fight and the odds. Artillery (an offset into
// the radius table, or a land unit that can bombard) shells; a ship that
// can bombard does so against a base on land, or a stack with fewer of the
// units it could hit at sea (stack_check 3, 2) than in all. An air unit
// (not a missile) attacking a stack with no air superiority unit meets
// the defender's interceptor, if any. Units that can both bombard fight
// weapon against weapon, but a ship at sea attacked by another ship; so do
// air superiority units against air units. Then the strengths, the moves
// the attack costs (what is left of a bombarding unit's, else a road
// move), and hit points; in psi combat (a psi weapon or psi armor), which
// ignores reactors, each round lost costs a unit its own reactor. Returns
// the odds when only weighing them (option 0): the attacker's strength
// times its share of hit points (and the defender's reactor in psi
// combat), 4 times for artillery (else 8), over one more than the
// defender's. The original doesn't check for no defender (Thinker returns
// 2).
static int battle_setup(struct battle *b) {
    struct veh *A = veh_(b->veh_att);
    if (b->table_offset) b->combat = COMBAT_ARTILLERY;
    else b->combat = can_arty(A->proto, 1) && veh_triad_(b->veh_att) == TRIAD_LAND ? COMBAT_ARTILLERY : 0;
    b->x = A->x, b->y = A->y, b->faction_att = A->faction;
    int v = veh_at(b->tx, b->ty);
    if (can_arty(A->proto, 1) && veh_triad_(b->veh_att) == TRIAD_SEA && !is_sea(b->tx, b->ty)) {
        int seaborne = stack_check(v, 3, 2, -1, -1);
        if (seaborne < stack_check(v, 1, -1, -1, -1)) b->combat = COMBAT_ARTILLERY;
        else {
            const struct map_tile *t = tile_at(b->tx, b->ty);
            if ((t->bits & MAP_BASE) && (t->owner_site & 0xf) < 8) b->combat = COMBAT_ARTILLERY;
        }
    }
    v = stack_fix(v);
    if (veh_triad_(b->veh_att) == TRIAD_AIR && !veh_missile(b->veh_att) &&
        !stack_check(v, 6, ABL_AIR_SUPERIORITY, -1, -1) && b->option && (*game_veh_attack_flags & ATTACK_MADE))
        b->intercepted = interceptor(veh_faction_any(v), b->faction_att, b->tx, b->ty);
    b->veh_def = best_defender(v, b->veh_att, b->combat);
    b->survivor = b->veh_def;
    b->faction_def = veh_(b->veh_def)->faction;
    b->base = base_at(b->tx, b->ty);
    if (can_arty(A->proto, 1) && can_arty(veh_(b->veh_def)->proto, 1)) {
        if (veh_triad_(b->veh_att) == TRIAD_LAND || b->combat || !is_sea(b->tx, b->ty)) b->combat |= COMBAT_WEAPONS;
    }
    if (has_abil(veh_(b->veh_def)->proto, ABL_AIR_SUPERIORITY) && veh_triad_(b->veh_att) == TRIAD_AIR &&
        !veh_missile(b->veh_att) && !veh_missile(b->veh_def) && veh_triad_(b->veh_def) == TRIAD_AIR)
        b->combat |= COMBAT_AIR;
    if ((b->combat & (COMBAT_ARTILLERY | COMBAT_WEAPONS)) && b->combat < 4)
        b->cost = game_range(veh_speed(b->veh_att, 0) - A->moves_spent, 0, 999);
    else
        b->cost = game_rules_basic->move_rate_roads;
    battle_compute(b->veh_att, b->veh_def, &b->off, &b->def, b->combat);
    b->hp_att = veh_hp(b->veh_att);
    b->hp_def = veh_hp(b->veh_def);
    int psi = veh_psi_weapon(b->veh_att) || veh_psi_armor(b->veh_def);
    b->dmg_def = psi ? veh_proto(b->veh_def)->reactor : 1;
    b->dmg_att = psi ? veh_proto(b->veh_att)->reactor : 1;
    if (b->option) return 0;
    int off = b->hp_att * b->dmg_def * b->off / max_hp(A);
    int def = b->hp_def * b->dmg_att * b->def / max_hp(veh_(b->veh_def));
    if (b->def_id) *b->def_id = b->veh_def;
    return (b->combat & COMBAT_ARTILLERY ? 4 : 8) * off / (def + 1);
}

// The AI's choice to use nerve gas: on a faction it has committed
// atrocities against already (TREATY_ATROCITY_VICTIM as it holds it), or
// with its faction flag 0x1000000; else only on a human, when the U.N.
// Charter has been repealed more often than reinstated, or when it wants
// revenge and the human's blemishes on its integrity reach 6 (counted
// higher by its ai_fight, under intense rivalry, when the human ranks
// above it or leads their war).
static int use_nerve_gas(int faction, int victim) {
    const struct faction_data *F = &game_factions[faction], *V = &game_factions[victim];
    if (F->diplo_status[victim] & TREATY_ATROCITY_VICTIM) return 1;
    if (F->flags & 0x1000000) return 1;
    if (!(*game_human_factions & bit(victim))) return 0;
    if (game_proposal_passes[PROPOSAL_REPEAL_CHARTER] > game_proposal_passes[PROPOSAL_REINSTATE_CHARTER]) return 1;
    if (!(F->diplo_status[victim] & TREATY_WANT_REVENGE)) return 0;
    int k = 0;
    if ((*game_rules & RULES_INTENSE_RIVALRY) &&
        (V->ranking > F->ranking || V->diplo_combat_count[faction] > F->diplo_combat_count[victim]))
        k = (F->ai_fight + 1) * 3;
    return V->integrity_blemishes + k >= 6;
}

// The attack declared. A human attacker (not a probe team, nor taking an
// artifact in the open, nor met by an interceptor in a network game) may
// call it off rather than break a pact, treaty or commlink (as Thinker
// notes, not a truce): 2. A unit with nerve gas (not in psi combat) is
// asked whether to use it, or for the AI decides. Its strength is cut for
// moves less than a road move, but for native life (and mind worms and
// spore launchers), and the moves are spent (not by artillery). Both are
// marked as having moved; when only weighing the attack, that is all.
static int battle_declare(struct battle *b) {
    struct veh *A = veh_(b->veh_att), *D = veh_(b->veh_def);
    uint32_t bit_att = bit(b->faction_att);
    if ((*game_human_factions & bit_att) && veh_proto(b->veh_att)->plan != PLAN_PROBE &&
        !(veh_proto(b->veh_def)->plan == PLAN_ARTIFACT && b->base < 0) && (*game_veh_attack_flags & ATTACK_ASK) &&
        !(*game_net_multiplayer && b->intercepted) &&
        game_break_treaty(b->faction_att, b->faction_def, TREATY_PACT | TREATY_TREATY | TREATY_COMMLINK))
        return 2;
    b->nerve = 0;
    if (has_abil(A->proto, ABL_NERVE_GAS) && !veh_psi_weapon(b->veh_att) && !veh_psi_armor(b->veh_def)) {
        if (!*game_net_multiplayer || (*game_veh_attack_flags & ATTACK_ASK)) {
            A->state &= ~(uint32_t)VEH_STATE_NERVE_GAS;
            if (b->faction_att == *game_current_faction && (*game_human_factions & bit_att)) {
                say(0, (char *)veh_proto(b->veh_att)->name);
                game_parse_say(1, (int)(intptr_t)game_abilities[game_abil_index(ABL_NERVE_GAS)].name, -1, -1);
                game_battle_win_stop_timer(game_battle_win);
                if (game_popp(*game_script_file, at_text(0x68a0e4), 0, at_text(0x68a0d4), 0))
                    A->state |= VEH_STATE_NERVE_GAS;
                game_battle_win_pulse_timer(game_battle_win);
            } else if (!(*game_human_factions & bit_att) && use_nerve_gas(b->faction_att, b->faction_def)) {
                A->state |= VEH_STATE_NERVE_GAS;
            }
            if (*game_net_multiplayer) game_synch_veh(b->veh_att);
        }
        if ((*game_veh_attack_flags & ATTACK_MADE) && (A->state & VEH_STATE_NERVE_GAS)) b->nerve = 1;
    }
    if (b->base >= 0) A->state &= ~(uint32_t)VEH_STATE_HELD_FIRE;
    if (!(A->state & VEH_STATE_NO_HASTE) && (*game_veh_attack_flags & ATTACK_MADE)) {
        if (b->faction_att && A->proto != PROTO_SPORE_LAUNCHER && A->proto != PROTO_MIND_WORMS) {
            int moves = game_range(veh_speed(b->veh_att, 0) - A->moves_spent, 0, 999);
            int road = game_rules_basic->move_rate_roads;
            int m = road < 0 ? 0 : moves > road ? road : moves;
            b->off = m * b->off / road;
            char *label = game_label(339);  // Hasty
            add_bat(0, (m - road) * 100 / road, label);
        }
        if (b->combat != COMBAT_ARTILLERY) A->moves_spent += (uint8_t)b->cost;
    }
    A->state = (A->state & ~(uint32_t)VEH_STATE_NO_HASTE) | VEH_STATE_MOVED;
    D->state |= VEH_STATE_MOVED;
    if (!(*game_veh_attack_flags & ATTACK_MADE)) return 0;
    A->state |= VEH_STATE_ATTACKED;
    return -1;
}

// The attack's diplomacy, but by a probe team or on an artifact. A human
// victim at peace (a truce or treaty, or a commlink, or the attacker's
// TREATY_UNK_800, without vendetta) is told it was a surprise; otherwise,
// with a treaty either way between two others, the player hears of it if
// allied to the victim (2) or the attacker (3), or watching all (1). The
// attack is an act of aggression. A third faction whose territory the
// attacker stands in, allied to the attacker, is asked to talk by a
// stronger victim it has a commlink with and isn't at war with, and earns
// friction where it is TREATY_UNK_800000 with the victim.
static void battle_diplomacy(struct battle *b) {
    if (veh_proto(b->veh_att)->plan == PLAN_PROBE || veh_proto(b->veh_def)->plan == PLAN_ARTIFACT) return;
    int fa = b->faction_att, fd = b->faction_def, current = *game_current_faction;
    struct faction_data *Fa = &game_factions[fa], *Fd = &game_factions[fd];
    uint32_t st = Fd->diplo_status[fa];
    int surprise = (*game_human_factions & bit(fd)) &&
                   ((st & (TREATY_TREATY | TREATY_TRUCE)) ||
                    (((st & TREATY_COMMLINK) || (Fa->diplo_status[fd] & TREATY_UNK_800)) && !(st & TREATY_VENDETTA)));
    if (surprise) {
        if (fd == current) {
            say_title(0, fa);
            say_leader(1, fa);
            say_noun(2, fa);
            game_netmsg_pop(game_netmsg, at_text(0x68a0f0), -5000, 0, NULL);
        } else if (*game_pbem) {
            say_title(0, fa);
            say_leader(1, fa);
            say_noun(2, fa);
            game_pop2(at_text(0x68a0fc), NULL, -1 - fd);
        }
        Fd->diplo_spoke[fa] = *game_turn;
    } else if (((Fa->diplo_status[fd] & TREATY_TREATY) || (Fd->diplo_status[fa] & TREATY_TREATY)) && fa != current &&
               fd != current) {
        const struct faction_data *C = &game_factions[current];
        if (C->diplo_status[fd] & TREATY_PACT) b->report = 2;
        else if (C->diplo_status[fa] & TREATY_PACT) b->report = 3;
        else if (*game_state & GAME_STATE_OMNISCIENT) b->report = 1;
    }
    act_of_aggression(fa, fd);
    int owner = tile_at(b->x, b->y)->territory;
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (i == fa || i == fd || owner != i || !(Fa->diplo_status[i] & TREATY_PACT)) continue;
        if ((Fd->diplo_status[i] & TREATY_VENDETTA) || !(Fd->diplo_status[i] & TREATY_COMMLINK) ||
            Fd->mil_strength <= game_factions[i].mil_strength)
            continue;
        treaty_on(i, fd, TREATY_WANT_TO_TALK);
        if (game_factions[i].diplo_status[fd] & 0x800000) cause_friction(fd, i, 3);
    }
}

// The strengths adjusted: nerve gas adds half. The player's first psi
// battle brings the tutorial's note. A human defender at a lower
// difficulty than 3 (2 against native life) faces the AI with a quarter
// of its strength for each level plus one; a human attacker at such a
// level has (4 - level) halves of its own. Native life against the AI
// fights with (ranking + 8) sixteenths, or half against a former (a
// quarter if it can't be seen, unless watching all or in a network
// game); and against a base, half against a faction's last base, half
// before turn 50 and half again at headquarters with fewer than 4 bases;
// the player's base attacked brings an interlude. Before turn 15 native
// attackers have a third of their strength and native defenders half.
// A base attacked from the air is marked. Thinker lists these among the
// battle's modifiers; the original doesn't, and Thinker drops the
// former's quarter.
static void battle_modifiers(struct battle *b) {
    int fa = b->faction_att, fd = b->faction_def, current = *game_current_faction;
    if (b->nerve) {
        b->off += b->off / 2;
        char *label = game_label(341);  // Nerve Gas
        add_bat(0, 50, label);
    }
    if (*game_net_multiplayer) game_log_say(at_text(0x68a108), b->veh_att, b->veh_def, 0);
    b->proto_att = veh_(b->veh_att)->proto, b->proto_def = veh_(b->veh_def)->proto;
    if ((fa == current || fd == current) && !*game_net_multiplayer &&
        (veh_psi_weapon(b->veh_att) || veh_psi_armor(b->veh_def)) && game_tut_check2(2))
        game_tut_map(game_tut_win, at_text(0x68a11c), b->tx, b->ty, fa == current ? b->veh_att : b->veh_def, NULL, 1, -1, -1);
    int level = fa ? 2 : 3;
    const struct faction_data *Fd = &game_factions[fd], *Fa = &game_factions[fa];
    uint8_t humans = *game_human_factions;
    if (Fd->diff_level < level && (humans & bit(fd)) && !(humans & bit(fa))) b->off = (Fd->diff_level + 1) * b->off / 4;
    if (Fa->diff_level < level && (humans & bit(fa)) && !(humans & bit(fd))) b->off = (4 - Fa->diff_level) * b->off / 2;
    if (!fa) {
        if (!(*game_human_factions & bit(fd))) {
            const struct veh *D = veh_(b->veh_def);
            if (veh_proto(b->veh_def)->plan == PLAN_TERRAFORM) {
                b->off /= 2;
                if (!(*game_state & GAME_STATE_OMNISCIENT) && !*game_net_multiplayer &&
                    (D->faction != current || (D->flags & 0x440) == 0x440) && !(D->visibility & bit(current)))
                    b->off /= 2;
            } else {
                b->off = (Fd->ranking + 8) * b->off / 16;
            }
        }
        if (b->base >= 0) {
            if (fd == current) b->interlude = 1;
            if (Fd->base_count <= 1) b->off /= 2;
            if (*game_turn < 50) b->off /= 2;
            if (has_facility(b->base, FAC_HEADQUARTERS_) && Fd->base_count < 4) b->off /= 2;
        }
    }
    if (*game_turn < 15) {
        if (!fa) b->off /= 3;
        if (!fd) b->def /= 2;
    }
    if (b->base >= 0 && veh_triad_(b->veh_att) == TRIAD_AIR) game_bases[b->base].state |= BASE_STATE_AIR_ATTACKED;
}

// The player attacking (not met by an interceptor, nor with artillery or a
// missile, nor on explore or alert) is warned of odds below one in two,
// or with the preference shown them, and may call the attack off: 1.
static int battle_confirm(struct battle *b) {
    const struct veh *A = veh_(b->veh_att), *D = veh_(b->veh_def);
    int current = *game_current_faction;
    if (!(*game_veh_attack_flags & ATTACK_ASK) || b->intercepted || !(*game_human_factions & bit(b->faction_att)) ||
        b->faction_att != current || b->combat || veh_missile(b->veh_att) ||
        (A->state & (VEH_STATE_EXPLORE | VEH_STATE_ON_ALERT)))
        return 0;
    if (2 * (b->hp_att * b->dmg_def * b->off) < b->hp_def * b->dmg_att * b->def) {
        show_battle(b, 0);
        say_title(0, current);
        say_leader(1, current);
        game_battle_win_stop_timer(game_battle_win);
        if (!game_popp(*game_script_file, at_text(0x68a138), 0, at_text(0x68a128), 0)) return called_off(b);
        game_battle_win_pulse_timer(game_battle_win);
        return 0;
    }
    if (!(*game_more_preferences & MPREF_CONFIRM_ODDS)) return 0;
    show_battle(b, 0);
    say_title(0, current);
    say_leader(1, current);
    char num[16];
    game_battle_text[0] = 0;
    snprintf(num, sizeof num, "%d", b->off >> 8);
    strcat(game_battle_text, num);
    strcat(game_battle_text, at_text(0x68a140));
    snprintf(num, sizeof num, "%d", (b->off & 0xff) * 10 >> 8);
    strcat(game_battle_text, num);
    say(2, game_battle_text);
    game_battle_text[0] = 0;
    snprintf(num, sizeof num, "%d", b->def >> 8);
    strcat(game_battle_text, num);
    strcat(game_battle_text, at_text(0x68a144));
    snprintf(num, sizeof num, "%d", (b->def & 0xff) * 10 >> 8);
    strcat(game_battle_text, num);
    say(3, game_battle_text);
    // The odds, as hit points times strength each way, in lowest terms
    // (dividing out factors from 2 up to the smaller), then rounded down
    // by powers of ten while both are large.
    int a = veh_hp(b->veh_att) * b->off, d = veh_hp(b->veh_def) * b->def;
    for (int c = 2; c < (a < d ? a : d); c++)
        while (a && d && !(a % c) && !(d % c)) a /= c, d /= c;
    static const int steps[][2] = {{20000, 10000}, {10000, 5000}, {2000, 1000}, {1000, 500}, {200, 100}, {100, 50}};
    for (int i = 0; i < 6; i++)
        while (a >= steps[i][0] && d >= steps[i][0]) a /= steps[i][1], d /= steps[i][1];
    game_battle_win_stop_timer(game_battle_win);
    game_parse_num(0, a);
    game_parse_num(1, d);
    char *label = at_text(A->damage || D->damage ? 0x68a158 : 0x68a164);
    if (!game_popp(*game_script_file, label, 0, at_text(0x68a148), 0)) return called_off(b);
    game_battle_win_pulse_timer(game_battle_win);
    return 0;
}

// The pause after a battle the player sees, if they asked for one (and
// may turn off from it).
static void battle_pause(uint32_t label, uint32_t picture) {
    game_battle_win_stop_timer(game_battle_win);
    game_popp(*game_script_file, at_text(label), 0, at_text(picture), 0);
    game_battle_win_pulse_timer(game_battle_win);
    if (*game_base_bits_quiet) {
        *game_more_preferences &= ~MPREF_PAUSE_AFTER_BATTLES;
        game_prefs_save(0);
    }
}

// Artillery shells every unit of the stack it can reach (an air unit only
// at a base or airbase, or by an air superiority unit), each shot a
// random part of the attacker's strength by the rules' artillery ratio
// over the target's (at least 1, up to 999), but leaving it at least the
// rules' share of its hit points (in a base or bunker, on land or at sea).
// The defender plans to defend the square and to attack the gun; a base
// shelled is marked. Units left with no hit points die afterwards, each
// counted to the attacker's score in the war. An AI base rethinks its
// build.
static int battle_artillery(struct battle *b) {
    int fd = b->faction_def;
    add_goal(fd, GOAL_ARTILLERY, 4, b->tx, b->ty, -1);
    add_goal(fd, GOAL_ATTACK, 4, b->x, b->y, -1);
    if (b->base >= 0) {
        game_bases[b->base].state |= BASE_STATE_SHELLED;
        game_bases[b->base].event |= BASE_EVENT_SHELLED;
    }
    const struct map_tile *t = tile_at(b->tx, b->ty);
    if (b->veh_def >= 0) {
        for (b->veh_def = top_of(b->veh_def); b->veh_def >= 0; b->veh_def = veh_(b->veh_def)->next_in_stack) {
            struct veh *D = veh_(b->veh_def);
            if (veh_triad_(b->veh_def) == TRIAD_AIR && !has_abil(veh_(b->veh_att)->proto, ABL_AIR_SUPERIORITY) &&
                b->base < 0 && !(t->bits & MAP_AIRBASE))
                continue;
            veh_skip(b->veh_att);
            battle_compute(b->veh_att, b->veh_def, &b->off, &b->def, COMBAT_ARTILLERY);
            int den = game_range(game_rules_basic->artillery_damage_den * b->def, 1, 9999999);
            int n = game_range(game_rules_basic->artillery_damage_num * b->off / den + 1, 1, 999);
            int dmg = game_rnd(n);
            int limit;
            if (b->base >= 0 || (t->bits & MAP_BUNKER)) {
                limit = ((100 - game_rules_basic->max_dmg_arty_base_bunker) * max_hp(D) + 99) / 100;
            } else {
                int pct = veh_triad_(b->veh_def) != TRIAD_LAND ? game_rules_basic->max_dmg_arty_sea
                                                               : game_rules_basic->max_dmg_arty_open;
                limit = ((100 - pct) * max_hp(D) + 99) / 100;
            }
            while (dmg > 0 && veh_hp(b->veh_def) - dmg < limit) dmg--;
            int boomed = 0;
            if (b->render) {
                *game_veh_draw_def = b->veh_def;
                veh_(b->veh_att)->visibility |= (uint8_t)bit(fd);
                *game_veh_draw_att = b->veh_att;
                if (!game_console_focus(game_console, b->tx, b->ty, b->faction_att)) {
                    game_draw_tile(b->x, b->y, 2);
                    game_draw_tile(b->tx, b->ty, 2);
                }
                show_battle(b, 1);
                game_flush_input();
                if (!(limit && D->damage == limit && b->veh_def >= 0 && D->next_in_stack >= 0)) {
                    game_boom(b->tx, b->ty, 0);
                    boomed = 1;
                }
            }
            D->damage += (uint8_t)dmg;
            D->state |= VEH_STATE_MOVED;
            if (veh_proto(b->veh_def)->plan == PLAN_TERRAFORM) D->movement_turns = 0;
            if (b->base < 0) D->state |= VEH_STATE_REPAIR;
            if (b->render) {
                status_draw(b, 1);
                game_draw_tile(b->tx, b->ty, 2);
                *game_veh_draw_att = *game_veh_draw_def = -1;
                if (*game_more_preferences & MPREF_PAUSE_AFTER_BATTLES) battle_pause(0x68a190, 0x68a180);
                else if (!shift_down() && !(*game_preferences & PREF_FAST_BATTLES) && !*game_net_multiplayer && boomed)
                    game_clock_wait(200);
            }
        }
    }
    for (int v = stack_fix(veh_at(b->tx, b->ty)); v >= 0;) {
        if (veh_hp(v) > 0) {
            v = veh_(v)->next_in_stack;
            continue;
        }
        stack_veh(v, 2);
        stack_kill(v);
        v = stack_fix(veh_at(b->tx, b->ty));
        game_factions[b->faction_att].diplo_combat_total[fd]++;
        game_factions[b->faction_att].diplo_combat_count[fd]++;
    }
    game_draw_tile_fixup(b->tx, b->ty, 1, 2);
    if (b->base >= 0 && !(*game_human_factions & bit(fd))) base_reset(b->base, 0);
    return 0;
}

// The square a defender retreating from the attack goes to: around the
// target, one it can stand on (any, for an air unit), out of enemy zones
// of control and without a pod, out of fungus at shelf depth and above
// unless native or with the Pholus Mutagen, without units or a base of a
// faction not allied to it; the farthest round from the attacker's side
// (at least three steps of the eight): an index of the eight, or -1.
static int retreat_dir(struct battle *b) {
    int best = -1, best_value = 0, fd = b->faction_def;
    const struct veh *D = veh_(b->veh_def);
    for (int i = 0; i < 8; i++) {
        int nx = wrap_x(b->tx + game_radius_near_x[i]), ny = b->ty + game_radius_near_y[i];
        if (!on_the_map(nx, ny) || zoc_move(nx, ny, fd)) continue;
        int triad = veh_triad_(b->veh_def);
        if (triad != TRIAD_AIR && is_sea(nx, ny) != (triad == TRIAD_SEA)) continue;
        if (goody_at(nx, ny)) continue;
        const struct map_tile *t = tile_at(nx, ny);
        if ((t->bits & MAP_FUNGUS) && (t->climate & 0xe0) >= 2 << 5) {
            int sp = game_secret_projects[SP_PHOLUS_MUTAGEN];
            if (!(sp >= 0 && game_bases[sp].faction == fd) &&
                (D->proto >= BASIC_PROTOS || (!veh_psi_weapon(b->veh_def) && D->proto != PROTO_SPORE_LAUNCHER)))
                continue;
        }
        if (t->bits & (MAP_BASE | MAP_UNIT)) {
            int o = t->owner_site & 0xf;
            if (o < 8 && o != fd && !(game_factions[o].diplo_status[fd] & TREATY_PACT)) continue;
        }
        int d = i - (b->offset ^ 4);
        if (d < 0) d = (b->offset ^ 4) - i;
        d &= 7;
        if (d > 4) d = 8 - d;
        if (d > 2 && d > best_value) best_value = d, best = i;
    }
    return best;
}

// A marine detachment boards the ship: the defending stack changes hands
// (homed at the attacker's base there of size 3 or more, else none), the
// square too, and both sides are told. The units' counts by faction are
// left as they were.
static void battle_capture(struct battle *b) {
    const struct veh *A = veh_(b->veh_att);
    int lost = veh_(b->veh_def)->faction, taker = A->faction, current = *game_current_faction;
    if (b->veh_def >= 0) {
        for (int v = top_of(b->veh_def); v >= 0; v = veh_(v)->next_in_stack) {
            struct veh *U = veh_(v);
            U->faction = A->faction;
            say(0, (char *)veh_proto(v)->name);
            int bf = base_find(b->tx, b->ty);
            const struct base *B = base_any(bf);
            U->home_base = (int16_t)(B->faction == A->faction && B->pop_size >= 3 ? bf : -1);
            veh_skip(v);
        }
    }
    bit_set(b->tx, b->ty, MAP_POD_TAKEN, 1);
    owner_set(b->tx, b->ty, A->faction);
    if (A->faction == current) game_draw_tile_fixup(b->tx, b->ty, 1, 2);
    else game_draw_tile(b->tx, b->ty, 2);
    if (taker == current) {
        say_adjective(1, lost);
        pop(0x68a1c8, 0x68a1b8);
    }
    if (lost == current) {
        say_adjective(1, taker);
        pop(0x68a1e8, 0x68a1d8);
    }
    game_log_say(at_text(0x68a1fc), 0, 0, 0);
    b->marine = 1;
}

// A round's damage shown: when the loser's damage passes a multiple of
// three rounds' worth, an explosion over it; one round of a single-round
// fight ends it (returns 1).
static int round_shown(struct battle *b, int loser, int value, int x, int y, int flags) {
    struct veh *L = veh_(loser);
    int dmg = loser == b->veh_att ? b->dmg_att : b->dmg_def;
    if (!b->render || !b->damage_mod) return 0;
    int unit = b->damage_mod * dmg;
    if ((L->damage - dmg) / unit == L->damage / unit) return 0;
    b->combat_value = value;
    status_draw(b, b->combat);
    game_boom(x, y, flags);
    if (veh_hp(loser) > 0) {
        game_draw_tile(b->x, b->y, 2);
        game_draw_tile(b->tx, b->ty, 2);
    }
    return b->single;
}

// The fight itself. Shown to the player (when they or the defender are
// the player, or watching all, or by preference when either unit can be
// seen): the attacker steps towards the target unless shift is held. A
// faster defender with a weapon (not attacked this turn) that isn't
// holding, alone in the open (no base, airbase or bunker), neither side in
// the air, nor jammed, may retreat once it has lost half its hit points.
// A marine detachment at sea against a ship (not native life, nor in a
// base) may board it once it has dealt two thirds of its hit points, an
// even chance a round. A planet buster detonates instead. Rounds go on
// while both live: each side rolls up to its strength, the attacker
// winning only above the defender's roll; a missile (or a network game)
// shows its blast up front. Weapon-against-weapon fights between land and
// sea end at the first damage shown.
static int battle_rounds(struct battle *b) {
    struct veh *A = veh_(b->veh_att), *D = veh_(b->veh_def);
    int fd = b->faction_def;
    if (b->combat) add_goal(fd, GOAL_ARTILLERY, 2, b->tx, b->ty, -1);
    if (b->render) {
        if (*game_net_multiplayer && *game_base_win_zoomed) b->render_base = 1;
        A->visibility |= (uint8_t)bit(fd);
        *game_veh_draw_att = b->veh_att;
        *game_veh_draw_def = b->veh_def;
        if (!game_console_focus(game_console, b->tx, b->ty, b->faction_att)) {
            game_draw_tile(b->x, b->y, 2);
            game_draw_tile(b->tx, b->ty, 2);
        }
        stack_veh(b->veh_att, 1);
        show_battle(b, b->combat);
        game_flush_input();
        if (!shift_down() && !(b->combat & COMBAT_ARTILLERY)) game_veh_scoot(b->veh_att, b->x, b->y, b->offset, 1);
        stack_put(b->veh_att, b->x, b->y);
        game_draw_tile(b->x, b->y, 2);
        game_draw_tile(b->tx, b->ty, 2);
    }
    b->retreat = 0;
    int speed_def = veh_speed(b->veh_def, 1);
    if (speed_def > veh_speed(b->veh_att, 1) && !b->combat && game_weapons[veh_proto(b->veh_def)->weapon].offense &&
        !(D->state & VEH_STATE_ATTACKED) && veh_triad_(b->veh_att) != TRIAD_AIR && veh_triad_(b->veh_def) != TRIAD_AIR &&
        (!has_abil(A->proto, ABL_COMM_JAMMER) || has_abil(D->proto, ABL_DISSOCIATIVE_WAVE)) && D->order != ORDER_HOLD_ &&
        b->base < 0 && !(tile_at(b->tx, b->ty)->bits & (MAP_AIRBASE | MAP_BUNKER)) &&
        stack_check(b->veh_def, 1, -1, -1, -1) == 1)
        b->retreat = (veh_hp(b->veh_def) + 1) / 2;
    b->detach = 0;
    if (has_abil(A->proto, ABL_MARINE_DETACHMENT) && veh_triad_(b->veh_att) == TRIAD_SEA && is_sea(b->tx, b->ty) &&
        veh_triad_(b->veh_def) == TRIAD_SEA &&
        (D->proto >= BASIC_PROTOS || (!veh_psi_weapon(b->veh_def) && D->proto != PROTO_SPORE_LAUNCHER)) && b->base < 0)
        b->detach = 2 * veh_hp(b->veh_def) / 3;
    const struct unit_proto *PA = veh_proto(b->veh_att);
    if (PA->plan == PLAN_PLANET_BUSTER_ && PA->reactor) {
        planet_busting(b->veh_att, b->tx, b->ty);
        *game_draw_state |= 4;
        return 1;
    }
    int missile = veh_missile(b->veh_att);
    b->damage_mod = !missile && !*game_net_multiplayer ? 3 : 0;
    b->combat_value = 0;
    if (missile || *game_net_multiplayer) game_boom(b->tx, b->ty, veh_hp(b->veh_def) ? 1 : 2);
    b->single = (b->combat & COMBAT_WEAPONS) && (veh_triad_(b->veh_att) == TRIAD_SEA) != (veh_triad_(b->veh_def) == TRIAD_SEA);
    while (veh_hp(b->veh_att) > 0 && veh_hp(b->veh_def) > 0) {
        int roll_att = game_rnd(b->off), roll_def = game_rnd(b->def);
        if (roll_att <= roll_def) {
            A->damage += (uint8_t)b->dmg_att;
            if (round_shown(b, b->veh_att, 1, b->x, b->y, 9)) break;
            continue;
        }
        D->damage += (uint8_t)b->dmg_def;
        if (round_shown(b, b->veh_def, 2, b->tx, b->ty, 1)) break;
        if (b->detach && veh_hp(b->veh_def) > 0) {
            b->detach -= b->dmg_def;
            if (b->detach <= 0 && game_rand() % 100 < 50) {
                battle_capture(b);
                break;
            }
        }
        if (b->retreat && veh_hp(b->veh_def) > 0) {
            b->retreat -= b->dmg_def;
            if (b->retreat <= 0) {
                int dir = retreat_dir(b);
                if (dir < 0) {
                    b->retreat = 0;
                    continue;
                }
                b->nx = wrap_x(b->tx + game_radius_near_x[dir]), b->ny = b->ty + game_radius_near_y[dir];
                stack_put(stack_veh(b->veh_def, 1), b->nx, b->ny);
                b->moved = 1;
                break;
            }
        }
    }
    return 0;
}

// The battle's end drawn: an explosion over the loser, or over both after
// a single round (no damage shown as an explosion in a network game).
static void battle_shown(struct battle *b) {
    int x = b->x, y = b->y, tx = b->tx, ty = b->ty, net = *game_net_multiplayer;
    if (b->render || b->render_tile) {
        game_draw_tile_fixup_2(x, y, tx, ty, 2, 2);
        if (b->moved && !(x == b->nx && y == b->ny) && !(tx == b->nx && ty == b->ny))
            game_draw_tile_fixup(b->nx, b->ny, 2, 2);
    }
    if (!b->render) return;
    status_draw(b, b->combat);
    if (!b->moved) {
        int hp_att = veh_hp(b->veh_att), hp_def = veh_hp(b->veh_def);
        if (b->hp_att_left) {
            if (b->single) {
                if (b->combat_value != 1 && !net) game_boom(x, y, (hp_att ? 1 : 2) | 8);
                game_boom(tx, ty, (hp_def ? 1 : 2) | 8);
            } else if (b->combat_value == 2) {
                if (net || !hp_def) game_boom(tx, ty, (hp_def ? 0 : 2) | 8);
            } else if (!net) {
                game_boom(tx, ty, (hp_def ? 1 : 2) | 8);
            } else {
                game_boom(tx, ty, (hp_def ? 0 : 2) | 8);
            }
        } else {
            if (b->single) {
                if (b->combat_value != 2 && !net) game_boom(tx, ty, hp_def != 0);
                game_boom(x, y, hp_att ? 1 : 2);
            } else if (b->combat_value == 1 || net) {
                if (net || !hp_att) game_boom(x, y, hp_att ? 0 : 2);
            } else {
                game_boom(x, y, hp_att ? 1 : 2);
            }
        }
    }
    *game_veh_draw_att = *game_veh_draw_def = -1;
}

// A network game's report to the defender playing here (when both survive
// a single round, or on a loss or win), or a hotseat game's to a human
// defender: the units and their strengths, and the base.
static void report_units(struct battle *b, int stats_def, int seen_by) {
    say(0, (char *)veh_proto(b->veh_def)->name);
    game_battle_text[0] = 0;
    game_say_stats_3(game_battle_text, stats_def);
    say(1, game_battle_text);
    say_adjective(2, b->faction_att);
    say(3, (char *)veh_proto(b->veh_att)->name);
    game_battle_text[0] = 0;
    game_say_stats_3(game_battle_text, veh_(b->veh_att)->proto);
    say(4, game_battle_text);
    int bf = base_find_3(b->tx, b->ty, -1, -1, -1, seen_by);
    if (bf >= 0) say(5, game_bases[bf].name);
}
static void report_result(struct battle *b, uint32_t net_label, uint32_t pbem_label) {
    int fd = b->faction_def;
    if (*game_net_multiplayer && fd == *game_current_faction) {
        report_units(b, veh_(b->veh_def)->proto, *game_current_faction);
        notify(at_text(net_label));
    } else if (*game_pbem && (*game_human_factions & bit(fd))) {
        report_units(b, veh_(b->veh_def)->proto, fd);
        game_pop3(at_text(pbem_label), NULL, fd, b->tx, b->ty);
    }
}

// The stack's units left that may still defend: all, less colony pods,
// supply crawlers, probe teams, artifacts and terraformers... as the
// original counts them (plans 12, 8, 11 and 10).
static int defenders_left(int v) {
    return stack_check(v, 1, -1, -1, -1) - stack_check(v, 2, PLAN_ARTIFACT, -1, -1) - stack_check(v, 2, PLAN_COLONY_, -1, -1) -
           stack_check(v, 2, PLAN_PROBE, -1, -1) - stack_check(v, 2, PLAN_SUPPLY_, -1, -1);
}

// A base destroyed in battle, which may eliminate its faction (then no
// report).
static void base_fallen(struct battle *b) {
    base_kill(b->base);
    b->base = -1;
    for (int i = 0; i < FACTION_COUNT; i++) synch_bit(b->tx, b->ty, i);
    game_draw_map(1);
    if (game_eliminate_player(b->faction_def, b->faction_att)) b->report = 0;
}
// The attacker won. In a base or bunker, in the air, by a probe team,
// against naval superiority or a colony pod's stack on land, only the
// defender dies: if native life attacks, else if the defender carries no
// one (or is in a base, or the attacker is a probe team) and the probe
// team or two defenders more remain; an artifact never. Otherwise the
// whole stack takes the blow: land units in a transport at sea (but those
// aboard another one) drown, and every unit but colony pods, supply
// crawlers, probe teams and artifacts (the defender always) takes three
// times the attacker's reactor (its own against psi), counted as damaged;
// the dead go (and native life with them), and a stack left with no
// defenders falls. At a base, the attack is marked; attacked from land
// (as Thinker notes, the original looks at the attacker's square), one
// not an objective of size one, with no perimeter defense or tachyon field and
// no Citizens' Defense Force (as Thinker notes, the defender's, wherever
// it is), loses a citizen to a faction's attack when the defender had a
// weapon and no units remain (not for a human at the lowest level); it
// falls at size 0. An AI base rethinks its build; nerve gas kills half
// its people (but an objective keeps one), and it is seen by the
// attacker. A base falling may eliminate its faction (then no report). A
// human's base left empty is invaded.
static void battle_won(struct battle *b) {
    int fa = b->faction_att, fd = b->faction_def, tx = b->tx, ty = b->ty;
    game_factions[fa].diplo_combat_total[fd]++;
    game_factions[fa].diplo_combat_count[fd]++;
    game_faction_combat_win[fa]++;
    game_faction_combat_loss[fd]++;
    report_result(b, 0x68a228, 0x68a234);
    const struct map_tile *t = tile_at(tx, ty);
    const struct unit_proto *PA = veh_proto(b->veh_att), *PD = veh_proto(b->veh_def);
    int alone = b->base >= 0 || (t->bits & MAP_BUNKER) || veh_triad_(b->veh_def) == TRIAD_AIR ||
                veh_triad_(b->veh_att) == TRIAD_AIR || PA->plan == PLAN_PROBE || PD->plan == PLAN_NAVAL_SUPERIORITY_ ||
                (stack_check(b->veh_def, 2, PLAN_COLONY_, -1, -1) && !is_sea(tx, ty));
    int single = 0;
    if (alone && PD->plan != PLAN_ARTIFACT) {
        if (!fa) single = 1;
        else if (veh_cargo(b->veh_def) && b->base < 0 && PA->plan != PLAN_PROBE) single = 0;
        else single = PA->plan == PLAN_PROBE || defenders_left(b->veh_def) > 1;
    }
    if (single) {
        battle_kill(b->veh_def, &b->killed, &b->captured, &b->credits, b->veh_att, fa);
    } else {
        stack_veh(b->veh_def, 0);
        int land_sea = is_sea(tx, ty) && !((t->bits & MAP_BASE) && (t->owner_site & 0xf) < 8);
        for (int v = top_of(b->veh_def); v >= 0; v = veh_(v)->next_in_stack) {
            struct veh *U = veh_(v);
            const struct unit_proto *PU = veh_proto(v);
            if (veh_triad_(v) == TRIAD_LAND && land_sea &&
                (U->order != ORDER_SENTRY_BOARD || U->waypoint_x[0] == b->veh_def || U->waypoint_x[0] < 0)) {
                U->damage += (uint8_t)veh_hp(v);
            } else if (v != b->veh_def && (PU->plan == PLAN_ARTIFACT || PU->plan == PLAN_COLONY_ ||
                                           PU->plan == PLAN_PROBE || PU->plan == PLAN_SUPPLY_)) {
                continue;
            }
            int reactor = veh_psi_weapon(b->veh_att) ? PU->reactor : veh_proto(b->veh_att)->reactor;
            U->damage += (uint8_t)(reactor * 3);
            U->state |= VEH_STATE_MOVED;
            b->damaged++;
        }
        int att = b->veh_att;
        for (int v = top_of(b->veh_def); v >= 0;) {
            int next = veh_(v)->next_in_stack;
            if (game_range(max_hp(veh_(v)) - veh_(v)->damage, 0, 9999) <= 0 || !veh_(v)->faction) {
                if (battle_kill(v, &b->killed, &b->captured, &b->credits, att, fa)) {
                    if (att > v) att--;
                    if (next > v) next--;
                }
            }
            v = next;
        }
        b->damaged -= b->killed + b->captured;
        if (b->damaged < 0) b->damaged = 0;
        b->survivor = stack_fix(veh_at(tx, ty));
        if (b->survivor >= 0 && !defenders_left(b->survivor)) {
            battle_kill_stack(b->survivor, &b->killed, &b->captured, &b->credits, b->veh_att, fa);
            b->damaged = 0;
        }
    }
    if (b->base < 0) return;
    struct base *B = &game_bases[b->base];
    B->state |= BASE_STATE_COMBAT_LOSS;
    if (!is_ocean(b->x, b->y) && (!is_objective(b->base) || B->pop_size > 1) && !has_fac(FAC_PERIMETER_DEFENSE, b->base, 0) &&
        !has_fac(FAC_TACHYON_FIELD, b->base, 0) && !has_project(SP_CITIZENS_DEFENSE_FORCE, fd) &&
        game_weapons[veh_proto(b->survivor)->weapon].offense && fa && veh_who(tx, ty) < 0 &&
        (game_factions[fd].diff_level || !(*game_human_factions & bit(fd)))) {
        B->pop_size--;
        if (B->pop_size <= 0) {
            b->nerve = 0;
            base_fallen(b);
            goto invaded;
        }
        B->pop_seen[fa] = B->pop_size;
    }
    if (!(*game_human_factions & bit(fd))) base_reset(b->base, 0);
    if (b->nerve) {
        int8_t pop = B->pop_size;
        b->nerve = (pop + 1) / 2;
        B->pop_size = (int8_t)(pop - b->nerve);
        if (is_objective(b->base) && B->pop_size < 1) B->pop_size = 1;
        if (B->pop_size <= 0) base_fallen(b);
        else spot_base(b->base, fa);
    }
invaded:
    if (veh_who(tx, ty) < 0 && (*game_human_factions & bit(fd)) && b->base >= 0) invasions(b->base);
}

// The attacker lost: the defender's win is counted, the attacker's stack
// dies (artifacts among it go to no one), and the defender scores in the
// war.
static void battle_lost(struct battle *b) {
    int fa = b->faction_att, fd = b->faction_def;
    game_faction_combat_win[fd]++;
    game_faction_combat_loss[fa]++;
    report_result(b, 0x68a240, 0x68a24c);
    stack_veh(b->veh_att, 1);
    battle_kill_stack(b->veh_att, &b->killed, &b->captured, &b->credits, -1, fd);
    game_factions[fd].diplo_combat_total[fa]++;
    game_factions[fd].diplo_combat_count[fa]++;
}

// The battle's report: planetpearls from native life the attacker killed
// (and the native units around grow in their lifecycle), the units killed
// and damaged, artifacts taken, a base gassed (as Thinker notes, even when
// the attacker died), a defender that got away; the pause after battles;
// and a third party's news of a broken treaty. The designs have met in
// combat; a native attack on the player's base brings an interlude.
static void battle_report(struct battle *b) {
    int fa = b->faction_att, fd = b->faction_def, tx = b->tx, ty = b->ty;
    if (b->hp_att_left && b->credits) {
        game_factions[fa].energy_credits += b->credits;
        game_console_update_data(game_console, 0);
        for (int k = 0; k < 8; k++) {
            int nx = xrange(tx + game_radius_near_x[k]), ny = ty + game_radius_near_y[k];
            if (!on_map(nx, ny)) continue;
            for (int v = veh_at(nx, ny); v >= 0; v = veh_(v)->next_in_stack) {
                struct veh *U = veh_(v);
                if (U->faction) continue;
                int life = U->flags >> 3 & 3;
                U->flags &= (uint16_t)~(life << 3);
                U->flags |= (uint16_t)(game_range(life + 1, 0, 3) << 3);
                game_draw_tile(nx, ny, 2);
            }
        }
    }
    int current = *game_current_faction;
    if (b->render) {
        if (b->hp_att_left) {
            game_draw_tile(b->x, b->y, 1);
            game_draw_tile_fixup(tx, ty, 2, 1);
        } else {
            game_draw_tile_fixup(b->x, b->y, 2, 1);
            game_draw_tile(tx, ty, 1);
        }
        game_do_all_draws();
        char num[16];
        if (b->hp_att_left && b->credits && fa == current) {
            game_parse_num(0, b->credits);
            notify(at_text(0x68a258));
        } else if ((b->killed > 1 || b->damaged) && (fa == current || fd == current)) {
            game_parse_num(0, b->killed);
            game_parse_num(1, b->damaged);
            game_battle_text[0] = 0;
            strcat(game_battle_text, at_text((fa == current) == (b->hp_att_left != 0) ? 0x68a260 : 0x68a268));
            if (b->killed > 1) {
                if (b->damaged) strcat(game_battle_text, at_text(0x68a270));
            } else if (b->damaged) {
                strcat(game_battle_text, at_text(0x68a274));
            }
            notify(game_battle_text);
        }
        if (b->captured) {
            game_parse_num(0, b->captured);
            if (fd == current) {
                game_battle_text[0] = 0;
                strcat(game_battle_text, at_text(0x68a278));
                if (!has_treaty(fa, fd, TREATY_VENDETTA)) {
                    say_noun(0, fa);
                    snprintf(num, sizeof num, "%d", 3);
                } else {
                    snprintf(num, sizeof num, "%d", game_range(b->captured, 1, 2));
                }
                strcat(game_battle_text, num);
                notify(game_battle_text);
            } else if (fa == current) {
                game_battle_text[0] = 0;
                strcat(game_battle_text, at_text(0x68a284));
                snprintf(num, sizeof num, "%d", game_range(b->captured, 1, 2));
                strcat(game_battle_text, num);
                notify(game_battle_text);
            }
        }
        if (b->nerve && strlen(b->base_name)) {
            say(0, b->base_name);
            notify(at_text(0x68a290));
        }
        if (b->moved) {
            say(0, (char *)veh_proto(b->survivor)->name);
            say_adjective(1, fd);
            if (fd == current) notify(at_text(0x68a29c));
            else if (fa == current) notify(at_text(0x68a2ac));
        }
        game_status_win_redraw(game_status_win);
        if (b->render_base) {
            game_base_win_focus(game_base_win);
            game_graphic_win_redraw(game_main_interface);
            game_do_all_draws();
        }
        if (*game_more_preferences & MPREF_PAUSE_AFTER_BATTLES) battle_pause(0x68a2cc, 0x68a2bc);
    }
    static const uint32_t reports[] = {0x68a2d8, 0x68a2e8, 0x68a2f8};
    if (b->report >= 1 && b->report <= 3) {
        if (b->report == 3) say_noun(0, fa);
        else say_adjective(0, fa);
        say_noun(1, fd);
        game_netmsg_pop(game_netmsg, at_text(reports[b->report - 1]), -5000, 0, NULL);
    }
    game_protos[b->proto_att].combat_factions |= (uint8_t)bit(fd);
    game_protos[b->proto_def].combat_factions |= (uint8_t)bit(fa);
    if (b->interlude) game_interlude(0, NULL, 1, 0);
}

// A battle between the unit and whatever defends (tx, ty), which lies at
// `offset` from it in the radius table (table_offset) or among the eight
// squares around. With option 0 only the odds are weighed (and the
// defender put in *def_id); else the fight is made, as these phases
// follow. Returns 0 when the attacker died, was a missile (spent) or
// shelled, or the battle went no further (a single round, a planet
// buster), 1 when it lives on (and when the player called it off), and
// 2 when the player kept a treaty. A ship or an air unit that ranges
// past one turn is done for the turn afterwards, the unit the game is
// moving (as Thinker notes, a road move more spent as well); the AI's air
// unit that took an empty base and has as many turns aloft as its range
// returns 1 even as a missile.
int battle_fight_2(int veh_att, int offset, int tx, int ty, int table_offset, int option, int *def_id) {
    struct battle battle = {.veh_att = veh_att, .offset = offset, .tx = tx, .ty = ty, .table_offset = table_offset,
                            .option = option, .def_id = def_id},
                  *b = &battle;
    int r = battle_setup(b);
    if (!option) return r;
    r = battle_declare(b);
    if (r >= 0) return r;
    battle_diplomacy(b);
    battle_modifiers(b);
    if (battle_confirm(b)) return 1;
    int fa = b->faction_att, fd = b->faction_def, current = *game_current_faction;
    if (b->nerve) {
        game_atrocity(fa, fd, b->base >= 0, 0);
        b->base_name[0] = 0;
        if (b->base >= 0) strcpy(b->base_name, game_bases[b->base].name);
    }
    struct veh *A = veh_(veh_att);
    if (A->order == ORDER_MOVE_TO) {
        A->order = 0;
        A->state &= ~(uint32_t)VEH_STATE_ORDERS;
    }
    b->render = fa == current || fd == current || (*game_state & GAME_STATE_OMNISCIENT);
    const struct veh *D = veh_(b->veh_def);
    b->render_tile = (A->faction == current && (A->flags & 0x440) != 0x440) || (A->visibility & bit(current)) ||
                     (D->faction == current && (D->flags & 0x440) != 0x440) || (D->visibility & bit(current));
    uint32_t show = game_factions[current].diplo_status[fa] & TREATY_PACT ? *game_preferences & PREF_SHOW_ALLY_MOVES
                                                                          : *game_preferences & PREF_SHOW_ENEMY_MOVES;
    if (show) b->render |= b->render_tile;
    if (b->combat == COMBAT_ARTILLERY) return battle_artillery(b);
    if (battle_rounds(b)) return 0;
    b->hp_att_left = veh_hp(veh_att);
    battle_shown(b);
    if (b->single && veh_hp(veh_att) > 0 && veh_hp(b->veh_def) > 0) {
        if (*game_net_multiplayer && fd == current) {
            report_units(b, b->veh_def, current);  // the original passes the unit for its design
            notify(at_text(0x68a208));
        }
        return 0;
    }
    // A third faction whose land this is, at war with the defender and at
    // peace with the attacker, takes offence (and is counted a gift).
    if (!is_sea(tx, ty)) {
        int o = tile_at(tx, ty)->territory;
        const struct faction_data *O = faction_any(o);
        if (o != fa && o != fd && (O->diplo_status[fd] & TREATY_VENDETTA) && (O->diplo_status[fa] & TREATY_BROKEN_BY_ATTACK)) {
            int32_t *gifts = game_factions[fa].diplo_gifts;
            if (!gifts[o]) gifts[o] = 1;
            cause_friction(o, fa, -1);
        }
    }
    // Promotion: the winner's strength and the loser's, each by reactor and
    // the hit points it ended with (the winner's share of those it began
    // with, the loser's of its most); a green winner always, else when a
    // roll up to both comes up within the loser's (a defender's also when
    // below elite in a base). Not for beating an artifact.
    if (b->hp_att_left) {
        b->off = veh_proto(veh_att)->reactor * veh_hp(veh_att) * b->off / b->hp_att;
        b->def = veh_proto(b->veh_def)->reactor * b->hp_def * b->def / max_hp(veh_(b->veh_def));
        if (veh_proto(b->veh_def)->plan != PLAN_ARTIFACT &&
            (morale_veh(veh_att, 1, 0) <= 1 || game_rnd(b->off + b->def) <= b->def))
            promote(veh_att);
        if (!fa) veh_(veh_att)->home_base = veh_(b->veh_def)->home_base;
    } else {
        b->off = veh_proto(veh_att)->reactor * b->hp_att * b->off / max_hp(veh_(veh_att));
        b->def = veh_proto(b->veh_def)->reactor * veh_hp(b->veh_def) * b->def / b->hp_def;
        if (veh_proto(veh_att)->plan != PLAN_ARTIFACT &&
            (morale_veh(b->veh_def, 1, 0) <= 1 || game_rnd(b->off + b->def) <= b->off ||
             (b->base >= 0 && morale_veh(b->veh_def, 1, 0) < 4)))
            promote(b->veh_def);
        if (!fd) veh_(b->veh_def)->home_base = veh_(veh_att)->home_base;
    }
    b->killed = b->captured = b->credits = b->damaged = 0;
    if (!b->marine && !b->moved) {
        if (b->hp_att_left) battle_won(b);
        else battle_lost(b);
        if (fa && fd) game_mon_enemy_destroyed(b->hp_att_left ? fa : fd, b->hp_att_left ? fd : fa);
        if (b->captured && !(*game_human_factions & bit(fd)) && !has_treaty(fa, fd, TREATY_VENDETTA)) {
            cause_friction(fd, fa, 5);
            treaty_on(fa, fd, TREATY_WANT_TO_TALK);
        }
    }
    battle_report(b);
    if (!b->hp_att_left) return 0;
    int cv = *game_current_veh;
    struct veh *C = veh_(cv);
    const struct rules_chassis *ch = &game_chassis[veh_proto(cv)->chassis];
    if ((ch->triad == TRIAD_AIR && ch->range != 1) || ch->triad == TRIAD_SEA) {
        veh_skip(cv);
        C->moves_spent += (uint8_t)game_rules_basic->move_rate_roads;
    }
    int ret = 1;
    if (game_chassis[veh_proto(cv)->chassis].missile) {
        kill(cv);
        ret = 0;
    }
    if (!(*game_human_factions & bit(fa)) && b->base >= 0 && veh_who(tx, ty) < 0) {
        ch = &game_chassis[veh_proto(cv)->chassis];
        if (ch->triad == TRIAD_AIR && ch->range && C->movement_turns + 1 >= ch->range) return 1;
    }
    return ret;
}

// The unit's attack on the square at offset (in the radius table, or
// among the eight around).
int battle_fight(int veh, int offset, int table_offset, int option, int *def_id) {
    const struct veh *V = veh_(veh);
    int x = wrap_x(V->x + (table_offset ? game_radius_x : game_radius_near_x)[offset]);
    int y = V->y + (table_offset ? game_radius_y : game_radius_near_y)[offset];
    return battle_fight_2(veh, offset, x, y, table_offset, option, def_id);
}

// ---- Shadow mode -----------------------------------------------------------------

extern struct hook_module combat_module;
#define COMBAT (&combat_module)

SHADOW3(COMBAT, int, vulnerable, int, int, int)
SHADOW1(COMBAT, int, steal_energy, int)
// Its text goes to the battle text and a held parse slot.
SHADOW_WRITER(COMBAT, int, success_rates, (int a, int b, int c, int d), (a, b, c, d), WRITES(game_battle_text, 256),
              "%d, %d, %d, %d", a, b, c, d)
SHADOW_WRITER(COMBAT, int, act_of_aggression, (int a, int b), (a, b), hooks_writes_game(&w_), "%d, %d", a, b)
SHADOW_WRITER(COMBAT, int, interceptor, (int a, int b, int c, int d), (a, b, c, d), hooks_writes_game(&w_),
              "%d, %d, %d, %d", a, b, c, d)

// The counts the kills add to, on the caller's stack, are compared too.
#define KILL_WRITES                \
    hooks_writes_game(&w_);        \
    if (b) WRITES(b, sizeof *b);   \
    if (c) WRITES(c, sizeof *c);   \
    if (d) WRITES(d, sizeof *d)
#define KILL_PARAMS (int a, int *b, int *c, int *d, int e, int f)
#define KILL_ARGS (a, b, c, d, e, f)
SHADOW_WRITER(COMBAT, int, battle_kill, KILL_PARAMS, KILL_ARGS, KILL_WRITES, "%d, %d, %d", a, e, f)
SHADOW_WRITER(COMBAT, void, battle_kill_stack, KILL_PARAMS, KILL_ARGS, KILL_WRITES, "%d, %d, %d", a, e, f)
SHADOW_WRITER(COMBAT, void, battle_kill_chaff, KILL_PARAMS, KILL_ARGS, KILL_WRITES, "%d, %d, %d", a, e, f)

// tech_pick() builds the window a human chooses in, and lists the techs
// in a dialog that allocates from the game's heap even for the AI, so a
// check can't run it. Instead it is called for real just before the
// check, as the first thing steal_tech() would do once past its tests,
// and a quiet hook gives both runs its answer. A sweep makes one up.
static int pick_fixed, pick_answer;
static int (*orig_tech_pick)(int faction, int unk, int other, char *label);
static struct hook_stats stats_tech_pick = {"(fixed tech_pick)"};
static int shadow_tech_pick(int faction, int unk, int other, char *label) {
    if (!pick_fixed) return orig_tech_pick(faction, unk, other, label);
    stats_tech_pick.calls++;
    return pick_answer;
}
SHADOW_WRITER_CHECK(int, steal_tech, (int a, int b, int c), (a, b, c), orig_steal_tech(a, b, c), hooks_writes_game(&w_),
                    "%d, %d, %d", a, b, c)
static int shadow_steal_tech(int a, int b, int c) {
    if (hooks_nested) return orig_steal_tech(a, b, c);
    if (shadow_bypass()) return steal_tech(a, b, c);
    stats_steal_tech.calls++;
    shadow_tick(COMBAT);
    if (!a || !b || (*game_net_multiplayer && (*game_human_factions & bit(a)) && a != *game_current_faction))
        return check_steal_tech(a, b, c);
    pick_answer = game_tech_pick(a, 0, b, at_text(c ? 0x68a334 : 0x68a340));
    pick_fixed = 1;
    int r = check_steal_tech(a, b, c);
    pick_fixed = 0;
    return r;
}

// A human defender playing here is asked whether to give up a pod: the
// original runs unchecked.
static int asks_player(int faction_def) {
    return !*game_net_multiplayer && (*game_human_factions & bit(faction_def)) && faction_def == *game_current_faction;
}
SHADOW_WRITER_CHECK(int, shoot, (int a, int b, int c, int d), (a, b, c, d), orig_shoot(a, b, c, d), hooks_writes_game(&w_),
                    "%d, %d, %d, %d", a, b, c, d)
static int shadow_shoot(int a, int b, int c, int d) {
    if (hooks_nested) return orig_shoot(a, b, c, d);
    if (shadow_bypass()) return shoot(a, b, c, d);
    stats_shoot.calls++;
    shadow_tick(COMBAT);
    if (asks_player(b)) return orig_shoot(a, b, c, d);
    return check_shoot(a, b, c, d);
}

// A planet buster is checked only among AI factions (a human may be asked
// to give up a pod, and the atrocity ends pacts with a dialog), and only
// while its explosion's sound isn't playing: the original waits for it,
// pumping messages, and a check holds the sounds back, so one already
// playing would keep the pump running inside the check.
static int busting_checkable(void) { return !*game_human_factions && !fx_playing(FX_PB_BLAST); }
SHADOW_WRITER_CHECK(void, planet_busting, (int a, int b, int c), (a, b, c), orig_planet_busting(a, b, c),
                    hooks_writes_game(&w_), "%d, %d, %d", a, b, c)
static void shadow_planet_busting(int a, int b, int c) {
    if (hooks_nested) {
        orig_planet_busting(a, b, c);
        return;
    }
    if (shadow_bypass()) {
        planet_busting(a, b, c);
        return;
    }
    stats_planet_busting.calls++;
    shadow_tick(COMBAT);
    if (!busting_checkable()) orig_planet_busting(a, b, c);
    else check_planet_busting(a, b, c);
}

// A battle is checked only among AI factions: a human attacker is asked
// about treaties, nerve gas and the odds, and a human defender may be
// told of a surprise attack. Nor is nerve gas the AI would use: the
// atrocity brings a dialog of its own. Also left unchecked: network
// and hotseat games, a base at the target with messages listed (its fall
// renumbers them on the heap, see base_kill), and a planet buster while
// its blast plays.
int battle_checkable(int veh, int tx, int ty) {
    int fa = veh_(veh)->faction, fd = -1;
    if (*game_net_multiplayer || *game_pbem || game_is_human(fa)) return 0;
    for (int v = 0; v < *game_veh_count; v++)
        if (game_vehs[v].x == tx && game_vehs[v].y == ty) {
            fd = game_vehs[v].faction;
            if (game_is_human(fd)) return 0;
        }
    if (fd >= 0 && has_abil(veh_(veh)->proto, ABL_NERVE_GAS) && use_nerve_gas(fa, fd)) return 0;
    int b = base_at(tx, ty);
    if (b >= 0 && (game_is_human(game_bases[b].faction) || base_listed(b))) return 0;
    const struct unit_proto *P = veh_proto(veh);
    return !(P->plan == PLAN_PLANET_BUSTER_ && P->reactor && !busting_checkable());
}
// The defender's number, on the caller's stack, is compared too.
#define BATTLE_WRITES       \
    hooks_writes_game(&w_); \
    if (g) WRITES(g, sizeof *g)
SHADOW_WRITER_CHECK(int, battle_fight_2, (int a, int b, int c, int d, int e, int f, int *g), (a, b, c, d, e, f, g),
                    orig_battle_fight_2(a, b, c, d, e, f, g), BATTLE_WRITES, "%d, %d, %d, %d, %d, %d", a, b, c, d, e, f)
SHADOW_WRITER_CHECK(int, battle_fight, (int a, int b, int e, int f, int *g), (a, b, e, f, g),
                    orig_battle_fight(a, b, e, f, g), BATTLE_WRITES, "%d, %d, %d, %d", a, b, e, f)
// The attacker's step towards its target isn't shown (quiet.c).
extern int quiet_drop_scoot;
// The base window and the message list are told of a base that fell in a
// check afterwards, as base_kill's check does.
static void after_battle(int base, int bases) {
    if (base < 0 || *game_base_count >= bases) return;
    game_base_win_base_killed(game_base_win, base);
    game_listbox_update_changes(game_message_box);
}
static int shadow_battle_fight_2(int a, int b, int c, int d, int e, int f, int *g) {
    if (hooks_nested) return orig_battle_fight_2(a, b, c, d, e, f, g);
    if (shadow_bypass()) return battle_fight_2(a, b, c, d, e, f, g);
    stats_battle_fight_2.calls++;
    shadow_tick(COMBAT);
    if (!battle_checkable(a, c, d)) return orig_battle_fight_2(a, b, c, d, e, f, g);
    int base = base_at(c, d), bases = *game_base_count;
    quiet_drop_scoot++;
    int r = check_battle_fight_2(a, b, c, d, e, f, g);
    quiet_drop_scoot--;
    after_battle(base, bases);
    return r;
}
static void battle_target(int veh, int offset, int table_offset, int *x, int *y) {
    const struct veh *V = veh_(veh);
    *x = wrap_x(V->x + (table_offset ? game_radius_x : game_radius_near_x)[offset]);
    *y = V->y + (table_offset ? game_radius_y : game_radius_near_y)[offset];
}
static int shadow_battle_fight(int a, int b, int e, int f, int *g) {
    if (hooks_nested) return orig_battle_fight(a, b, e, f, g);
    if (shadow_bypass()) return battle_fight(a, b, e, f, g);
    stats_battle_fight.calls++;
    shadow_tick(COMBAT);
    int x, y;
    battle_target(a, b, e, &x, &y);
    if (!battle_checkable(a, x, y)) return orig_battle_fight(a, b, e, f, g);
    int base = base_at(x, y), bases = *game_base_count;
    quiet_drop_scoot++;
    int r = check_battle_fight(a, b, e, f, g);
    quiet_drop_scoot--;
    after_battle(base, bases);
    return r;
}

// The sweep's own random numbers, so that the game's are left alone.
static uint32_t sweep_seed;
static int rnd(int n) {
    sweep_seed = sweep_seed * 1103515245u + 12345u;
    return n > 0 ? (int)((sweep_seed >> 16) % (uint32_t)n) : 0;
}

// A unit whose stack links hold together.
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
// A random unit on the map, with sound stack links, or -1.
static int some_unit(void) {
    int n = *game_veh_count;
    for (int tries = 0; tries < 20 && n > 0; tries++) {
        int v = rnd(n);
        if (on_the_map(game_vehs[v].x, game_vehs[v].y) && stack_sound(v)) return v;
    }
    return -1;
}
// Another faction's unit on the map, or -1.
static int enemy_unit(int faction) {
    int n = *game_veh_count;
    for (int tries = 0; tries < 30 && n > 0; tries++) {
        int v = rnd(n);
        if (game_vehs[v].faction != faction && on_the_map(game_vehs[v].x, game_vehs[v].y) && stack_sound(v)) return v;
    }
    return -1;
}
static int alive(int f) { return f > 0 && f < FACTION_COUNT && (*game_factions_alive & bit(f)); }
static int some_faction(int not) {
    for (int tries = 0; tries < 20; tries++) {
        int f = 1 + rnd(FACTION_COUNT - 1);
        if (f != not && alive(f)) return f;
    }
    return -1;
}

// The pure ones, and success_rates() under each probe setting.
static void sweep_probes(void) {
    int bases = *game_base_count, w = *game_map_width, h = *game_map_height;
    for (int f = 0; f < FACTION_COUNT; f++) {
        for (int b = 0; b < bases; b += 1 + bases / 8) check_vulnerable(f, game_bases[b].x, game_bases[b].y);
        for (int k = 0; k < 6; k++) check_vulnerable(f, rnd(w), rnd(h));
    }
    for (int b = 0; b < bases; b++) check_steal_energy(b);
    for (int k = 0; k < 4 && bases; k++) {  // reserves and population varied
        struct faction_data *F = &game_factions[game_bases[rnd(bases)].faction];
        int credits = F->energy_credits, pop = F->pop_total;
        F->energy_credits = rnd(3) ? rnd(2000) : -rnd(50);
        F->pop_total = rnd(4) ? rnd(200) : 0;
        for (int b = 0; b < bases; b++) check_steal_energy(b);
        F->energy_credits = credits, F->pop_total = pop;
    }
    int algo = *game_probe_algo, target = *game_probe_target, hsa = *game_probe_target_hsa;
    for (int v = 0; v < 8; v++) {
        *game_probe_algo = v & 1, *game_probe_target_hsa = v >> 1 & 1;
        *game_probe_target = v < 2 ? target : rnd(FACTION_COUNT);
        int probe = game_factions[*game_probe_target].se[SE_PROBE];
        if (v >= 4) game_factions[*game_probe_target].se[SE_PROBE] = rnd(7) - 3;
        for (int morale = -1; morale <= 7; morale++)
            for (int diff = -1; diff <= 3; diff++) {
                int b = rnd(3) || !bases ? -1 : rnd(bases);
                if (b >= 0 && rnd(2)) {  // with or without a Covert Ops Center
                    int offset;
                    uint32_t mask;
                    bitmask(FAC_COVERT_OPS_CENTER, &offset, &mask);
                    game_bases[b].facilities_built[offset] ^= (uint8_t)mask;
                }
                check_success_rates(rnd(3), morale, diff, b);
            }
        game_factions[*game_probe_target].se[SE_PROBE] = probe;
    }
    *game_probe_algo = algo, *game_probe_target = target, *game_probe_target_hsa = hsa;
}

// Attacks between factions, with their treaties varied; and techs
// stolen or acquired between AI factions.
static void sweep_diplomacy(void) {
    static const uint32_t bits[] = {TREATY_PACT, TREATY_TREATY, TREATY_TRUCE, TREATY_VENDETTA, TREATY_COMMLINK};
    for (int f = 1; f < FACTION_COUNT; f++) {
        if (!alive(f)) continue;
        for (int k = 0; k < 2; k++) {
            int v = some_faction(f);
            if (v < 0) continue;
            for (unsigned i = 0; i < sizeof bits / sizeof bits[0]; i++)
                if (!rnd(3)) game_factions[v].diplo_status[f] ^= bits[i];
            check_act_of_aggression(f, v);
        }
        int v = some_faction(f);
        if (v < 0) continue;
        pick_fixed = 1;
        pick_answer = rnd(3) ? rnd(TECH_COUNT) : -1;
        check_steal_tech(f, v, rnd(2));
        pick_fixed = 0;
    }
    check_steal_tech(0, 1, 1);
}

// An existing prototype of the given plan, or -1.
static int proto_of_plan(int plan) {
    for (int p = 0; p < PROTO_COUNT; p++)
        if ((game_protos[p].flags & PROTO_FLAG_ACTIVE) && game_protos[p].plan == plan) return p;
    return -1;
}

// Units killed: alone and with their stacks, by an enemy or by no one,
// as artifacts (on land, at sea, objectives), as the Assistant Worm, as
// native life, badly damaged, and with the counts left out.
static void sweep_kills(void) {
    int artifact = proto_of_plan(PLAN_ARTIFACT);
    uint32_t prefs = *game_preferences;
    for (int k = 0; k < 12; k++) {
        int v = some_unit();
        if (v < 0) return;
        struct veh *V = &game_vehs[v];
        int a = rnd(5) ? enemy_unit(V->faction) : -1;
        int fa = a >= 0 ? game_vehs[a].faction : rnd(2) ? -1 : rnd(FACTION_COUNT);
        switch (k % 6) {
        case 1:
            if (artifact >= 0) V->proto = (int16_t)artifact;
            if (rnd(2)) V->flags |= VEH_FLAG_UNK_20;
            break;
        case 2:
            V->state |= VEH_STATE_UNK_800000;
            if (rnd(3)) *game_preferences &= ~(uint32_t)PREF_NO_INTERLUDES;
            else *game_preferences |= PREF_NO_INTERLUDES;
            if (a >= 0 && rnd(2)) game_vehs[a].home_base = -1;
            break;
        case 3:
            V->faction = 0;
            break;
        case 4:
            for (int u = v; u >= 0; u = game_vehs[u].next_in_stack)
                if (rnd(2)) game_vehs[u].damage = (uint8_t)rnd(60);
            break;
        case 5:
            // The attacker among the units killed (its number shifts as
            // they go), the others native life it is paid for.
            a = veh_top(v), fa = game_vehs[a].faction;
            if (!fa) game_vehs[a].faction = (uint8_t)(fa = 1);
            for (int u = game_vehs[a].next_in_stack; u >= 0; u = game_vehs[u].next_in_stack)
                if (rnd(3)) game_vehs[u].faction = 0;
            break;
        }
        int killed = 0, captured = 0, credits = 0;
        int *pk = rnd(5) ? &killed : NULL, *pc = rnd(5) ? &captured : NULL;
        int *pr = rnd(5) || k % 6 == 5 ? &credits : NULL;
        check_battle_kill(v, pk, pc, pr, a, fa);
        check_battle_kill_stack(v, pk, pc, pr, a, fa);
        check_battle_kill_chaff(v, pk, pc, pr, a, fa);
        *game_preferences = prefs;
    }
}

// Air superiority units: a unit at a base, on an airbase or in the open
// made one (its design given an air chassis and the ability), meeting
// attacks on squares around it, with the player either side or neither.
static void sweep_interceptors(void) {
    int air = -1;
    for (int c = 0; c < CHASSIS_COUNT; c++)
        if (game_chassis[c].triad == TRIAD_AIR && !game_chassis[c].missile) air = c;
    if (air < 0) return;
    int current = *game_current_faction;
    for (int k = 0; k < 6; k++) {
        int v = some_unit();
        if (v < 0) return;
        struct veh *V = &game_vehs[v];
        struct unit_proto *P = &game_protos[V->proto];
        P->chassis = (uint8_t)air;
        if (rnd(4)) P->abilities |= ABL_AIR_SUPERIORITY;
        if (rnd(3)) V->damage = (uint8_t)rnd(30);
        struct map_tile *t = tile_at(V->x, V->y);
        if (k & 1) t->bits |= MAP_AIRBASE;
        int f = V->faction, att = some_faction(f);
        for (int j = 0; j < 3; j++) {
            int i = rnd(SHOOT_SQUARES);
            int x = wrap_x(V->x + 2 * game_radius_x[i] - (k & 1) * game_radius_x[i]), y = V->y + game_radius_y[i];
            if (!on_the_map(x, y)) continue;
            int r = rnd(3);
            *game_current_faction = r == 0 ? f : r == 1 && att > 0 ? att : current;
            check_interceptor(f, att, x, y);
        }
        *game_current_faction = current;
    }
}

// Missiles at bases and around them, with pods (some used), flechette
// defenses near, the player on either side or neither, a human defender,
// and in a network game or not.
static void sweep_shoot(void) {
    int bases = *game_base_count;
    int current = *game_current_faction, net = *game_net_multiplayer;
    uint8_t humans = *game_human_factions;
    int offset;
    uint32_t mask;
    bitmask(FAC_FLECHETTE_DEFENSE, &offset, &mask);
    for (int k = 0; k < 16 && bases; k++) {
        int b = rnd(bases), d = game_bases[b].faction, a = some_faction(d);
        if (a < 0) continue;
        struct faction_data *D = &game_factions[d];
        D->satellites[3] = rnd(4) ? rnd(4) : 0;
        D->odp_deployed = rnd(3);
        if (rnd(2)) game_bases[rnd(bases)].facilities_built[offset] ^= (uint8_t)mask;
        if (rnd(2)) game_bases[b].facilities_built[offset] ^= (uint8_t)mask;
        int r = rnd(4);
        *game_current_faction = r == 0 ? d : r == 1 ? a : current;
        *game_human_factions = rnd(3) ? humans : (uint8_t)(humans | bit(d));
        *game_net_multiplayer = rnd(4) ? net : !net;
        int i = rnd(3) ? 0 : rnd(SHOOT_SQUARES);
        int x = wrap_x(game_bases[b].x + game_radius_x[i]), y = game_bases[b].y + game_radius_y[i];
        if (on_the_map(x, y)) check_shoot(a, d, x, y);
    }
    *game_current_faction = current, *game_net_multiplayer = net, *game_human_factions = humans;
}

// Whether every faction with a base within four squares of (x, y) has at
// least three, so that a blast there eliminates no one.
static int spared(int x, int y) {
    int n[FACTION_COUNT] = {0};
    for (int b = 0; b < *game_base_count; b++) n[game_bases[b].faction]++;
    for (int b = 0; b < *game_base_count; b++)
        if (vector_dist(x, y, game_bases[b].x, game_bases[b].y) <= 4 && n[game_bases[b].faction] < 3) return 0;
    return 1;
}

// A planet buster (a unit made one, with a reactor of 1 or 2) aimed at a
// base, or at a square near it, where it eliminates no faction, among
// AI factions, with the defenders' pods varied.
static void sweep_busters(void) {
    if (fx_playing(FX_PB_BLAST)) return;
    uint8_t humans = *game_human_factions;
    *game_human_factions = 0;
    int bases = *game_base_count;
    for (int k = 0; k < 2 && bases; k++) {
        int b = rnd(bases), d = game_bases[b].faction;
        if (!spared(game_bases[b].x, game_bases[b].y)) continue;
        int v = enemy_unit(d);
        if (v < 0 || !game_vehs[v].faction) continue;
        struct unit_proto *P = &game_protos[game_vehs[v].proto];
        P->plan = PLAN_PLANET_BUSTER_;
        P->reactor = (uint8_t)(1 + rnd(2));
        struct faction_data *D = &game_factions[d];
        D->satellites[3] = rnd(2) ? rnd(3) : 0;
        D->odp_deployed = 0;
        int i = rnd(2) ? 0 : rnd(9);
        int x = wrap_x(game_bases[b].x + game_radius_x[i]), y = game_bases[b].y + game_radius_y[i];
        if (on_the_map(x, y)) check_planet_busting(v, x, y);
    }
    *game_human_factions = humans;
}

// A random design in use, or -1.
static int some_proto(void) {
    for (int tries = 0; tries < 40; tries++) {
        int p = rnd(PROTO_COUNT);
        if (game_protos[p].flags & PROTO_FLAG_ACTIVE) return p;
    }
    return -1;
}

// A unit's design varied for one battle (the copy is put back): another
// chassis (any triad, missiles), weapon (psi among them), armor, reactor
// or plan (probe teams, artifacts, formers, colony pods, supply crawlers,
// naval superiority), or an ability added (artillery, air superiority,
// marines, comm jammer, dissociative wave, nerve gas).
static void vary_design(int veh) {
    struct veh *V = veh_(veh);
    if (!rnd(3)) {
        int p = some_proto();
        if (p >= 0) V->proto = (int16_t)p;
    }
    struct unit_proto *P = &game_protos[V->proto];
    static const uint32_t abilities[] = {0x8000, ABL_AIR_SUPERIORITY, ABL_MARINE_DETACHMENT, ABL_COMM_JAMMER,
                                         ABL_DISSOCIATIVE_WAVE, ABL_NERVE_GAS};
    static const uint8_t plans[] = {PLAN_PROBE, PLAN_ARTIFACT, PLAN_TERRAFORM, PLAN_COLONY_, PLAN_SUPPLY_,
                                    PLAN_NAVAL_SUPERIORITY_, 0, 1, 2};
    for (int n = rnd(3); n > 0; n--) {
        switch (rnd(6)) {
        case 0: P->chassis = (uint8_t)rnd(CHASSIS_COUNT); break;
        case 1: P->weapon = (uint8_t)rnd(WEAPON_COUNT); break;
        case 2: P->armor = (uint8_t)rnd(ARMOR_COUNT); break;
        case 3: P->reactor = (uint8_t)(1 + rnd(4)); break;
        case 4: P->plan = plans[rnd(sizeof plans)]; break;
        default: P->abilities |= abilities[rnd(sizeof abilities / sizeof abilities[0])]; break;
        }
    }
}

// Whether a unit of the faction may be put on the square: no other
// faction's units or base there.
static int free_for(int faction, int x, int y) {
    if (!on_the_map(x, y)) return 0;
    for (int v = 0; v < *game_veh_count; v++)
        if (game_vehs[v].x == x && game_vehs[v].y == y && game_vehs[v].faction != faction) return 0;
    int b = base_at(x, y);
    return b < 0 || game_bases[b].faction == faction;
}

// Battles on the copy: a unit of one faction put next to (or two squares
// from, for artillery) a stack of another, in a base or the open, each
// side varied in design, morale, damage, moves and orders; the target's
// square with or without a bunker, airbase and fungus; treaties between
// them and with the player varied; the player either side or neither,
// and at times human (their questions held, so answered no); the flags
// and the option to only weigh the odds varied. A base that could fall
// is one with no messages listed of a faction with three or more, and a
// planet buster is left out (sweep_busters has those).
static void sweep_battles(void) {
    uint8_t humans = *game_human_factions;
    int current = *game_current_faction, flags = *game_veh_attack_flags, cv = *game_current_veh;
    uint32_t prefs = *game_preferences, more = *game_more_preferences, gstate = *game_state;
    int counts[FACTION_COUNT] = {0};
    for (int b = 0; b < *game_base_count; b++) counts[game_bases[b].faction]++;
    for (int k = 0; k < 28; k++) {
        int t = k % 4 == 0 && *game_base_count ? -1 : some_unit();
        int tx, ty;
        if (t < 0) {  // a base's defenders
            int b = rnd(*game_base_count);
            tx = game_bases[b].x, ty = game_bases[b].y;
            t = stack_fix(veh_at(tx, ty));
            if (t < 0) continue;
        }
        tx = game_vehs[t].x, ty = game_vehs[t].y;
        int fd = game_vehs[t].faction;
        int base = base_at(tx, ty);
        if (base >= 0 && (base_listed(base) || counts[game_bases[base].faction] < 3)) continue;
        int a = enemy_unit(fd);
        if (a < 0) continue;
        int fa = game_vehs[a].faction;
        int table = !rnd(5), dir = table ? 9 + rnd(16) : rnd(8);
        int ax = wrap_x(tx - game_radius_x[dir]), ay = ty - game_radius_y[dir];
        if (!table) ax = wrap_x(tx - game_radius_near_x[dir]), ay = ty - game_radius_near_y[dir];
        if (!free_for(fa, ax, ay)) continue;
        veh_drop(veh_lift(a), ax, ay);
        if (rnd(2)) vary_design(a);
        if (!rnd(3)) vary_design(t);
        if (veh_proto(a)->plan == PLAN_PLANET_BUSTER_) continue;
        int nerve = has_abil(game_vehs[a].proto, ABL_NERVE_GAS);
        struct veh *A = veh_(a), *D = veh_(t);
        A->morale = (uint8_t)rnd(7), D->morale = (uint8_t)rnd(7);
        if (rnd(2)) A->damage = (uint8_t)rnd(max_hp(A));
        if (rnd(2)) D->damage = (uint8_t)rnd(max_hp(D));
        A->moves_spent = (uint8_t)(rnd(2) ? 0 : rnd(9));
        if (!rnd(4)) A->state ^= VEH_STATE_NO_HASTE;
        if (!rnd(4)) D->state ^= VEH_STATE_ATTACKED;
        if (!rnd(5)) D->order = ORDER_HOLD_;
        if (!rnd(6)) A->order = ORDER_MOVE_TO;
        // A design varied may leave units with no hit points (an artifact's
        // one, a smaller reactor), which the game never fights with.
        int dead = !veh_hp(a);
        for (int v = 0; v < *game_veh_count; v++)
            if (game_vehs[v].x == tx && game_vehs[v].y == ty && !veh_hp(v)) dead = 1;
        if (dead) continue;
        struct map_tile *T = tile_at(tx, ty);
        if (!rnd(4)) T->bits ^= MAP_BUNKER;
        if (!rnd(5)) T->bits ^= MAP_AIRBASE;
        if (!rnd(4)) T->bits ^= MAP_FUNGUS;
        if (!rnd(6)) tile_at(ax, ay)->territory = (int8_t)rnd(FACTION_COUNT);
        static const uint32_t treaties[] = {TREATY_PACT, TREATY_TREATY, TREATY_TRUCE, TREATY_COMMLINK,
                                            TREATY_VENDETTA, TREATY_UNK_800};
        for (int i = 0; i < 2; i++)
            if (!rnd(3)) {
                uint32_t bits = treaties[rnd(sizeof treaties / sizeof treaties[0])];
                game_factions[i ? fa : fd].diplo_status[i ? fd : fa] ^= bits;
            }
        int r = rnd(6);
        *game_current_faction = r == 0 ? fd : r == 1 ? fa : current;
        if (!rnd(3)) game_factions[*game_current_faction].diplo_status[rnd(2) ? fa : fd] ^= TREATY_PACT;
        *game_human_factions = 0;
        r = rnd(6);
        if (r == 0) *game_human_factions = (uint8_t)bit(fd);
        if (r == 1) *game_human_factions = (uint8_t)bit(fa);
        if (r == 2) game_factions[fd].diff_level = rnd(4);
        if (r == 3) game_factions[fa].diff_level = rnd(4);
        // The AI's nerve gas brings the atrocity's dialog.
        if (nerve && !(*game_human_factions & bit(fa)) && use_nerve_gas(fa, fd)) {
            *game_human_factions = 0;
            *game_current_faction = current;
            continue;
        }
        *game_preferences = rnd(2) ? prefs : prefs ^ (rnd(2) ? PREF_SHOW_ENEMY_MOVES : PREF_FAST_BATTLES);
        *game_more_preferences = rnd(4) ? more : more ^ (rnd(2) ? MPREF_PAUSE_AFTER_BATTLES : MPREF_CONFIRM_ODDS);
        if (!rnd(8)) *game_state ^= GAME_STATE_OMNISCIENT;
        *game_veh_attack_flags = rnd(6) ? ATTACK_ASK | ATTACK_MADE : rnd(4);
        *game_current_veh = a;
        int option = rnd(8) ? 1 : 0, def_id = -2, *pd = rnd(2) ? &def_id : NULL;
        if (table || rnd(3)) check_battle_fight_2(a, dir, tx, ty, table, option, pd);
        else check_battle_fight(a, dir, table, option, pd);
        *game_human_factions = 0;
        *game_current_faction = current;
    }
    *game_human_factions = humans, *game_current_faction = current, *game_veh_attack_flags = flags, *game_current_veh = cv;
    *game_preferences = prefs, *game_more_preferences = more, *game_state = gstate;
}

// On a protected copy of the game, put back after.
static void combat_sweep(void) {
    if (!*game_map || *game_veh_count <= 0 || *game_base_count <= 0) return;
    int sweeping = hooks_sweeping;
    struct hook_writes outer = {.protect_only = 1};
    hooks_writes_game(&outer);
    hooks_writes_begin(&outer);
    hooks_sweeping = 1;
    sweep_seed = (uint32_t)*game_turn * 7919u + 17u;
    sweep_probes();
    sweep_diplomacy();
    sweep_kills();
    sweep_interceptors();
    sweep_shoot();
    sweep_busters();
    sweep_battles();
    hooks_writes_abandon(&outer);
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook combat_hooks[] = {
    HOOK(0x505d80, battle_kill, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),              // sub esp, 8
    HOOK(0x506130, battle_kill_stack, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x8b, 0x75, 0x08),  // push esi; mov esi, [ebp+8]
    HOOK(0x5061b0, battle_kill_chaff, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x8b, 0x75, 0x08),
    HOOK(0x50c2e0, act_of_aggression, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56, 0x8b, 0x75, 0x0c),  // push ebx; push esi; mov esi, [ebp+12]
    HOOK(0x50c340, steal_tech, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56, 0x8b, 0x75, 0x08),
    HOOK(0x50c4b0, steal_energy, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x45, 0x08),                  // mov eax, [ebp+8]
    HOOK(0x59e980, vulnerable, PUSH_EBP_MOV_EBP_ESP, 0x51, 0xa1, 0xcc, 0x64, 0x9a, 0x00),  // push ecx; mov eax, [0x9a64cc]
    HOOK(0x59eee0, success_rates, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x50),                 // sub esp, 0x50
    HOOK(0x506650, interceptor, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x18),
    HOOK(0x500560, shoot, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x1c),
    HOOK(0x500b60, planet_busting, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x34),
    {0x5beb70, (const uint8_t[]){PUSH_EBP_MOV_EBP_ESP, 0x6a, 0xff, 0x68, 0x5a, 0x2a, 0x66, 0x00}, 10, NULL,
     (void *)shadow_tech_pick, (void **)&orig_tech_pick, &stats_tech_pick, 1},  // push -1; push 0x662a5a
    HOOK(0x506a60, battle_fight, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x10),                    // push ebx; mov ebx, [ebp+16]
    HOOK(0x506af0, battle_fight_2, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0x38, 0x01, 0x00, 0x00),       // sub esp, 0x138
};
// The game seldom calls probe() (only for probe teams' missions), so its
// sweep comes with this one.
extern struct hook_module probe_module;
struct hook_module combat_module = {"combat", combat_hooks, HOOK_COUNT(combat_hooks), combat_sweep, &probe_module, -1};
