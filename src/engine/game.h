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
    TREATY_VENDETTA = 0x10, TREATY_WANT_REVENGE = 0x20, TREATY_UNK_40 = 0x40,
    TREATY_UNK_100 = 0x100, TREATY_UNK_200 = 0x200, TREATY_SHALL_BETRAY = 0x400, TREATY_UNK_800 = 0x800,
    TREATY_WANT_TO_TALK = 0x2000, TREATY_UNK_8000 = 0x8000, TREATY_UNK_20000 = 0x20000,
    TREATY_ATROCITY_VICTIM = 0x40000, TREATY_UNK_1000000 = 0x1000000, TREATY_UNK_4000000 = 0x4000000,
    TREATY_UNK_10000000 = 0x10000000, TREATY_UNK_20000000 = 0x20000000,
};
#define TREATY_UNK_80000000 0x80000000u
enum { AGENDA_UNK_20 = 0x20, AGENDA_UNK_40 = 0x40, AGENDA_UNK_400 = 0x400, AGENDA_UNK_800 = 0x800 };  // diplo_agenda bits
enum { FACTION_FLAG_EXT_SHAMELESS = 0x20 };  // flags_ext: betrays humans without cause

enum { FUNGUS_NUTRIENT, FUNGUS_MINERALS, FUNGUS_ENERGY, FUNGUS_UNUSED, FUNGUS_COUNT };

enum {  // region_base_plan values that tech_val() looks at
    PLAN_OFFENSIVE = 0, PLAN_COMBAT = 1, PLAN_DEFENSIVE = 2, PLAN_RECON = 3, PLAN_AIR_SUPERIORITY = 4,
    PLAN_PLANET_BUSTER = 5, PLAN_NAVAL_TRANSPORT = 7, PLAN_COLONY = 8, PLAN_SUPPLY = 10,
};

enum { REGION_COUNT = 128, LAND_REGION_COUNT = 64 };

// Something the AI wants done at a square: GOAL_* (or a PLAN_* for units
// of that plan), with a priority.
struct goal {
    int16_t type, priority;
    int32_t x, y, base;
};
_Static_assert(sizeof(struct goal) == 0x10, "goal size");
enum { GOAL_COUNT = 75 };
enum {
    GOAL_ATTACK = 0, GOAL_DEFEND = 2, GOAL_SCOUT = 3, GOAL_UNK_6 = 6, GOAL_COLONIZE = 8, GOAL_TERRAFORM = 9,
    GOAL_ARTILLERY = 16, GOAL_RAISE_LAND = 41,
};

