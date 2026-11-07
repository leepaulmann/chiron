// Bases: finishing what they build. Reimplemented from terranx.exe 2.0's
// disassembly; names follow OpenSMACX (Brendan Casey, GPL-3.0-or-later) and
// Thinker (MIT), which keeps the original base_production and patches calls
// out of it.
//
// The original keeps a popup object on its stack for the whole call (made at
// the start and taken apart on the way out, used only for the dialogs it
// shows the player), and reads the current base and its id afresh after the
// game functions it calls, which may move them: this does the same.
#include <stdio.h>
#include <string.h>

#include "base.h"
#include "game.h"
#include "score.h"
#include "shadow.h"
#include "veh.h"

static inline struct map_tile *tile(int x, int y) { return &(*game_map)[(x >> 1) + y * *game_map_half_width]; }
static inline int has_facility(int base, int facility) { return game_has_facility(&game_bases[base], facility); }
static inline int holds_project(int project, int faction) {
    int b = game_secret_projects[project];
    return b >= 0 && game_bases[b].faction == faction;
}
static inline int is_human(int faction) { return (*game_human_factions >> faction) & 1; }
static inline int is_alien(int faction) { return (game_faction_defs[faction].rule_flags & RULE_FLAG_ALIEN) != 0; }
// The original reads these from memory each time.
#define B (*game_current_base)
#define ID (*game_current_base_id)

enum {
    PLAN_ARTIFACT = 12,
    PROTO_MIND_WORMS = 8, PROTO_SEALURK = 14, PROTO_SPORE_LAUNCHER = 15,
    WEAPON_PROBE = 21, FAC_COVERT_OPS = 34, FAC_SUBSPACE_GENERATOR = 40, FAC_BIOENHANCEMENT = 30,
    FAC_PRESERVE = 31, FAC_TEMPLE = 32,
    SP_PLANETARY_TRANSIT = 7, SP_UNIVERSAL_TRANSLATOR = 20,
    SP_XENOEMPATHY_DOME = 8, SP_NETHACK_TERMINUS = 34, SP_CYBORG_FACTORY = 17,
    BASE_STATE_RIOTS = 0x2, BASE_STATE_COMBAT_LOSS = 0x8, BASE_STATE_ARTIFACT_LINKED = 0xc00,
    BASE_STATE_PRODUCTION_DONE = 0x800000, BASE_STATE_COLONY_1 = 0x1000000, BASE_STATE_COLONY_2 = 0x2000000,
    BASE_STATE_UNK_80000000 = 0x80000000,
    GOVERNOR_PRODUCTION = 0x1, GOVERNOR_NEW_UNITS_AUTO = 0x80,
    VEH_STATE_ON_ALERT = 0x200, VEH_STATE_NEW = 0x2000, VEH_STATE_ASSISTANT_WORM = 0x800000,
    VEH_FLAG_PLAYERS = 0x1000,
};

static inline int governed(const struct base *b) {
    return (b->governor & GOVERNOR_PRODUCTION) && (b->governor & GOVERNOR_ACTIVE);
}
static inline char *text(uint32_t addr) { return (char *)game_at(addr); }

// ---- The popup on the original's stack ---------------------------------------

static void pop_end(uint8_t *pop) {
    *(uint32_t *)pop = game_va(0x6695c8);
    *(uint32_t *)(pop + 0x444) = game_va(0x6695c0);
    game_popup_close(pop);
    *(uint32_t *)(pop + 0x3230) = game_va(0x669d58);
    *(uint32_t *)(pop + 0x3674) = game_va(0x669d50);
    game_scroll_close(pop + 0x3230);
    game_flat_button_dtor(pop + 0x4828);
    game_flat_button_dtor(pop + 0x3cdc);
    game_graphic_win_dtor(pop + 0x3230);
    game_base_pop_dtor(pop);
}

static void append_number(int n) {
    char num[16];
    snprintf(num, sizeof(num), "%d", n);
    strcat(game_str_buffer, num);
}

// What a human's base carries over: no more than the retooling exemption
// or its surplus, nor less than nothing.
static void carry_over(int human) {
    int limit = game_rules_basic->retool_exemption, surplus = B->surplus[RESOURCE_MINERAL];
    int keep = limit < surplus ? limit : surplus;
    if (B->minerals > keep && human) B->minerals = keep < 0 ? 0 : keep;
    B->minerals_2 = B->minerals;
}

static void reset(int base) { game_base_reset(base, 0); }

// ---- Units ----------------------------------------------------------------------

