// Setting up a game and its factions: a faction's start, or its escape when
// it loses its last base (setup_player), a new game's state (setup_game),
// and the landing message (planetfall). Reimplemented from terranx.exe
// 2.0's disassembly; names follow Thinker (MIT) and OpenSMACX (Brendan
// Casey, GPL-3.0-or-later). Thinker's mod_setup_player served as a map of
// the faction's reset; its landing search is its own, and the original's
// is followed here.
//
// game_init() and game_reload() are not here. All they do of their own is
// a few stores (the halted flag, three pointers); the rest is loading
// labels, strings, sprites and the rules from files, starting timers and
// closing windows, which allocate and free from the game's heap and touch
// the screen, so shadow mode could not check them, and once they have run
// there is nothing left to compare.
#include "setup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "shadow.h"
#include "strategy.h"
#include "tech.h"
#include "veh.h"

enum {
    ALT_SHELF = 2 << 5, ALT_SHORE = 3 << 5, ALT_MOUNTAINS = 5 << 5,  // climate's altitude bits
    RULES_DO_OR_DIE = 0x1, RULES_LOOK_FIRST = 0x10, RULES_TIME_WARP = 0x80,
    STATE_RANDOM_PERSONALITIES = 0x800000, STATE_RANDOM_AGENDAS = 0x1000000,
    STATE_EDITOR = 0xe0,  // game_state: scenario editor, cheated and omniscient
    TREATY_SURRENDERED = 0x2000000,
    AGENDA_RIVALS = 0x200,  // agenda_on: the faction's natural rivals
    FACTION_FLAG_NO_LANDING = 0x1000,  // faction_data.flags: it can't land again
    FACTION_FLAG_FORMERS = 0x100,      // given a colony pod among its file's units
    RULE_FLAG_SPARE_PAIR = 0x1000,     // faction_def.rule_flags: drawn a second rival
    BONUS_TECH = 0, BONUS_UNIT = 1, BONUS_SOCIAL = 3,
    PROTO_FUNGAL_TOWER = 19,
    // Basic designs: colony pod, formers, sea formers, foil, sea colony pod
    // and the transport foil, and the units an Alien Crossfire alien
    // faction or a network game's player starts with.
    PROTO_COLONY_POD = 0, PROTO_FORMERS_ = 1, PROTO_SCOUT = 2, PROTO_SEA_FORMERS = 4, PROTO_ROVER = 11,
    PROTO_FOIL = 13, PROTO_BATTLE_OGRE = 16, PROTO_SEA_COLONY_POD = 21, PROTO_TRANSPORT_FOIL = 22,
    TECH_ALIEN_1 = 0x4e, TECH_ALIEN_2 = 0x4f,  // the alien factions' own techs
    LABEL_TEMPLATE = 0x261,                   // labels.txt: a production queue template's name
    LABEL_NATIVE = 0xf5, LABEL_NATIVE_ADJ = 0xf4,  // native life, as faction 0
    VICTORY_LOST_CAPTURE = 7, VICTORY_LOST = 15, VICTORY_CONQUEST = 4, VICTORY_COOPERATIVE = 5,
    STATE_CONQUERED = 0x2000,
};

// Fields of faction_data the struct doesn't name.
enum {
    F_QUEUE_NAME = 0x470,  // [8][24] the production queue templates' names
    F_QUEUE_SIZE = 0x530,  // [8] and their lengths
    F_UNK_46C = 0x46c, F_UNK_1CC = 0x1cc, F_UNK_104 = 0x104,  // [8]
    F_UNK_32C = 0x32c,    // [12]
    F_UNK_36C = 0x36c,    // 0x59 bytes
    F_UNK_3C8 = 0x3c8, F_UNK_2088 = 0x2088,
    F_GOODIES = 0x203c,   // [6] pods opened and what came of them
    F_TIME_BONUS = 0x2054,
};
static inline int32_t *faction_word(int faction, int offset) { return (int32_t *)((char *)&game_factions[faction] + offset); }
// The leader's priorities in the faction's file (faction_def).
enum { D_AI_FIGHT = 0x570, D_AI_GROWTH = 0x574, D_AI_TECH = 0x578, D_AI_WEALTH = 0x57c, D_AI_POWER = 0x580 };
static inline int32_t def_word(int faction, int offset) { return *(int32_t *)((char *)&game_faction_defs[faction] + offset); }

static inline struct map_tile *tile(int x, int y) { return &(*game_map)[(x >> 1) + y * *game_map_half_width]; }
static inline int alt_of(const struct map_tile *t) { return t->climate & 0xe0; }
static inline char *text(uint32_t addr) { return (char *)game_at(addr); }
static inline int flat(void) { return *game_map_flat & 1; }
static inline int wrap_x(int x) {
    if (!flat()) {
        if (x < 0) x += *game_map_width;
        else if (x >= *game_map_width) x -= *game_map_width;
    }
    return x;
}
static inline int on_the_map(int x, int y) { return y >= 0 && y < *game_map_height && x >= 0 && x < *game_map_width; }
static inline int occupied(const struct map_tile *t) { return (t->bits & (MAP_BASE | MAP_UNIT)) && (t->owner_site & 0xf) < 8; }
static inline int fungus_up(const struct map_tile *t) { return (t->bits & MAP_FUNGUS) && alt_of(t) >= ALT_SHELF; }
static inline int rocky(const struct map_tile *t) { return (t->rocky_lock & 0xc0) > 0x40; }
static inline int aquatic(int faction) { return (game_faction_defs[faction].rule_flags & RULE_FLAG_AQUATIC) != 0; }
static inline int alien(int faction) { return (game_faction_defs[faction].rule_flags & RULE_FLAG_ALIEN) != 0; }
static inline int initial(int setup_id) { return !*game_turn || setup_id == SETUP_LATE_ARRIVAL; }
static inline int starting_energy(void) { return *(int32_t *)((char *)game_rules_basic + 0x7c); }

// The game's distance between squares: the longer side, less half the
// shorter's excess over half their sum.
static int distance_to(int x, int y, int x2, int y2) {
    int dx = abs(x - x2);
    if (!flat() && dx > *game_map_half_width) dx = *game_map_width - dx;
    int dy = abs(y - y2);
    dx = abs(dx), dy = abs(dy);
    int big = dx > dy ? dx : dy, small = dx < dy ? dx : dy;
    return big - ((((dx + dy) >> 1) - small + 1) >> 1);
}

// The site value of a square, worked out and kept if it has none yet; 0
// within a base's radius.
static int site_of(int x, int y) {
    struct map_tile *t = tile(x, y);
    if (t->bits & MAP_BASE_RADIUS) return 0;
    int site = t->owner_site >> 4;
    if (!site) {
        site = world_site(x, y, 0);
        site_set(x, y, site);
    }
    return site;
}

// A faction named as an earlier one is (the same file twice) gets a
// number: "Name-2". Its leader's name, its name and its formal name.
static void number_duplicates(int faction) {
    static const struct { int offset, keep; uint32_t dash; } fields[] = {{0x34, 0x15, 0x691500},
                                                                         {0x2e4, 0x15, 0x691504},
                                                                         {0x384, 0x25, 0x691508}};
    for (int k = 0; k < 3; k++) {
        int same = 0;
        char *mine = (char *)&game_faction_defs[faction] + fields[k].offset;
        for (int i = 1; i < faction; i++)
            if (!strcmp((char *)&game_faction_defs[i] + fields[k].offset, mine)) same++;
        if (same) {
            char number[16];
            mine[fields[k].keep] = 0;
            strcat(mine, text(fields[k].dash));
            snprintf(number, sizeof number, "%d", same + 1);
            strcat(mine, number);
        }
    }
}