// A faction's state in the game being played (Thinker's Faction, OpenSMACX's
// PlayerData). Only the fields Chiron uses are named.
struct faction_data {
    uint32_t flags;
    int32_t ranking;     // 0 (lowest) to 7
    int32_t diff_level;  // humans' difficulty; 3 for the AI
    int32_t base_name_offset, base_sea_name_offset;  // base names used from the faction's file
    int32_t last_base_turn;  // when it last gained a base
    uint32_t diplo_status[FACTION_COUNT];  // TREATY_* towards each faction
    uint32_t diplo_agenda[FACTION_COUNT];  // AGENDA_* towards each faction
    int32_t diplo_friction[FACTION_COUNT];
    int32_t diplo_spoke[FACTION_COUNT];  // the turn each last spoke to it, or -1
    int32_t diplo_merc[FACTION_COUNT];   // turns left of a betrayal's mood (TREATY_UNK_40)
    int8_t diplo_patience[FACTION_COUNT];
    int32_t sanction_turns;  // economic sanctions: no commerce while above 0
    int32_t loan_balance[FACTION_COUNT];  // energy owed to each faction
    int32_t loan_payment[FACTION_COUNT];  // and paid to it each turn
    GAME_PAD(0x104, 0x124);
    int32_t integrity_blemishes;  // treaties broken
    GAME_PAD(0x128, 0x1d0);
    uint32_t base_governor_adv;  // the governor settings new bases start with
    int32_t atrocities;
    int32_t major_atrocities;
    int32_t mind_control_total;  // bases mind-controlled (4 each) and units subverted by its probes
    int32_t diplo_mind_control[FACTION_COUNT];  // bases (4 each) and units (1) its probes took from each faction
    int32_t diplo_stolen_techs[FACTION_COUNT];  // techs and maps its probes stole from each
    int32_t energy_credits, hurry_cost_total;
    int32_t se_models_pending[4];  // politics, economics, values, future
    int32_t se_models[4];          // as they stand this turn
    int32_t se_upheaval_paid;  // energy paid this turn to change social models
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
    int32_t alloc_psych, alloc_labs;  // energy allocation, in tenths; economy gets the rest
    GAME_PAD(0x32c, 0x35c);
    int32_t tech_ranking;  // twice the techs discovered
    int32_t unk_360;
    int32_t odp_deployed;  // orbital defense pods used this turn
    int32_t transcendent_techs;  // techs past the end of the tree
    GAME_PAD(0x36c, 0x3cc);
    int32_t tech_research_id;  // the tech being researched, or negative
    int32_t tech_cost;  // research cost, or negative until computed
    int32_t earned_techs_saved;
    int32_t net_random_event;  // base_research() keeps the hundredths of a lab left over here
    int32_t ai_fight, ai_growth, ai_tech, ai_wealth, ai_power;  // the AI's priorities
    int32_t target_x, target_y;  // the AI's, or -1
    int32_t best_mineral_output;  // the most a base of the faction has made, its surplus counted twice
    GAME_PAD(0x3fc, 0x458);
    uint8_t facility_announced[12];  // bit per facility the player has been shown
    int32_t clean_minerals;  // minerals a base makes before ecological damage
    int32_t attack_target_base;  // the AI's, or -1
    GAME_PAD(0x46c, 0x690);
    // Units costing support at each SUPPORT from -4, added up for the AI.
    int32_t social_support[8];
    // How content bases would be at each TALENT from -3 and POLICE from -5,
    // added up for the AI.
    int32_t social_psych[8][9];
    int32_t social_effic[9];  // energy lost this turn at each EFFICIENCY from 4 down, for the AI
    int32_t unk_7f4;  // social_ai's starting weight for a high ECONOMY
    int32_t armed_units;  // armed units, offensive ones twice, added up over the turn
    int32_t eco_damage_total;  // ecological damage over the game, weighed by PLANET
    int32_t nutrient_surplus_total, labs_total;
    int32_t satellites[4];  // orbital nutrient, mineral and energy satellites, and defense pods
    int32_t best_weapon_value;
    int32_t best_psi_offense;  // land units' psi attack
    int32_t best_psi_defense, best_armor_value, best_land_speed;
    int32_t enemy_best_weapon_value;
    int32_t enemy_best_armor_value;
    GAME_PAD(0x834, 0x840);
    // Counted by enemy_strategy among the units of factions it has a
    // commlink with and isn't at peace with: transports, the reactors of
    // naval superiority ships, air attackers that can pierce its best
    // armor, and missiles other than planet busters.
    int32_t enemy_transports, enemy_naval_reactors, enemy_air_attackers, unk_84c, enemy_missiles;
    GAME_PAD(0x854, 0x858);
    uint8_t units_active[512], units_queue[512];  // per prototype (PROTO_COUNT): its units, and those being built
    GAME_PAD(0xc58, 0x1058);
    int32_t total_combat_units;
    int32_t base_count;
    int32_t mil_strength;  // military strength, for the AI's diplomacy
    int32_t mil_attack;  // the attack part of mil_strength
    int32_t pop_total;
    // Sea units, plus transports << 11 and the most cargo one carries << 22
    // (enemy_strategy); its rivals' greatest cargo is added to
    // enemy_transports << 11.
    int32_t naval;
    int32_t planet_busters;
    int32_t target_units;  // land units sent against the target (target_x)
    uint32_t target_kinds;  // 1 with a defender among them, 2 with a probe
    // The AI's planning for each region (the map's land and sea areas),
    // updated by enemy_strategy.
    uint16_t region_total_combat_units[REGION_COUNT];
    uint8_t region_total_bases[REGION_COUNT];
    uint8_t region_offensive[REGION_COUNT];  // units kept back to defend, in the goal pass
    uint16_t region_force_rating[REGION_COUNT];  // attack strength
    uint16_t region_flags[REGION_COUNT];  // movement planning
    // Counted by reset_territory: squares the faction owns, has seen, owns
    // and could farm (rainy, not rocky, no fungus), and those of the last
    // within a base's radius; the best site value and the pods it owns.
    uint16_t region_territory_tiles[REGION_COUNT];
    uint16_t region_visible_tiles[REGION_COUNT];
    uint16_t region_good_tiles[REGION_COUNT];
    uint16_t region_good_base_tiles[REGION_COUNT];
    uint8_t region_best_site[REGION_COUNT];
    uint8_t region_territory_pods[REGION_COUNT];
    uint8_t region_base_plan[REGION_COUNT];  // PLAN_* for the AI
    struct goal goals[GOAL_COUNT];  // the AI's, for this turn
    struct goal sites[25];  // places the AI means to settle (type 3) or otherwise act on
    GAME_PAD(0x203c, 0x205c);
    // News of the secret projects other factions have begun: bitmask()'s
    // byte index and bit, so 8 bits a word.
    uint32_t project_intel[8];
    int32_t corner_market_turn, corner_market_cost;  // when the energy market is cornered, and what it cost
    GAME_PAD(0x2084, 0x2090);
    uint32_t flags_ext;  // FACTION_FLAG_EXT_*
    int32_t air_offense, air_superiority;  // air units of each plan
    GAME_PAD(0x209c, 0x20cc);
};
_Static_assert(sizeof(struct faction_data) == 0x20cc, "faction_data size");
GAME_AT(struct faction_data, diplo_status, 0x18);
GAME_AT(struct faction_data, diplo_merc, 0x98);
GAME_AT(struct faction_data, diplo_patience, 0xb8);
GAME_AT(struct faction_data, integrity_blemishes, 0x124);
GAME_AT(struct faction_data, odp_deployed, 0x364);
GAME_AT(struct faction_data, target_x, 0x3f0);
GAME_AT(struct faction_data, mil_strength, 0x1060);
GAME_AT(struct faction_data, flags_ext, 0x2090);
GAME_AT(struct faction_data, diplo_stolen_techs, 0x200);
GAME_AT(struct faction_data, best_armor_value, 0x824);
GAME_AT(struct faction_data, sites, 0x1eac);
GAME_AT(struct faction_data, enemy_best_armor_value, 0x830);
GAME_AT(struct faction_data, enemy_missiles, 0x850);
GAME_AT(struct faction_data, mil_attack, 0x1064);
GAME_AT(struct faction_data, target_kinds, 0x1078);
GAME_AT(struct faction_data, region_force_rating, 0x127c);
GAME_AT(struct faction_data, goals, 0x19fc);
GAME_AT(struct faction_data, air_superiority, 0x2098);
GAME_AT(struct faction_data, last_base_turn, 0x14);
GAME_AT(struct faction_data, base_sea_name_offset, 0x10);
GAME_AT(struct faction_data, attack_target_base, 0x468);
GAME_AT(struct faction_data, se_pending, 0x24c);
GAME_AT(struct faction_data, se_base, 0x2d0);
GAME_AT(struct faction_data, energy_credits, 0x220);
GAME_AT(struct faction_data, facility_maint_total, 0x300);
GAME_AT(struct faction_data, net_random_event, 0x3d8);
GAME_AT(struct faction_data, tech_commerce_bonus, 0x304);
GAME_AT(struct faction_data, tech_fungus, 0x314);
GAME_AT(struct faction_data, tech_cost, 0x3d0);
GAME_AT(struct faction_data, ai_fight, 0x3dc);
GAME_AT(struct faction_data, ai_power, 0x3ec);
GAME_AT(struct faction_data, major_atrocities, 0x1d8);
GAME_AT(struct faction_data, mind_control_total, 0x1dc);
GAME_AT(struct faction_data, transcendent_techs, 0x368);
GAME_AT(struct faction_data, clean_minerals, 0x464);
GAME_AT(struct faction_data, nutrient_surplus_total, 0x800);
GAME_AT(struct faction_data, sanction_turns, 0xc0);
GAME_AT(struct faction_data, loan_payment, 0xe4);
GAME_AT(struct faction_data, unk_310, 0x310);
GAME_AT(struct faction_data, tech_research_id, 0x3cc);
GAME_AT(struct faction_data, alloc_labs, 0x328);
GAME_AT(struct faction_data, region_total_bases, 0x117c);
GAME_AT(struct faction_data, region_base_plan, 0x197c);
GAME_AT(struct faction_data, region_territory_tiles, 0x147c);
GAME_AT(struct faction_data, region_territory_pods, 0x18fc);
GAME_AT(struct faction_data, se_models_pending, 0x228);
GAME_AT(struct faction_data, se_models, 0x238);
GAME_AT(struct faction_data, se_upheaval_paid, 0x248);
GAME_AT(struct faction_data, energy_surplus_total, 0x2fc);
GAME_AT(struct faction_data, alloc_psych, 0x324);
GAME_AT(struct faction_data, social_support, 0x690);
GAME_AT(struct faction_data, social_psych, 0x6b0);
GAME_AT(struct faction_data, armed_units, 0x7f8);
GAME_AT(struct faction_data, social_effic, 0x7d0);
GAME_AT(struct faction_data, base_count, 0x105c);
GAME_AT(struct faction_data, corner_market_cost, 0x2080);
GAME_AT(struct faction_data, units_active, 0x858);
GAME_AT(struct faction_data, units_queue, 0xa58);
GAME_AT(struct faction_data, base_governor_adv, 0x1d0);
GAME_AT(struct faction_data, project_intel, 0x205c);
GAME_AT(struct faction_data, best_weapon_value, 0x818);
GAME_AT(struct faction_data, best_psi_offense, 0x81c);
GAME_AT(struct faction_data, unk_7f4, 0x7f4);
GAME_AT(struct faction_data, total_combat_units, 0x1058);
GAME_AT(struct faction_data, pop_total, 0x1068);
GAME_AT(struct faction_data, planet_busters, 0x1070);
GAME_AT(struct faction_data, region_total_combat_units, 0x107c);
GAME_AT(struct faction_data, region_flags, 0x137c);

enum {  // faction_def bonus ids
    BONUS_FUNGUS_NUTRIENT = 7, BONUS_FUNGUS_MINERALS = 8, BONUS_FUNGUS_ENERGY = 9,
    BONUS_NODRONE = 14,  // drones made content at each base
    BONUS_FACILITY = 2,  // a free facility at each base
    BONUS_FREEFAC = 12,  // the same, once its tech is known
    BONUS_REVOLT = 13,   // percent more chance that another faction's revolting bases join it
    // Social engineering: a model's penalties cancelled or doubled; an
    // effect kept from going negative, or its negative halved.
    BONUS_IMMUNITY = 4, BONUS_IMPUNITY = 5, BONUS_PENALTY = 6, BONUS_ROBUST = 10,
};

