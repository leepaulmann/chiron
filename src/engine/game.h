// terranx.exe 2.0's game data, as the reimplemented modules see it. Layouts
// and addresses follow OpenSMACX (GPL-3.0-or-later) and Thinker (MIT).
//
// The data lives in the game's memory; game_bind() points these at it. A
// unit test may point them at its own tables instead.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../loader/pe.h"

// ---- Technology --------------------------------------------------------------

enum {
    TECH_COUNT = 89,      // entries in the #TECHNOLOGY table
    TECH_NONE = -1,       // "no prerequisite"
    TECH_DISABLED = -2,   // a tech switched off in alphax.txt
    TECH_TRANSCEND = 88,  // Transcendent Thought, never "had"
    // Techs that exist only in Alien Crossfire.
    TECH_FIRST_SMACX = 78,  // Progenitor Psych
    TECH_LAST_SMACX = 86,   // Temporal Mechanics
};

struct rules_tech {
    uint32_t flags;
    char *name;
    char id[8];  // short name, such as "Biogen"
    int32_t unused;
    int32_t growth, tech, wealth, power;  // the AI's view of what the tech is for
    int32_t preq1, preq2;                 // prerequisites, TECH_NONE or TECH_DISABLED
};
_Static_assert(sizeof(struct rules_tech) == 44, "layout of the game's #TECHNOLOGY entries");

enum {
    TECH_FLAG_SECRETS = 0x1,            // the first faction to discover it gets a free tech
    TECH_FLAG_COMMERCE = 0x4,           // +1 commerce
    TECH_FLAG_FUNGUS_ENERGY = 0x40,     // +1 energy from fungus
    TECH_FLAG_FUNGUS_MINERALS = 0x80,   // +1 minerals from fungus
    TECH_FLAG_FUNGUS_NUTRIENT = 0x100,  // +1 nutrient from fungus
};

// Techs tech_val() singles out.
enum { TECH_DOCINIT = 35, TECH_CENTMED = 47, TECH_ENVECON = 59, TECH_PLAECON = 61, TECH_ALPHCEN = 64 };

// ---- Layout helpers ---------------------------------------------------------

// Padding from offset `from` up to offset `to` inside a game struct.
#define GAME_PAD(from, to) uint8_t pad_##from[(to) - (from)]
#define GAME_AT(type, field, offset) _Static_assert(offsetof(type, field) == (offset), #type "." #field)

// ---- Factions -----------------------------------------------------------------

enum { FACTION_COUNT = 8 };  // 0 is the natives, 1 to 7 the players

enum {  // social effects, in the game's order
    SE_ECONOMY, SE_EFFIC, SE_SUPPORT, SE_TALENT, SE_MORALE, SE_POLICE,
    SE_GROWTH, SE_PLANET, SE_PROBE, SE_INDUSTRY, SE_RESEARCH, SE_COUNT
};

enum {  // diplo_status bits
    TREATY_PACT = 0x1, TREATY_TREATY = 0x2, TREATY_COMMLINK = 0x8,
    TREATY_VENDETTA = 0x10, TREATY_WANT_REVENGE = 0x20,
};

enum { FUNGUS_NUTRIENT, FUNGUS_MINERALS, FUNGUS_ENERGY, FUNGUS_UNUSED, FUNGUS_COUNT };

enum {  // region_base_plan values that tech_val() looks at
    PLAN_OFFENSIVE = 0, PLAN_DEFENSIVE = 2, PLAN_NAVAL_TRANSPORT = 7,
};

enum { REGION_COUNT = 128, LAND_REGION_COUNT = 64 };