// What a faction starts (or starts again) with: no bases or units, and at
// the start of the game (or as a late arrival) its records wiped and its
// leader's priorities from its file, or drawn at random.
static void reset_faction(int faction, int setup_id) {
    struct faction_data *F = &game_factions[faction];
    const struct faction_def *d = &game_faction_defs[faction];
    for (int b = *game_base_count - 1; b >= 0; b--)
        if (game_bases[b].faction == faction) base_kill(b);
    for (int v = *game_veh_count - 1; v >= 0; v--) {
        if (v >= *game_veh_count) continue;
        const struct veh *V = &game_vehs[v];
        if (V->faction == faction && (V->proto != PROTO_FUNGAL_TOWER || faction)) kill(v);
    }
    F->target_y = F->target_x = F->attack_target_base = -1;
    if (initial(setup_id)) {
        init_goals(faction);
        memset(&game_bunglist[faction * 64], 0xff, 64 * sizeof(struct bung));  // clear_bunglist()
        F->flags = 0;
        F->base_governor_adv = *game_difficulty ? 0xdfbe41u : 0xc0dfbe41u;
        F->base_name_offset = F->base_sea_name_offset = 0;
        F->ai_fight = def_word(faction, D_AI_FIGHT), F->ai_power = def_word(faction, D_AI_POWER);
        F->ai_tech = def_word(faction, D_AI_TECH), F->ai_wealth = def_word(faction, D_AI_WEALTH);
        F->ai_growth = def_word(faction, D_AI_GROWTH);
        *faction_word(faction, F_UNK_46C) = 0;
        for (int i = 0; i < 8; i++) {
            char number[16];
            faction_word(faction, F_QUEUE_SIZE)[i] = 0;
            game_battle_text[0] = 0;
            strcat(game_battle_text, game_label(LABEL_TEMPLATE));
            strcat(game_battle_text, text(0x682820));
            snprintf(number, sizeof number, "%d", i + 1);
            strcat(game_battle_text, number);
            strcpy((char *)F + F_QUEUE_NAME + 24 * i, game_battle_text);
        }
        memset(F->se_models_pending, 0, sizeof F->se_models_pending);
        memset(F->se_models, 0, sizeof F->se_models);
        memset(F->se_pending, 0, sizeof F->se_pending);
        memset(F->project_intel, 0, sizeof F->project_intel);
        F->se_upheaval_paid = 0;
        F->diff_level = game_is_human(faction) ? *game_difficulty : 3;
        F->integrity_blemishes = F->diplo_reputation = 0;
        *faction_word(faction, F_UNK_1CC) = 0;
        F->energy_credits = F->atrocities = F->major_atrocities = F->clean_minerals = F->sanction_turns = 0;
        game_tectonic_detonations[faction] = 0;
        F->council_call_turn = -999;
        if (*game_state & STATE_RANDOM_PERSONALITIES) {
            F->ai_fight = game_rand() % 3 - 1;
            F->ai_power = game_rand() % 2;
            F->ai_tech = game_rand() % 2;
            F->ai_wealth = game_rand() % 2;
            F->ai_growth = game_rand() % 2;
        }
        if (*game_rules & RULES_INTENSE_RIVALRY) F->ai_fight = 1;
        F->total_combat_units = F->base_count = F->mil_strength = F->mil_attack = F->pop_total = F->naval = 0;
        F->mind_control_total = 0;
        memset(F->diplo_mind_control, 0, sizeof F->diplo_mind_control);
        memset(F->diplo_stolen_techs, 0, sizeof F->diplo_stolen_techs);
        F->net_random_event = 0;
        for (int r = 0; r < REGION_COUNT; r++) {
            F->region_total_combat_units[r] = 0;
            F->region_total_bases[r] = F->region_offensive[r] = 0;
            F->region_force_rating[r] = F->region_flags[r] = 0;
        }
        memset(F->council_pledge, 0, sizeof F->council_pledge);
        memset(F->council_pledge_to, 0, sizeof F->council_pledge_to);
        memset(&F->corner_market_turn, 0, 40);  // to unk_20a0
        memset(faction_word(faction, F_GOODIES), 0, 6 * 4);
        *faction_word(faction, F_TIME_BONUS) = 1;
    }
    F->energy_credits += (*game_net_multiplayer ? 100 : starting_energy()) + d->rule_energy;
    F->hurry_cost_total = 0;
    F->tech_cost = -1;
    F->last_base_turn = *game_turn;
    for (int i = 0; i < FACTION_COUNT; i++) {
        if (*game_turn && setup_id != SETUP_LATE_ARRIVAL) {
            treaty_off(faction, i, 0x9000ff48u);
        } else {
            F->diplo_status[i] = 0;
            F->diplo_merc[i] = 0;
            int n = 3 * ((*game_rules & RULES_INTENSE_RIVALRY) ? 5 : *game_difficulty) + 8, friction = 1;
            if (n - 1 > 0) {
                int r = game_rand() % n;
                friction = r < 1 ? 1 : r > 16 ? 16 : r;
            }
            F->diplo_friction[i] = friction;
            F->diplo_gifts[i] = F->diplo_wrongs[i] = F->diplo_betrayed[i] = 0;
            F->diplo_patience[i] = 0;
            F->diplo_spoke[i] = -1;
            F->diplo_combat_count[i] = F->diplo_combat_total[i] = 0;
        }
        faction_word(faction, F_UNK_104)[i] = 0;
        F->loan_balance[i] = F->loan_payment[i] = 0;
    }
    for (int p = 0; p < PROTO_COUNT; p++) {
        F->units_active[p] = F->units_queue[p] = 0;
        if (!*game_turn) F->units_lost[p] = 0;
    }
    if (initial(setup_id)) {
        memset(faction_word(faction, F_UNK_32C), 0, 12 * 4);
        memset(faction_word(faction, F_UNK_36C), 0, 0x59);
        memset(F->facility_announced, 0, 9);
        memset(F->satellites, 0, sizeof F->satellites);
        F->transcendent_techs = F->tech_ranking = F->odp_deployed = 0;
        F->tech_research_id = -1;
        *faction_word(faction, F_UNK_3C8) = 0;
        F->earned_techs_saved = F->unk_360 = 0;
        *faction_word(faction, F_UNK_2088) = 0;
    }
    F->best_mineral_output = F->energy_surplus_total = F->facility_maint_total = 0;
    F->unk_310 = F->unk_30c = F->tech_commerce_bonus = 0;
    memset(F->tech_fungus, 0, sizeof F->tech_fungus);
    F->turn_commerce_income = 0;
    F->best_weapon_value = F->best_armor_value = F->best_land_speed = F->best_psi_offense = F->best_psi_defense = 0;
    F->enemy_best_weapon_value = F->enemy_best_armor_value = 0;
    F->enemy_best_land_speed = F->enemy_best_psi_offense = F->enemy_best_psi_defense = 0;
}

// The landing search: random squares, nearer the poles and the edges as it
// goes on, until one passes or it gives up (1024 tries; a human at the
// start of the game never gives up). The rules loosen with each try.
struct landing {
    int x, y, region, failed;
    uint32_t neighbours;  // bit per faction (or native life) with a base or unit on the land
    int near_base;        // what base_find() found
};

// Rings out from (x, y) to the nearest landmark square, up to 7, or 32.
static int landmark_ring(int x, int y) {
    for (int k = 0; k < 8; k++)
        for (int i = k ? game_radius_count[k - 1] : 0; i < game_radius_count[k]; i++) {
            int sx = wrap_x(x + game_radius_x[i]), sy = y + game_radius_y[i];
            if (!on_the_map(sx, sy)) continue;
            uint32_t mark = tile(sx, sy)->bits2;
            if (mark && (int32_t)(mark & 0xff000000u) > 0) return k;
        }
    return 32;
}

// How good the land about (x, y) is for the faction: its squares of wet
// flat land; for an aquatic faction, a third of 5 for each square of deep
// sea, 3 for ocean, 1 for shelf, less on land (the original adds one to
// every square's 4, 2, 0...).
static int land_quality(int faction, int x, int y) {
    int q = 0;
    for (int i = 0; i < BASE_RADIUS_SQUARES; i++) {
        int sx = wrap_x(x + game_radius_x[i]), sy = y + game_radius_y[i];
        if (!on_the_map(sx, sy)) continue;
        const struct map_tile *t = tile(sx, sy);
        if (fungus_up(t)) continue;
        if (aquatic(faction)) q += 2 * (2 - (t->climate >> 5)) + 1;
        else if (alt_of(t) >= ALT_SHORE && !rocky(t) && (t->climate & 0x18)) q++;
    }
    return aquatic(faction) ? q / 3 : q;
}