// A faction as its faction file defines it (Thinker's MFaction, OpenSMACX's
// Player).
struct faction_def {
    int32_t leader_female;
    char filename[24];
    char search_key[24];
    char leader_name[24], leader_title[24];
    GAME_PAD(0x064, 0x2e4);
    char noun[24];
    int32_t noun_gender;
    int32_t noun_plural;
    char adjective[128];  // the name as an adjective, such as "Gaian"
    GAME_PAD(0x384, 0x4d4);
    int32_t rule_tech_selected;  // free techs at the start
    int32_t rule_morale;
    int32_t rule_research, rule_drone, rule_talent, rule_energy, rule_interest;
    int32_t rule_population, rule_hurry, rule_techcost, rule_psi, rule_sharetech, rule_commerce, rule_flags;
    int32_t bonus_count;
    int32_t bonus_id[8], bonus_val1[8], bonus_val2[8];
    GAME_PAD(0x570, 0x584);
    // Its leader's social preferences: a model it favours (category and
    // model, or -1), one it will never choose, and an effect it favours
    // (SE_*, or -1).
    int32_t ideology_category, anti_ideology_category, ideology_model, anti_ideology_model;
    int32_t ideology_effect;
    GAME_PAD(0x598, 0x59c);
};
_Static_assert(sizeof(struct faction_def) == 0x59c, "faction_def size");
GAME_AT(struct faction_def, noun, 0x2e4);
GAME_AT(struct faction_def, adjective, 0x304);
GAME_AT(struct faction_def, rule_flags, 0x508);
GAME_AT(struct faction_def, rule_tech_selected, 0x4d4);
GAME_AT(struct faction_def, rule_interest, 0x4ec);
GAME_AT(struct faction_def, rule_techcost, 0x4f8);
GAME_AT(struct faction_def, leader_title, 0x4c);
GAME_AT(struct faction_def, rule_research, 0x4dc);
GAME_AT(struct faction_def, bonus_count, 0x50c);
GAME_AT(struct faction_def, bonus_val1, 0x530);
GAME_AT(struct faction_def, rule_population, 0x4f0);
GAME_AT(struct faction_def, ideology_category, 0x584);
GAME_AT(struct faction_def, ideology_effect, 0x594);

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
    uint8_t carry, cost;
    uint8_t plan;  // PLAN_*; up to PLAN_RECON for combat units
    uint8_t group;  // 21 for units that take no goals
    uint8_t obsolete_factions;  // bit per faction that has a better design
    GAME_PAD(0x2d, 0x30);
    uint16_t flags;  // PROTO_FLAG_*
    int16_t preq_tech;
};
enum { PROTO_FLAG_ACTIVE = 0x1, PROTO_FLAG_BUILT = 0x4, PROTO_FLAG_SEEN_ONLINE = 0x100 };
_Static_assert(sizeof(struct unit_proto) == 0x34, "unit_proto size");
GAME_AT(struct unit_proto, chassis, 0x24);
GAME_AT(struct unit_proto, plan, 0x2a);
GAME_AT(struct unit_proto, obsolete_factions, 0x2c);
GAME_AT(struct unit_proto, flags, 0x30);

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
    uint8_t range;  // turns aloft for air units, else 0
    uint8_t cargo;  // units a transport of it carries, before the reactor
    GAME_PAD(0x4c, 0x4d);
    uint8_t missile;
    GAME_PAD(0x4e, 0x8e);
    int16_t preq_tech;
};
_Static_assert(sizeof(struct rules_chassis) == 0x90, "rules_chassis size");
GAME_AT(struct rules_chassis, cargo, 0x4b);

struct rules_facility {
    char *name;
    GAME_PAD(0x04, 0x0c);
    int32_t cost;  // in rows of minerals
    int32_t maint;  // energy a turn
    int32_t preq_tech;
    int32_t free_tech;
    int32_t ai_fight, ai_growth, ai_tech, ai_wealth, ai_power;  // the AI's weights for it, by its priorities
};
_Static_assert(sizeof(struct rules_facility) == 0x30, "rules_facility size");

// A region: a continent, an island or a body of water.
struct continent {
    int32_t tile_count;
    int32_t open_terrain;  // land squares that are neither arid, rocky nor fungus
    int32_t best_site;     // the highest site value (world_site) of its squares
    int32_t pods;          // supply and unity pods left in it
    GAME_PAD(0x10, 0x14);
    uint8_t sea_coasts[8];  // bit per land region its shore touches
};
_Static_assert(sizeof(struct continent) == 0x1c, "continent size");

enum {  // game_rules bits
    RULES_TECH_STAGNATION = 0x20,
    RULES_INTENSE_RIVALRY = 0x40,
    RULES_VICTORY_COOPERATIVE = 0x1000,
    RULES_VICTORY_TRANSCENDENCE = 0x800,
    RULES_BLIND_RESEARCH = 0x200,
    RULES_NO_UNITY_SCATTERING = 0x2000,
};

enum { FACTION_FLAG_MAP_REVEALED = 0x200, FACTION_FLAG_MAINT_HALF = 0x20, FACTION_FLAG_ECO_SPARED = 0x4000,
       FACTION_FLAG_UNK_20000000 = 0x20000000 };  // faction_data.flags

enum { TREATY_UNK_8000000 = 0x8000000 };  // with TREATY_COMMLINK, whose_territory() names the owner

enum { GAME_STATE_DONE = 0x1, GAME_STATE_FINAL_SCORE_DONE = 0x8, GAME_STATE_OMNISCIENT = 0x80, GAME_STATE_RANKED = 0x200 /* set by rankings() late in the game */, GAME_STATE_VOLCANO = 0x20000, GAME_STATE_PERIHELION = 0x100000 };  // game_state bits
enum { FACTION_FLAG_BASE_SUBMERGED = 0x80000 };  // faction_data.flags

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
    MAP_RIVER = 0x80, MAP_RIVERBED = 0x100, MAP_RIVER_LAKE = 0x200,  // a river, where one may begin, more water shown
    MAP_FOREST = 0x200000, MAP_CONDENSER = 0x400000,
    MAP_BASE = 0x1, MAP_UNIT = 0x2, MAP_RESOURCE = 0x400, MAP_MONOLITH = 0x2000,
    MAP_ENERGY_RESOURCE = 0x10000, MAP_MINERAL_RESOURCE = 0x20000, MAP_POD_TAKEN = 0x100000,
    MAP_UNITY_POD = 0xc000000,  // either bit, without unity scattering
    MAP_SUPPLY_POD = 0x10000000, MAP_NUTRIENT_RESOURCE = 0x20000000, MAP_SENSOR = 0x80000000,
    MAP_FUNGUS = 0x20, MAP_UNK_4000 = 0x4000, MAP_THERMAL_BORE = 0x1000000, MAP_UNK_40000000 = 0x40000000,
    MAP_ROAD = 0x4, MAP_MINE = 0x10, MAP_SOLAR = 0x40, MAP_FARM = 0x8000, MAP_SOIL_ENRICHER = 0x80000,
    MAP_ECHELON_MIRROR = 0x800000,
    MAP_BASE_RADIUS = 0x1000,  // within a base's radius
};
enum {  // map_tile.bits2
    MAP2_CRATER = 0x1,
    MAP2_NEXUS = 0x2000,          // the Manifold Nexus
    MAP2_CHANGED = 0x400000,      // rainfall or rockiness set since the map was made
    MAP2_RIVER_PATH = 0x800000,   // world_rivers() marks the river it is laying
    MAP2_UNK_80000000 = 0x80000000,  // the square's landmark is switched off
    MAP2_LANDMARK_CODE = 0xff000000,  // which square of its landmark this is
};
// The landmarks, in alphax.txt's #NATURAL order; each marks its squares
// with bits2 bit 1 << id.
enum {
    LM_CRATER, LM_VOLCANO, LM_JUNGLE, LM_URANIUM, LM_SARGASSO, LM_RUINS, LM_DUNES, LM_FRESH,
    LM_MESA, LM_CANYON, LM_GEOTHERMAL, LM_RIDGE, LM_BOREHOLE, LM_NEXUS, LM_UNITY, LM_FOSSIL, LM_COUNT
};
#define MAP2_LANDMARK(id) (1u << (id))

struct rules_natural {
    char *name, *name_short;
};
enum {  // game_draw_state bits the map setters raise
    DRAW_TERRAIN = 0x1, DRAW_LANDMARKS = 0x4,
};

enum {  // map_tile.climate >> 5
    ALT_SHORE_LINE = 3,  // the lowest land
};

enum { LANDMARK_COUNT = 64 };

// alphax.txt's #WORLDBUILDER: how maps are made.
struct rules_worldbuilder {
    int32_t land_base, land_mod, continent_base, continent_mod, hills_base, hills_mod, plateau_base, plateau_mod;
    int32_t rivers_base, rivers_rain_mod, solar_energy, thermal_band, thermal_deviance, global_warming;
    int32_t sea_level_rises, cloudmass_peaks, cloudmass_hills, rainfall_coeff;
    int32_t deep_water, shelf, plains, beach, hills, peaks, fungus;
    int32_t cont_size_ratio[5], islands;
};
GAME_AT(struct rules_worldbuilder, solar_energy, 0x28);
GAME_AT(struct rules_worldbuilder, islands, 0x78);

