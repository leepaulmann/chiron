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
#include "shadow.h"
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
};
struct hook_module combat_module = {"combat", combat_hooks, HOOK_COUNT(combat_hooks), combat_sweep, NULL, -1};