// A faction's state in the game being played (Thinker's Faction, OpenSMACX's
// PlayerData). Only the fields Chiron uses are named.
struct faction_data {
    uint32_t flags;
    int32_t ranking;     // 0 (lowest) to 7
    int32_t diff_level;  // humans' difficulty; 3 for the AI
    GAME_PAD(0x00c, 0x018);
    uint32_t diplo_status[FACTION_COUNT];  // TREATY_* towards each faction
    GAME_PAD(0x038, 0x24c);
    int32_t se_pending[SE_COUNT];
    int32_t se[SE_COUNT];
    int32_t se_2[SE_COUNT];
    int32_t se_base[SE_COUNT];
    int32_t energy_surplus_total;
    int32_t facility_maint_total;
    int32_t tech_commerce_bonus;
    int32_t turn_commerce_income;
    int32_t unk_30c, unk_310;
    int32_t tech_fungus[FUNGUS_COUNT];  // extra output from fungus squares
    GAME_PAD(0x324, 0x35c);
    int32_t tech_ranking;  // twice the techs discovered
    int32_t unk_360;
    GAME_PAD(0x364, 0x3d0);
    int32_t tech_cost;  // research cost, or negative until computed
    int32_t earned_techs_saved;
    int32_t net_random_event;
    int32_t ai_fight, ai_growth, ai_tech, ai_wealth, ai_power;  // the AI's priorities
    GAME_PAD(0x3f0, 0x7fc);
    int32_t unk_7fc;  // tech_val() treats it as ecological damage
    GAME_PAD(0x800, 0x818);
    int32_t best_weapon_value;
    GAME_PAD(0x81c, 0x82c);
    int32_t enemy_best_weapon_value;
    GAME_PAD(0x830, 0x105c);
    int32_t base_count;
    GAME_PAD(0x1060, 0x117c);
    uint8_t region_total_bases[REGION_COUNT];
    GAME_PAD(0x11fc, 0x157c);
    uint16_t region_visible_tiles[REGION_COUNT];
    GAME_PAD(0x167c, 0x197c);
    uint8_t region_base_plan[REGION_COUNT];  // PLAN_* for the AI
    GAME_PAD(0x19fc, 0x207c);
    int32_t corner_market_turn;
    GAME_PAD(0x2080, 0x20cc);
};
_Static_assert(sizeof(struct faction_data) == 0x20cc, "faction_data size");
GAME_AT(struct faction_data, diplo_status, 0x18);
GAME_AT(struct faction_data, se_pending, 0x24c);
GAME_AT(struct faction_data, se_base, 0x2d0);
GAME_AT(struct faction_data, tech_commerce_bonus, 0x304);
GAME_AT(struct faction_data, tech_fungus, 0x314);
GAME_AT(struct faction_data, tech_cost, 0x3d0);
GAME_AT(struct faction_data, ai_fight, 0x3dc);
GAME_AT(struct faction_data, ai_power, 0x3ec);

enum {  // faction_def bonus ids
    BONUS_FUNGUS_NUTRIENT = 7, BONUS_FUNGUS_MINERALS = 8, BONUS_FUNGUS_ENERGY = 9,
};

// A faction as its faction file defines it (Thinker's MFaction, OpenSMACX's
// Player).
struct faction_def {
    GAME_PAD(0x000, 0x2e4);
    char noun[24];
    int32_t noun_gender;
    int32_t noun_plural;
    char adjective[128];  // the name as an adjective, such as "Gaian"
    GAME_PAD(0x384, 0x4f0);
    int32_t rule_population, rule_hurry, rule_techcost, rule_psi, rule_sharetech, rule_commerce, rule_flags;
    int32_t bonus_count;
    int32_t bonus_id[8], bonus_val1[8], bonus_val2[8];
    GAME_PAD(0x570, 0x59c);
};
_Static_assert(sizeof(struct faction_def) == 0x59c, "faction_def size");
GAME_AT(struct faction_def, adjective, 0x304);
GAME_AT(struct faction_def, rule_techcost, 0x4f8);
GAME_AT(struct faction_def, bonus_count, 0x50c);
GAME_AT(struct faction_def, bonus_val1, 0x530);

// ---- Units and the rules tables ---------------------------------------------