struct landmark {
    int32_t x, y;
    char name[32];
};
_Static_assert(sizeof(struct landmark) == 0x28, "landmark size");

// ---- Bases ---------------------------------------------------------------------

enum { BASE_COUNT = 512 };

enum {
    FAC_HEADQUARTERS = 1, FAC_CHILDREN_CRECHE = 2, FAC_RECYCLING_TANKS = 3, FAC_RECREATION_COMMONS = 6,
    FAC_ENERGY_BANK = 7, FAC_NETWORK_NODE = 8, FAC_BIOLOGY_LAB = 9, FAC_HOLOGRAM_THEATRE = 11,
    FAC_PARADISE_GARDEN = 12, FAC_FUSION_LAB = 15, FAC_PUNISHMENT_SPHERE = 23, FAC_AEROSPACE_COMPLEX = 29,
    FAC_BROOD_PIT = 35,
    FAC_QUANTUM_LAB = 16, FAC_RESEARCH_HOSPITAL = 17, FAC_NANOHOSPITAL = 18, FAC_ROBOTIC_ASSEMBLY_PLANT = 19,
    FAC_NANOREPLICATOR = 20, FAC_QUANTUM_CONVERTER = 21, FAC_GENEJACK_FACTORY = 22, FAC_PRESSURE_DOME = 26,
    FAC_AQUAFARM = 36, FAC_SUBSEA_TRUNKLINE = 37, FAC_THERMOCLINE_TRANSDUCER = 38, FAC_GEOSYNC_SURVEY_POD = 41,
    // Built in orbit; the Space Elevator doubles minerals for them.
    FAC_SKY_HYDRO_LAB = 65, FAC_ORBITAL_DEFENSE_POD = 68, FAC_STOCKPILE_ENERGY = 69,
    FAC_FIRST_PROJECT = 70,
};
// Secret projects, as indices into game_secret_projects (facility id - 70).
enum {
    SP_HUMAN_GENOME_PROJECT = 0, SP_MERCHANT_EXCHANGE = 3, SP_EMPATH_GUILD = 4, SP_VIRTUAL_WORLD = 6, SP_PLANETARY_TRANSIT_SYSTEM = 7, SP_SUPERCOLLIDER = 12, SP_LONGEVITY_VACCINE = 14,
    SP_ASCETIC_VIRTUES = 13, SP_PHOLUS_MUTAGEN = 16, SP_THEORY_OF_EVERYTHING = 18, SP_NETWORK_BACKBONE = 21, SP_SPACE_ELEVATOR = 27, SP_SINGULARITY_INDUCTOR = 28,
    SP_BULK_MATTER_TRANSMITTER = 29, SP_VOICE_OF_PLANET = 31, SP_ASCENT_TO_TRANSCENDENCE = 32, SP_SELF_AWARE_COLONY = 25, SP_CLONING_VATS = 24,
    SP_CLINICAL_IMMORTALITY = 26, SP_MANIFOLD_HARMONICS = 33,
    SP_CLOUDBASE_ACADEMY = 35, SP_PLANETARY_ENERGY_GRID = 36, SP_COUNT = 64,
};

enum { BASE_STATE_DRONE_RIOT = 0x2, BASE_STATE_GOLDEN_AGE = 0x4, BASE_STATE_UNK_100 = 0x100, BASE_STATE_WORKS_OCEAN = 0x8000 };
enum {  // base.governor
    GOVERNOR_CITIZENS = 0x40,  // the governor places the workers
    GOVERNOR_EXPLORE = 0x1000000, GOVERNOR_DISCOVER = 0x2000000, GOVERNOR_BUILD = 0x4000000,
    GOVERNOR_CONQUER = 0x8000000, GOVERNOR_ACTIVE = 0x80000000,
};
enum {  // base.event: this turn's random events
    BASE_EVENT_BUMPER = 0x200, BASE_EVENT_FAMINE = 0x400,        // nutrients +1 / -1 a square
    BASE_EVENT_INDUSTRY = 0x800, BASE_EVENT_BUST = 0x1000,       // minerals
    BASE_EVENT_HEAT_WAVE = 0x2000, BASE_EVENT_CLOUD_COVER = 0x4000,  // energy
};
enum { RESOURCE_NUTRIENT, RESOURCE_MINERAL, RESOURCE_ENERGY, RESOURCE_UNUSED, RESOURCE_COUNT };

struct base {
    int16_t x, y;
    uint8_t faction;
    uint8_t faction_former;
    int8_t pop_size;
    uint8_t assimilation_turns, nerve_staple_turns, ai_plan;
    uint8_t visibility;  // bit per faction that can see it
    int8_t pop_seen[FACTION_COUNT];  // the size each faction last saw
    char name[25];
    int16_t terraform_x, terraform_y;
    uint32_t state;   // BASE_STATE_*
    uint32_t event;   // BASE_EVENT_*
    uint32_t governor;
    int32_t nutrients, minerals;  // stored
    int32_t production_last, eco_damage;
    int32_t queue_size;
    int32_t queue[10];
    uint32_t worked_tiles;  // bit per square of the radius table
    int32_t specialist_total, specialist_adjust;
    uint32_t specialist_types[2];
    uint8_t facilities_built[12];  // bit per facility, below the secret projects
    int32_t mineral_surplus_final, minerals_2;
    int32_t unused_a0[4];
    int32_t intake[RESOURCE_COUNT], intake_2[RESOURCE_COUNT], surplus[RESOURCE_COUNT];
    int32_t inefficiency[RESOURCE_COUNT], consumption[RESOURCE_COUNT];
    int32_t economy_total, psych_total, labs_total, unk_total;
    int16_t autoforward[3], pad_116;
    int32_t talent_total, drone_total, superdrone_total;
    int32_t random_event_turns, nerve_staple_count;
    int32_t unused_12c[2];
};
_Static_assert(sizeof(struct base) == 0x134, "base size");
GAME_AT(struct base, visibility, 0x0a);
GAME_AT(struct base, name, 0x13);
GAME_AT(struct base, state, 0x30);
GAME_AT(struct base, queue, 0x50);
GAME_AT(struct base, facilities_built, 0x8c);
GAME_AT(struct base, intake, 0xb0);
GAME_AT(struct base, economy_total, 0x100);
GAME_AT(struct base, talent_total, 0x118);

// What each kind of square yields (Thinker's CResourceInfo), from alphax.txt.
struct res_value {
    int32_t nutrient, mineral, energy, unused;
};
struct res_info {
    struct res_value ocean, base, bonus, forest, recycling, improved_land, improved_sea, monolith, borehole;
};
_Static_assert(sizeof(struct res_info) == 0x90, "res_info size");