// The unit's starting morale: native life from the faction's projects and
// the base's facilities, others from the base's morale and training, plus
// one, and one more for a new prototype.
static int unit_morale(int proto, int faction, int first) {
    const struct unit_proto *p = &game_protos[proto];
    int m;
    if (proto < 64 && game_weapons[p->weapon].offense < 0) {
        int id = ID;
        m = holds_project(SP_XENOEMPATHY_DOME, faction);
        m += holds_project(SP_PHOLUS_MUTAGEN, faction);
        m += holds_project(SP_VOICE_OF_PLANET, faction);
        m += has_fac(FAC_PRESERVE, id, 0) != 0;
        m += has_fac(FAC_TEMPLE, id, 0) != 0;
        m += has_fac(FAC_BIOLOGY_LAB, id, 0) != 0;
        m += has_fac(FAC_BIOENHANCEMENT, id, 0) || holds_project(SP_CYBORG_FACTORY, faction) ? 2 : 1;
    } else {
        m = game_morale_mod(ID, faction, game_chassis[p->chassis].triad);
        if (p->weapon == WEAPON_PROBE) {
            int id = ID;
            if (has_facility(id, FAC_COVERT_OPS)) m += 2;
            if (holds_project(SP_NETHACK_TERMINUS, faction)) m++;
        }
        if (game_has_abil(proto, 0x4000)) m++;
        m += first ? 2 : 1;
    }
    return m < 0 ? 0 : m > 6 ? 6 : m;
}

// The message's picture: native life by its triad (sealurks apart), others
// by their triad, Progenitor pictures for the Progenitors.
static char *unit_picture(int proto, int faction) {
    const struct unit_proto *p = &game_protos[proto];
    int triad = game_chassis[p->chassis].triad, alien = is_alien(faction);
    if (proto < 64 && (game_weapons[p->weapon].offense < 0 || proto == PROTO_SPORE_LAUNCHER)) {
        if (triad == TRIAD_AIR) return text(alien ? 0x689818 : 0x689884);
        if (triad == TRIAD_SEA)
            return text(proto == PROTO_SEALURK ? (alien ? 0x689828 : 0x689894) : (alien ? 0x689838 : 0x6898a4));
        return text(alien ? 0x689844 : 0x6898b0);
    }
    if (triad == TRIAD_AIR) return text(alien ? 0x689854 : 0x6898c0);
    if (triad == TRIAD_SEA) return text(alien ? 0x689864 : 0x6898cc);
    return text(alien ? 0x689874 : 0x6898dc);
}

// A human's colony pod at size 1 would use up the base: asks first.
// Returns whether to go on and build it.
static int colony_pod_ok(int proto, int faction, int human) {
    if (!human) {
        if (*game_difficulty > 3) B->nutrients += *game_difficulty - 2;
        return 0;
    }
    if (governed(B)) return 0;
    if ((*game_state & GAME_STATE_DONE) && !(*game_state & GAME_STATE_FINAL_SCORE_DONE)) return 0;
    game_parse_says(0, B->name, -1, -1);
    game_parse_says(1, game_protos[proto].name, -1, -1);
    game_str_buffer[0] = 0;
    strcat(game_str_buffer, text(0x6897a4));
    int cost = cost_factor(B->faction, 0, ID) * (B->pop_size + 1);
    if (B->surplus[RESOURCE_NUTRIENT] > 0) {
        strcat(game_str_buffer, text(0x6897b0));
        int turns = (cost - B->nutrients) / B->surplus[RESOURCE_NUTRIENT];
        game_parse_num(0, turns < 0 ? 0 : turns > 9999 ? 9999 : turns);
    }
    int answer = 0;
    if (!*game_net_multiplayer) {
        int alien = is_alien(B->faction);
        answer = game_popp(*game_script_file, text(alien ? 0x6897c0 : 0x6897dc), 0, text(alien ? 0x6897b4 : 0x6897cc), 0);
    }
    if (!*game_net_multiplayer && answer >= 2) return 1;
    if (answer) *game_upkeep_flag = 1;
    return 0;
}