enum { PROTO_COUNT = 512, PROTO_FORMERS = 1 };
enum { WEAPON_COUNT = 26, ARMOR_COUNT = 14, REACTOR_COUNT = 4, CHASSIS_COUNT = 9 };
enum { CHASSIS_FOIL = 3, REACTOR_FUSION = 2, REACTOR_QUANTUM = 3 };  // ids; reactors count from 1
enum {
    FAC_TREE_FARM = 13, FAC_HYBRID_FOREST = 14, FAC_HAB_COMPLEX = 24, FAC_HABITATION_DOME = 25,
    FAC_CENTAURI_PRESERVE = 31, FAC_TEMPLE_OF_PLANET = 32, FAC_HUNTER_SEEKER = 85,
    FAC_DREAM_TWISTER = 89, FAC_ASCENT = 102, FACILITY_COUNT = 134,
};

struct unit_proto {
    char name[32];
    uint32_t abilities;
    uint8_t chassis, weapon, armor, reactor;
    GAME_PAD(0x28, 0x32);
    int16_t preq_tech;
};
_Static_assert(sizeof(struct unit_proto) == 0x34, "unit_proto size");
GAME_AT(struct unit_proto, chassis, 0x24);

struct rules_weapon {
    char *name, *short_name;
    int8_t offense;  // negative for psi
    int8_t icon;
    uint8_t mode, cost;
    int16_t preq_tech;
    int16_t pad;
};
_Static_assert(sizeof(struct rules_weapon) == 0x10, "rules_weapon size");

struct rules_armor {
    char *name, *short_name;
    int8_t defense;
    uint8_t mode, cost;
    int8_t pad1;
    int16_t preq_tech;
    int16_t pad2;
};
_Static_assert(sizeof(struct rules_armor) == 0x10, "rules_armor size");

struct rules_reactor {
    char *name, *short_name;
    int16_t preq_tech;
    int16_t power;
};
_Static_assert(sizeof(struct rules_reactor) == 0xc, "rules_reactor size");

enum { TRIAD_LAND, TRIAD_SEA, TRIAD_AIR };

struct rules_chassis {
    GAME_PAD(0x00, 0x48);
    uint8_t speed;
    uint8_t triad;  // TRIAD_*
    GAME_PAD(0x4a, 0x8e);
    int16_t preq_tech;
};
_Static_assert(sizeof(struct rules_chassis) == 0x90, "rules_chassis size");

struct rules_facility {
    GAME_PAD(0x00, 0x14);
    int32_t preq_tech;
    GAME_PAD(0x18, 0x30);
};
_Static_assert(sizeof(struct rules_facility) == 0x30, "rules_facility size");

// A region: a continent, an island or a body of water.
struct continent {
    int32_t tile_count;
    GAME_PAD(0x04, 0x14);
    uint8_t sea_coasts[8];  // bit per land region its shore touches
};
_Static_assert(sizeof(struct continent) == 0x1c, "continent size");

enum {  // game_rules bits
    RULES_TECH_STAGNATION = 0x20,
    RULES_BLIND_RESEARCH = 0x200,
    RULES_NO_UNITY_SCATTERING = 0x2000,
};

enum { FACTION_FLAG_MAP_REVEALED = 0x200 };  // faction_data.flags

enum { TREATY_UNK_8000000 = 0x8000000 };  // with TREATY_COMMLINK, whose_territory() names the owner

enum { GAME_STATE_OMNISCIENT = 0x80 };  // game_state bits

// ---- The map -------------------------------------------------------------------

// Squares are stored for x + y even only, so a row holds width / 2 of them.
struct map_tile {
    uint8_t climate;     // altitude << 5 | rainfall << 3 | temperature
    uint8_t contour;     // detailed altitude
    uint8_t owner_site;  // site << 4 | the faction with a unit or base here (0xf for none)
    uint8_t region;
    uint8_t visibility;  // bit per faction that has seen the square
    uint8_t rocky_lock;  // rockiness << 6 | locking faction << 3 | using faction
    uint8_t unk_6;
    int8_t territory;    // owning faction, or -1
    uint32_t bits;       // MAP_* below
    uint32_t bits2;      // landmark sequence << 24 | MAP2_* landmarks
    uint32_t bits_seen[7];  // what each faction last saw of `bits`
};
_Static_assert(sizeof(struct map_tile) == 0x2c, "map_tile size");