// alphax.txt's #RULES, the part Chiron reads.
struct rules_basic {
    int32_t move_rate_roads, nutrient_intake_citizen, max_airdrop_range, artillery_max_range;
    int32_t artillery_damage_num, artillery_damage_den, nutrient_cost_multi, mineral_cost_multi;
    int32_t tech_discovery_rate, mine_road_limit, nutrient_effect_mine, min_base_size_specialists;
    int32_t drones_genejack, pop_limit_hab_complex, pop_limit_hab_dome;
    int32_t tech_improve_fungus, tech_ease_fungus_move, tech_road_fungus, tech_two_special;
    int32_t tech_three_nutrients, tech_three_minerals, tech_three_energy;
    int32_t extra_cost_prototype[3];  // percent more for a new design's first unit, by TRIAD_*
    int32_t psi_combat_ratio_atk[3], psi_combat_ratio_def[3];  // psi combat odds, by TRIAD_*
    GAME_PAD(0x7c, 0x80);
    // Combat percentages (battle_compute): a base's own defense, attacking
    // along a road, from higher ground, and the defense against an attack
    // from lower ground.
    int32_t combat_base_def, combat_atk_road, combat_atk_higher, combat_pen_atk_lower;
    int32_t tech_orbital_insert;  // lets air drops reach anywhere without the Space Elevator
    GAME_PAD(0x94, 0x9c);
    int32_t territory_max_dist;  // how far a base's territory reaches over land; half that at sea
    int32_t turns_corner_market;  // from cornering the energy market to the win
    int32_t tech_mining_platform_bonus;
    int32_t tech_economic_victory;  // needed to corner the energy market
    // More combat percentages: the penalty after an air drop, fanatics'
    // attack, land artillery against ships and ships' against land, artillery
    // per level of altitude, a mobile unit in the open, rough ground against
    // a mobile unit, trance against psi, empath song against psi, infantry
    // against a base, air superiority against ground units and other air
    // units, a non-combat unit's penalty, comm jammers, attacks on ships in
    // port, AAA tracking, sensors, and PLANET in a psi attack.
    int32_t combat_pen_airdrop, combat_fanatic, combat_art_vs_ship, combat_art_altitude;
    int32_t combat_mobile_open, combat_def_vs_mobile, combat_trance, combat_empath;
    int32_t combat_infantry_vs_base, combat_pen_air_vs_ground, combat_air_vs_air, combat_pen_noncombat;
    int32_t combat_comm_jammer, combat_vs_ship_port, combat_aaa, combat_sensor, combat_psi_planet;
    int32_t retool_strictness;  // which changes of production lose minerals (base_making)
    int32_t retool_penalty;     // the percentage lost, for humans
    int32_t retool_exemption;  // the most minerals a human's base carries over after building
    GAME_PAD(0xfc, 0x114);
    int32_t warming_num, warming_den;  // how fast ecological damage warms the planet
};
GAME_AT(struct rules_basic, warming_num, 0x114);
GAME_AT(struct rules_basic, retool_strictness, 0xf0);
GAME_AT(struct rules_basic, mine_road_limit, 0x24);
GAME_AT(struct rules_basic, tech_three_nutrients, 0x4c);
GAME_AT(struct rules_basic, tech_mining_platform_bonus, 0xa4);
GAME_AT(struct rules_basic, tech_economic_victory, 0xa8);
GAME_AT(struct rules_basic, territory_max_dist, 0x9c);
GAME_AT(struct rules_basic, extra_cost_prototype, 0x58);
GAME_AT(struct rules_basic, psi_combat_ratio_def, 0x70);
GAME_AT(struct rules_basic, tech_orbital_insert, 0x90);
GAME_AT(struct rules_basic, combat_base_def, 0x80);
GAME_AT(struct rules_basic, combat_pen_airdrop, 0xac);
GAME_AT(struct rules_basic, combat_psi_planet, 0xec);

enum { RULE_FLAG_WORM_POLICE = 0x40, RULE_FLAG_ALIEN = 0x80, RULE_FLAG_AQUATIC = 0x100, RULE_FLAG_INTEREST = 0x4000 };  // faction_def.rule_flags
enum { ECONOMICS_FREE_MARKET = 1, ECONOMICS_PLANNED = 2 };  // faction_data.se_models_pending[1]
enum { ABILITY_CLEAN_REACTOR = 0x10000, ABILITY_POLICE_2X = 0x400000 };  // unit_proto.abilities
enum { ORDER_SENTRY_BOARD = 1, ORDER_CONVOY = 3 };  // on board: waypoint_x[0] is the transport
enum {  // veh.state bits base_support() sets
    VEH_STATE_SUPPORTED = 0x10,         // costs its base minerals
    VEH_STATE_PACIFISM_DRONE = 0x200000, VEH_STATE_PACIFISM_FREE = 0x400000,
};

// A kind of specialist (Thinker's CCitizen), from alphax.txt.
struct citizen {
    char *name, *plural;
    int32_t preq_tech, obsol_tech;
    int32_t econ, psych, labs;  // what one adds to the base
};
_Static_assert(sizeof(struct citizen) == 0x1c, "citizen size");
enum { CITIZEN_KINDS = 7 };  // the ones base_energy() looks at

static inline int game_has_facility(const struct base *b, int facility) {
    return (b->facilities_built[facility / 8] & (1 << (facility % 8))) != 0;
}

// ---- Units ---------------------------------------------------------------------

enum { VEH_FLAG_UNK_20 = 0x20, VEH_FLAG_INVISIBLE = 0x400 };
enum { UNIT_FUNGAL_TOWER = 19 };

struct veh {
    int16_t x, y;
    uint32_t state;
    uint16_t flags;  // VEH_FLAG_*
    int16_t proto;
    int16_t unk_c;
    uint8_t faction;
    uint8_t lurking;  // turns left hidden: at 0 it shows again
    uint8_t damage;
    uint8_t order;  // ORDER_*
    uint8_t waypoint_count, patrol_point;
    int16_t waypoint_x[4];
    union {
        int16_t order_turns;  // turns left on sentry or hold, or 0 for no end
        int16_t waypoint_y0;  // the first waypoint's y, for a move
        int16_t waypoint_y[4];
    };
    uint8_t morale;
    uint8_t movement_turns;
    uint8_t order_auto_type;  // for a convoy, the RESOURCE_* it carries
    uint8_t visibility;  // bit per other faction that can see it
    uint8_t moves_spent;  // in the units veh_speed() counts in
    uint8_t unk_29, iter_count;
    uint8_t status_icon;  // a letter or digit: 'F' fortified, '1' given a goal this turn
    uint8_t unk_2c[2];
    int16_t home_base;
    int16_t next_in_stack, prev_in_stack;  // -1 at the ends
};
_Static_assert(sizeof(struct veh) == 0x34, "veh size");
GAME_AT(struct veh, damage, 0x10);
GAME_AT(struct veh, order, 0x11);
GAME_AT(struct veh, order_turns, 0x1c);
GAME_AT(struct veh, moves_spent, 0x28);
GAME_AT(struct veh, iter_count, 0x2a);
GAME_AT(struct veh, visibility, 0x27);
GAME_AT(struct veh, home_base, 0x2e);
GAME_AT(struct veh, waypoint_x, 0x14);
GAME_AT(struct veh, movement_turns, 0x25);
GAME_AT(struct veh, status_icon, 0x2b);