static int land_ok(int faction, int setup_id, int tries, struct landing *L, int quality, int ring) {
    int turn = *game_turn, sq = *game_map_area_sqrt, aq = aquatic(faction), x = L->x, y = L->y;
    const struct map_tile *t = tile(x, y);
    if (aq ? alt_of(t) > ALT_SHELF : alt_of(t) < ALT_SHORE) return 0;
    if (fungus_up(t)) return 0;
    if (!aq && rocky(t)) return 0;
    if (*game_base_find_dist < (initial(setup_id) ? 6 : 12) * sq / 56) return 0;
    if (alt_of(t) >= ALT_MOUNTAINS) return 0;
    if (occupied(t)) return 0;
    if (y < 2 || y >= *game_map_height - 2) return 0;
    if (quality < 12 - tries / 80) return 0;
    int q = tries / 64;
    if (*game_base_find_dist < (16 - q) * sq / 56) return 0;
    const struct continent *c = &game_continents[L->region];
    if (initial(setup_id) && tries < 900 && c->tile_count < 36 - q) return 0;
    int human = game_is_human(faction);
    if (!aq) {
        int open = (*game_ocean_coverage + 1) * c->open_terrain / 2;
        if (open < -(tries / 16) - turn / 16 + (human ? (6 - *game_difficulty) * 8 : 8) + 24) return 0;
    }
    int level = human ? *game_difficulty : 3;
    if (c->tile_count < (5 - level) * 48 - turn / 16 - tries / 4) return 0;
    if (L->neighbours && setup_id != SETUP_LATE_ARRIVAL &&
        c->tile_count / (game_bit_count(L->neighbours) + 1) < 64 - q)
        return 0;
    if (site_of(x, y) < 15 - q && !turn && !(*game_rules & RULES_LOOK_FIRST)) return 0;
    if (!aq && (ring < (0x400 - tries) / 128 || !ring)) return 0;
    if (!initial(setup_id) && tile(x, y)->territory > 0) return 0;
    if (L->near_base >= 0) {
        const struct base *B = &game_bases[L->near_base];
        if (B->faction == setup_id && tile(B->x, B->y)->region == L->region) return 0;
    }
    return 1;
}

static void find_landing(int faction, int setup_id, struct landing *L) {
    int aq = aquatic(faction);
    for (int tries = 0;;) {
        int w = *game_map_width, h = *game_map_height, m = 0x400 - tries;
        int bx = game_range(m * w / 8192, 0, 9999), by = game_range(m * h / 8192, 0, 9999);
        int x = (flat() ? bx : 0) + game_rnd(w - (flat() ? 2 * bx : 0));
        int y = by + game_rnd(h - 2 * by);
        if (x & 1) x--;
        if (y & 1) x++;
        L->x = x, L->y = y;
        int ring = landmark_ring(x, y), quality = land_quality(faction, x, y);
        L->region = tile(x, y)->region;
        L->neighbours = 0;
        for (int i = 1; i < FACTION_COUNT; i++)
            if (game_factions[i].region_total_bases[L->region]) L->neighbours |= 1u << i;
        if (!aq && setup_id != SETUP_LATE_ARRIVAL) L->near_base = base_find_3(x, y, -1, L->region, -1, -1);
        else L->near_base = base_find(x, y);
        if (L->near_base < 0) *game_base_find_dist = 32;
        if (initial(setup_id))
            for (int v = 0; v < *game_veh_count; v++) {
                const struct veh *V = &game_vehs[v];
                if (tile(V->x, V->y)->region != L->region) continue;
                L->neighbours |= 1u << V->faction;
                int dist = distance_to(x, y, V->x, V->y);
                if (dist < *game_base_find_dist) *game_base_find_dist = dist;
            }
        if (++tries > 0x400 && (!game_is_human(faction) || *game_turn)) {
            L->failed = 1;
            return;
        }
        if (land_ok(faction, setup_id, tries, L, quality, ring)) return;
    }
}

// At the start, the landing moves to the best square about it: on its own
// land (or sea), clear, away from units, the best site with the most coast
// (for an aquatic faction, the deepest sea). Units' distances are measured
// from the first square, not the one weighed, as the original does.
static void refine_landing(int faction, struct landing *L) {
    int best = -1, pick = 0, aq = aquatic(faction);
    for (int i = 0; i < BASE_RADIUS_SQUARES; i++) {
        int sx = wrap_x(L->x + game_radius_x[i]), sy = L->y + game_radius_y[i];
        if (!on_the_map(sx, sy)) continue;
        const struct map_tile *t = tile(sx, sy);
        if ((alt_of(t) < ALT_SHORE) != aq || t->region != L->region || alt_of(t) >= ALT_MOUNTAINS) continue;
        if (fungus_up(t) || occupied(t)) continue;
        base_find(sx, sy);
        for (int v = 0; v < *game_veh_count; v++) {
            int dist = distance_to(L->x, L->y, game_vehs[v].x, game_vehs[v].y);
            if (dist < *game_base_find_dist) *game_base_find_dist = dist;
        }
        if (*game_base_find_dist < 6) continue;
        int score = site_of(sx, sy);
        for (int j = 0; j < 8; j++) {
            int nx = wrap_x(sx + game_radius_near_x[j]), ny = sy + game_radius_near_y[j];
            if (!on_the_map(nx, ny)) continue;
            int level = tile(nx, ny)->climate >> 5;
            if (aq) score += 2 - level;
            else if (level < 3) {
                score++;
                break;
            }
        }
        if (score > best) best = score, pick = i;
    }
    L->x = wrap_x(L->x + game_radius_x[pick]), L->y += game_radius_y[pick];
}

// A faction with nowhere to land is out of the game. The player has lost;
// for an AI faction the game may be won by conquest, by the player alone
// or with allies.
static void eliminated(int faction, int setup_id) {
    uint8_t bit = (uint8_t)(1u << faction);
    if (game_is_human(faction) && !*game_net_multiplayer) {
        *game_factions_alive &= (uint8_t)~bit;
        *game_victory_type = VICTORY_LOST_CAPTURE;
        *game_state |= GAME_STATE_DONE;
        return;
    }
    *faction_word(faction, F_UNK_2088) = 0;
    *game_factions_alive &= (uint8_t)~bit;
    int other = game_faction_rankings[7];
    if (faction == other) other = game_faction_rankings[6];
    int count = 0, allied = 0, current = *game_current_faction;
    for (int i = 1; i < FACTION_COUNT; i++) {
        if (!(*game_factions_alive & (1u << i))) continue;
        count++;
        uint32_t to_player = game_factions[i].diplo_status[current];
        if (!*game_net_multiplayer && (to_player & TREATY_PACT) && (to_player & TREATY_SURRENDERED)) {
            count--;
            continue;
        }
        if (i != other && (game_factions[i].diplo_status[other] & TREATY_PACT) &&
            (*game_rules & RULES_VICTORY_COOPERATIVE) && allied < 2)
            allied++;
    }
    if (faction == current) {
        const struct faction_def *k = &game_faction_defs[setup_id < 0 ? 0 : setup_id];
        *game_parse_gender = k->noun_gender, *game_parse_plural = k->noun_plural;
        game_parse_says(0, (char *)k->noun, -1, -1);
        if (*game_net_multiplayer) {
            static const uint32_t cleared[] = {0x93e8c0, 0x93e8c4, 0x93e8c8, 0x93e8cc, 0x93e8f0, 0x93e8ec, 0x93e8fc,
                                               0x93e8f8, 0x93e90c, 0x93e938, 0x93e930, 0x93e934, 0x93e95c, 0x93e958,
                                               0x93e8e0, 0x93e8e4, 0x93e8e8, 0x93e954, 0x93e950, 0x93e940, 0x93e93c,
                                               0x93e944, 0x93e8d0, 0x93e8d4, 0x93e968, 0x93e964, 0x93e8d8, 0x93e8dc};
            game_net_daemon_hang_up(game_net_daemon);
            for (size_t i = 0; i < sizeof cleared / sizeof cleared[0]; i++) *(int32_t *)game_at(cleared[i]) = 0;
            *(int32_t *)game_at(0x93e960) = 0xff;
            *game_net_veh_ref = NULL;
            memset(game_at(0x93e910), 0, 8 * 4);
            game_lock_clear(game_at(0x93e230));
            game_alpha_net_close(game_net_daemon);
            *game_net_multiplayer = 0;
            *(int32_t *)game_at(0x9b2068) = 1;
        }
        game_x_pop(text(!setup_id && !*game_language ? 0x69152c : 0x691524), 0);
        *game_state |= GAME_STATE_DONE;
        *game_victory_type = setup_id ? VICTORY_LOST_CAPTURE : VICTORY_LOST;
        return;
    }
    if (count - allied < 2) {
        if (allied) {
            game_popp(*game_script_file, text(0x691544), 0, text(0x691538), 0);
            if (!*game_victory_type) *game_victory_type = VICTORY_COOPERATIVE;
        } else if (other == current) {
            game_popp(*game_script_file, text(0x69155c), 0, text(0x691550), 0);
            if (!*game_victory_type) *game_victory_type = VICTORY_CONQUEST;
        }
        *game_state |= GAME_STATE_DONE | STATE_CONQUERED;
    }
}