enum {  // map_tile.bits
    MAP_BASE = 0x1, MAP_UNIT = 0x2, MAP_RESOURCE = 0x400, MAP_MONOLITH = 0x2000,
    MAP_ENERGY_RESOURCE = 0x10000, MAP_MINERAL_RESOURCE = 0x20000, MAP_POD_TAKEN = 0x100000,
    MAP_UNITY_POD = 0xc000000,  // either bit, without unity scattering
    MAP_SUPPLY_POD = 0x10000000, MAP_NUTRIENT_RESOURCE = 0x20000000, MAP_SENSOR = 0x80000000,
};
enum {  // map_tile.bits2
    MAP2_NEXUS = 0x2000,          // the Manifold Nexus
    MAP2_CHANGED = 0x400000,      // rainfall or rockiness set since the map was made
    MAP2_UNK_80000000 = 0x80000000,
    MAP2_LANDMARK_CODE = 0xff000000,  // which square of its landmark this is
};
enum {  // game_draw_state bits the map setters raise
    DRAW_TERRAIN = 0x1, DRAW_LANDMARKS = 0x4,
};

enum {  // map_tile.climate >> 5
    ALT_SHORE_LINE = 3,  // the lowest land
};

enum { LANDMARK_COUNT = 64 };

struct landmark {
    int32_t x, y;
    char name[32];
};
_Static_assert(sizeof(struct landmark) == 0x28, "landmark size");

// ---- Bases ---------------------------------------------------------------------

enum { BASE_COUNT = 512 };

enum { FAC_GEOSYNC_SURVEY_POD = 41 };

struct base {
    int16_t x, y;
    uint8_t faction;
    GAME_PAD(0x05, 0x09);
    uint8_t visibility;  // bit per faction that can see it
    GAME_PAD(0x0a, 0x8c);
    uint8_t facilities_built[12];  // bit per facility, below the secret projects
    GAME_PAD(0x98, 0x134);
};
_Static_assert(sizeof(struct base) == 0x134, "base size");
GAME_AT(struct base, facilities_built, 0x8c);

static inline int game_has_facility(const struct base *b, int facility) {
    return (b->facilities_built[facility / 8] & (1 << (facility % 8))) != 0;
}

// ---- Units ---------------------------------------------------------------------

enum { VEH_FLAG_INVISIBLE = 0x400 };

struct veh {
    int16_t x, y;
    uint32_t state;
    uint16_t flags;  // VEH_FLAG_*
    int16_t proto;
    int16_t unk_c;
    uint8_t faction;
    GAME_PAD(0x0f, 0x27);
    uint8_t visibility;  // bit per other faction that can see it
    GAME_PAD(0x28, 0x2e);
    int16_t home_base;
    int16_t next_in_stack, prev_in_stack;  // -1 at the ends
};
_Static_assert(sizeof(struct veh) == 0x34, "veh size");
GAME_AT(struct veh, visibility, 0x27);
GAME_AT(struct veh, home_base, 0x2e);

enum { RADIUS_SQUARES = 289 };

// ---- The game's data ----------------------------------------------------------

extern struct rules_tech *game_tech;  // [TECH_COUNT]
extern uint8_t *game_tech_achieved;   // [TECH_COUNT], one bit per faction
extern int32_t *game_expansion;       // non-zero when playing Alien Crossfire
extern int32_t *game_turn;            // the current turn

extern struct faction_data *game_factions;    // [FACTION_COUNT]
extern struct faction_def *game_faction_defs;  // [FACTION_COUNT]
extern uint8_t *game_human_factions;          // bit per faction
extern struct unit_proto *game_protos;        // [PROTO_COUNT]
extern struct rules_weapon *game_weapons;     // [WEAPON_COUNT]
extern struct rules_armor *game_armor;        // [ARMOR_COUNT]
extern struct rules_reactor *game_reactors;   // [REACTOR_COUNT], for reactor ids 1 to 4
extern struct rules_chassis *game_chassis;    // [CHASSIS_COUNT]
extern struct rules_facility *game_facilities;  // [FACILITY_COUNT]
extern struct continent *game_continents;     // [REGION_COUNT]
extern int32_t *game_tech_discovery_rate;     // percent; 0 stops research
extern uint32_t *game_rules;                  // RULES_* chosen for this game
extern int32_t *game_difficulty;              // the AI's difficulty level
extern int32_t *game_map_area_sqrt;           // square root of the map's area
extern int32_t *game_cloud_cover;
extern int32_t *game_language;                // non-zero for translated text
extern int32_t *game_parse_gender, *game_parse_plural;  // grammar for the next noun in text
extern char ***game_labels;                   // the strings of labels.txt
extern void *game_strings;                    // the string table
extern char *game_tech_name_buffer;           // what tech_name() returns