// The unit at the head of the queue: built once the base has its cost.
static void build_unit(int proto, int faction, int human) {
    struct unit_proto *p = &game_protos[proto];
    int cost = cost_factor(B->faction, 1, -1) * game_veh_cost(proto, ID, 0);
    if (B->minerals < cost) return;
    if (p->plan == PLAN_COLONY && B->pop_size == 1 && game_factions[faction].diff_level > 1 &&
        !colony_pod_ok(proto, faction, human))
        return;
    int first = 0;
    if (proto >= 64) {
        first = !(p->flags & PROTO_FLAG_BUILT);
        p->flags |= PROTO_FLAG_BUILT;
        if (first) {
            int id = ID;
            game_prune_protos(faction, proto, 0);
            ID = id;
            B = &game_bases[id];
            if (*game_computed_base_id != id) {
                *game_computed_base_id = id;
                base_support();
                base_yield();
                base_nutrient();
                base_minerals();
                base_energy();
            }
        }
    }
    B->minerals -= cost;
    carry_over(human);
    if (*game_base_build_blocked) return;
    int v = game_veh_init(proto, faction, B->x, B->y);
    if (v < 0) return;
    struct veh *u = &game_vehs[v];
    if (p->plan == PLAN_COLONY) {
        uint32_t state = B->state;
        if ((state & BASE_STATE_COLONY_2) || !game_factions[faction].ai_power || game_factions[faction].base_count > 8)
            B->state = state | BASE_STATE_COLONY_1;
        B->state |= BASE_STATE_COLONY_2;
    }
    if (human && (B->governor & GOVERNOR_NEW_UNITS_AUTO) && (B->governor & GOVERNOR_ACTIVE)) {
        u->state |= VEH_STATE_ON_ALERT;
        u->order_auto_type = 0;
    }
    if (proto == PROTO_MIND_WORMS && faction == *game_current_faction && !(*game_interlude_state & 0x10)) {
        game_interlude(4, B->name, 1, 0);
        u->state |= VEH_STATE_ASSISTANT_WORM;
    }
    u->morale = (uint8_t)unit_morale(proto, faction, first);
    u->state |= VEH_STATE_NEW;
    if (faction == *game_current_faction) u->flags |= VEH_FLAG_PLAYERS;
    if (p->plan == PLAN_COLONY) {
        if (B->nutrients < 0) B->nutrients = 0;
        if (B->pop_size > 1 || (game_factions[faction].diff_level > 1 && !is_objective(ID))) B->pop_size--;
        if (B->pop_size <= 0) {
            u->home_base = -1;
            game_base_kill(ID);
            game_draw_map(1);
            game_eliminate_player(faction, 0);
            return;
        }
    }
    B->state |= BASE_STATE_PRODUCTION_DONE;
    B->state &= ~BASE_STATE_UNK_80000000;
    if (!human) {
        reset(ID);
    } else {
        int next = base_queue(ID);
        game_parse_says(1, p->name, -1, -1);
        int warn = *game_warnings, flags;
        if ((!next && !governed(B)) || (first && (warn & 4)) || (warn & 0x2000)) {
            if (first && (warn & 4)) {
                game_parse_num(0, cost_factor(B->faction, 1, -1) * game_veh_cost(proto, ID, 0));
                game_str_buffer[0] = 0;
                strcat(game_str_buffer, text(0x6897e8));
                flags = 4;
            } else {
                game_str_buffer[0] = 0;
                if (p->plan < PLAN_COLONY) {
                    strcat(game_str_buffer, text(0x6897f8));
                    flags = 0x400;
                } else {
                    strcat(game_str_buffer, text(0x689800));
                    flags = 2;
                }
            }
        } else {
            game_str_buffer[0] = 0;
            strcat(game_str_buffer, text(0x689808));
            flags = 0;
        }
        if (next) {
            game_parse_num(1, B->queue_size + 1);
            strcat(game_str_buffer, text(0x689810));
        } else if (governed(B)) {
            strcat(game_str_buffer, text(0x689814));
        }
        game_popb(game_str_buffer, flags, -1, unit_picture(proto, faction), 0);
        if (!next && governed(B)) game_base_reset(ID, 0);
    }
    if (first) {
        game_mon_prototype_built(faction);
        if (*game_net_multiplayer && human) {
            p->flags |= PROTO_FLAG_SEEN_ONLINE;
        } else {
            *game_base_proto_upkeep = 0;
            if (!human || (*game_more_preferences & 0x100000))
                for (int i = 0; i < 128; i++) {
                    int q = i < 64 ? i : i % 64 + faction * 64;
                    uint16_t f = game_protos[q].flags;
                    if ((f & PROTO_FLAG_ACTIVE) && (f & PROTO_FLAG_BUILT) && (f & PROTO_FLAG_SEEN_ONLINE))
                        upgrade_prototypes(faction, q);
                }
        }
    }
    int triad = game_chassis[p->chassis].triad;
    if (proto < 64 && (game_weapons[p->weapon].offense < 0 || proto == PROTO_SPORE_LAUNCHER)) {
        game_mon_native_life_bred(faction);
    } else {
        if (triad == TRIAD_SEA && game_weapons[p->weapon].offense) game_mon_naval_unit_built(faction);
        if (game_chassis[p->chassis].triad == TRIAD_AIR) game_mon_air_unit_built(faction);
    }
}

// ---- Facilities and secret projects ---------------------------------------

// A satellite: the faction's first reveals the whole map to it.
static void satellite_built(int fac, int faction) {
    struct faction_data *f = &game_factions[faction];
    f->satellites[fac - FAC_SKY_HYDRO_LAB]++;
    if (f->flags & FACTION_FLAG_MAP_REVEALED) return;
    uint8_t bit = (uint8_t)(1 << faction);
    struct map_tile *t = *game_map;
    for (int i = 0; i < *game_map_area; i++, t++) {
        t->visibility |= bit;
        if (faction) t->bits_seen[faction - 1] = t->bits;
    }
    for (int i = 0; i < *game_base_count; i++) {
        game_bases[i].visibility |= bit;
        game_bases[i].pop_seen[faction] = game_bases[i].pop_size;
    }
    f->flags |= FACTION_FLAG_MAP_REVEALED;
    if (faction == *game_current_faction && !(*game_state & GAME_STATE_DONE)) {
        game_popp(*game_script_file, text(0x689984), 0, text(0x689974), 0);
        game_set_dirty();
        game_draw_map(1);
        game_graphic_win_redraw(game_world_win);
    }
}

// A network node frees the base's alien artifacts to be linked again.
static void network_node_built(void) {
    B->state &= ~BASE_STATE_ARTIFACT_LINKED;
    for (int v = game_stack_fix(game_veh_at(B->x, B->y)); v >= 0; v = game_vehs[v].next_in_stack)
        if (game_protos[game_vehs[v].proto].plan == PLAN_ARTIFACT) game_vehs[v].order = 0;
}