// The AI's leanings for the game (flags_ext and flags), drawn once it
// has landed at the start: more probes, sea bases, missiles, terraformers
// and so on, by its file and its land.
static void ai_leanings(int faction, const struct landing *L) {
    struct faction_data *F = &game_factions[faction];
    const struct faction_def *d = &game_faction_defs[faction];
    if ((F->ai_wealth || F->ai_tech) && !(game_rand() % 4)) F->flags_ext |= 0x8;
    game_battle_text[0] = 0;
    strcat(game_battle_text, d->filename);
    (*game_char_upper)(game_battle_text);
    if (!strcmp(game_battle_text, text(0x691568))) F->flags_ext |= 0x8;
    if (d->rule_flags & RULE_FLAG_AQUATIC) F->flags_ext |= 0x4, F->flags |= 0x8000000;
    if (d->rule_flags & RULE_FLAG_ALIEN) F->flags_ext |= 0x10, F->flags |= 0x10000000;
    if (!strcmp(game_battle_text, text(0x691570))) F->flags_ext = (F->flags_ext & ~0x8u) | 0x2;
    int tiles = game_continents[L->region].tile_count;
    if (tiles < 250 && !(game_rand() % 3)) F->flags_ext |= 0x4;
    if (F->ai_fight >= 0 && !(game_rand() % 4)) F->flags_ext |= 0x10;
    if (!(game_rand() % 6)) F->flags_ext |= 0x2;
    if (!(game_rand() % 6)) F->flags_ext |= 0x1;
    if (!(game_rand() % 5)) F->flags_ext |= 0x40;
    int intense = (*game_rules & RULES_INTENSE_RIVALRY) != 0;
    if ((intense || *game_difficulty >= 4) && F->ai_fight > (intense ? -1 : 0) && !(game_rand() % 4))
        F->flags_ext |= FACTION_FLAG_EXT_SHAMELESS;
    if ((intense || *game_difficulty >= 4) && F->ai_fight > (intense ? -1 : 0) && !(game_rand() % 4)) {
        F->flags |= 0x1000000;
        if (!(game_rand() % 2)) F->flags |= 0x2000000;
    }
    if (F->ai_power || F->ai_fight > 0) {
        if (game_continents[L->region].tile_count >= 400 && !(game_rand() % 4)) F->flags |= 0x4000000;
        if (!game_rnd(game_continents[L->region].tile_count > 250 ? 6 : 4)) F->flags |= 0x8000000;
        if (!(game_rand() % 4)) F->flags |= 0x10000000;
    }
}

// A unit of the faction's that starts out without a home base.
static int free_unit(int proto, int faction, int x, int y) {
    int v = veh_init(proto, faction, x, y);
    game_vehs[v].home_base = -1;
    veh_demote(v);
    return v;
}

// A foil next to a coastal landing, as a veteran.
static void coast_foil(int faction, int x, int y, int i) {
    int v = veh_init(PROTO_FOIL, faction, wrap_x(x + game_radius_x[i]), y + game_radius_y[i]);
    game_vehs[v].morale = 2;
    veh_demote(v);
    game_vehs[v].home_base = -1;
}

// What the faction lands with at the start (or as a late arrival): its
// free techs and designs, a base (or, looking first, a colony pod), a
// scout (its last reconnaissance design), and a foil or two on a small
// coast. In a network game it also gets the techs its research bias
// draws, half its file's extra energy, a rover and formers.
static void first_landing(int faction, int setup_id, struct landing *L) {
    struct faction_data *F = &game_factions[faction];
    const struct faction_def *d = &game_faction_defs[faction];
    int x = L->x, y = L->y, aq = aquatic(faction), scout = PROTO_SCOUT;
    for (int i = 0; i < d->bonus_count; i++)
        if (d->bonus_id[i] == BONUS_TECH) game_tech_achieve(faction, d->bonus_val1[i], 0, 0);
    if (*game_expansion && alien(faction)) {
        game_tech_achieve(faction, TECH_ALIEN_1, 0, 0);
        game_tech_achieve(faction, TECH_ALIEN_2, 0, 0);
    }
    if (*game_net_multiplayer) {
        for (int k = 0;; k++) {
            int research = F->se_base[SE_RESEARCH], morale = F->se_base[SE_MORALE];
            int n = (research > 0 ? 1 : research < 0 ? -1 : 0) - (morale > 0 ? 1 : morale < 0 ? -1 : 0) + 1;
            if (k >= n) break;
            game_tech_achieve(faction, tech_ai(faction), 0, 0);
        }
        F->energy_credits += d->rule_energy / 2;
    }
    consider_designs(faction);
    for (int i = 0; i < 64; i++) {
        struct unit_proto *P = &game_protos[faction * 64 + i];
        if (!(P->flags & PROTO_FLAG_ACTIVE)) continue;
        P->flags |= PROTO_FLAG_BUILT;
        if (P->plan == PLAN_RECON) scout = faction * 64 + i;
    }
    if ((*game_net_multiplayer || (*game_rules & RULES_LOOK_FIRST)) && !(*game_rules & RULES_TIME_WARP)) {
        veh_init(aq ? PROTO_SEA_COLONY_POD : PROTO_COLONY_POD, faction, x, y);
    } else {
        int b = base_init(faction, x, y);
        game_battle_text[0] = 0;
        strcat(game_battle_text, d->filename);
        (*game_char_upper)(game_battle_text);
        // A late arrival's base starts larger (indices as the original's,
        // unchecked).
        struct base *B = (struct base *)((char *)game_bases + b * (int)sizeof(struct base));
        if (!strcmp(game_battle_text, text(0x691578)) && setup_id == SETUP_LATE_ARRIVAL) B->pop_size = 3;
        if ((d->rule_flags & RULE_FLAG_ALIEN) && setup_id == SETUP_LATE_ARRIVAL) B->pop_size = 3;
    }
    if (!aq) free_unit(scout, faction, x, y);
    if (*game_net_multiplayer && !aq) {
        free_unit(PROTO_ROVER, faction, x, y);
        free_unit(PROTO_FORMERS_, faction, x, y);
    }
    int coast = is_coast(x, y, 0);
    if (coast && !(*game_difficulty <= 1 && !*game_net_multiplayer && game_continents[L->region].tile_count > 32)) {
        int region = tile(x, y)->region;
        L->region = region;
        int share = game_continents[region].tile_count / (game_bit_count(L->neighbours) + 1);
        if (*game_net_multiplayer && share <= 75 && game_is_human(faction)) coast_foil(faction, x, y, coast);
        if (game_continents[region].tile_count / (game_bit_count(L->neighbours) + 1) <= 50)
            coast_foil(faction, x, y, coast);
    }
}