enum { RADIUS_SQUARES = 289, BASE_RADIUS_SQUARES = 21 };  // BASE_RADIUS_SQUARES: a base's own radius

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
extern struct veh *game_vehs;  // [VEH_COUNT]
enum { VEH_COUNT = 2048 };  // and one more slot, for veh_fake()
// Where veh_lift() last took a unit from, and the unit (-1 once dropped);
// veh_at() sets the flag once it has rebuilt the map's unit bits in a
// network game.
extern int32_t *game_veh_lift_x, *game_veh_lift_y, *game_veh_lifted, *game_veh_at_rebuilt;
extern int32_t *game_veh_find_dist;  // veh_find()'s distance to the unit it found
extern int (*game_contribution)(int veh, int terraform);  // a former's work a turn on a terraforming order
// Unit numbers the game keeps outside the unit table, which veh_kill()
// renumbers: two of the map window's, two others, the unit being moved
// (0 also for none) and, in a network game, a pointer to one more.
extern int32_t *game_veh_ref_map_1, *game_veh_ref_map_2, *game_veh_ref_3, *game_veh_ref_4, *game_current_veh;
extern int32_t **game_net_veh_ref;
extern void *game_deletion_list;  // network games: units deleted, to tell the other players
extern void(__attribute__((thiscall)) * game_deletion_add)(void *list, int veh);
extern void(__attribute__((thiscall)) * game_base_win_check_base)(void *win, int base);  // redraws if it shows the base
extern void(__attribute__((thiscall)) * game_base_win_check_loc)(void *win, int x, int y, int unk);
extern void (*game_spot_all)(int veh, int unk);  // updates who can see the unit and what it sees
extern int32_t *game_veh_count;
extern uint32_t *game_state;                  // GAME_STATE_*
extern int32_t *game_net_multiplayer;         // non-zero in a network game
extern uint32_t *game_draw_state;             // what the map view must redraw
extern int32_t *game_base_find_dist;          // set by the game's base_find()
extern struct rules_worldbuilder *game_worldbuilder;
extern int32_t *game_map_area;                // squares stored: half width times height
extern int32_t *game_abstract_area;           // bytes of the abstract map
extern int32_t *game_sea_level_council;       // sea level change voted in council
extern int32_t *game_land_coverage, *game_planet_orbit;  // the map settings chosen
extern int32_t *game_alt_natural_default;     // [11] game_alt_natural as the map is made
extern int32_t *game_base_submerged_count;    // [8] world_alt_set() resets these
extern int32_t *game_base_submerged_veh;      // [8]
extern int32_t *game_world_skip_territory;    // non-zero: alt_set() leaves bases and units alone
extern int32_t *game_ocean_coverage;          // the map setting: 2 - land coverage
// The map settings: planet size, ocean coverage, land coverage, erosive
// forces, orbit, cloud cover and native life, each 0 to 2 or negative for
// random.
extern int32_t *game_map_settings;            // [7]
extern int32_t *game_erosive_forces, *game_native_life;
extern int32_t *game_world_add_temperature;   // non-zero while a map is made: shown as it grows
extern int32_t *game_halted;                  // non-zero: don't redraw
extern int32_t *game_current_faction;         // the faction the player is
extern int32_t *game_control_turn;
extern int32_t *game_battle_state;            // non-zero: no messages now
// A battle's report: the modifiers listed for each side (count, then up to 4
// values and 80-byte labels each, attacker first), the defender's terrain
// label, and both sides' morale as get_basic_offense and _defense found it.
extern int32_t *game_battle_mod_count;        // [2], followed by game_battle_state and one more flag
extern int32_t *game_battle_mod;              // [2 * 4]
extern char (*game_battle_mod_label)[80];     // [2 * 4]
extern char **game_battle_terrain;
extern int32_t *game_battle_morale;           // [2]
extern char *game_battle_text;                // a label battle_compute puts together
extern uint32_t *game_fungus_clears;          // map_tile.bits fungus removes
// world_rainfall(): a base near a square whose rainfall changed, and the
// faction of a base near one that got drier; -1 for none.
extern int32_t *game_rain_base, *game_rain_faction;
extern void **game_map_wins;                  // [8] the map windows
extern void *game_world_win, *game_console;
extern char **game_script_file;               // script.txt, for popups
extern char *game_flood_texts;                // [8][16] popup labels and pictures when a base floods
// base_growth()'s popup labels and pictures, from GROWTH_TEXTS in the
// image; game_growth_text(addr) is the one at addr.
enum { GROWTH_TEXTS = 0x6894a0 };
extern char *game_growth_texts;
static inline char *game_growth_text(uint32_t addr) { return game_growth_texts + (addr - GROWTH_TEXTS); }
extern char *game_text_climate_begin, *game_text_climate_end;
// The four diagonal neighbours, north-east clockwise, then the square.
extern int32_t *game_diag_x, *game_diag_y;    // [5]
// Brush counts while land is painted: squares since the world was begun,
// the land wanted before continents shrink, and squares this stroke.
extern int32_t *game_brush_total, *game_brush_land_wanted, *game_brush_stroke;
extern struct rules_natural *game_natural;    // [LM_COUNT]
extern int32_t *game_mount_planet_x, *game_mount_planet_y;  // where the volcano was put
extern int32_t *game_canyon_squares;          // [12] radius-table indices the canyon covers
extern int32_t *game_ridge_squares;           // [13] and the ridge; the last lies past the table
// terranx.exe's writable memory: all of its game state but the heap.
extern uint8_t *game_data;
extern size_t game_data_size;
// Square offsets: the 8 neighbours clockwise from north-east, then the
// square itself; and every square within each radius, nearest first, with
// game_radius_count[r] the number within radius r.
extern int32_t *game_radius_near_x, *game_radius_near_y;  // [9]
extern int32_t *game_adjacent_x, *game_adjacent_y;        // [4] the squares sharing a side
extern int32_t *game_radius_x, *game_radius_y;            // [RADIUS_SQUARES]
extern int32_t *game_radius_count;                        // [9]