// Wraps x unless the map is flat.
static int wrap_x(int x) {
    int w = *game_map_width;
    if (*game_map_flat & 1) return x;
    return x < 0 ? x + w : x >= w ? x - w : x;
}
static int on_map(int x, int y) { return y >= 0 && y < *game_map_height && x >= 0 && x < *game_map_width; }
static int base_square(const struct map_tile *t) { return (t->bits & MAP_BASE) && (t->owner_site & 0xf) < 8; }

static void fungus_square(int x, int y) {
    game_bit_set(x, y, MAP_FUNGUS, 1);
    game_bit_set(x, y, *game_vop_clear_bits, 0);
    game_synch_bit(x, y, *game_current_faction);
    game_draw_tile(x, y, 2);
}

// The Voice of Planet: fungus on a tenth of the map, and meant to be around
// a square in each faction's territory near its first land base. The
// original never sets its count of tries (at most 1000, for all factions
// together) and finds in it the return address its stack probe left there,
// so it gives up on each faction after one try, as here.
static void voice_of_planet(int faction) {
    int done[FACTION_COUNT];
    for (int f = 0; f < FACTION_COUNT; f++) done[f] = !(*game_factions_alive & (1 << f));
    int tries = (int)game_va(0x4f0802);
    for (int i = 0; i < *game_base_count; i++) {
        const struct base *b = &game_bases[i];
        int bf = b->faction;
        if (done[bf] || (tile(b->x, b->y)->climate >> 5) < ALT_SHORE_LINE) continue;
        done[bf] = 1;
        int x, y;
        for (;;) {
            int r = game_rand() % 288;
            x = wrap_x(b->x + game_radius_x[1 + r]);
            y = game_radius_y[1 + r] + b->y;
            if (++tries >= 1000) break;
            if (!on_map(x, y) || base_square(tile(x, y))) continue;
            if (game_whose_territory(bf, x, y, 0, 0) == bf) break;
        }
        if (tries >= 1000) continue;
        if (bf == *game_current_faction) game_console_focus(game_console, x, y, bf);
        for (int k = 0; k < 9; k++) {
            int x2 = wrap_x(x + game_radius_x[k]), y2 = game_radius_y[k] + y;
            if (!on_map(x2, y2) || base_square(tile(x2, y2))) continue;
            fungus_square(x2, y2);
        }
    }
    for (int n = *game_map_area / 10; n > 0; n--) {
        int w = *game_map_width, h = *game_map_height;
        int x = w - 1 > 0 ? game_rand() % w : 0;
        int y = h - 1 > 0 ? game_rand() % h : 0;
        x = (x & ~1) + (y & 1);
        if (!on_map(x, y) || base_square(tile(x, y))) continue;
        fungus_square(x, y);
    }
    game_interlude(0xd, 0, 1, 0);
    *game_default_plural = 0;
    *game_default_gender = *(int32_t *)&game_faction_defs[faction];
    game_parse_says(5, (char *)&game_faction_defs[faction] + 0x34, -1, -1);
    (*game_char_upper)(game_upper_buffer);
    game_interlude(0xe, 0, 0, 0);
}

// The Ascent to Transcendence ends the game.
static void ascent(int faction) {
    game_mon_winning_trans(faction, -1);
    if (*game_rules & 0x1000)
        for (int i = 1; i < FACTION_COUNT; i++)
            if (i != faction && (game_factions[faction].diplo_status[i] & 1) &&
                (!(game_factions[i].diplo_status[faction] & 0x2000000) || is_human(i)))
                game_mon_winning_trans(i, faction);
    if (faction == *game_current_faction) {
        game_interlude(0x11, 0, 2, 0);
        *game_victory_type = 1;
    } else {
        *game_victory_type = 3;
        *game_default_plural = 0;
        *game_default_gender = *(int32_t *)&game_faction_defs[faction];
        game_parse_says(5, (char *)&game_faction_defs[faction] + 0x34, -1, -1);
        game_parse_num(2, game_year(*game_current_turn + 100));
        game_parse_num(3, (*game_current_turn + 100) / 100);
        if (is_alien(faction)) game_interlude(0x1b, 0, 4, 0);
        else game_interlude(0x12, 0, 3, 0);
    }
    *game_state |= GAME_STATE_DONE;
}