// The faction file's social bonuses and units; an Alien Crossfire alien
// faction's battle ogre and colony pod; an aquatic faction's sea colony
// pod and transport foil (for the AI, with the design made for it).
static void file_units(int faction, int setup_id, const struct landing *L) {
    struct faction_data *F = &game_factions[faction];
    const struct faction_def *d = &game_faction_defs[faction];
    int x = L->x, y = L->y;
    memset(F->se_base, 0, sizeof F->se_base);
    for (int i = 0, n = d->bonus_count; i < n; i++)
        if (d->bonus_id[i] == BONUS_SOCIAL) F->se_base[d->bonus_val1[i]] += d->bonus_val2[i];
    for (int i = 0; i < d->bonus_count; i++) {
        if (d->bonus_id[i] != BONUS_UNIT) continue;
        free_unit(d->bonus_val1[i], faction, x, y);
        if (!d->bonus_val1[i]) F->flags |= FACTION_FLAG_FORMERS;
    }
    if (*game_expansion && alien(faction)) {
        free_unit(PROTO_BATTLE_OGRE, faction, x, y);
        free_unit(PROTO_COLONY_POD, faction, x, y);
    }
    if (!aquatic(faction)) return;
    if (!game_is_human(faction)) {
        // A transport foil design, in the first free slot (or, with none
        // free, at the index setup_id gives, as the original does).
        int p = setup_id;
        for (int i = 0; i < 64; i++)
            if (!(game_protos[faction * 64 + i].flags & PROTO_FLAG_ACTIVE)) {
                p = faction * 64 + i;
                break;
            }
        make_proto(p, 3, 0x11, 0, 0, 1);
        struct unit_proto *P = &game_protos[p];
        P->flags = (uint16_t)((P->flags & 0xff3f) | PROTO_FLAG_BUILT);
        char name[0x50];
        name_proto(name, p, faction, 3, 0x11, 0, 0, 1);
        P->flags &= 0xfffd;
        name[0x1f] = 0;
        strcpy(P->name, name);
    }
    free_unit(PROTO_SEA_COLONY_POD, faction, x, y);
    free_unit(PROTO_TRANSPORT_FOIL, faction, x, y);
}

// An escape: a colony pod and the faction's best units for the time, as
// many as the game has gone on (the most defensive design, the most
// rounded, and the fastest).
static void escape_units(int faction, const struct landing *L) {
    int x = L->x, y = L->y, aq = aquatic(faction), turn = *game_turn;
    int best_def = -1, def = 2, best_all = -1, all = -1, best_fast = -1, fast = -1;
    for (int p = 0; p < PROTO_COUNT; p++) {
        if (!veh_avail(p, faction, -1)) continue;
        const struct unit_proto *P = &game_protos[p];
        if (!game_weapons[P->weapon].offense) continue;
        const struct rules_chassis *C = &game_chassis[P->chassis];
        if (aq ? C->triad != TRIAD_SEA : C->triad != TRIAD_LAND) continue;
        int s = armor_val(p, faction) * 8;
        s -= weap_val(p, faction);
        if (s > best_def) best_def = s, def = p;
        int w = weap_val(p, faction);
        s = C->speed + (armor_val(p, faction) + w * 2) * 2;
        if (s > best_all) best_all = s, all = p;
        w = weap_val(p, faction);
        s = C->speed * 4 - w - armor_val(p, faction);
        if (s > best_fast && C->speed > 1) best_fast = s, fast = p;
    }
    int pod = veh_init(aq ? PROTO_SEA_COLONY_POD : PROTO_COLONY_POD, faction, x, y);
    veh_init(def, faction, x, y);
    if (turn > 20 && fast >= 0) veh_init(fast, faction, x, y);
    if (turn > 40 && all >= 0) veh_init(all, faction, x, y);
    if (turn > 60 && def >= 0) veh_init(def, faction, x, y);
    if (turn > 80 && all >= 0) veh_init(all, faction, x, y);
    if (turn > 100) veh_init(aq ? PROTO_SEA_COLONY_POD : PROTO_COLONY_POD, faction, x, y);
    veh_promote(pod);
    struct faction_data *F = &game_factions[faction];
    F->unk_360 = F->tech_ranking * 3 / 4;
}

int setup_player(int faction, int setup_id, int probe) {
    if (faction > 1) number_duplicates(faction);
    reset_faction(faction, setup_id);
    if (!faction) return 0;
    uint8_t bit = (uint8_t)(1u << faction);
    if (!(*game_factions_alive & bit)) return 0;
    if (setup_id < -1 && setup_id != SETUP_LATE_ARRIVAL) {
        *game_factions_alive &= (uint8_t)~bit;
        return 0;
    }
    struct faction_data *F = &game_factions[faction];
    struct landing L = {0};
    find_landing(faction, setup_id, &L);
    if (F->flags & FACTION_FLAG_NO_LANDING) L.failed = 1;
    if (!L.failed || probe) {
        if (initial(setup_id)) refine_landing(faction, &L);
        if (probe) L.failed = 0;
    }
    int lands = probe || (!L.failed && !(*game_turn && setup_id != SETUP_LATE_ARRIVAL && (*game_rules & RULES_DO_OR_DIE)) &&
                          *game_turn <= 100);
    if (!lands) {
        eliminated(faction, setup_id);
        return 0;
    }
    F->alloc_labs = 5;
    F->alloc_psych = 0;
    if (initial(setup_id) && *game_difficulty >= 2) ai_leanings(faction, &L);
    for (int i = 0; i < BASE_RADIUS_SQUARES; i++) {
        int sx = wrap_x(L.x + game_radius_x[i]), sy = L.y + game_radius_y[i];
        if (!on_the_map(sx, sy)) continue;
        tile(sx, sy)->visibility |= bit;
        synch_bit(sx, sy, faction);
    }
    if (initial(setup_id)) {
        first_landing(faction, setup_id, &L);
    } else if (aquatic(faction)) {
        veh_init(PROTO_SEA_COLONY_POD, faction, L.x, L.y);
        veh_demote(veh_init(PROTO_SEA_FORMERS, faction, L.x, L.y));
    } else {
        veh_init(PROTO_COLONY_POD, faction, L.x, L.y);
        veh_demote(veh_init(PROTO_FORMERS_, faction, L.x, L.y));
    }
    if (initial(setup_id)) file_units(faction, setup_id, &L);
    if (*game_turn && !L.failed && setup_id != SETUP_LATE_ARRIVAL) escape_units(faction, &L);
    if (faction == *game_current_faction) {
        char *win = game_map_wins[0];
        *(int32_t *)(win + 0x1dd9c) = L.x;
        *(int32_t *)(win + 0x1dda0) = L.y;
        for (int i = 0; i < 32; i++) {
            ((int32_t *)game_at(0x9392c0))[i] = L.x;
            ((int32_t *)game_at(0x939340))[i] = L.y;
        }
    }
    return 1;
}

// ---- A new game ---------------------------------------------------------------------

static inline int32_t *word_at(uint32_t addr) { return (int32_t *)game_at(addr); }
static inline int32_t rules_basic_word(int offset) { return *(int32_t *)((char *)game_rules_basic + offset); }

// With random social agendas, each AI leader is dealt a social model to
// favour (one not dealt already, if a few draws find one) and the first
// effect it raises.
static void random_agendas(void) {
    int dealt[12] = {0};
    for (int f = 1; f < FACTION_COUNT; f++) {
        struct faction_def *d = &game_faction_defs[f];
        d->ideology_category = -1, d->ideology_model = 0, d->ideology_effect = -1;
        int category, model;
        for (int tries = 0;;) {
            category = game_rand() % 3;
            model = game_rand() % 3 + 1;
            if (!dealt[category * 3 + model] || ++tries >= f + 1) break;
        }
        d->ideology_category = category, d->ideology_model = model;
        dealt[category * 3 + model] = 1;
        const int32_t *effects = (const int32_t *)(game_social_effects + category * 212 + model * 44);
        for (int e = 0; e < SE_COUNT; e++)
            if (effects[e] > 0) {
                d->ideology_effect = e;
                break;
            }
    }
}