extern struct res_info *game_res_info;
extern struct rules_basic *game_rules_basic;
extern int32_t *game_secret_projects;   // [SP_COUNT] the base holding each, or negative
extern int32_t *game_governor_faction;  // the Planetary Governor, or 0
extern int32_t *game_dust_cloud;        // turns left of a dust cloud: energy -1 a square
extern int32_t *game_solar_flares;      // bits: energy tripled
extern int32_t *game_control_upkeep_a;  // non-zero: game_solar_flares bit 1 counts, not bit 0
extern struct base **game_current_base; // the base being worked on, or NULL
// Output the 2-a-square limit took off, summed over yield calls; and a base
// square's energy.
extern int32_t *game_terraform_reduce, *game_terraform_energy;
// What base_compute() works out for the current base, besides the base
// itself: its growth rating, resources sent by supply convoys from and to
// it, and the minerals its units cost.
extern int32_t *game_current_base_id;
extern int32_t *game_computed_base_id;  // the base base_compute() last worked out
extern int32_t *game_faction_rankings;  // [8] factions from weakest to strongest
extern int32_t *game_cost_base;         // [6] the AI's cost factor at each difficulty, in tenths
extern int (*game_great_satan)(int faction, int is_aggressive);  // whether the faction is far ahead
extern int32_t *game_base_growth;
extern int32_t *game_base_convoy_from, *game_base_convoy_to;  // [RESOURCE_COUNT]
extern int32_t *game_base_forces_upkeep;
extern int32_t *game_base_upkeep_state;  // 1 while bases are worked through at the end of a turn
extern int32_t *game_tectonic_detonations;  // [FACTION_COUNT]
// Energy the current base gains from and gives to each faction by trade.
extern int32_t *game_base_commerce_import, *game_base_commerce_export;  // [FACTION_COUNT]
extern struct citizen *game_citizens;
extern int32_t *game_tech_commerce_count;  // techs that raise commerce
// How often each Planetary Council proposal has passed.
extern int32_t *game_proposal_passes;
enum { PROPOSAL_GLOBAL_TRADE_PACT = 3, PROPOSAL_REPEAL_GLOBAL_TRADE_PACT = 4 };
// base_psych()'s talents, drones and superdrones after each step: before
// anything, psych energy, facilities, police, projects.
extern int32_t *game_psych_talents, *game_psych_drones, *game_psych_superdrones;  // [5]
extern int32_t *game_base_pacifism;  // military units away from home, as POLICE counts them
// base_yield(): citizens left to place, squares other bases or units have
// taken (farm_compute() works them out), and whether a square was weighed
// with food to spare.
extern int32_t *game_base_workers, *game_base_reserved, *game_base_food_ok;
// Why farm_compute() keeps each radius square from the base: SQUARE_*.
extern int32_t *game_base_square_flags;  // [25]
enum {
    SQUARE_OUTSIDE = 0x1,   // past the base's 21 squares
    SQUARE_UNSEEN = 0x2,    // off the map, or not yet seen by the faction
    SQUARE_BASE = 0x4, SQUARE_HOSTILE = 0x8,
    SQUARE_NEIGHBOUR = 0x10,  // worked by a nearby base
    SQUARE_FOREIGN = 0x20,    // another faction's territory
};
extern int32_t *game_current_turn;
// Units the current base supports, and how many of them cost minerals.
extern int32_t *game_base_forces_supported, *game_base_forces_paid;
// The base-compute globals from game_base_convoy_from to game_current_base.
enum { BASE_GLOBALS_START = 0x90e8fc, BASE_GLOBALS_END = 0x90ea38 };
extern uint8_t *game_base_globals;

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
extern int (*game_rand)(void);                    // the C runtime's rand()
extern void *(*game_getptd)(void);                // the C runtime's per-thread data
extern int (*game_random)(int min, int max);      // the game's own generator: min to max - 1
extern void (*game_random_reseed)(int seed);
extern void (*game_do_all_non_input)(void);       // redraws and the message pump
extern int (*game_best_specialist)(void);         // for the current base
extern int (*game_has_abil)(int proto, uint32_t ability);
extern void (*game_add_goal)(int faction, int type, int priority, int x, int y, int base);
extern int (*game_stack_check)(int veh, int type, int cond1, int cond2, int cond3);
extern void(__attribute__((thiscall)) * game_path_continents)(void *path);  // finds the regions
extern void (*game_draw_radius)(int x, int y, int radius, int unk);           // redraws squares
extern int (*game_base_find2)(int x, int y, int faction);  // the nearest base of the faction, or -1
extern void (*game_delete_landmark)(int x, int y);
extern void (*game_set_fac)(int facility, int base, int on);
extern int (*game_is_objective)(int base);
extern int(__attribute__((thiscall)) * game_console_focus)(void *console, int x, int y, int faction);
extern int (*game_parse_says)(int slot, char *text, int gender, int plural);
extern int (*game_parse_say)(int slot, int text, int gender, int plural);
extern int (*game_parse_num)(int slot, int value);  // text from the string table
extern int (*game_popb)(char *label, int flags, int sound, char *picture, void *sprite);
extern int (*game_eliminate_player)(int faction, int by);
extern int (*game_veh_avail)(int proto, int faction, int base);  // whether the base may build the unit
extern int (*game_facility_avail)(int facility, int faction, int base, int queue);
extern void (*game_wave_it)(int sound);
// For the end of a base's turn.
extern int (*game_best_reactor)(int faction);
extern void (*game_tech_research)(int faction, int labs);
extern int (*game_kill)(int veh);                    // disbands a unit
extern int (*game_at_goal)(int faction, int type, int x, int y);
extern int (*game_planet_buster2)(int proto);
extern int (*game_proto_cost)(int chassis, int weapon, int armor, int abilities, int reactor);
extern int (*game_at_climax)(int faction);
extern int32_t *game_sunspot_duration;
extern int32_t *game_ranked_faction;  // [2]: factions base_hurry() and social_ai() compare themselves with
// For base_production().
extern void (*game_base_reset)(int base, int unk);       // the AI (or governor) picks what to build next
extern int (*game_ascending)(int faction);                 // may it build the Ascent to Transcendence
extern int (*game_veh_cost)(int proto, int base, int *unk);
extern void (*game_prune_protos)(int faction, int proto, int unk);
extern int (*game_morale_mod)(int base, int faction, int triad);
extern void (*game_upgrade_prototypes)(int faction, int proto);
extern void (*game_set_dirty)(void);
extern void (*game_interlude)(int id, char *text, int unk1, int unk2);
extern int (*game_generators)(int faction, int *count);
extern void (*game_bit_set)(int x, int y, uint32_t bit, int on);
extern void (*game_synch_bit)(int x, int y, int faction);
extern int (*game_whose_territory)(int faction, int x, int y, int unk1, int unk2);
extern int (*game_game_year)(int turn);
extern void (*game_tech_advance)(int faction);
extern void (*game_amovie_project)(int project);
extern void (*game_amovie_project_2)(char *name);
extern void (*game_help_project)(int project);
// The tutorial's and the AI's notes on what was built.
extern void (*game_mon_prototype_built)(int faction), (*game_mon_native_life_bred)(int faction);
extern void (*game_mon_naval_unit_built)(int faction), (*game_mon_air_unit_built)(int faction);
extern void (*game_mon_first_in_space)(int faction), (*game_mon_facility_built)(int faction, char *base_name);
extern void (*game_mon_built_preserve)(int faction), (*game_mon_secret_project)(int faction, int project);
extern void (*game_mon_winning_trans)(int faction, int other);
// The popup object base_production() keeps on its stack, and its methods.
enum { BASE_POP_SIZE = 0x5430 - 0xb4 };
extern void(__attribute__((thiscall)) * game_popup_ctor)(void *pop);
extern void(__attribute__((thiscall)) * game_popup_close)(void *pop);
extern void(__attribute__((thiscall)) * game_scroll_close)(void *scroll);
extern void(__attribute__((thiscall)) * game_flat_button_dtor)(void *button);
extern void(__attribute__((thiscall)) * game_graphic_win_dtor)(void *win);
extern void(__attribute__((thiscall)) * game_base_pop_dtor)(void *pop);
extern void(__attribute__((thiscall)) * game_base_pop_init)(void *pop, int unk1, int unk2);
extern void(__attribute__((thiscall)) * game_base_pop_set_width)(void *pop, int width);
extern void(__attribute__((thiscall)) * game_base_pop_string)(void *pop, char *text);
extern int(__attribute__((thiscall)) * game_popup_start)(void *pop, char *file, char *label, int a, int b, int c, int d);
extern int(__attribute__((thiscall)) * game_base_pop_exec_3)(void *pop, int a, int b);
extern int(__attribute__((thiscall)) * game_sprite_init)(void *sprite, char *file, int w, int h);
extern int (*game_pcx_dimensions)(char *file, int *w, int *h);
extern void(__attribute__((thiscall)) * game_graphic_win_redraw)(void *win);
extern void *game_wave;  // the voice-over player
extern void(__attribute__((thiscall)) * game_wave_unload)(void *wave);
extern void(__attribute__((thiscall)) * game_wave_load)(void *wave, char *file);
extern void(__attribute__((thiscall)) * game_wave_play)(void *wave);
extern void(__attribute__((thiscall)) * game_sound_fade)(void *wave, int ms);
extern uint32_t(__attribute__((stdcall)) * *game_char_upper)(char *s);  // CharUpperA, as imported
// Its globals.
extern char *game_str_buffer;            // where messages are put together
extern char *game_pop_file;              // [] the file a popup's text is read from
extern char *game_upper_buffer;          // a name upper-cased for a message
extern int32_t *game_preferences, *game_more_preferences, *game_warnings, *game_interlude_state;
extern int32_t *game_victory_type, *game_screen_width, *game_upkeep_flag;
extern int32_t *game_base_build_blocked, *game_base_proto_upkeep;  // 0x90ea40, 0x90ea3c
extern int32_t *game_default_gender, *game_default_plural;     // for parse_says with -1
extern int32_t *game_ascent_numbers;     // [2] shown with the subspace generators
extern uint8_t *game_factions_alive;     // bit per faction still in the game
extern uint32_t *game_vop_clear_bits;    // map bits the Voice of Planet clears where it puts fungus
// The game's own address `addr` as loaded, and as a pointer.
uint32_t game_va(uint32_t addr);
void *game_at(uint32_t addr);
extern uint8_t *game_test_image;
extern int (*game_popp)(char *file, char *label, int flags, char *picture, int unk);
extern int (*game_x_pop2)(char *label, char *picture, int unk);
extern void (*game_base_kill)(int base);
extern int (*game_stack_fix)(int veh);
extern int (*game_veh_kill)(int veh);
extern int (*game_veh_init)(int unit, int faction, int x, int y);
extern void(__attribute__((thiscall)) * game_map_win_clear_terrain)(void *win);
extern void(__attribute__((thiscall)) * game_world_win_clear_terrain)(void *win);
extern void (*game_draw_map)(int unk);
extern void (*game_draw_tile)(int x, int y, int unk);
extern void (*game_do_checksums)(int stage);
extern void (*game_log_say)(char *text, int a, int b, int c);
extern void (*game_reset_territory)(void);
extern int (*game_world_site)(int x, int y, int unk);  // how good a place for a base, 0 to 15
// The AI's sites (goals of one type at a square): add one, or find one.
extern void (*game_add_site)(int faction, int type, int priority, int x, int y);
extern int (*game_at_site)(int faction, int type, int x, int y);
extern void (*game_fixup_landmarks)(void);
extern uint32_t(__attribute__((stdcall)) * game_time_get_time)(void);         // the game's timeGetTime import
extern int16_t(__attribute__((stdcall)) * game_get_async_key_state)(int key);  // and GetAsyncKeyState
// For a base that changes hands: the replay's record of it, what its old
// owner knows of it, and a prototype like one of the old owner's for the new.
extern void (*game_replay_base)(int event, int x, int y, int faction);
extern void (*game_spot_base)(int base, int faction);
extern int (*game_propose_proto)(int faction, int chassis, int weapon, int armor, uint32_t abilities, int reactor,
                                 int plan, char *name);