// The first time the player builds a facility (or a project): a picture,
// its voice-over, and a message.
static void first_built_dialog(int fac, int proj, int faction, int next, uint8_t *pop) {
    game_parse_says(0, B->name, -1, -1);
    game_base_pop_init(pop, 0, 0);
    game_base_pop_set_width(pop, 500);
    if (*game_warnings & 1) {
        game_str_buffer[0] = 0;
        if (proj >= 0) {
            strcat(game_str_buffer, text(0x6899a8));
            append_number(proj);
        } else {
            strcat(game_str_buffer, text(0x6899b0));
            append_number(fac);
            game_factions[faction].facility_announced[fac / 8] |= (uint8_t)(1 << (fac % 8));
        }
        game_popup_start(pop, text(0x6899b4), game_str_buffer, -1, 0, 0x80, 0);
        game_base_pop_string(pop, text(0x6899bc));
        game_base_pop_string(pop, text(0x6899c0));
        game_base_pop_string(pop, text(0x6899c8));
    }
    game_str_buffer[0] = 0;
    strcat(game_str_buffer, text(0x6899cc));
    append_number(fac >= 65 && fac <= 69 ? fac - 31 : fac);
    if (fac >= 34 && fac <= 41) append_number(0);
    strcat(game_str_buffer, text(0x6899d8));
    if ((*game_more_preferences & 0x4000) && faction == *game_current_faction && is_human(faction)) {
        game_wave_unload(game_wave);
        game_wave_load(game_wave, game_str_buffer);
        game_wave_play(game_wave);
    }
    game_str_buffer[0] = 0;
    strcat(game_str_buffer, text(0x6899e0));
    if (next) {
        game_parse_num(1, B->queue_size + 1);
        strcat(game_str_buffer, text(0x6899ec));
    } else if (governed(B)) {
        strcat(game_str_buffer, text(0x6899f0));
    }
    game_popb(game_str_buffer, 1, -1, text(is_alien(faction) ? 0x6899f4 : 0x689a04), 0);
    if ((*game_more_preferences & 0x4000) && (*game_more_preferences & 0x20000)) game_sound_fade(game_wave, 2000);
}

// The message that a facility, satellite or project is built.
static void built_message(int fac, int sat, int faction, int next) {
    game_str_buffer[0] = 0;
    strcat(game_str_buffer, text(0x689a14));
    if (next) {
        game_parse_num(1, B->queue_size + 1);
        strcat(game_str_buffer, text(0x689a1c));
    } else if (governed(B)) {
        strcat(game_str_buffer, text(0x689a20));
    }
    uint32_t picture = is_alien(faction) ? (sat ? 0x689a24 : fac >= FAC_FIRST_PROJECT ? 0x689a34 : 0x689a44)
                                         : (sat ? 0x689a54 : fac >= FAC_FIRST_PROJECT ? 0x689a64 : 0x689a74);
    int flags = (!next && !governed(B)) || (*game_warnings & 0x2000) ? 1 : 0;
    game_popb(game_str_buffer, flags, -1, text(picture), 0);
}

// Another faction's project: a picture for the player.
static void project_news(int proj, uint8_t *pop) {
    game_parse_says(0, B->name, -1, -1);
    game_base_pop_init(pop, 0, 0);
    game_base_pop_set_width(pop, 600);
    game_str_buffer[0] = 0;
    strcat(game_str_buffer, text(0x689a84));
    append_number(proj);
    game_popup_start(pop, text(0x689a8c), game_str_buffer, -1, 0, 0x80, 0);
    if (*game_screen_width != 800) game_base_pop_string(pop, text(0x689a94));
    game_base_pop_string(pop, text(0x689a98));
    game_base_pop_string(pop, text(0x689aa0));
    game_popup_start(pop, game_pop_file, text(0x689aa4), -1, 0, 0x80, 0);
    if ((*game_state & GAME_STATE_DONE) && !(*game_state & GAME_STATE_FINAL_SCORE_DONE)) return;
    int w, h;
    if (!game_pcx_dimensions(text(0x689ab0), &w, &h) && !game_sprite_init(pop + 0x2118, text(0x689ac0), w, h))
        *(uint8_t **)(pop + 0x2144) = pop + 0x2118;
    game_base_pop_exec_3(pop, 0, 0);
}

// The subspace generators towards the Ascent.
static void generators_message(int faction) {
    int count;
    game_interlude(0x26, 0, 1, 0);
    game_parse_num(0, game_generators(faction, &count));
    game_parse_num(1, count);
    game_parse_num(2, game_ascent_numbers[1]);
    game_parse_num(3, game_ascent_numbers[0]);
    game_parse_says(0, B->name, -1, -1);
    game_popp(*game_script_file, text(0x68999c), 0, text(0x68998c), 0);
}