// Factions are drawn in pairs, each with another (two humans seldom), and
// those whose file asks for it a second time, as the original does; but
// nothing comes of the draws besides the rand() calls they take.
static void draw_pairs(void) {
    int counts[FACTION_COUNT] = {0};
    for (int f = 1; f < FACTION_COUNT; f++) {
        if (counts[f]) continue;
        int tries = 0, r;
        for (;;) {
            r = game_rand() % 7 + 1;
            tries++;
            if (r == f) continue;
            if (counts[r] && tries < 200) continue;
            if (counts[r] && tries < 400 && !(game_faction_defs[r].rule_flags & RULE_FLAG_SPARE_PAIR)) continue;
            if (game_is_human(f) && game_is_human(r) && tries < 100) continue;
            break;
        }
        counts[f]++, counts[r]++;
    }
    for (int f = 1; f < FACTION_COUNT; f++) {
        if (counts[f] > 1 || !(game_faction_defs[f].rule_flags & RULE_FLAG_SPARE_PAIR)) continue;
        int tries = 0, r;
        for (;;) {
            r = game_rand() % 7 + 1;
            if (r == f) continue;
            if (game_is_human(f) && game_is_human(r) && ++tries < 200) continue;
            break;
        }
        counts[f]++, counts[r]++;
    }
}

// The factions' natural rivals, by the pairs of names the game knows, and
// any alien faction; the two alien factions are at war from the start.
static void natural_rivals(void) {
    static const uint32_t pairs[][2] = {{0x691630, 0x691638}, {0x691644, 0x69164c}, {0x691654, 0x69165c},
                                        {0x691664, 0x69166c}, {0x691674, 0x69167c}, {0x691684, 0x69168c},
                                        {0x691694, 0x69169c}, {0x6916a4, 0x6916ac}, {0x6916b4, 0x6916bc},
                                        {0x6916c4, 0x6916cc}, {0x6916d4, 0x6916dc}};
    for (int a = 1; a < FACTION_COUNT; a++)
        for (int b = 1; b < FACTION_COUNT; b++) {
            if (a == b) continue;
            char key_a[24] = "", key_b[24] = "";
            strcat(key_a, game_faction_defs[a].search_key);
            strcat(key_b, game_faction_defs[b].search_key);
            (*game_char_upper)(key_a);
            (*game_char_upper)(key_b);
            int rivals = alien(a) || alien(b);
            for (size_t k = 0; k < sizeof pairs / sizeof pairs[0] && !rivals; k++)
                if (!strcmp(key_a, text(pairs[k][0])) && !strcmp(key_b, text(pairs[k][1]))) rivals = 1;
            if (rivals) agenda_on(a, b, AGENDA_RIVALS);
            if (alien(a) && alien(b)) {
                set_treaty(b, a, 0xc80c0c38u, 1);
                game_set_agenda(b, a, 0x2c2d, 1);
            }
        }
}

// With random leaders, each human gets a rival or two (with intense
// rivalry, and at the higher difficulty levels) among the AI factions.
static void random_rivals(void) {
    for (int f = 1; f < FACTION_COUNT; f++) {
        if (!game_is_human(f)) continue;
        for (int k = 0; k < (int)((*game_rules >> 6) & 1) + (game_factions[f].diff_level >= 3); k++) {
            for (int tries = 0;;) {
                int r = game_rand() % 7 + 1;
                if (++tries >= 100) break;
                if (game_factions[r].diplo_agenda[f] & AGENDA_RIVALS) continue;
                if (!(*game_factions_alive & (1u << r)) || game_is_human(r)) continue;
                agenda_on(r, f, AGENDA_RIVALS);
                break;
            }
        }
    }
}

void setup_game(int editor) {
    *(uint8_t *)game_at(0x9a66c9) = 0;
    game_prefs_put(text(0x6915e4), text(0x945e78));
    game_prefs_put(text(0x6915f0), text(0x945e7c));
    int32_t *saved_prefs = word_at(0x94b464);
    if (!*game_difficulty && !editor && !game_prefs_get_2(text(0x691600), 0, 0)) {
        game_prefs_load(1);
        *saved_prefs |= 0x20;
        game_prefs_put_2(text(0x691618), 0, 0);
    }
    if (*game_net_multiplayer) *saved_prefs &= ~0x20;
    game_prefs_use();
    // The scenario's objectives cleared, and the years of the game.
    *game_objective_points = 0;
    *game_objectives_required = *game_objectives_sudden_death = 9999;
    *game_victory_points = 0;
    *game_start_year = rules_basic_word(0x11c);
    *word_at(0x94b4d4) = rules_basic_word(*game_difficulty < 3 ? 0x120 : 0x124);
    game_clear_monuments();
    game_wipe_undo();
    *word_at(0x9469f8) = 0;
    *game_interlude_state = 0;
    *game_state &= STATE_RANDOM_PERSONALITIES | STATE_RANDOM_AGENDAS;
    *word_at(0x9a64ac) = *word_at(0x9a64b0) = *game_tut_map_seen = *word_at(0x9a64b8) = *word_at(0x9a64bc) = 0;
    *game_turn = 0;
    *game_factions_alive = 0xff;
    *game_base_count = 0;
    *word_at(0x9a64d0) = 0;
    game_clear_all_offers();
    game_clear_hotseat_chat();
    game_clear_council_notify();
    game_clear_tamper();
    *word_at(0x9a64e4) = 0;
    *game_governor_faction = -1;
    for (int i = 0; i < 11; i++) word_at(0x9a6618)[i] = 0, word_at(0x9a6644)[i] = -9999;
    game_climate_shown[0] = game_climate_shown[1] = game_climate_shown[2] = game_climate_shown[3] = 0;
    if (!*game_net_multiplayer && editor && !*word_at(0x93a948)) *game_state |= STATE_EDITOR;
    memset(game_tech_achieved, 0, 0x59);
    for (int i = 0; i < SP_COUNT; i++) game_secret_projects[i] = -1;
    memset(game_at(0x950324), 0, 0x800 * 4);
    memset(game_turn_might, 0, 0xfa0 * 4);
    memset(game_sunspot_duration, 0, 10 * 4);
    if (*game_state & STATE_RANDOM_AGENDAS) random_agendas();
    for (int f = 0; f < FACTION_COUNT; f++)
        if (game_is_human(f)) setup_player(f, -1 - editor, 0);
    memset(game_tectonic_detonations, 0, FACTION_COUNT * 4);
    for (int f = 0; f < FACTION_COUNT; f++)
        if (!game_is_human(f)) setup_player(f, -1 - editor, 0);
    memset(game_diplo_state_with, 0, FACTION_COUNT * FACTION_COUNT * 4);
    struct faction_def *natives = &game_faction_defs[0];
    strcpy((char *)natives + 0x384, game_label(LABEL_NATIVE));
    strcpy(natives->noun, game_label(LABEL_NATIVE));
    strcpy(natives->adjective, game_label(LABEL_NATIVE_ADJ));
    if (editor) return;
    draw_pairs();
    if (!*game_net_multiplayer || (*game_rules & RULES_INTENSE_RIVALRY)) {
        if (!(*game_state & STATE_RANDOM_PERSONALITIES)) natural_rivals();
        else random_rivals();
    }
    if (*game_rules & RULES_TIME_WARP) game_time_warp();
    else game_balance();
}

// ---- Planetfall ----------------------------------------------------------------------