extern void(__attribute__((thiscall)) * game_map_win_set_center)(void *win, int x, int y, int unk);
// The base window, and a base it is redrawn for after the base's upkeep
// (Thinker's BaseUpkeepDrawID); its redraw, and opening it on a base.
extern void *game_base_win;
extern int32_t *game_base_win_redraw_base;
extern void(__attribute__((thiscall)) * game_base_win_on_redraw)(void *win);
extern void(__attribute__((thiscall)) * game_base_win_zoom)(void *win, int base, int unk);
extern void (*game_draw_tiles)(int x, int y, int unk);
// A faction's turn: its reserves at the start, whether the New Tech
// windows are skipped, and the debug log's five-part line.
extern int32_t *game_turn_start_credits;
extern int32_t *game_skip_tech_screen;
extern int32_t *game_skip_project_news;  // non-zero keeps base_change() from telling of projects begun
extern void (*game_help_topic)(int kind, int id);
extern void (*game_log_say5)(char *a, char *b, int n1, int n2, int n3);
// The faction's SOCIAL screen.
extern void (*game_social_select)(int faction);
// The social engineering table: each category's models (0x2c bytes, their
// SE_COUNT effects), 0xd4 bytes a category, from the first category's
// first model's effects.
enum { SOCIAL_CATEGORIES = 4, SOCIAL_MODELS = 4, SOCIAL_CATEGORY_SIZE = 0xd4, SOCIAL_MODEL_SIZE = 0x2c };
extern uint8_t *game_social_effects;
// Each model's prerequisite tech, at [category * SOCIAL_PREQ_STRIDE + model].
enum { SOCIAL_PREQ_STRIDE = SOCIAL_CATEGORY_SIZE / 4 };
extern int32_t *game_social_preqs;
// For social_ai(): whether a faction is close to a diplomatic victory
// (the faction, or 0), its social choice put into effect, and the AI
// redesigning its units.
extern int (*game_aah_ooga)(int faction, int pact_faction);
extern void (*game_social_set)(int faction);
extern void (*game_consider_designs)(int faction);
// Repairs: whether a unit carries others, and whether a base breeds native
// life for its faction.
extern int (*game_veh_cargo)(int veh);
extern int (*game_breed_mod)(int base, int faction);
extern void (*game_do_all_draws)(void);
// The rest of a faction's turn: the AI's diplomacy, strategy and social
// engineering, the Planetary Council, and the autosave.
extern void (*game_enemy_diplomacy)(int faction);
extern void (*game_enemy_strategy)(int faction);
// The AI's diplomacy (Thinker's names). The first faction acts towards the
// second unless noted.
extern void (*game_cause_friction)(int faction, int other, int friction);
extern void (*game_treaty_on)(int faction, int other, uint32_t status);  // sets TREATY_* bits both ways
extern int (*game_reputation)(int faction, int other);
extern void (*game_diplomacy_check)(int faction, int other, int unk);  // sets the game_diplo_* moods
extern void (*game_communicate)(int faction, int other, int unk);       // talks to the player
extern void (*game_enemies_trade_tech)(int faction, int other);
extern void (*game_encounter)(int faction, int other, int x, int y, int fungal_tower, int native);
// How far the second faction has pressed into the first's territory, with
// one of the bases concerned and a count.
extern int (*game_territory)(int faction, int other, int flags, int *base, int *count);
extern void (*game_action_sat_attack)(int faction, int target, int kind, int base);
// The AI's strategy (Thinker's names).
extern int (*game_garrison_check)(int base);          // the defenders a base wants
extern int (*game_weap_val)(int proto, int faction);  // a design's weapon, as the AI rates it
extern int (*game_arm_val)(int armor, int faction);  // the game's callers pass a prototype
extern int (*game_weap_strat)(int weapon, int faction);
extern int (*game_arm_strat)(int armor, int faction);
extern int (*game_planet_buster)(int veh);            // non-zero for a planet buster
extern void (*game_veh_sleep)(int veh);                // sentries it
extern int (*game_morale_veh)(int veh, int unk1, int unk2);
extern int (*game_want_monolith)(int veh);
extern void (*game_bases_reset)(int region, int faction, int same_plan);  // rethinks the region's builds
extern int (*game_terraform_cost)(int x, int y, int faction);
extern int (*game_contiguous)(int faction, int other, int region, int by_sea);
extern void (*game_enemy_capabilities)(int faction);
extern int(__attribute__((thiscall)) * game_console_upgrade)(void *console, int veh);  // 0 when it can't
extern void (*game_del_site)(int faction, int type, int x, int y, int unk);
extern void (*game_wipe_goals)(int faction);
// The AI's choice of what a base builds.
extern int (*game_speed_proto)(int proto);              // moves a turn, in thirds
extern int (*game_worm_mod)(int base, int faction);     // morale against native life
extern int (*game_base_lose_minerals)(int base, int item);  // minerals a change of build would lose
extern int32_t *game_gov_claims, *game_gov_claim_scores;
extern int32_t *game_tech_unk_9a5e9c;  // a tech the AI checks rivals for before building probe teams (use unknown)  // [4] base_reset()'s choice for each governor priority, and its score
extern int32_t *game_dome_depth_a, *game_dome_depth_b;
extern const char *game_name_believers, *game_name_peacekeepers, *game_name_caretakers;  // faction file names the AI singles out  // depths below sea level a pressure dome is wanted at (unconfirmed)
extern int32_t *game_pbem;                 // non-zero in a hotseat or play-by-email game
extern int32_t *game_diplo_state;          // [FACTION_COUNT] what the AI will raise with the player
extern int32_t (*game_diplo_state_with)[FACTION_COUNT];  // [player][faction] the same, kept per player
extern int32_t *game_diplo_attack_chance;  // diplomacy_check's: the rival's openings to attack
extern int32_t *game_diplo_hostility;      // and its hostility, 0 to 4
extern int32_t *game_diplo_friction;
extern int (*game_social_ai)(int faction, int a, int b, int c, int d, int e);
extern int (*game_can_call_council)(int faction, int unk);
extern void (*game_call_council)(int faction);
extern void (*game_auto_save)(void);

// Ecological damage: fungus, native life, warming and volcanoes.
struct rules_terraform {
    char *name, *name_sea;
    int32_t preq_tech, preq_tech_sea;
    uint32_t bit;  // the map_tile.bits it makes
    GAME_PAD(0x14, 0x20);
};
_Static_assert(sizeof(struct rules_terraform) == 0x20, "rules_terraform size");
enum { TERRAFORM_COUNT = 19 };
extern struct rules_terraform *game_terraform;  // [TERRAFORM_COUNT]
extern int32_t *game_climate_level;            // warming from ecological damage, towards the next sea rise
extern int32_t *game_climate_change;           // the sea's coming change, from warming
extern int32_t *game_climate_shown;            // [3] what the planet monitor shows of it
extern void (*game_clock_wait)(int ms);        // waits, drawing
extern void (*game_boom)(int x, int y, int flags);  // an explosion on the map
extern int (*game_pop2)(char *label, char *picture, int base);  // a message with a picture

// The game's reader of its text files: a section opened by label (0 when
// found), its lines one by one into *game_text_line, and the next item of a
// line.
extern int (*game_text_open)(char *file, char *label);
extern void (*game_text_get)(void);
extern char *(*game_text_item)(void);
extern void (*game_text_close)(void);
extern char **game_text_line;

// The AI's terraforming.
struct rules_ability {
    char *name, *description, *abbreviation;
    int32_t cost, unk_10;
    uint32_t flags;
    int16_t preq_tech, pad;
};
_Static_assert(sizeof(struct rules_ability) == 0x1c, "rules_ability size");
extern struct rules_ability *game_abilities;
extern int (*game_abil_index)(uint32_t flag);
extern int (*game_can_terraform)(int faction, int x, int y, int unk, int base);
extern int (*game_terrain_avail)(int terraform, int sea, int faction);
extern void (*game_base_connect)(int level);  // roads to the faction's other bases
extern void *game_path;

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

// rand()'s state, in the calling thread's C runtime data.
static inline uint32_t *game_rand_state(void) { return (uint32_t *)((uint8_t *)game_getptd() + 0x14); }

// The game's rnd(n): 0 to n - 1, or 0 for n below 2.
static inline int game_rnd(int n) { return n - 1 > 0 ? game_rand() % n : 0; }

// A label from labels.txt, by index.
static inline char *game_label(int i) { return game_strings_get(game_strings, (*game_labels)[i]); }