// A finished facility, satellite or secret project, and what follows:
// messages, and a secret project's own effect.
static void facility_built(int fac, int proj, int sat, int cost, int faction, int human, uint8_t *pop) {
    B->minerals -= cost;
    carry_over(human);
    B->state |= BASE_STATE_PRODUCTION_DONE;
    B->state &= ~BASE_STATE_UNK_80000000;
    if (fac == FAC_HEADQUARTERS)
        for (int i = 0; i < *game_base_count; i++)
            if (game_bases[i].faction == faction) game_bases[i].facilities_built[0] &= (uint8_t)~(1 << FAC_HEADQUARTERS);
    if (proj >= 0) game_secret_projects[proj] = ID;
    else if (sat) satellite_built(fac, faction);
    else B->facilities_built[fac / 8] |= (uint8_t)(1 << (fac % 8));
    if (fac == FAC_NETWORK_NODE) network_node_built();
    game_draw_tile(B->x, B->y, 2);
    int next = base_queue(ID);
    if (faction == *game_current_faction || fac >= FAC_SUBSPACE_GENERATOR) {
        game_parse_say(1, (int)(intptr_t)game_facilities[fac].name, -1, -1);
        game_parse_says(2, game_faction_defs[faction].adjective, -1, -1);
        if (faction != *game_current_faction) {
            if (proj >= 0) {
                project_news(proj, pop);
            } else if (fac == FAC_SUBSPACE_GENERATOR) {
                if (!(*game_state & GAME_STATE_DONE) || (*game_state & GAME_STATE_FINAL_SCORE_DONE)) {
                    if (!is_alien(*game_current_faction)) game_interlude(0x1a, B->name, 1, 0);
                    int count;
                    game_parse_num(0, game_generators(faction, &count));
                    game_parse_num(1, count);
                    game_parse_num(2, game_ascent_numbers[1]);
                    game_parse_num(3, game_ascent_numbers[0]);
                    game_parse_says(0, B->name, -1, -1);
                    *game_default_gender = *(int32_t *)&game_faction_defs[faction];
                    *game_default_plural = 0;
                    game_parse_says(1, (char *)&game_faction_defs[faction] + 0x4c, -1, -1);
                    *game_default_gender = *(int32_t *)&game_faction_defs[faction];
                    *game_default_plural = 0;
                    game_parse_says(2, (char *)&game_faction_defs[faction] + 0x34, -1, -1);
                    *game_default_gender = game_faction_defs[faction].noun_gender;
                    *game_default_plural = game_faction_defs[faction].noun_plural;
                    game_parse_says(3, game_faction_defs[faction].noun, -1, -1);
                    game_popp(*game_script_file, text(0x689ae0), 0, text(0x689ad0), 0);
                }
            } else {
                game_parse_says(0, B->name, -1, -1);
                if (!(*game_state & GAME_STATE_DONE) || (*game_state & GAME_STATE_FINAL_SCORE_DONE))
                    game_popp(*game_script_file, text(sat ? 0x689b0c : 0x689b18), 0, text(sat ? 0x689aec : 0x689afc), 0);
            }
        } else if (fac == FAC_SUBSPACE_GENERATOR) {
            generators_message(faction);
        } else if (fac < FAC_FIRST_PROJECT &&
                   !(game_factions[faction].facility_announced[fac / 8] & (1 << (fac % 8))) &&
                   !(*game_state & GAME_STATE_DONE) && (*game_warnings & 1)) {
            first_built_dialog(fac, proj, faction, next, pop);
        } else {
            built_message(fac, sat, faction, next);
        }
    }
    if (sat) game_mon_first_in_space(faction);
    else if (fac < FAC_FIRST_PROJECT) game_mon_facility_built(faction, B->name);
    if (fac == FAC_PRESERVE) {
        game_mon_built_preserve(faction);
        if (faction == *game_current_faction) game_interlude(*game_interlude_state & 0x20 ? 9 : 5, 0, 1, 0);
    }
    if (fac == FAC_PRESERVE || fac == FAC_TEMPLE || fac == FAC_TREE_FARM || fac == FAC_HYBRID_FOREST) {
        struct faction_data *f = &game_factions[faction];
        if (f->clean_minerals) f->clean_minerals++;
        f->flags &= ~0x4000u;
    }
    if (proj >= 0) {
        if (proj == SP_VOICE_OF_PLANET) game_interlude(0xc, 0, 1, 0);
        if ((*game_preferences & 0x10000000) && !*game_net_multiplayer && faction == *game_current_faction)
            game_amovie_project(proj);
        if ((*game_preferences & 0x10000000) && !*game_net_multiplayer && faction != *game_current_faction &&
            proj == SP_ASCENT_TO_TRANSCENDENCE)
            game_amovie_project_2(text(0x689b24));
        if (faction == *game_current_faction) game_help_project(proj);
        if (proj == SP_VOICE_OF_PLANET) voice_of_planet(faction);
        game_mon_secret_project(faction, proj);
        if (proj == SP_ASCENT_TO_TRANSCENDENCE) {
            ascent(faction);
            return;
        }
        if (proj == SP_UNIVERSAL_TRANSLATOR) {
            game_tech_advance(faction);
            game_tech_advance(faction);
        } else if (proj == SP_PLANETARY_TRANSIT) {
            for (int i = 0; i < *game_base_count; i++) {
                struct base *b = &game_bases[i];
                if (b->faction != faction || b->pop_size >= 3) continue;
                b->pop_size = 3;
                if (faction == *game_current_faction) game_draw_tile(b->x, b->y, 2);
            }
        }
    }
    if ((governed(B) && !next) || !human) game_base_reset(ID, 0);
    if (human && (B->governor & (GOVERNOR_ACTIVE | GOVERNOR_PRODUCTION)) != (GOVERNOR_ACTIVE | GOVERNOR_PRODUCTION) &&
        B->queue[0] == -fac && !B->queue_size) {
        B->queue[0] = -FAC_STOCKPILE_ENERGY;
        game_draw_radius(B->x, B->y, 2, 2);
    }
    if (proj < 0) return;
    // Other bases of the faction building the same project switch with the
    // base of theirs furthest from finishing another project, or choose
    // again; the human player's only under the governor.
    int id = ID;
    for (int i = 0; i < *game_base_count; i++) {
        if (i == id) continue;
        struct base *bi = &game_bases[i];
        int bf = bi->faction;
        if (is_human(bf) && !governed(bi)) continue;
        if (bi->queue[0] != -fac) continue;
        int key = bi->minerals + 10 * bi->surplus[RESOURCE_MINERAL], best = -1;
        for (int j = 0; j < *game_base_count; j++) {
            const struct base *bj = &game_bases[j];
            if (j == i || j == id || bj->faction != bf || bj->queue[0] == -fac || bj->queue[0] > -FAC_FIRST_PROJECT)
                continue;
            int k = bj->minerals + 10 * bj->surplus[RESOURCE_MINERAL];
            if (k < key) key = k, best = j;
        }
        if (best >= 0) {
            bi->queue[0] = game_bases[best].queue[0];
            game_bases[best].queue[0] = -fac;
        } else {
            game_base_reset(i, 0);
        }
    }
    ID = id;
    B = &game_bases[id];
    if (*game_computed_base_id != id) {
        *game_computed_base_id = id;
        base_support();
        base_yield();
        base_nutrient();
        base_minerals();
        base_energy();
    }
}