void planetfall(int faction) {
    const struct faction_def *d = &game_faction_defs[faction];
    *game_parse_plural = 0;
    *game_parse_gender = d->leader_female;
    game_parse_says(2, (char *)d->leader_title, -1, -1);
    *game_parse_gender = d->leader_female, *game_parse_plural = 0;
    game_parse_says(3, (char *)d->leader_name, -1, -1);
    game_parse_says(0, (char *)d->adjective, -1, -1);
    *game_parse_gender = d->noun_gender, *game_parse_plural = d->noun_plural;
    game_parse_says(1, (char *)d->noun, -1, -1);
    // The message for the faction: its own for two of the factions.
    char *label = game_battle_text;
    label[0] = 0;
    strcat(label, d->filename);
    (*game_char_upper)(label);
    if ((d->rule_flags & RULE_FLAG_ALIEN) && !strcmp(label, text(0x68f544))) {
        label[0] = 0;
        strcat(label, text(0x68f54c));
    } else {
        int other = strcmp(label, text(0x68f558));
        label[0] = 0;
        strcat(label, text(other ? 0x68f56c : 0x68f560));
    }
    // A late landing tells the turn and the faction's secret projects.
    if (*game_turn) {
        game_parse_num(0, *game_turn);
        for (int i = 0; i < SP_COUNT; i++) {
            int b = game_secret_projects[i];
            if (b >= 0 && game_bases[b].faction == faction)
                game_parse_say(4, (int)(intptr_t)game_facilities[FAC_FIRST_PROJECT + i].name, -1, -1);
        }
        strcat(label, text(0x68f578));
    }
    game_x_pop(label, 0);
}

// ---- Shadow mode ---------------------------------------------------------------------
//
// A landing founds a base at once unless the rules say look first, naming
// it from the faction's file, which a check can't open: the name is found
// first, before any state is protected, and both runs take it (as
// base_init's check does). Only one name can be held, so a new game whose
// factions all found bases runs unchecked; its sweep looks first. The
// network game's end isn't checked, and neither is a start that would
// destroy a base with a message on the main interface's list (see
// base_kill).

extern struct hook_module setup_module;
#define SETUP (&setup_module)

// The game's state, and the map window's centre, which the landing sets.
static void writes_setup(struct hook_writes *w) {
    hooks_writes_game(w);
    char *win = game_map_wins[0];
    if (win && (win + 0x1dd9c < (char *)game_data || win + 0x1dda4 > (char *)game_data + game_data_size))
        hooks_writes_add(w, win + 0x1dd9c, 8);
}

static int founds_base(int setup_id) {
    return initial(setup_id) &&
           !((*game_net_multiplayer || (*game_rules & RULES_LOOK_FIRST)) && !(*game_rules & RULES_TIME_WARP));
}

// The name the faction's new base would get, held for the check. Its count
// of names used starts again with a new start.
static void hold_base_name(int faction, int setup_id) {
    if (!faction || !founds_base(setup_id) || *game_base_count >= BASE_COUNT) return;
    struct faction_data *F = &game_factions[faction];
    int32_t land = F->base_name_offset, seas = F->base_sea_name_offset;
    F->base_name_offset = F->base_sea_name_offset = 0;
    char name[sizeof game_bases[0].name];
    memcpy(name, game_bases[*game_base_count].name, sizeof name);
    name[0] = 0;
    name_base_hold(faction, name, aquatic(faction));
    F->base_name_offset = land, F->base_sea_name_offset = seas;
}

static int base_listed_of(int faction) {
    for (int b = 0; b < *game_base_count; b++)
        if (game_bases[b].faction == faction && base_listed(b)) return 1;
    return 0;
}

SHADOW_WRITER_CHECK(int, setup_player, (int a, int b, int c), (a, b, c), orig_setup_player(a, b, c), writes_setup(&w_),
                    "%d, %d, %d", a, b, c)
static int shadow_setup_player(int faction, int setup_id, int probe) {
    if (hooks_nested) return orig_setup_player(faction, setup_id, probe);
    if (shadow_bypass()) return setup_player(faction, setup_id, probe);
    stats_setup_player.calls++;
    shadow_tick(SETUP);
    // A landing during the game (an escape after a defeat) can meet other
    // factions, and first contact opens talks (communicate, then
    // wants_to_speak's window, which allocates even between AIs); the
    // sweep checks those landings on protected copies.
    if (*game_net_multiplayer || base_listed_of(faction) || !initial(setup_id))
        return orig_setup_player(faction, setup_id, probe);
    hold_base_name(faction, setup_id);
    int r = check_setup_player(faction, setup_id, probe);
    name_base_release();
    return r;
}

// The hot seat's chat lists, which a new game frees.
static int hotseat_chat(void) {
    for (int i = 0; i < 64; i++)
        if (*word_at(0x737ce0 + 0x30 * i)) return 1;
    return 0;
}
// A new game is checked unless it reads the preferences file (the first
// game at the easiest level), frees the hot seat's chat, closes a network
// game, or founds the factions' bases; then each faction's start is
// checked on its own.
static int setup_game_checkable(int editor) {
    if (*game_net_multiplayer || (!*game_difficulty && !editor) || hotseat_chat()) return 0;
    return editor || ((*game_rules & RULES_LOOK_FIRST) && !(*game_rules & RULES_TIME_WARP));
}
SHADOW_WRITER_CHECK(void, setup_game, (int a), (a), orig_setup_game(a), writes_setup(&w_), "%d", a)
static void shadow_setup_game(int editor) {
    if (hooks_nested) {
        orig_setup_game(editor);
        return;
    }
    if (shadow_bypass()) {
        setup_game(editor);
        return;
    }
    stats_setup_game.calls++;
    shadow_tick(SETUP);
    if (setup_game_checkable(editor)) check_setup_game(editor);
    else orig_setup_game(editor);
}

SHADOW_WRITER(SETUP, void, planetfall, (int a), (a), hooks_writes_game(&w_), "%d", a)

// ---- Sweep -------------------------------------------------------------------------
//
// Once a turn, on protected copies of the game: factions set up as at the
// start (under the rules that decide what they land with), as late
// arrivals, escaping in a colony pod, landed by a probe team, or put out
// of the game, with the turn, the difficulty and the factions' standing
// varied, the land now and then left only at the poles, and rand() seeded
// anew for each; landing messages; and a whole new game, looking first.

static uint32_t mix(uint32_t a) {
    a ^= a >> 16, a *= 0x7feb352du;
    a ^= a >> 15, a *= 0x846ca68bu;
    return a ^ (a >> 16);
}
static inline int coin(uint32_t h, int bit, int n) { return (int)((h >> bit) % (uint32_t)n) == 0; }

static void case_begin(struct hook_writes *w) {
    *w = (struct hook_writes){.protect_only = 1};
    writes_setup(w);
    hooks_writes_begin(w);
}

// A human lands at the start only where no unit or base is within 6/56 of
// the map's size, which no try loosens: on a map cleared of them that is
// 32 squares, so a sweep makes a faction human there only on a map small
// enough (any the game makes).
static int human_can_land(void) { return *game_map_area_sqrt * 6 / 56 <= 32; }

// The faction's bases handed to another, so none is destroyed.
static void give_bases_away(int faction, int other) {
    for (int b = 0; b < *game_base_count; b++)
        if (game_bases[b].faction == faction) game_bases[b].faction = (uint8_t)other;
}

// Fungus everywhere but the three rows at each pole, so that a landing
// search runs long and comes to the rows next to the edges.
static void polar_only(void) {
    for (int y = 3; y < *game_map_height - 3; y++)
        for (int x = y & 1; x < *game_map_width; x += 2) tile(x, y)->bits |= MAP_FUNGUS;
}

enum { CASE_START, CASE_LATE, CASE_ESCAPE, CASE_PROBE, CASE_EDITOR, CASE_OUT, CASE_KINDS };