extern struct map_tile **game_map;            // the squares, row by row
extern int32_t *game_map_width, *game_map_height;
extern int32_t *game_map_half_width;          // squares in a row
extern uint32_t *game_map_flat;               // bit 0: the map doesn't wrap east to west
extern int32_t *game_map_seed;                // places resources and pods
extern int32_t *game_sea_level;
extern int32_t *game_elev_detail;             // [11] contour where each altitude starts, for elevation
extern int32_t *game_alt_natural;             // [11] contour above sea level where each altitude starts
extern int32_t *game_goody_mult;              // [2] how pods are spread
extern uint8_t **game_abstract_map;           // the coarse map the AI plans on
extern int32_t *game_abstract_width, *game_abstract_height;
extern int32_t *game_landmark_count;
extern struct landmark *game_landmarks;       // [LANDMARK_COUNT]
extern struct base *game_bases;               // [BASE_COUNT]
extern int32_t *game_base_count;
extern struct veh *game_vehs;
extern int32_t *game_veh_count;
extern uint32_t *game_state;                  // GAME_STATE_*
extern int32_t *game_net_multiplayer;         // non-zero in a network game
extern uint32_t *game_draw_state;             // what the map view must redraw
extern int32_t *game_base_find_dist;          // set by the game's base_find()
// Square offsets: the 8 neighbours clockwise from north-east, then the
// square itself; and every square within each radius, nearest first, with
// game_radius_count[r] the number within radius r.
extern int32_t *game_radius_near_x, *game_radius_near_y;  // [9]
extern int32_t *game_radius_x, *game_radius_y;            // [RADIUS_SQUARES]
extern int32_t *game_radius_count;                        // [9]

// Game functions the reimplementations call.
extern char *(__attribute__((thiscall)) * game_strings_get)(void *table, char *s);
extern int (*game_climactic_battle)(void);
extern int (*game_weapon_budget)(int faction, int condition, int check_mode);
extern int (*game_wants_to_attack)(int faction, int other, int odds);
extern int (*game_base_at)(int x, int y);    // the base on the square, or -1
extern int (*game_veh_at)(int x, int y);     // the top unit on the square, or -1
extern int (*game_base_find)(int x, int y);  // the nearest base, or -1
// The nearest base in `region` (any if negative) that `faction` may see,
// held by `owner` (see base_find in OpenSMACX), or -1.
extern int (*game_base_find3)(int x, int y, int faction, int region, int owner, int seen_by);

// Point the variables above at a loaded terranx.exe.
void game_bind(struct pe_image *exe);

// ---- Small helpers the original code inlines --------------------------------

static inline int game_veh_triad(int veh) { return game_chassis[game_protos[game_vehs[veh].proto].chassis].triad; }

static inline int game_is_human(int faction) { return (*game_human_factions & (1u << (faction & 31))) != 0; }

static inline uint32_t game_has_treaty(int faction, int other, uint32_t treaty) {
    return game_factions[faction].diplo_status[other] & treaty;
}

// The game's range(): min if below it (or if max < min), max if above it.
static inline int game_range(int value, int min, int max) {
    if (value < min || max < min) return min;
    return value > max ? max : value;
}

static inline int game_bit_count(uint32_t bits) {
    int n = 0;
    for (; bits; n++) bits &= bits - 1;
    return n;
}

// A region number that stands for no real land or sea area.
static inline int game_bad_region(int region) { return (region & 63) == 63 || !(region & 63); }

// A label from labels.txt, by index.
static inline char *game_label(int i) { return game_strings_get(game_strings, (*game_labels)[i]); }