// The project is already another base's: a human hears whose, unless the
// governor is choosing; otherwise the base chooses again.
static void project_taken(int fac, int proj, int faction, int human) {
    if (!human || governed(B)) {
        game_base_reset(ID, 0);
        return;
    }
    game_parse_say(1, (int)(intptr_t)game_facilities[fac].name, -1, -1);
    game_str_buffer[0] = 0;
    strcat(game_str_buffer, text(0x689908));
    int holder = game_secret_projects[proj];
    if (holder >= 0) {
        game_parse_says(2, game_bases[holder].name, -1, -1);
        int hf = game_bases[holder].faction;
        *game_default_gender = game_faction_defs[hf].noun_gender;
        *game_default_plural = game_faction_defs[hf].noun_plural;
        game_parse_says(3, game_faction_defs[hf].noun, -1, -1);
        strcat(game_str_buffer, text(hf == faction ? 0x689918 : 0x68991c));
    } else {
        strcat(game_str_buffer, text(0x689920));
    }
    game_popb(game_str_buffer, 0x200, -1, text(0x689924), 0);
}

// The facility at the head of the queue, which the base has already (and
// nothing else is left to build): a human hears so.
static void nothing_left(int fac, int faction, int human) {
    if (!human || governed(B)) {
        game_base_reset(ID, 0);
        return;
    }
    game_parse_say(1, (int)(intptr_t)game_facilities[fac].name, -1, -1);
    int alien = is_alien(faction);
    game_popb(text(alien ? 0x689944 : 0x689964), 0x200, -1, text(alien ? 0x689934 : 0x689954), 0);
}

// The AI waits for a human's base about to finish the same project.
static int human_finishing(int fac) {
    int item = B->queue[0];
    for (int i = 0; i < *game_base_count; i++) {
        const struct base *bi = &game_bases[i];
        if (!is_human(bi->faction) || bi->queue[0] != item) continue;
        if (item == -FAC_ASCENT && !ascending(bi->faction)) continue;
        int cost = cost_factor(bi->faction, 1, -1) * game_facilities[fac].cost;
        if (bi->minerals + bi->surplus[RESOURCE_MINERAL] >= cost) return 1;
    }
    return 0;
}

static void produce(int faction, int human, uint8_t *pop) {
    for (;;) {
        int item = B->queue[0];
        if (item == -FAC_STOCKPILE_ENERGY) return;
        if (item >= 0) {
            build_unit(item, faction, human);
            return;
        }
        int fac = -item, proj = fac >= FAC_FIRST_PROJECT ? fac - FAC_FIRST_PROJECT : -1;
        int sat = fac >= FAC_SKY_HYDRO_LAB && fac < FAC_STOCKPILE_ENERGY;
        int cost = cost_factor(B->faction, 1, -1) * game_facilities[fac].cost;
        if (B->minerals >= cost && proj == SP_ASCENT_TO_TRANSCENDENCE && !ascending(faction)) {
            game_popb(text(0x6898f8), 0x1000, -1, text(0x6898e8), 0);
            return;
        }
        if (proj >= 0) {
            if (game_secret_projects[proj] != -1) {
                project_taken(fac, proj, faction, human);
                return;
            }
        } else if (fac < FAC_SKY_HYDRO_LAB && has_facility(ID, fac)) {
            if (!base_queue(ID)) {
                nothing_left(fac, faction, human);
                return;
            }
            continue;
        }
        if (B->minerals < cost) return;
        if (proj >= 0 && !human && human_finishing(fac)) return;
        facility_built(fac, proj, sat, cost, faction, human, pop);
        return;
    }
}