// A case's setup_id, probe flag and turn, and whether the map is cleared
// and the faction human, by its kind.
struct player_case {
    int setup_id, probe, turn, empty, human;
};
static struct player_case player_case(int kind, uint32_t s) {
    struct player_case c = {-1, 0, *game_turn, 0, 0};
    if (kind == CASE_START) {
        c.turn = 0;
        c.empty = coin(s, 0, 2);
        c.human = c.empty && human_can_land() && coin(s, 1, 3);
    } else if (kind == CASE_LATE) {
        c.setup_id = SETUP_LATE_ARRIVAL;
        c.turn = 1 + (int)((s >> 2) % 200);
    } else if (kind == CASE_ESCAPE) {
        c.setup_id = (int)((s >> 2) % 9) - 1;  // its killer, native life or none
        c.turn = 1 + (int)((s >> 6) % 150);
        c.human = coin(s, 14, 4);
    } else if (kind == CASE_PROBE) {
        c.setup_id = (int)((s >> 2) % 8);
        c.probe = 1;
        c.turn = 1 + (int)((s >> 6) % 150);
    } else if (kind == CASE_EDITOR) {
        c.setup_id = -2 - (int)((s >> 2) % 3);
        c.turn = (int)((s >> 6) % 3) * 50;
    } else {
        c.setup_id = (int)((s >> 2) % 8);
        c.turn = coin(s, 5, 2) ? 101 + (int)((s >> 6) % 100) : 1 + (int)((s >> 6) % 100);
        c.human = coin(s, 14, 4);
    }
    return c;
}

static void sweep_setup_player(int turn) {
    for (int k = 0; k < 12; k++) {
        uint32_t h = mix((uint32_t)turn * 0x9e3779b1u + (uint32_t)k), g = mix(h), s = mix(g), u = mix(s);
        int f = (int)(h % 7) + 1, kind = (int)((h >> 3) % CASE_KINDS), other = (int)((h >> 6) % 7) + 1;
        if (other == f) other = other % 7 + 1;
        // The rules first: the name a base would get is held before the
        // state is protected.
        uint32_t rules = *game_rules;
        if (coin(g, 0, 2)) rules ^= RULES_LOOK_FIRST;
        if (coin(g, 1, 5)) rules ^= RULES_TIME_WARP;
        if (coin(g, 4, 3)) rules ^= RULES_INTENSE_RIVALRY;
        if (coin(g, 6, 3)) rules ^= RULES_DO_OR_DIE;
        if (coin(g, 8, 3)) rules ^= RULES_VICTORY_COOPERATIVE;
        struct player_case c = player_case(kind, s);
        uint32_t saved_rules = *game_rules;
        int32_t saved_turn = *game_turn;
        *game_rules = rules, *game_turn = c.turn;
        hold_base_name(f, c.setup_id);
        *game_rules = saved_rules, *game_turn = saved_turn;
        struct hook_writes w;
        case_begin(&w);
        *game_rules = rules, *game_turn = c.turn;
        if (c.empty) *game_veh_count = *game_base_count = 0;
        if (base_listed_of(f) || !coin(s, 17, 3)) give_bases_away(f, other);
        *game_human_factions = c.human ? (uint8_t)(1u << f) : 0;
        if (coin(s, 19, 2)) *game_current_faction = f;
        else if (coin(s, 20, 2)) *game_current_faction = other;
        if (coin(s, 21, 3)) *game_difficulty = (int32_t)((s >> 23) % 6);
        if (coin(s, 26, 4)) *game_expansion = !*game_expansion;
        if (coin(s, 28, 3)) *game_state ^= STATE_RANDOM_PERSONALITIES;
        struct faction_data *F = &game_factions[f];
        if (kind == CASE_OUT && coin(u, 0, 3)) F->flags |= FACTION_FLAG_NO_LANDING;
        else if (coin(u, 0, 8)) F->flags ^= FACTION_FLAG_NO_LANDING;
        if (coin(u, 2, 3)) {
            game_faction_rankings[7] = (int32_t)((u >> 4) % 8);
            game_faction_rankings[6] = (int32_t)((u >> 7) % 8);
        }
        for (int i = 1; i < FACTION_COUNT; i++) {
            uint32_t v = mix(u + (uint32_t)i);
            if (coin(v, 0, 3)) game_factions[i].diplo_status[*game_current_faction] ^= TREATY_PACT;
            if (coin(v, 2, 4)) game_factions[i].diplo_status[*game_current_faction] ^= TREATY_SURRENDERED;
            if (coin(v, 4, 3)) game_factions[i].diplo_status[game_faction_rankings[7] & 7] ^= TREATY_PACT;
            if (coin(v, 6, 4)) *game_factions_alive ^= (uint8_t)(1u << i);
        }
        // The faction itself is in the game, but for a probe team's landing
        // now and then.
        *game_factions_alive |= (uint8_t)(1u << f);
        if (kind == CASE_PROBE && coin(u, 9, 3)) *game_factions_alive &= (uint8_t)~(1u << f);
        if (coin(u, 12, 4)) F->se_base[SE_RESEARCH] = (int32_t)((u >> 14) % 5) - 2;
        if (coin(u, 18, 4)) F->se_base[SE_MORALE] = (int32_t)((u >> 20) % 5) - 2;
        if (coin(u, 24, 3)) *game_victory_type = (int32_t)((u >> 26) % 3);
        if (!(c.human && !c.turn) && coin(mix(u ^ 0x27d4eb2du), 0, 4)) polar_only();
        *game_rand_state() = mix(u + 1);
        check_setup_player(f, c.setup_id, c.probe);
        hooks_writes_abandon(&w);
        name_base_release();
    }
}

static void sweep_planetfall(int turn) {
    for (int k = 0; k < 3; k++) {
        uint32_t h = mix((uint32_t)turn * 0x85ebca6bu + (uint32_t)k);
        int f = (int)(h % 8);
        struct hook_writes w;
        case_begin(&w);
        if (coin(h, 3, 2)) *game_turn = 0;
        for (int i = 0; i < SP_COUNT && *game_base_count > 0; i++)
            if (coin(mix(h + (uint32_t)i), 0, 6))
                game_secret_projects[i] = (int32_t)(mix(h ^ (uint32_t)i) % (uint32_t)*game_base_count);
        check_planetfall(f);
        hooks_writes_abandon(&w);
    }
}

// A whole new game, every faction looking first, with no human (whose
// landing could search for ever on a crowded map) unless the map is
// cleared, and none at the easiest level, which reads the preferences.
static void sweep_setup_game(int turn) {
    uint32_t h = mix((uint32_t)turn * 0xc2b2ae35u + 7);
    if (*game_net_multiplayer || hotseat_chat()) return;
    struct hook_writes w;
    case_begin(&w);
    int editor = coin(h, 0, 5);
    *game_rules |= RULES_LOOK_FIRST;
    *game_rules &= ~(uint32_t)RULES_TIME_WARP;
    if (coin(h, 1, 3)) *game_rules ^= RULES_INTENSE_RIVALRY;
    if (coin(h, 3, 2)) *game_state ^= STATE_RANDOM_PERSONALITIES;
    if (coin(h, 4, 2)) *game_state ^= STATE_RANDOM_AGENDAS;
    *game_difficulty = 1 + (int32_t)((h >> 6) % 5);
    *game_human_factions = 0;
    if (coin(h, 10, 2)) {
        *game_veh_count = 0;
        if (human_can_land() && coin(h, 11, 2)) *game_human_factions = (uint8_t)(1u << (1 + (h >> 13) % 7));
    }
    *game_rand_state() = mix(h + 1);
    check_setup_game(editor);
    hooks_writes_abandon(&w);
}

static void setup_sweep(void) {
    if (!*game_map || *game_veh_count <= 0) return;
    int sweeping = hooks_sweeping, turn = *game_turn;
    hooks_sweeping = 1;
    sweep_setup_player(turn);
    sweep_planetfall(turn);
    sweep_setup_game(turn);
    hooks_sweeping = sweeping;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook setup_hooks[] = {
    HOOK(0x5b0e00, setup_player, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0xdc, 0x00, 0x00, 0x00),  // sub esp, 0xdc
    HOOK(0x5b3920, setup_game, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x64),                    // sub esp, 0x64
    HOOK(0x589180, planetfall, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x8b, 0x5d, 0x08),              // push ebx; mov ebx, [ebp+8]
};
struct hook_module setup_module = {"setup", setup_hooks, HOOK_COUNT(setup_hooks), setup_sweep, NULL, -1};