// The current base at the end of its turn: adds its minerals, and finishes
// what it is building when it has enough. The AI, and a base that lost a
// fight, choose what to build next first.
//
// The popup lives where the original's frame keeps it, at [ebp-0x5430]
// from the same caller, as the game remembers its address (base_production
// below sets up that frame).
void base_production_in(uint8_t *pop);
void base_production_in(uint8_t *pop) {
    game_popup_ctor(pop);
    int faction = B->faction, human = is_human(faction);
    B->state &= ~BASE_STATE_PRODUCTION_DONE;
    struct faction_data *f = &game_factions[faction];
    int output = B->intake_2[RESOURCE_MINERAL] + 2 * B->surplus[RESOURCE_MINERAL];
    if (f->best_mineral_output < output) f->best_mineral_output = output;
    if (B->nutrients >= 0) {
        if (B->surplus[RESOURCE_MINERAL] >= 0 && B->queue[0] != -FAC_STOCKPILE_ENERGY && !(B->state & BASE_STATE_RIOTS))
            B->minerals += B->surplus[RESOURCE_MINERAL];
        // The AI chooses again when its base has no units in it.
        if (!human || (governed(B) && !(B->state & BASE_STATE_UNK_80000000))) {
            const struct map_tile *t = tile(B->x, B->y);
            int garrison = (t->bits & MAP_UNIT) && (t->owner_site & 0xf) < 8;
            if ((B->state & BASE_STATE_COMBAT_LOSS) || (!garrison && !human)) game_base_reset(ID, 0);
        }
        produce(faction, human, pop);
    }
    pop_end(pop);
}

__asm__(".text\n"
        ".globl base_production\n"
        "base_production:\n"
        "    pushl %ebp\n"
        "    movl %esp, %ebp\n"
        "    subl $0x5460, %esp\n"
        "    leal -0x5430(%ebp), %eax\n"
        "    pushl %eax\n"
        "    call base_production_in\n"
        "    movl %ebp, %esp\n"
        "    popl %ebp\n"
        "    ret\n");

// ---- Shadow checks ----------------------------------------------------------

// Building writes widely (new units, the map, other bases, the whole map
// for a satellite), so checks cover all of the game's state. A human's
// colony pod at size 1 asks the player, whose answer a check can't give, so
// calls for human bases run the original unchecked; sweeps check them on a
// copy, where every question gets no answer.
extern struct hook_module production_module;
#define PRODUCTION (&production_module)
#define WRITES_GAME hooks_writes_game(&w_)
SHADOW_WRITER_CHECK(void, base_production, (void), (), orig_base_production(), WRITES_GAME, "%s", "")
static void shadow_base_production(void) {
    if (hooks_nested) {
        orig_base_production();
        return;
    }
    if (shadow_bypass()) {
        base_production();
        return;
    }
    stats_base_production.calls++;
    shadow_tick(PRODUCTION);
    if (is_human(B->faction)) orig_base_production();
    else check_base_production();
}

static int slice;

// What the base could build: a unit, a facility, a satellite or a project.
static int sweep_item(int b, int k) {
    int faction = game_bases[b].faction;
    switch (k % 5) {
    case 0: return (b + slice) % 2 ? 1 : 2;  // formers or a scout
    case 1: {
        for (int i = 0; i < 64; i++) {
            int p = faction * 64 + (i + slice) % 64;
            if (game_protos[p].flags & PROTO_FLAG_ACTIVE) return p;
        }
        return 2;
    }
    case 2: {
        for (int i = 0; i < 60; i++) {
            int fac = 1 + (i + slice + b) % 64;
            if (!has_facility(b, fac) && game_facility_avail(fac, faction, b, 0)) return -fac;
        }
        return -FAC_HEADQUARTERS;
    }
    case 3: return -(FAC_SKY_HYDRO_LAB + (b + slice) % 4);
    default: {
        for (int i = 0; i < SP_COUNT; i++) {
            int sp = (i + slice) % SP_COUNT;
            if (sp != SP_ASCENT_TO_TRANSCENDENCE && game_secret_projects[sp] == -1) return -(FAC_FIRST_PROJECT + sp);
        }
        return -FAC_HEADQUARTERS;
    }
    }
}

// A few bases a turn, as they are, and with enough minerals to finish
// something of each kind.
static void production_sweep(void) {
    slice++;
    if (!*game_map || *game_base_count <= 0) return;
    int sweeping = hooks_sweeping;
    hooks_sweeping = 1;
    struct base *current = B;
    int current_id = ID;
    for (int n = 0; n < 3; n++) {
        int b = (slice * 7 + n * 13) % *game_base_count;
        struct base *base = &game_bases[b], saved = *base;
        B = base;
        ID = b;
        check_base_production();
        for (int k = 0; k < 5; k++) {
            int item = sweep_item(b, k + n + slice);
            base->queue[0] = item;
            int cost = item >= 0 ? cost_factor(base->faction, 1, -1) * game_veh_cost(item, b, 0)
                                 : cost_factor(base->faction, 1, -1) * game_facilities[-item].cost;
            base->minerals = cost + (slice + k) % 3 - base->surplus[RESOURCE_MINERAL] * ((k & 1) == 0);
            // Every other turn, as the first of a new prototype, which
            // retires older ones and upgrades units.
            uint16_t flags = item >= 64 ? game_protos[item].flags : 0;
            if (item >= 64 && (slice & 1)) game_protos[item].flags &= (uint16_t)~PROTO_FLAG_BUILT;
            check_base_production();
            if (item >= 64) game_protos[item].flags = flags;
            *base = saved;
        }
    }
    B = current;
    ID = current_id;
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook production_hooks[] = {
    // push ebp; mov ebp, esp; mov eax, fs:0
    HOOK(0x4f07e0, base_production, PUSH_EBP_MOV_EBP_ESP, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00),
};
struct hook_module production_module = {"production", production_hooks, HOOK_COUNT(production_hooks), production_sweep,
                                        NULL, -1};
