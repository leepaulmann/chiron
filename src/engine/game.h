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
    TREATY_VENDETTA = 0x10, TREATY_WANT_REVENGE = 0x20, TREATY_UNK_40 = 0x40, TREATY_UNK_80 = 0x80,
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
enum { GOAL_COUNT = 75, SITE_COUNT = 25 };
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
    int32_t diplo_reputation;     // treaties made, less those broken (never below 0)
    int32_t diplo_gifts[FACTION_COUNT];  // gifts and bribes it has given each faction
    int32_t diplo_wrongs[FACTION_COUNT];    // times it double-crossed each faction
    int32_t diplo_betrayed[FACTION_COUNT];  // times each faction double-crossed it
    int32_t diplo_combat_total[FACTION_COUNT];  // its score in all its wars with each
    int32_t diplo_combat_count[FACTION_COUNT];  // its score in the current war with each, reset at a vendetta
    uint32_t maps_traded;  // bit per faction whose maps it has had in a trade, and those they had
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
    uint8_t tech_source[0x60];  // per tech: the faction it came from by trade or theft, or 0
    int32_t tech_research_id;  // the tech being researched, or negative
    int32_t tech_cost;  // research cost, or negative until computed
    int32_t earned_techs_saved;
    int32_t net_random_event;  // base_research() keeps the hundredths of a lab left over here
    int32_t ai_fight, ai_growth, ai_tech, ai_wealth, ai_power;  // the AI's priorities
    int32_t target_x, target_y;  // the AI's, or -1
    int32_t best_mineral_output;  // the most a base of the faction has made, its surplus counted twice
    int32_t council_call_turn;
    // Votes promised in the Planetary Council (buy_council_vote): the
    // answer on each proposal, or 0, and for the governor the candidate.
    int32_t council_pledge[11], council_pledge_to[11];
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
    int32_t enemy_best_land_speed, enemy_best_psi_offense, enemy_best_psi_defense;
    // Counted by enemy_strategy among the units of factions it has a
    // commlink with and isn't at peace with: transports, the reactors of
    // naval superiority ships, air attackers that can pierce its best
    // armor, and missiles other than planet busters.
    int32_t enemy_transports, enemy_naval_reactors, enemy_air_attackers, unk_84c, enemy_missiles;
    GAME_PAD(0x854, 0x858);
    uint8_t units_active[512], units_queue[512];  // per prototype (PROTO_COUNT): its units, and those being built
    int16_t units_lost[512];  // per prototype: its units killed in combat
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
    struct goal sites[SITE_COUNT];  // places the AI means to settle (type 3) or otherwise act on
    GAME_PAD(0x203c, 0x2050);
    int32_t techs_achieved;  // techs it has gained (tech_achieved counts them)
    GAME_PAD(0x2054, 0x205c);
    // News of the secret projects other factions have begun: bitmask()'s
    // byte index and bit, so 8 bits a word.
    uint32_t project_intel[8];
    int32_t corner_market_turn, corner_market_cost;  // when the energy market is cornered, and what it cost
    int32_t objective_bonus;  // objectives a scenario credits it with, besides those num_objectives() counts
    GAME_PAD(0x2088, 0x208c);
    int32_t bung_next;  // the next slot of the faction's game_bunglist to fill, 0 to 63
    uint32_t flags_ext;  // FACTION_FLAG_EXT_*
    int32_t air_offense, air_superiority;  // air units of each plan
    int32_t score;  // compute_score()'s last, doubled in an ironman game
    int32_t unk_20a0;
    int32_t score_total;     // compute_score()'s default places for its total
    int32_t score_parts[9];  // and the parts of it
};
_Static_assert(sizeof(struct faction_data) == 0x20cc, "faction_data size");
GAME_AT(struct faction_data, diplo_status, 0x18);
GAME_AT(struct faction_data, diplo_merc, 0x98);
GAME_AT(struct faction_data, diplo_patience, 0xb8);
GAME_AT(struct faction_data, integrity_blemishes, 0x124);
GAME_AT(struct faction_data, diplo_gifts, 0x12c);
GAME_AT(struct faction_data, maps_traded, 0x1cc);
GAME_AT(struct faction_data, diplo_combat_count, 0x1ac);
GAME_AT(struct faction_data, odp_deployed, 0x364);
GAME_AT(struct faction_data, tech_source, 0x36c);
GAME_AT(struct faction_data, techs_achieved, 0x2050);
GAME_AT(struct faction_data, target_x, 0x3f0);
GAME_AT(struct faction_data, mil_strength, 0x1060);
GAME_AT(struct faction_data, flags_ext, 0x2090);
GAME_AT(struct faction_data, diplo_stolen_techs, 0x200);
GAME_AT(struct faction_data, best_armor_value, 0x824);
GAME_AT(struct faction_data, sites, 0x1eac);
GAME_AT(struct faction_data, enemy_best_armor_value, 0x830);
GAME_AT(struct faction_data, enemy_best_psi_defense, 0x83c);
GAME_AT(struct faction_data, enemy_missiles, 0x850);
GAME_AT(struct faction_data, mil_attack, 0x1064);
GAME_AT(struct faction_data, target_kinds, 0x1078);
GAME_AT(struct faction_data, region_force_rating, 0x127c);
GAME_AT(struct faction_data, goals, 0x19fc);
GAME_AT(struct faction_data, air_superiority, 0x2098);
GAME_AT(struct faction_data, objective_bonus, 0x2084);
GAME_AT(struct faction_data, score, 0x209c);
GAME_AT(struct faction_data, score_parts, 0x20a8);
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
GAME_AT(struct faction_data, council_pledge, 0x400);
GAME_AT(struct faction_data, council_pledge_to, 0x42c);
GAME_AT(struct faction_data, facility_announced, 0x458);
GAME_AT(struct faction_data, atrocities, 0x1d4);

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
    char adj_leader[128], adj_insult_leader[128];  // the leader's name as an adjective, and the insulting one
    char adj_faction[128], adj_insult_faction[128];  // the same for the faction
    GAME_PAD(0x264, 0x2e4);
    char noun[24];
    int32_t noun_gender;
    int32_t noun_plural;
    char adjective[128];  // the name as an adjective, such as "Gaian"
    char formal_name[40], insult_leader[24], desc_name[24];  // formal name; the noun for insulting the leader
    char assistant_name[24], scientist_name[24];
    char assistant_city[24];  // the name the Assistant Worm's killer's home takes, captured by the player
    GAME_PAD(0x424, 0x4d4);
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
GAME_AT(struct faction_def, adj_insult_faction, 0x1e4);
GAME_AT(struct faction_def, insult_leader, 0x3ac);
GAME_AT(struct faction_def, assistant_name, 0x3dc);
GAME_AT(struct faction_def, assistant_city, 0x40c);
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
GAME_AT(struct faction_def, rule_psi, 0x4fc);
GAME_AT(struct faction_def, anti_ideology_model, 0x590);

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
    uint8_t combat_factions;    // bit per faction that has met it in combat (all for a basic design)
    uint8_t icon;               // 0xff until one is chosen
    uint8_t unk_2f;
    uint16_t flags;  // PROTO_FLAG_*
    int16_t preq_tech;
};
enum { PROTO_FLAG_ACTIVE = 0x1, PROTO_FLAG_BUILT = 0x4, PROTO_FLAG_SEEN_ONLINE = 0x100 };
_Static_assert(sizeof(struct unit_proto) == 0x34, "unit_proto size");
GAME_AT(struct unit_proto, chassis, 0x24);
GAME_AT(struct unit_proto, combat_factions, 0x2d);
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
    // Its names in designs (name_proto): offensive and defensive, each in
    // two forms and a third for large designs.
    char *offsv1_name, *offsv2_name, *offsv_name_lrg, *defsv1_name, *defsv2_name, *defsv_name_lrg;
    GAME_PAD(0x18, 0x48);
    uint8_t speed;
    uint8_t triad;  // TRIAD_*
    uint8_t range;  // turns aloft for air units, else 0
    uint8_t cargo;  // units a transport of it carries, before the reactor
    uint8_t cost;   // its part of a design's cost (proto_cost)
    uint8_t missile;
    GAME_PAD(0x4e, 0x8e);
    int16_t preq_tech;
};
_Static_assert(sizeof(struct rules_chassis) == 0x90, "rules_chassis size");
GAME_AT(struct rules_chassis, cargo, 0x4b);
GAME_AT(struct rules_chassis, missile, 0x4d);

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

enum { GAME_STATE_DONE = 0x1, GAME_STATE_FINAL_SCORE_DONE = 0x8, GAME_STATE_OMNISCIENT = 0x80, GAME_STATE_RANKED = 0x200 /* set by rankings() late in the game */, GAME_STATE_DEBUG = 0x1000, GAME_STATE_VOLCANO = 0x20000, GAME_STATE_PERIHELION = 0x100000 };  // game_state bits
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
    int32_t min_turns_councils;  // between a faction's calls of the Council, and between putting a proposal (or its passing) and its return
    int32_t minerals_harvesting_forest;  // a base's minerals from a forest its faction clears
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
    GAME_PAD(0xfc, 0x108);
    // The most damage artillery deals, in percent: to units in a base or
    // bunker, on land in the open, and at sea.
    int32_t max_dmg_arty_base_bunker, max_dmg_arty_open, max_dmg_arty_sea;
    int32_t warming_num, warming_den;  // how fast ecological damage warms the planet
    GAME_PAD(0x11c, 0x128);
    int32_t oblit_atrocity;  // non-zero: obliterating a base is an atrocity
};
GAME_AT(struct rules_basic, warming_num, 0x114);
GAME_AT(struct rules_basic, minerals_harvesting_forest, 0x98);
GAME_AT(struct rules_basic, min_turns_councils, 0x94);
GAME_AT(struct rules_basic, oblit_atrocity, 0x128);
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
extern uint32_t *game_random_seed;              // game_random()'s state
extern void (*game_do_all_non_input)(void);       // redraws and the message pump
extern int (*game_best_specialist)(void);         // for the current base
extern int (*game_has_abil)(int proto, uint32_t ability);
extern void (*game_add_goal)(int faction, int type, int priority, int x, int y, int base);
extern int (*game_stack_check)(int veh, int type, int cond1, int cond2, int cond3);
extern void(__attribute__((thiscall)) * game_path_continents)(void *path);  // finds the regions
// The first step from (x1, y1) towards (x2, y2), as a direction, or -1.
extern int(__attribute__((thiscall)) * game_path_find)(void *path, int x1, int y1, int x2, int y2, int veh, int faction,
                                                     int flags, int unk);
// A square near (*x, *y) for the faction's next sensor, into *x, *y; nonzero for none.
extern int(__attribute__((thiscall)) * game_path_sensors)(void *path, int faction, int *x, int *y);
extern void (*game_draw_radius)(int x, int y, int radius, int unk);           // redraws squares
extern int (*game_base_find2)(int x, int y, int faction);  // the nearest base of the faction, or -1
extern void (*game_delete_landmark)(int x, int y);
extern void (*game_set_fac)(int facility, int base, int on);
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
extern int (*game_at_climax)(int faction);
extern int32_t *game_sunspot_duration;
extern int32_t *game_ranked_faction;  // [2]: factions base_hurry() and social_ai() compare themselves with
// Scores and scenario objectives (score.c).
extern int32_t *game_ranking_scores;  // [FACTION_COUNT] each faction's strength, as rankings() last weighed it
extern int16_t (*game_turn_might)[FACTION_COUNT];  // [1000] the same by turn, for the replay
// A scenario's objectives for a win and for a sudden-death win (9000 or more
// for none), its score per objective held, and its score for a win.
extern int32_t *game_objectives_required, *game_objectives_sudden_death, *game_objective_points;
extern int32_t *game_victory_points;
extern int32_t *game_start_year;            // the year of turn 0
extern int32_t *game_scn_victory_facility;  // a scenario's facility that makes bases objectives
// compute_score(): the Ascent's points, and each faction's share of the
// population behind it ([0] the total).
extern int32_t *game_score_ascent_points, *game_score_ascent_pop;
extern void (*game_set_treaty)(int faction, int other, uint32_t status, int on);
// For base_production().
extern void (*game_base_reset)(int base, int unk);       // the AI (or governor) picks what to build next
extern int (*game_veh_cost)(int proto, int base, int *unk);
extern int (*game_morale_mod)(int base, int faction, int triad);
extern void (*game_set_dirty)(void);
extern void (*game_interlude)(int id, char *text, int unk1, int unk2);
extern int (*game_generators)(int faction, int *count);
extern void (*game_bit_set)(int x, int y, uint32_t bit, int on);
extern void (*game_synch_bit)(int x, int y, int faction);
extern int (*game_whose_territory)(int faction, int x, int y, int unk1, int unk2);
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
extern void(__attribute__((thiscall)) * game_map_win_clear_alt)(void *win, int x, int y, int unk);
extern void(__attribute__((thiscall)) * game_map_win_draw_map)(void *win, int unk);  // redraws the whole map
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
extern int (*game_propose_proto)(int faction, int chassis, int weapon, int armor, uint32_t abilities, int reactor,
                                 int plan, char *name);
extern void(__attribute__((thiscall)) * game_map_win_set_center)(void *win, int x, int y, int unk);
// The base window, and a base it is redrawn for after the base's upkeep
// (Thinker's BaseUpkeepDrawID); its redraw, and opening it on a base.
extern void *game_base_win;
extern void(__attribute__((thiscall)) * game_base_win_base_killed)(void *win, int base);  // a base is gone
// The main interface's list box of messages, and the list it shows: each
// node's key names what the message is about (0x40000000 | base for a
// base), and its item is the message, an object of the game's own.
extern void *game_message_box;
extern int(__attribute__((thiscall)) * game_listbox_update_changes)(void *box);
struct game_list_node {
    void *vtbl;
    int32_t key;
    void *item;
    struct game_list_node *next, *prev;
};
struct game_list {
    void **vtbl;  // [1]: told of an item about to go
    int32_t unk_4;
    struct game_list_node *head, *cursor;
    int32_t count, pos;
};
static inline struct game_list *game_message_list(void) {
    char *box = game_message_box;
    return (struct game_list *)(box + 0xbc + (*(int32_t **)box)[2]);
}
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
// The social engineering screen (social_select): its window opened on a
// faction and closed, the choice sent to the other players, and in the
// scenario editor the faction it picks (or negative).
extern void *game_social_win;
extern void(__attribute__((thiscall)) * game_social_win_init)(void *win, int faction);
extern void(__attribute__((thiscall)) * game_social_win_close)(void *win);
extern void (*game_synch_soc)(int faction);
extern int(__attribute__((thiscall)) * game_console_editor_who)(void *console, int faction, int unk);
// A faction's end (eliminate_player): set up again, which may let it
// escape in a colony pod (non-zero then); its pronoun; the monument to it.
extern int (*game_setup_player)(int faction, int killer, int probe);
extern char *(*game_get_his_her)(int faction, int unk);
extern void (*game_mon_killed_faction)(int faction, int other);
// The resources' names: [2 * kind], plural [2 * kind + 1] (nutrient,
// mineral, energy).
extern char **game_resource_names;
extern void (*game_consider_designs)(int faction);
// Designs a faction retired (retire_proto), the last 64 for each faction,
// which propose_proto won't bring back for a human player.
struct bung {
    uint32_t abilities;
    uint8_t chassis, weapon, armor, reactor;
};
_Static_assert(sizeof(struct bung) == 8, "bung size");
extern struct bung *game_bunglist;  // [FACTION_COUNT * 64]
// The design workshop asking the AI for a suggestion: while mode is set,
// propose_proto records its design here (mode 2 for one the faction has,
// 3 for a new one) instead of making it; a plan of 0 or more only takes
// designs of that group.
struct design_suggestion {
    uint32_t abilities;
    int32_t plan, weapon, chassis, reactor, unk_14, unk_18, armor;
    int32_t mode;
};
_Static_assert(sizeof(struct design_suggestion) == 0x24, "design_suggestion size");
extern struct design_suggestion *game_design_suggestion;
extern int32_t *game_first_new_proto;  // a design new with a tech, shown with it; -1 for none
// tech_achieved(): the other half of a prototype upgrade (every design of a
// faction brought up to date), the design workshop for a design new with
// a tech and its shutdown, the New Tech window, the net daemon's call to
// synchronise bases, the Monuments' record of a secret learned, and the
// popup's help text for a tech; the sprites of the techs' icons, 0x2c bytes each.
extern void (*game_upgrade_any_prototypes)(int faction);
extern void (*game_design_new_veh)(int faction, int proto);
extern void *game_design_win, *game_new_tech_win;
extern void(__attribute__((thiscall)) * game_design_win_shutdown)(void *win);
extern void(__attribute__((thiscall)) * game_new_tech_win_exec)(void *win, int tech, int from);
extern void(__attribute__((thiscall)) * game_net_daemon_synch)(void *daemon, int a, int b, int c, int d, int e, int f, int g);
extern void (*game_mon_secrets_of_tech)(int faction);
extern void (*game_help_tech_info_2)(void *pop, int tech, int faction);
extern uint8_t *game_tech_sprites;
extern void (*game_synch_obs)(int proto);  // a design's obsolete bits, sent in a network game
extern void (*game_say_stats_2)(char *text, int proto);  // a design's strengths, added to text
// Network games: a base kept from the other players while it changes.
extern void *game_net_daemon;
extern int(__attribute__((thiscall)) * game_net_lock_base)(void *daemon, int base, int unk, int unk2, int unk3);
extern void(__attribute__((thiscall)) * game_net_unlock_base)(void *daemon, int base);
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
extern int(__attribute__((thiscall)) * game_console_upgrade)(void *console, int veh);  // 0 when it can't
// For upgrades and promotions: a design's stats as text, each morale
// level's name (and native life's), the message bar's notices, the
// console's refresh, and a network game's message to the other players.
extern char *(*game_morale_names)[2];
extern void *game_netmsg;
extern void(__attribute__((thiscall)) * game_netmsg_pop)(void *netmsg, char *label, int delay, int unk, char *sound);
extern void(__attribute__((thiscall)) * game_console_update_data)(void *console, int unk);
extern void (*game_message_data)(int type, int unk, int a, int b, int c, int d);
extern int(__attribute__((thiscall)) * game_net_daemon_receive)(void *daemon);
// Units moving and their orders (move.c): a step in a direction (order_veh,
// the whole move with its fights and finds), a former's work started, a
// square named in a message, the labels for a road or tube finished, and a
// network game's unit sent to the others or handed to the daemon.
extern int (*game_order_veh)(int veh, int dir, int type);
extern int (*game_action_terraform)(int veh, int terraform, int unk);
extern void (*game_say_loc)(char *buf, int x, int y, int a, int b, int c);
extern char *game_text_road_to_done, *game_text_tube_to_done;
extern void (*game_synch_veh)(int veh);
extern void (*game_synch_base)(int base);  // tells the other players of a base (a no-op outside network games)
extern int(__attribute__((thiscall)) * game_net_order_veh)(void *daemon, int veh, int dir, int unk);
extern int(__attribute__((thiscall)) * game_net_await_exec)(void *daemon, int unk);
extern int(__attribute__((thiscall)) * game_net_await_synch)(void *daemon);
extern void(__attribute__((thiscall)) * game_net_unlock_veh)(void *daemon);
extern void (*game_del_site)(int faction, int type, int x, int y, int unk);
// The step's own calls (order_veh): a probe team's mission, a supply
// crawler's menu, a base captured, supply pods, monoliths and artifacts
// studied, the tutorial's checks, a script.txt section opened for a menu,
// a menu's items, the map cursor moved on and the engine's pitch; the
// console's flags for a move shown quickly (1, 2 for the player's unit)
// and for one the player sees, and the tutorial's counts of zones of
// control and fungus met and its flag for fractional moves seen.
extern int (*game_probe)(int veh, int base, int veh_target, int unk);
extern int (*game_supply_options)(int veh, int base);
// The AI's units' moves (ai.c): a unit given to another faction, dropped by
// air, a base site's worth, an aircraft's move, a network lock, a base
// message to the other players.
extern void (*game_action_give)(int veh, int faction);
extern int (*game_action_airdrop)(int veh, int x, int y, int flags);
extern int (*game_site_at)(int x, int y, int unk);
extern int (*game_air_power)(int veh, int base_dist, int zoc, int *x, int *y);
extern int(__attribute__((thiscall)) * game_net_add_lock)(void *daemon, int unk, int x, int y);
extern void (*game_message_base)(int type, int veh, char *text, int unk);
extern void (*game_capture_base)(int base, int faction, int probe);
extern int (*game_goody_box)(int veh, int goody);
extern void (*game_monolith)(int veh);
extern int (*game_study_artifact)(int veh);
extern int (*game_tut_check)(int bit);
extern int (*game_x_text_open)(char *file, char *label);
extern void (*game_parse_string)(char *text, char *buffer);
extern void(__attribute__((thiscall)) * game_dialogs_item)(void *dialogs, char *text, int id);
extern void(__attribute__((thiscall)) * game_console_cursor_next)(void *console, int x, int y);
extern void(__attribute__((thiscall)) * game_fx_engine_pitch)(void *fx, int pitch);
extern int32_t *game_moved_quickly, *game_console_23be4;
extern int32_t *game_zoc_tutorial_count, *game_fungus_tutorial_count, *game_tutorial_moves_seen;
// Units' other actions (move.c): a fight started from the map, a missile's
// target faction given its chance to shoot it down, an atrocity and a
// broken treaty noted, a colony's monument, a sound at a square, the
// tutorial's notes on the map (and which it has shown, by land and sea),
// the base window told of a unit, and a network game's locks and messages.
extern int (*game_battle_fight)(int veh, int dir, int unk1, int unk2, int unk3);
extern int (*game_shoot_it)(int faction, int other, int x, int y, int fungal);
extern void (*game_atrocity)(int faction, int other, int type, int unk);
extern void (*game_double_cross)(int faction, int other, int unk);
extern void (*game_mon_colony_founded)(int faction, char *name);
extern void *game_fx;
extern int(__attribute__((thiscall)) * game_fx_play_loc)(void *fx, int sound, int x, int y);
extern int32_t *game_tut_map_seen;  // [2] bit per terraform shown, on land and at sea
extern void *game_sprite_farm, *game_sprite_soil, *game_sprite_mine, *game_sprite_solar;
extern void(__attribute__((thiscall)) * game_base_win_check_veh)(void *win, int veh);
extern int32_t *game_veh_attack_flags, *game_terraform_sound_played;
extern int(__attribute__((thiscall)) * game_net_lock_veh)(void *daemon, int *veh, int type, int x, int y, int unk);
extern int(__attribute__((thiscall)) * game_net_maybe_lock)(void *daemon, int *veh, int a, int b, int x, int y, int type);
extern void (*game_message_veh)(int type, int veh, int dir, int unk);
extern int (*game_x_pop)(char *label, int unk);  // a popup from script.txt: the player's answer
// The AI's turn around its units (ai.c): the AI's choice of a unit's move,
// and a network game's unit handed to the daemon.
extern int (*game_enemy_move)(int veh);
extern int(__attribute__((thiscall)) * game_net_daemon_action)(void *daemon, int veh, int unk);
// base_at(): set while its "Base Bits Error" is being reported, and the two
// flags that decide whether the popup shows and the Ctrl key is read.
extern int32_t *game_base_bits_reporting, *game_base_bits_popup, *game_base_bits_quiet;
extern void (*game_wipe_goals)(int faction);
// The debugging build's warning (format, arguments), and add_site()'s format and buffer for it.
extern void (*game_danger)(const char *format, char *buffer, int a, int b, int c);
extern const char *game_text_add_site;
extern char *game_text_buffer;
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
extern int32_t *game_diplo_friction_faction, *game_diplo_friction_with;  // the AI and the player game_diplo_friction follows
// tech_analysis()'s findings for a player and a rival: the four techs the
// player could best give the rival and their worth to it; the two the rival
// could best give the player, their worth to the player and the secret
// project that makes each worth less (see faction.c), or -1; and the first
// such project found.
extern int32_t *game_trade_want_tech[4], *game_trade_want_value[4];
extern int32_t *game_trade_offer_tech[2], *game_trade_offer_value[2], *game_trade_offer_project[2];
extern int32_t *game_trade_project;
extern int32_t *game_ambience_flags;  // bit 0: the player is at war, for the music
extern int (*game_ambience)(int sound);
extern void *game_multi_win;  // the multiplayer status window
extern int32_t *game_multi_win_visible;
// diplomacy_check()'s findings for a player (`faction`) and a rival
// (`other`), named as in Thinker, and the rest of the talks' state.
extern int32_t *game_diplo_second;  // the rival in the talks
extern int32_t *game_diplo_tribute, *game_diplo_fear, *game_diplo_conquest_fear, *game_diplo_exposed_bases,
    *game_diplo_open_base, *game_diplo_captured_bases, *game_diplo_near_endgame, *game_diplo_rival_outranks,
    *game_diplo_great_satan, *game_diplo_runaway_leader, *game_diplo_reputation, *game_diplo_player_pacts,
    *game_diplo_rival_pacts, *game_diplo_rival_strong_pacts, *game_diplo_rival_pact_score, *game_diplo_spoke_turn,
    *game_diplo_enemy_allies, *game_diplo_enemy_ally, *game_diplo_prev_hostility, *game_diplo_goodwill,
    *game_diplo_help_faction, *game_diplo_unmet_enemy, *game_diplo_player_enemy, *game_diplo_unmet_faction,
    *game_diplo_mutual_faction, *game_diplo_rival_wars, *game_diplo_can_surrender, *game_diplo_can_betray,
    *game_diplo_best_proto, *game_diplo_mention_proto, *game_diplo_caption_off, *game_diplo_caption_mode,
    *game_diplo_maps_news;
// game_diplo_maps_news: 1 once a map trade showed the first faction something new.
extern int32_t *game_diplo_region_factions;  // [REGION_COUNT] bit per faction with its headquarters or many bases there
extern int32_t *game_encounter_x, *game_encounter_y;  // where two factions met, while they talk
extern int32_t *game_un_charter_a, *game_un_charter_b;  // the UN Charter stands while a <= b
extern int32_t *game_net_income;  // energy_compute()'s answer
extern void **game_faction_portraits;  // [FACTION_COUNT]
extern int32_t *game_console_veh_mode;  // non-zero while the console moves game_veh_ref_map_1
extern void *game_info_win, *game_diplo_pop, *game_tut_win, *game_pop_wait;
extern void (*game_energy_compute)(int faction, int unk);
extern void (*game_comm_check)(int faction, int other);
extern void (*game_enemies_war)(int faction, int other);
extern void (*game_enemies_treaty)(int faction, int other, int trade);
extern void (*game_enemies_unpact)(int faction, int other);  // two AIs in a pact part
extern void (*game_enemies_team_up)(int faction, int other, int third);  // two AIs in a pact turn on a third
extern char *(*game_get_he_she)(int faction, int unk);
extern int (*game_tut_check2)(int bit);
extern int(__attribute__((thiscall)) * game_tut_map)(void *win, char *label, int x, int y, int a, void *sprite, int b,
                                                     int x2, int y2);
extern void (*game_diplomacy_caption)(int faction, int other);
extern char *(*game_get_pact_hood)(int faction, int other);
extern char *(*game_get_him_her)(int faction, int unk);
// communicate()'s helpers: whether a faction has an agenda towards another (the bits of diplo_agenda), the player's
// auto contact in a hotseat game, a loan sent in a network game, a truce pledged, and a faction's withdrawal.
extern int (*game_has_agenda)(int faction, int other, uint32_t agenda);
extern int (*game_auto_contact)(void);
extern void (*game_net_loan)(int faction, int other, int total, int payment);
extern void (*game_pledge_truce)(int faction, int other, int wait);
extern int (*game_do_withdrawal)(int faction, int other);
extern int (*game_diplomacy_menu)(int faction, int other);  // the player's menu of what to ask
extern int (*game_demand_withdrawal)(int faction, int other);
extern int (*game_dont_withdrawal)(int faction, int other);  // a faction to cross over the demand, or 0
extern void (*game_call_off_vendetta)(int faction, int other);
extern void (*game_diplomacy_ends)(int faction, int other);
extern void(__attribute__((thiscall)) * game_map_win_focus)(void *win, int x, int y);
extern int (*game_x_pops_13)(char *file, char *label, int flags, void *sprite, void *fn);  // a leader's message from a faction's file
extern int (*game_x_pops)(char *label, void *sprite, void *fn);
extern int (*game_x_pops_11)(char *label, int flags, void *sprite, void *fn);
extern int (*game_wants_prototype)(int proto, int faction);
extern void (*game_tech_achieve)(int faction, int tech, int from, int steal);  // tech_achieved()
extern void(__attribute__((thiscall)) * game_net_daemon_await_diplo)(void *daemon, int unk);
extern int (*game_spying)(int faction);
extern void (*game_set_agenda)(int faction, int other, uint32_t agenda, int on);
extern void(__attribute__((thiscall)) * game_console_set_move)(void *console, int unk);
extern void(__attribute__((thiscall)) * game_popup_dtor)(void *pop);
extern void(__attribute__((thiscall)) * game_diplo_pop_start)(void *pop, int faction);
extern void (*game_introduce)(int faction, int other);
extern void (*game_report_intel)(int faction);
extern void(__attribute__((thiscall)) * game_info_win_set_state)(void *win, int a, int b);
extern void(__attribute__((thiscall)) * game_sprite_box_sprite)(void *box, void *sprite, int a, int b);
// The player's proposal in talks and what it offers in return (Thinker's
// DiploCurrentProposal and DiploCounterProposal; see diplomacy.c), the
// faction a joint attack or a commlink is about, the AI's base asked for
// and the player's base offered.
extern int32_t *game_diplo_proposal, *game_diplo_counter, *game_diplo_proposal_faction, *game_diplo_ask_base,
    *game_diplo_bid_base;
// A faction whose fight the player agreed to join, and a measure of the
// AI's defiance that propose_pact() reads (Thinker's DiploAlterFear).
extern int32_t *game_diplo_accepted_faction, *game_diplo_alter_fear;
// Phrases from a faction's file (get_noun_phrase and get_phrase open it),
// and the words for its pact.
extern char *(*game_get_noun_phrase)(int faction, int unk);
extern char *(*game_get_phrase)(int faction, int unk);
extern char *(*game_get_pact)(int faction);
extern char *(*game_get_pacts)(int faction);
// The talks: maps traded (net_maps(), or sent in a network game), a tech
// trade (tech_trade()), and the planet window the base menus show bases on.
extern void (*game_net_maps)(int faction, int other, int unk);
extern int (*game_tech_trade)(int faction, int other, int unk, int counter, int proposal);
extern void(__attribute__((thiscall)) * game_plan_win_init)(void *win, int unk);
extern void(__attribute__((thiscall)) * game_plan_win_back_redraw)(void *win);
extern void(__attribute__((thiscall)) * game_plan_win_clear_lines)(void *win);
extern void(__attribute__((thiscall)) * game_plan_win_monocenter)(void *win, int x, int y, int unk);
extern void(__attribute__((thiscall)) * game_plan_win_add_line_2)(void *win, int a, int b, int c);
extern void(__attribute__((thiscall)) * game_plan_win_on_redraw)(void *win);
extern void(__attribute__((thiscall)) * game_plan_win_close)(void *win);
extern void(__attribute__((thiscall)) * game_win_show)(void *win, int unk);
extern int32_t *game_diplo_menu_a, *game_diplo_menu_b;  // what the proposal's answer left for communicate()
extern int32_t *game_diplo_trade_refused;  // the player declined the last tech trade
extern int32_t *game_diplo_trade_offered;  // set in the talks when the AI has offered a trade already
extern int32_t *game_diplo_plans_faction, *game_diplo_plans_other;  // battle_plans()'s two, for its menu's lines
extern int (*game_suggest_plan)(int faction, int other);  // a base to attack, or -1
extern int (*game_attack_from)(int base, int faction);    // the base to attack it from
extern void(__attribute__((thiscall)) * game_plan_win_find_view)(void *win, int x1, int y1, int x2, int y2, int x3,
                                                                  int y3, int x4, int y4);
extern char *(*game_get_pacts_2)(int faction, int other);
extern void (*game_give_a_base)(int base, int faction);  // a base ceded in talks
extern void (*game_pact_of_brotherhood)(int faction, int other);
extern void (*game_make_treaty)(int faction, int other);
// A network game's talks held in step (diplo_lock()'s parts), no-ops in
// one played alone.
extern int(__attribute__((thiscall)) * game_net_daemon_add_global)(void *daemon);
extern void(__attribute__((thiscall)) * game_net_daemon_unlock)(void *daemon);
extern void (*game_net_pump)(void);
// Planetary Council proposals.
struct proposal {
    char *name, *description;
    int32_t preq_tech;
};
extern struct proposal *game_proposals;  // [11]
extern int (*game_social_ai)(int faction, int a, int b, int c, int d, int e);
extern void (*game_call_council)(int faction);
// The Planetary Council.
enum { MORE_RULES_NO_COUNCIL = 0x4 };  // game_more_rules bits
extern uint32_t *game_more_rules;
extern int32_t *game_council_candidate;  // [FACTION_COUNT] the governor each faction voted for in the council being held, or 0
extern int32_t *game_council_pending;    // non-zero while a hotseat or e-mail game's council waits on its players
extern int32_t *game_proposal_turn;      // [11] the turn each proposal was last put (council()) or passed, or -9999
extern int32_t *game_council_session;   // non-zero while the Council's window is open
extern int32_t *game_council_net_call;  // [3] an AI's call in a network game: faction, proposal and candidate
// The Council's session (its window, the votes and the result), and its
// window told of a faction's vote.
extern void (*game_council)(int faction, int prop, int target);
extern void *game_council_win;
extern void(__attribute__((thiscall)) * game_council_win_set_vote)(void *win, int faction, int vote);
// Network games: the diplomacy lock, the wait for other players, a
// faction's leader state sent, and the message bar cleared.
extern int (*game_diplo_lock)(int unk);
extern void (*game_diplo_unlock)(void);
extern void (*game_wait_loop)(void);
extern void (*game_synch_leader)(int faction);
extern void(__attribute__((thiscall)) * game_netmsg_close)(void *netmsg);
extern void (*game_auto_save)(void);

// Ecological damage: fungus, native life, warming and volcanoes.
struct rules_terraform {
    char *name, *name_sea;
    int32_t preq_tech, preq_tech_sea;
    uint32_t bit;  // the map_tile.bits it makes
    uint32_t bit_incompatible;  // the map_tile.bits it clears
    int32_t rate;  // turns of a former's work
    GAME_PAD(0x1c, 0x20);
};
_Static_assert(sizeof(struct rules_terraform) == 0x20, "rules_terraform size");
GAME_AT(struct rules_terraform, rate, 0x18);
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
// The rest of it the rules readers (rules.c) use: the next item kept in the
// string table, as a number, as binary digits; the index of a file's
// sections. The string table (game_strings), the labels, a button's bubble
// help, a faction's art, the factions chosen in the settings, and the C
// runtime's case-blind compares and atoi (the game's own, for their locale
// and overflow).
extern char *(*game_text_item_string)(void);
extern int (*game_text_item_number)(void), (*game_text_item_binary)(void);
extern void (*game_text_clear_index)(void);
extern void (*game_text_make_index)(char *file);
extern int (*game_labels_init)(void);
extern int(__attribute__((thiscall)) * game_strings_init)(void *table, int size);
extern char *(__attribute__((thiscall)) * game_strings_put)(void *table, char *s);
extern int(__attribute__((thiscall)) * game_button_set_bubble_text)(void *button, char *s);
extern void (*game_load_faction_art)(int faction);
extern void (*game_prefs_fac_load)(void);
extern int (*game_stricmp)(const char *a, const char *b);
extern int (*game_strnicmp)(const char *a, const char *b, size_t n);
extern int (*game_atoi)(const char *s);

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
// The path finder's object: a table the size of the map, and the x and y
// lists it uses as a queue (squares are added at `tail` and taken from
// `head`). Path_find keeps the table it filled from a destination, and the
// destination, flags, faction and unit type it filled it for, to reuse it.
// `faction` is the owner of the last unit Path_move moved.
struct game_path {
    int32_t *map;
    int16_t *x, *y;
    int32_t tail, head;
    int32_t faction;
    int32_t x_dst, y_dst;
    int32_t flags;
    int32_t faction2;
    int32_t proto;
};
_Static_assert(sizeof(struct game_path) == 0x2c, "game_path size");
extern struct game_path *game_path;

// Combat and probe teams (combat.c).
enum { FACTION_FLAG_UNK_1000 = 0x1000 };  // faction_data.flags: its unit killed the Assistant Worm
enum { BASE_STATE_UNK_40000 = 0x40000 };  // the home of a unit that killed the Assistant Worm
enum { VEH_STATE_UNK_800000 = 0x800000 };  // the Assistant Worm
// A probe team's action: whether it has the algorithmic enhancement, the
// faction it acts against, and whether that faction has the Hunter-Seeker
// Algorithm.
extern int32_t *game_probe_algo, *game_probe_target, *game_probe_target_hsa;
extern int32_t *game_pb_reactor;  // a planet buster's reactor, for its explosion
// The records the Monuments window shows, 0x4f4 bytes a faction: 21
// events of 0x38 bytes (the year, two words of detail, the faction, and
// whether it happened), then the latest event and names. Events 15 to 20
// are the leaders of eliminated factions it holds captive (the second
// detail word names the faction).
enum { MONUMENT_SIZE = 0x4f4, MONUMENT_EVENT_SIZE = 0x38, MONUMENT_CAPTIVES = 15 };
extern uint8_t *game_monuments;
// Treaties and techs changed on every machine of a network game (the
// change itself otherwise).
extern void (*game_net_tech)(int faction, int tech, int from, int unk);
extern void (*game_net_treaty_on)(int faction, int other, uint32_t status, int unk);
extern void (*game_net_treaty_off)(int faction, int other, uint32_t status, int unk);
extern void (*game_net_set_treaty)(int faction, int other, uint32_t status, int on, int unk);
// A label's text from a script file, put together in game_battle_text.
extern int (*game_parse_it)(char *file, char *label);
// The sound effects: each a 0x6c-byte object, whose method at 0x5c says
// whether it is playing.
enum { FX_SIZE = 0x6c, FX_PB_BLAST = 59, FX_PB_AFTER = 60 };
extern void(__attribute__((thiscall)) * game_fx_play)(void *fx, int id);
extern int (*game_do_task)(void);  // the message pump, once
// A unit shown moving a square in a direction (an animation only).
extern void (*game_veh_scoot)(int veh, int x, int y, int dir, int unk);
// The tech a faction would take from another, or negative; `label` names
// the window that lets a human choose.
extern int (*game_tech_pick)(int faction, int unk, int other, char *label);
extern void (*game_major_atrocity)(int faction, int victim);
// The battle (battle_fight_2): the caller's flags (1: the player may be
// asked, of treaties and odds; 2: the attack is made, not only weighed),
// the units the map draws fighting (or -1), and battles won and lost.
extern int32_t *game_veh_draw_att, *game_veh_draw_def;
extern int32_t *game_faction_combat_win, *game_faction_combat_loss;  // [FACTION_COUNT]
extern int32_t *game_base_win_zoomed;  // non-zero while the base window is open
extern int32_t *game_status_win_battle;  // set while the status window shows a battle
extern void *game_battle_win, *game_status_win, *game_main_interface;
extern void(__attribute__((thiscall)) * game_status_win_draw)(void *win, int veh_att, int veh_def, int off, int def,
                                                              int combat);
extern void(__attribute__((thiscall)) * game_status_win_reset)(void *win);
extern void(__attribute__((thiscall)) * game_status_win_redraw)(void *win);
extern void(__attribute__((thiscall)) * game_set_iface_mode)(void *win);  // SubInterface_set_iface_mode
extern void(__attribute__((thiscall)) * game_battle_win_clear)(void *win);
extern void(__attribute__((thiscall)) * game_battle_win_stop_timer)(void *win);
extern void(__attribute__((thiscall)) * game_battle_win_pulse_timer)(void *win);
extern void(__attribute__((thiscall)) * game_base_win_focus)(void *win);
extern void (*game_flush_input)(void);
extern void (*game_prefs_save)(int unk);
extern int (*game_break_treaty)(int faction, int other, uint32_t treaties);  // asks; non-zero: the attack is called off
extern void (*game_say_stats_3)(char *text, int proto);
extern void (*game_mon_enemy_destroyed)(int faction, int other);
extern int (*game_pop3)(char *label, char *picture, int faction, int x, int y);
extern void (*game_draw_tile_fixup)(int x, int y, int a, int b);
extern void (*game_draw_tile_fixup_2)(int x1, int y1, int x2, int y2, int a, int b);

// Pods and monoliths (events.c).
// A unity pod around the landing site that is a special one (a mining
// laser, a chopper, the engine's energy, the radar): non-zero if so.
extern int (*game_do_unity_crash)(int x, int y, int faction);
extern void (*game_my_srand)(int seed);  // srand(), logged, for network games
extern void (*game_say_morale_2)(int veh, int unk);  // a unit's morale, into game_battle_text
// Sound effects faded out or stopped (by id).
extern void(__attribute__((thiscall)) * game_fx_fade)(void *fx, int id);
extern void(__attribute__((thiscall)) * game_fx_stop)(void *fx, int id);
extern uint32_t(__attribute__((stdcall)) * *game_char_lower)(char *s);  // CharLowerA, as imported
extern char *game_parse_text;  // the text variables parse_says() fills, 256 bytes each
extern int32_t *game_parse_nums;     // [10] the numbers parse_num() sets
extern int32_t *game_parse_genders;  // [10] the grammar parse_say() and parse_says() set for each variable
extern int32_t *game_parse_plurals;  // [10]
extern const int8_t *game_findnum_range;  // the lowest and highest character findnum() takes for a digit
extern const char *game_fr_articles;      // [6][20] the French words parse_string() joins to a vowel
// What the popup entry points call (cdecl): run a popup from the file and label they were given and the rest,
// and the same for a question with an answer typed in.
extern int (*game_pops_18)(int file, int label, int a, int b, int c, int d, int e, int f, int g);
extern int (*game_pop_ask_6)(int file, int label, int len, int a, int b, int c);
extern char **game_text_cursor;           // where text_item() and its kin take the next item from the line
extern char **game_text_item_buf;         // the buffer they put the item in (512 bytes)
extern uint8_t *game_text_slots;          // [4][0x118] the text indexes (a file name at 0, the number of labels at 0x100, the labels at 0x108)
// A new game (setup_game): the preferences file, and the game's state
// cleared; time warp, or the balance of the factions' starts.
extern void (*game_prefs_put)(char *key, char *value);
extern int (*game_prefs_get_2)(char *key, int value, int flag);
extern void (*game_prefs_load)(int unk);
extern void (*game_prefs_put_2)(char *key, int value, int flag);
extern void (*game_prefs_use)(void);
extern void (*game_clear_monuments)(void), (*game_wipe_undo)(void), (*game_clear_all_offers)(void);
extern void (*game_clear_hotseat_chat)(void), (*game_clear_council_notify)(void), (*game_clear_tamper)(void);
extern void (*game_time_warp)(void), (*game_balance)(void);
// A network game closed when its player is out of it.
extern void(__attribute__((thiscall)) * game_net_daemon_hang_up)(void *daemon);
extern void(__attribute__((thiscall)) * game_lock_clear)(void *lock);
extern void(__attribute__((thiscall)) * game_alpha_net_close)(void *daemon);
// A base changing hands (capture_base): the console's search for a unit
// of the player's waiting for orders (-1 for none), and the Monuments
// window's record of the conquest.
extern int(__attribute__((thiscall)) * game_console_ready_search)(void *console, int unk);
extern void (*game_mon_conquer_base)(int faction, char *base_name);
// The game's end (end_of_game): the mission year and the year it ends, the
// turn loop's flags to stop (Thinker's ControlTurnA and B), and the end's
// screens: the credits, the score, the faction's epitaph, the hall of fame
// and the replay, and the Monuments window's record of the win.
extern int32_t *game_mission_year, *game_ending_year, *game_control_turn_a, *game_control_turn_b;
extern void (*game_show_credits)(void), (*game_show_replay)(void);
extern void (*game_report_score)(int final), (*game_quayle)(int faction), (*game_hall_of_fame)(int final);
extern void (*game_mon_winning_unify)(int faction, int other);
// The colony pod's landing (crash_landing): image buffers, FLC players
// (an image buffer at +4, its palette at +0x5bc), a sound, the map
// window's square to screen position and its partial redraw, and Win32's
// UnionRect as imported.
#define GAME_THIS __attribute__((thiscall))
extern void(GAME_THIS *game_buffer_ctor)(void *b), (GAME_THIS *game_buffer_dtor)(void *b), (GAME_THIS *game_buffer_close)(void *b);
extern void(GAME_THIS *game_buffer_init)(void *b, int w, int h, int a, int c);
extern void(GAME_THIS *game_buffer_copy)(void *b, void *to, int x, int y, int w, int h);
extern void(GAME_THIS *game_buffer_copy_2)(void *b, void *from, int *rect);
extern void(GAME_THIS *game_buffer_draw_2)(void *b, void *to, int mode, int x, int y, int a, int c);
extern void(GAME_THIS *game_buffer_draw_multi)(void *b, void *to, int color, int x, int y, int a, int c, void *table);
extern void(GAME_THIS *game_flic_ctor)(void *f), (GAME_THIS *game_flic_dtor)(void *f), (GAME_THIS *game_palette_dtor)(void *p);
extern int(GAME_THIS *game_flic_load)(void *f, void *buffer, char *file, int a, int b, int c);
extern int(GAME_THIS *game_flic_frame)(void *f, int a, int b);
extern void(GAME_THIS *game_wave_init)(void *wave, char *file, int unk);
extern void(GAME_THIS *game_map_win_tile_to_pixel)(void *win, int x, int y, int *px, int *py);
extern void(GAME_THIS *game_graphic_win_update_2)(void *win, int *rect, int unk);
extern int(__attribute__((stdcall)) * game_union_rect)(int *to, const int *a, const int *b);
#undef GAME_THIS

// Saved games (save.c): the game's C runtime file functions, which take
// its own FILE (passed as void *), its allocator, and what loading calls.
extern size_t (*game_fread)(void *buf, size_t len, size_t count, void *fp);
extern size_t (*game_fwrite)(const void *buf, size_t len, size_t count, void *fp);
extern int (*game_fclose)(void *fp), (*game_fseek)(void *fp, long offset, int origin);
extern int (*game_fgetc)(void *fp), (*game_fputc)(int c, void *fp);
extern void *(*game_env_open)(const char *name, const char *mode);  // fopen in the game's folders
extern int (*game_remove)(const char *name);
extern void *(*game_mem_get)(size_t size);  // malloc, or the game stops when out of memory
extern void (*game_free)(void *p);
extern int (*game_map_init)(void);  // allocates the map for the size set
extern void(__attribute__((thiscall)) * game_map_win_close)(void *win);
extern void(__attribute__((thiscall)) * game_path_init)(void *path);
extern void (*game_check_tamper)(void);
// A string list's add: the text at list->src_text, under id.
extern void(__attribute__((thiscall)) * game_string_list_add)(void *list, int id);

// ---- Drawing: palettes and buffers ----------------------------------------------

// The game's Palette class: 256 entries of red, green, blue and a flag byte
// (the Windows PALETTEENTRY order), a number that changes whenever the
// colours do, and five colour cycles.
struct palette_cycle {
    int32_t id;       // -1 when the slot is free
    void *timer;      // the game's Time object, which calls Palette_timer_callback
    uint8_t start, count;
    uint8_t pad[2];
    void *saved;      // the cycled entries as they were
};
struct palette {
    uint8_t rgb[256][4];
    uint32_t serial;
    struct palette_cycle cycle[5];
};
_Static_assert(sizeof(struct palette) == 0x454, "layout of the game's Palette");
GAME_AT(struct palette, serial, 0x400);
GAME_AT(struct palette, cycle, 0x404);
GAME_AT(struct palette_cycle, start, 8);
GAME_AT(struct palette_cycle, saved, 12);

// Non-zero once the game has a GDI palette (the functions that need one
// return 7 until then).
extern uint32_t *game_palette_handle;
// The game's C runtime sin and cos (the x87 instructions, answering in 80 bits), and the double it
// multiplies degrees by to get radians (a little off pi / 180, so read, not computed).
extern long double (*game_crt_sin)(double), (*game_crt_cos)(double);
extern double *game_palette_rad;

// The game's Buffer class (OpenSMACX calls it so): a drawing surface that is
// either a DIB (bits, drawn on directly) or a DirectDraw surface (dd, locked
// for each drawing call). Only the fields the reimplemented functions read.
struct game_buffer {
    void **vtable;
    uint8_t pad_4[0x20 - 4];
    int32_t clip[4];   // left, top, right, bottom: what drawing is clipped to
    int32_t full[4];   // the whole surface
    uint8_t pad_40[0x50 - 0x40];
    uint8_t *base;     // the pixels while locked, else NULL
    uint8_t *bits;     // a DIB's pixels
    void *dd;          // a DirectDraw surface, or NULL
    uint8_t pad_5c[0x6c - 0x5c];
    int32_t locks;     // how many drawing calls are in progress
    uint8_t pad_70[0x80 - 0x70];
    int32_t width;
    int32_t neg_height;  // minus the number of rows
    uint8_t pad_88[0x4a8 - 0x88];
    int32_t pitch;       // bytes from one row to the next
    uint8_t pad_4ac[0x588 - 0x4ac];
};
_Static_assert(sizeof(struct game_buffer) == 0x588, "layout of the game's Buffer");
GAME_AT(struct game_buffer, clip, 0x20);
GAME_AT(struct game_buffer, full, 0x30);
GAME_AT(struct game_buffer, base, 0x50);
GAME_AT(struct game_buffer, bits, 0x54);
GAME_AT(struct game_buffer, dd, 0x58);
GAME_AT(struct game_buffer, locks, 0x6c);
GAME_AT(struct game_buffer, width, 0x80);
GAME_AT(struct game_buffer, neg_height, 0x84);
GAME_AT(struct game_buffer, pitch, 0x4a8);

// Scratch the original's rectangle fill leaves behind: the pitch less the
// width of the last rectangle.
extern int32_t *game_fill_gap;
// Scratch the original's buffer copies leave behind: the bytes left over after whole
// 4-byte units in a row; and for the masked copy the row length in units and the
// gaps to the next row of the buffer read and the buffer written.
extern int32_t *game_copy_rem, *game_mask_units, *game_mask_gap_a, *game_mask_gap_b;

// ---- Drawing: fonts and text (text.c) ----
// The Windows calls the game's Font class makes, as imported (stdcall): the
// device context all fonts measure in, the font the game starts with, and
// the line-break slack the original leaves in a global.
extern void *(__attribute__((stdcall)) * game_select_object)(void *dc, void *obj);
extern int(__attribute__((stdcall)) * game_get_text_extent)(void *dc, const char *s, int n, int32_t *size);
extern void *(__attribute__((stdcall)) * game_get_stock_object)(int which);
// Text drawing's calls (Buffer_write_*), likewise as imported.
extern int(__attribute__((stdcall)) * game_set_bk_mode)(void *dc, int mode);
extern uint32_t(__attribute__((stdcall)) * game_set_text_color)(void *dc, uint32_t color);
extern int(__attribute__((stdcall)) * game_text_out)(void *dc, int x, int y, const char *s, int n);
extern int(__attribute__((thiscall)) * game_font_init)(void *font, const char *face, int size, int flags);
extern void **game_font_dc, **game_font_default;
extern int32_t *game_font_slack;
// The globals text drawing leaves behind: the table of hyperlink rectangles (three of 16 bytes and a colour for each link number), and the x
// the last wrapped line ended at.
extern int32_t *game_link_rects, *game_text_end_x;

// ---- Drawing: a Buffer's life (bufobj.c) ----------------------------------------------
// The Windows calls a Buffer makes when it is set up, cut to a clip rectangle and closed,
// as imported (stdcall), and what else it uses: the DirectDraw object (NULL unless the game
// draws through DirectDraw), the palette new Buffers take their colours from, the class's
// virtual method table, the hot-spot list each Buffer holds (Spot), the value a new
// Buffer's field 0x520 starts with, the C runtime's exit, and the message shown when a
// DIB can't be made.
extern void *(__attribute__((stdcall)) * game_create_compatible_dc)(void *dc);
extern void *(__attribute__((stdcall)) * game_create_dib_section)(void *dc, void *info, int usage, void **bits, void *section, int offset);
extern int(__attribute__((stdcall)) * game_delete_dc)(void *dc);
extern int(__attribute__((stdcall)) * game_delete_object)(void *obj);
extern void *(__attribute__((stdcall)) * game_create_rect_rgn_indirect)(const int32_t *rect);
extern int(__attribute__((stdcall)) * game_select_clip_rgn)(void *dc, void *rgn);
extern int(__attribute__((stdcall)) * game_set_text_align)(void *dc, int align);
extern int(__attribute__((stdcall)) * game_intersect_rect)(int32_t *out, const int32_t *a, const int32_t *b);
extern int(__attribute__((stdcall)) * game_equal_rect)(const int32_t *a, const int32_t *b);
extern int(__attribute__((stdcall)) * game_set_rect)(int32_t *r, int l, int t, int rt, int b);
extern int(__attribute__((stdcall)) * game_message_box_a)(void *wnd, const char *text, const char *title, int type);
extern void **game_ddraw, **game_palette_now;
extern void **game_buffer_vtable;
extern int32_t *game_buffer_520_default;
extern const char *game_dib_error_text, *game_dib_error_title;
extern void(__attribute__((thiscall)) * game_spot_ctor)(void *spot);
extern void(__attribute__((thiscall)) * game_spot_dtor)(void *spot);
extern void(__attribute__((thiscall)) * game_spot_init)(void *spot, int n);
// The popup's combined widget's layout (0x6132a0): calls the layout of the part its type (at 0x180) says; answers 0, or what that did.
extern int(__attribute__((thiscall)) * game_dialog_calc)(void *widget);
extern void *(__attribute__((thiscall)) * game_dialog_ctor)(void *dialog);
extern void(__attribute__((thiscall)) * game_dialog_dtor)(void *dialog);  // the original's: Dialog_dtor, GraphicWin_dtor (for sweeps)
extern void(__attribute__((thiscall)) * game_graphic_win_dtor)(void *win);
extern void(__attribute__((thiscall)) * game_win_close)(void *win);  // Win_close (0x5eb640): the game's calls reach Chiron's (or the original's in shadow mode)
extern void *(*game_operator_new)(size_t size);  // 0x64558a: the game's operator new
// The scroll bar a window makes for its own frame (Scroll_init_vert_nc and Scroll_init_horz_nc): the bar, then
// four numbers the window works out and the window it belongs to, then -1 or -2.
extern void(__attribute__((thiscall)) * game_scroll_init_vert_nc)(void *bar, int a, int b, int c, void *win, int d);
extern void(__attribute__((thiscall)) * game_scroll_init_horz_nc)(void *bar, int a, int b, int c, void *win, int d);
extern void (*game_flip_restore)(const int32_t *box, int arg);  // 0x5f7320: puts back what the bubble text covered
extern void(__attribute__((thiscall)) * game_win_set_bubble_text)(void *win, const char *text, const int32_t *rect);  // 0x5f83d0: shows the bubble text in a box near the rectangle
extern void (*game_win_clear_bubble)(void);  // 0x5f8500 (Win_clear_bubble_text): takes the bubble text away, if one is shown
extern void(__attribute__((stdcall)) * game_sleep)(uint32_t ms);  // the game's Sleep import
// The original's, called by the life cycle of the popup (dialog.c): the combined widget a BasePop holds (its constructor, given whether
// it is the whole object, and the part of its destructor that lies before the bases'), the CheckBox's close, and the string arena's
// constructor and close.
extern void *(__attribute__((thiscall)) * game_dialogs_ctor)(void *o, int most_derived);
extern void(__attribute__((thiscall)) * game_dialogs_dtor_part)(void *gw);
extern void(__attribute__((thiscall)) * game_checkbox_close)(void *o);
extern void *(__attribute__((thiscall)) * game_arena_ctor)(void *a);
extern void(__attribute__((thiscall)) * game_arena_close)(void *a);
extern void (*game_exit)(int code);
// ---- Image files and Sprite files (bufimg.c) ----------------------------------------------------
// The game's file-mapping object (16 bytes: the view, the file handle, the mapping handle and the size): made empty,
// closed, opened for reading (a view of the whole file, or 0) or for reading and writing, made new with a size
// (zero-filled), closed with the file cut at an end pointer, or let go without cutting. Then the C runtime's search
// backwards for a character (cdecl: from `end` down to just after `start`; 0 if none), the Windows calls the image
// functions make, as imported, the palette the Buffers take their colours from when they are copied, the video
// mode number that picks the extension of the picture files, and the strings in the game's data (file extensions, the
// prefix of a scrambled picture file, the mode and signature of a bitmap file).
struct game_mfile {
    void *view, *handle, *mapping;
    uint32_t size;
};
extern void *(__attribute__((thiscall)) * game_mfile_ctor)(struct game_mfile *f);
extern void(__attribute__((thiscall)) * game_mfile_close)(struct game_mfile *f);
extern uint8_t *(__attribute__((thiscall)) * game_mfile_open)(struct game_mfile *f, const char *name, int flag);
extern uint8_t *(__attribute__((thiscall)) * game_mfile_open_rw)(struct game_mfile *f, const char *name, int flag);
extern uint8_t *(__attribute__((thiscall)) * game_mfile_create)(struct game_mfile *f, const char *name, int size, int flag);
extern void(__attribute__((thiscall)) * game_mfile_release)(struct game_mfile *f);
extern void(__attribute__((thiscall)) * game_mfile_finish)(struct game_mfile *f, const uint8_t *end);
extern const char *(*game_rfind_char)(const char *start, const char *end, int c);
extern uint32_t(__attribute__((stdcall)) * game_get_file_size)(void *file, uint32_t *high);
extern int(__attribute__((stdcall)) * game_set_dib_color_table)(void *dc, unsigned start, unsigned n, const void *colors);
extern int(__attribute__((stdcall)) * game_stretch_blt)(void *dst, int x, int y, int w, int h, void *src, int sx, int sy, int sw, int sh, unsigned rop);
extern struct palette **game_palette_copy;
extern int32_t *game_video_mode;
extern const char *game_pcx_prefix, *game_pcx_ext, *game_pcx_write_ext, *game_bmp_write_ext, *game_bmp_mode, *game_bmp_signature;
extern const char *game_tga_ext[3], *game_bmp_ext[3];
// The queue "flying" text is laid out in: the x and y the line being filled goes at, the width left for it, the text queued so far (a
// C string, 1 KB or so) and the strings the original adds to it and to the widths (a gap between pieces, and one measured as the
// space a piece takes in each of the three layouts).
extern int32_t *game_flying_x, *game_flying_y, *game_flying_left;
extern char *game_flying_text;
extern const char *game_flying_gap, *game_flying_pad_height, *game_flying_pad, *game_flying_pad_cent;
// ---- The Font class's life (font.c) ------------------------------------------------------
// What Font_init and its kin call, as imported (stdcall), the font class's use count (0x9bb48c),
// the face the class makes the default font in, the extension of a font resource file, the
// face the game names everywhere (a pointer variable), and the font file the game loads.
extern void *(__attribute__((stdcall)) * game_create_font_indirect)(const void *logfont);
extern int(__attribute__((stdcall)) * game_get_text_metrics)(void *dc, void *tm);
extern int(__attribute__((stdcall)) * game_remove_font_resource)(const char *file);
extern int(__attribute__((stdcall)) * game_add_font_resource)(const char *file);
extern int(__attribute__((stdcall)) * game_create_scalable_font_resource)(int hidden, const char *fot, const char *ttf, const char *path);
extern int(__attribute__((stdcall)) * game_post_message)(void *wnd, unsigned msg, uintptr_t wp, intptr_t lp);
extern unsigned(__attribute__((stdcall)) * game_get_current_directory)(unsigned size, char *buf);
extern int32_t *game_font_class_count;
extern const char *game_fot_ext, *game_font_class_face;
extern const char **game_font_face;
extern const char *game_font_file;

// ---- Sprites (sprite.c) ---------------------------------------------------------

// The game's Sprite class (OpenSMACX calls it so): a keyed 8-bit image with
// its transparent margins cut away. `left` and `top` say how far the picture
// sits from where the sprite is drawn, `width` by `height` is what is kept,
// and `full_w` by `full_h` the size before the margins were cut. `data` is
// shared with the buffer it was cut from when `shared` is set.
struct game_sprite {
    char *name;       // set for a sprite loaded by name: drawn through the game's resource code
    uint8_t *data;    // width * height pixels
    uint8_t key;      // the transparent colour it was cut with
    uint8_t pad_9[3];
    int32_t stride;   // bytes from one row to the next (the width)
    int32_t width, height;
    int32_t full_w, full_h;
    int32_t left, top;
    int32_t shared;
};
_Static_assert(sizeof(struct game_sprite) == 0x2c, "layout of the game's Sprite");
GAME_AT(struct game_sprite, data, 4);
GAME_AT(struct game_sprite, key, 8);
GAME_AT(struct game_sprite, stride, 0xc);
GAME_AT(struct game_sprite, full_w, 0x18);
GAME_AT(struct game_sprite, full_h, 0x1c);
GAME_AT(struct game_sprite, width, 0x10);
GAME_AT(struct game_sprite, height, 0x14);
GAME_AT(struct game_sprite, left, 0x20);
GAME_AT(struct game_sprite, top, 0x24);
GAME_AT(struct game_sprite, shared, 0x28);

// What Buffer_box_sprite draws a frame from: a sprite for each corner and each edge (any may be
// missing), and a pattern Buffer to tile over the frame and inside it, its key colour the one the
// pattern shows through.
struct game_box_frame {
    struct game_sprite *top_left, *top_right, *bottom_left, *bottom_right;
    struct game_sprite *top, *bottom, *left, *right;
    struct game_buffer *fill;
    int32_t fill_key;
};
_Static_assert(sizeof(struct game_box_frame) == 0x28, "layout of the game's frame");

// The scratch memory the sprite drawing code works in: the tables made from
// the scale (how many pixels each source pixel makes, or how many source
// pixels each pixel drawn takes), the clipped ones for the part of the
// sprite on the buffer, and the loop counters the original keeps in memory.
// All of it is state only while one drawing call runs, but the originals
// leave it behind, and a check compares it.
struct game_sprite_scratch {
    uint32_t ytab_ptr;       // 0x9b3ab8: the next entry of the row table
    int32_t src_gap;         // 0x9b3abc: sprite stride less the pixels a row uses
    uint8_t pad_c0[0x9b3ac8 - 0x9b3ac0];
    int32_t pitch;           // 0x9b3ac8: bytes a row of the buffer
    int32_t dst_gap;         // 0x9b3acc: pitch less the pixels a row draws
    uint8_t pad_d0[4];
    int32_t width;           // 0x9b3ad4: pixels a row draws
    int32_t scale[800];      // 0x9b3ad8: the scaling table
    int32_t x_rem;           // 0x9b4758
    int32_t x_count;         // 0x9b475c: source pixels (or entries) in a row
    int32_t ytab[800];       // 0x9b4760: row table of the part drawn
    int32_t color;           // 0x9b53e0: the colour the mono draw uses
    uint8_t pad_e4[4];
    int32_t rows;            // 0x9b53e8: rows to draw
    int32_t xtab[800];       // 0x9b53ec: pixel table of the part drawn
    uint32_t xtab_ptr;       // 0x9b606c
    int32_t stride;          // 0x9b6070: the sprite's stride
    int32_t ycount;          // 0x9b6074
    int32_t xgroups;         // 0x9b6078: pixels a row draws in fours, rounded up
};
_Static_assert(sizeof(struct game_sprite_scratch) == 0x9b607c - 0x9b3ab8, "layout of the sprite scratch");
GAME_AT(struct game_sprite_scratch, pitch, 0x9b3ac8 - 0x9b3ab8);
GAME_AT(struct game_sprite_scratch, width, 0x9b3ad4 - 0x9b3ab8);
GAME_AT(struct game_sprite_scratch, scale, 0x9b3ad8 - 0x9b3ab8);
GAME_AT(struct game_sprite_scratch, x_rem, 0x9b4758 - 0x9b3ab8);
GAME_AT(struct game_sprite_scratch, ytab, 0x9b4760 - 0x9b3ab8);
GAME_AT(struct game_sprite_scratch, color, 0x9b53e0 - 0x9b3ab8);
GAME_AT(struct game_sprite_scratch, rows, 0x9b53e8 - 0x9b3ab8);
GAME_AT(struct game_sprite_scratch, xtab, 0x9b53ec - 0x9b3ab8);
GAME_AT(struct game_sprite_scratch, xtab_ptr, 0x9b606c - 0x9b3ab8);
GAME_AT(struct game_sprite_scratch, xgroups, 0x9b6078 - 0x9b3ab8);
extern struct game_sprite_scratch *game_sprite_scratch;
// The scale sprites are drawn at, numerator then denominator, and the pair the
// scaling table was last made for (0x696d18).
extern int32_t *game_sprite_scale;
// The scale as a float (0x9b6624), the largest the game draws (0x66fdc8, a
// double), and the smallest width and height it draws when shrinking
// (0x9b661c, 0x9b6620).
extern float *game_sprite_ratio;
extern double *game_sprite_max_ratio;
extern int32_t *game_sprite_min_size;
// The bytes of picture data the game's Sprites hold (0x9b6618): extract adds what it takes, and a
// Sprite that frees its data takes it off.
extern int32_t *game_sprite_bytes;
// The Sprite the game points at a Buffer's pixels to draw the Buffer as one (0x9b3a50), and the key
// colour a Buffer's draws use when asked for -1 (0x696d14, a byte).
extern struct game_sprite *game_buffer_sprite;
extern uint8_t *game_default_key;

// ---- Textures (texture.c) -------------------------------------------------------

// The game's Texture class (OpenSMACX calls it so): the picture the map's
// terrain is drawn from, which the polygon fills map onto a buffer. `data`
// is `pitch` bytes a row (256 for the fills that wrap), `uv4` the corner
// coordinates of a 4-point polygon and `uv8` those of an 8-point one, each a
// pair of ints (u then v, in pixels). `ready` is set by extract and, for the
// two fills that wrap, must be non-zero.
struct game_texture {
    uint8_t *data;
    int32_t pitch;
    int32_t height;
    int32_t uv4[8];
    int32_t uv8[16];
    int32_t ready;
};
_Static_assert(sizeof(struct game_texture) == 0x70, "layout of the game's Texture");
GAME_AT(struct game_texture, pitch, 4);
GAME_AT(struct game_texture, height, 8);
GAME_AT(struct game_texture, uv4, 0xc);
GAME_AT(struct game_texture, uv8, 0x2c);
GAME_AT(struct game_texture, ready, 0x6c);

// The scratch the polygon fills work in (0x9bb490 to 0x9bb570): the
// originals keep their working values in globals and leave them behind. Named
// by address in texture.c. And the little table the fills' inner loops add
// from (0x6972c4, 0x6972c8) with the limits for the shading values
// (0x6972cc and 0x6972d0, -256 and 256).
extern uint8_t *game_tex_scratch;
extern int32_t *game_tex_tbl;
// The scratch the flat polygon fills (buffer.c) keep their working values in, from
// 0x9bc000 (they use 0x9bc008 to 0x9bc04c).
extern uint8_t *game_poly_scratch;

// ---- Windows (win.c) ----------------------------------------------------------
//
// The game's Win class (OpenSMACX's and Thinker's Win), the base of every window and widget. A
// Win is an object on the game's heap with a vtable; only the top-level window has an HWND of its
// own, and a child is drawn into its parent's buffers. Only the fields Chiron uses are named.
//
// Two rectangles (left, top, right, bottom) give its place: `outer` for a window with a frame
// (flags2 bit 1) and `inner` for one without, which is the one a child's position is counted
// from. `flags` and `flags2` hold the window's kind and state (WIN_F_* and WIN_F2_*).
enum {
    WIN_F_MARGIN_H = 0x4,       // the nonclient margin is added to the height
    WIN_F_MARGIN_W = 0x8,       // and to the width
    WIN_F_CAPTION = 0x10,       // has a caption bar
    WIN_F_CHILD = 0x20,         // positioned relative to its parent
    WIN_F_THICK = 0x400,        // has a thick border on all sides
    WIN_F_DIALOG = 0x1000,      // a dialog: takes the focus of its parent's dialog list
    WIN_F_PARENT_INNER = 0x8000,  // positioned from its parent's inner rectangle
    WIN_F_DISABLED = 0x200000,
    WIN_F_ALWAYS_TOP = 0x2000000,  // keeps its place in a list when another is brought to the front
    WIN_F2_VISIBLE = 0x1,
    WIN_F2_FRAME = 0x2,  // has a frame: placed by `outer`
    WIN_F2_BUSY = 0x8,
};
// Slots of the Win vtable, by byte offset. Subclasses override them.
enum { WIN_VT_DTOR = 0x0, WIN_VT_SHOW = 0x4, WIN_VT_HIDE = 0x8, WIN_VT_RESIZE = 0xc, WIN_VT_NC_HEIGHT = 0x16c };

struct game_win;
// A node of the dialog list a Win keeps: the window it names is at 0x4, and the nodes make a ring.
struct game_win_node {
    int32_t pad0;
    struct game_win *win;
    int32_t pad8;
    struct game_win_node *next, *prev;
    int32_t pad14, pad18;
};
struct game_win_btn;
struct game_win_scroll;
struct game_win {
    void **vtable;
    GAME_PAD(0x4, 0x98);
    uint32_t flags;   // 0x98
    uint32_t flags2;  // 0x9c
    GAME_PAD(0xa0, 0xa8);
    struct game_win *root;  // 0xa8: itself for a top-level window or a widget
    GAME_PAD(0xac, 0xb4);
    void *nc_buf_b4, *nc_buf_b8, *nc_buf_bc, *nc_buf_c0;  // 0xb4: the buffers the nonclient area is drawn into
    struct game_win *parent;  // 0xc4
    void *dlg_vtable;         // 0xc8: the dialog list
    struct game_win_node *dlg_head;  // 0xcc
    struct game_win_node *dlg_cur;   // 0xd0
    int32_t dlg_count;               // 0xd4
    int32_t dlg_index;               // 0xd8: where dlg_cur is in the ring
    GAME_PAD(0xdc, 0xe0);
    const char *title;  // 0xe0: drawn in the caption
    struct game_win_btn *btn_e4, *btn_e8, *btn_ec;  // 0xe4: the caption buttons (the last is the rightmost)
    struct game_win *nc_owner;  // 0xf0: the window whose NC_HEIGHT slot adds to a nonclient position
    int32_t bg_color;           // 0xf4: the palette entry the window's background is painted with
    void *nc_font;                  // 0xf8: the Font the title is written in
    struct game_buffer *nc_tile;    // 0xfc: the picture the frame is tiled from, or NULL for a plain colour
    int32_t nc_color;               // 0x100: that colour
    int32_t text_color[4];          // 0x104: the title's colours (see buffer_set_text_color)
    int32_t caption_h;  // 0x114
    int32_t border;     // 0x118
    int32_t bottom;     // 0x11c: the bottom border, or -1 for the same as `border`
    int32_t box_color[2];  // 0x120: the frame's lines, light and dark
    int32_t cap_btn;  // 0x128: the size of a caption button
    struct game_buffer *bg_pic;  // 0x12c: the picture painted over the background, or NULL
    int32_t bg_center;           // 0x130: the picture is centred (else tiled)
    GAME_PAD(0x134, 0x13c);
    int32_t inner[4];  // 0x13c
    int32_t outer[4];  // 0x14c
    int32_t clip[4];   // 0x15c: what a copy to the screen is cut to
    int32_t title_h;       // 0x16c: the height of the caption area for a hit test
    int32_t resize_border;  // 0x170
    GAME_PAD(0x174, 0x184);
    uint32_t pal_serial;    // 0x184: the serial number of the palette the window last used
    GAME_PAD(0x188, 0x1a4);
    struct game_win *children[150];  // 0x1a4, front first
    int32_t n_children;              // 0x3fc
    void *resize_cb;                 // 0x400: called (cdecl) with the new width and height
    GAME_PAD(0x404, 0x43c);
    struct game_win_scroll *scroll_v, *scroll_h;  // 0x43c, 0x440
};
// A caption button (a Win with more behind it): its width, and the picture it shows.
struct game_win_btn {
    struct game_win win;
    GAME_PAD(0x444, 0x4c4);
    int32_t width, height;  // 0x4c4
    GAME_PAD(0x4cc, 0xab8);
    int32_t picture;  // 0xab8
};
// A scroll bar is a Win with more behind it; the ones a Win points at (0x43c and 0x440). The
// range is `lo` to `hi` (`inverted` counts the position from the far end).
struct game_win_scroll {
    struct game_win win;
    GAME_PAD(0x444, 0x4c4);
    int32_t width, height;  // 0x4c4
    GAME_PAD(0x4cc, 0xa20);
    int32_t lo, hi, inverted;  // 0xa20
    int32_t pos;  // 0xa2c
    GAME_PAD(0xa30, 0xa40);
    int32_t paging;  // 0xa40
    GAME_PAD(0xa44, 0x2144);
    int32_t f2144, f2148;
    GAME_PAD(0x214c, 0x2150);
};
GAME_AT(struct game_win, flags, 0x98);
GAME_AT(struct game_win, flags2, 0x9c);
GAME_AT(struct game_win, root, 0xa8);
GAME_AT(struct game_win, nc_buf_b4, 0xb4);
GAME_AT(struct game_win, nc_buf_c0, 0xc0);
GAME_AT(struct game_win, parent, 0xc4);
GAME_AT(struct game_win, dlg_head, 0xcc);
GAME_AT(struct game_win, dlg_cur, 0xd0);
GAME_AT(struct game_win, dlg_count, 0xd4);
GAME_AT(struct game_win, dlg_index, 0xd8);
GAME_AT(struct game_win, btn_e4, 0xe4);
GAME_AT(struct game_win, btn_ec, 0xec);
GAME_AT(struct game_win, nc_owner, 0xf0);
GAME_AT(struct game_win, bg_color, 0xf4);
GAME_AT(struct game_win, bg_pic, 0x12c);
GAME_AT(struct game_win, bg_center, 0x130);
GAME_AT(struct game_win, clip, 0x15c);
GAME_AT(struct game_win, pal_serial, 0x184);
GAME_AT(struct game_win, title, 0xe0);
GAME_AT(struct game_win, nc_font, 0xf8);
GAME_AT(struct game_win, nc_tile, 0xfc);
GAME_AT(struct game_win, nc_color, 0x100);
GAME_AT(struct game_win, text_color, 0x104);
GAME_AT(struct game_win, box_color, 0x120);
GAME_AT(struct game_win, caption_h, 0x114);
GAME_AT(struct game_win, border, 0x118);
GAME_AT(struct game_win, bottom, 0x11c);
GAME_AT(struct game_win, cap_btn, 0x128);
GAME_AT(struct game_win, inner, 0x13c);
GAME_AT(struct game_win, outer, 0x14c);
GAME_AT(struct game_win, title_h, 0x16c);
GAME_AT(struct game_win, resize_border, 0x170);
GAME_AT(struct game_win, children, 0x1a4);
GAME_AT(struct game_win, n_children, 0x3fc);
GAME_AT(struct game_win, resize_cb, 0x400);
GAME_AT(struct game_win, scroll_v, 0x43c);
GAME_AT(struct game_win, scroll_h, 0x440);
_Static_assert(sizeof(struct game_win) == 0x444, "layout of the game's Win");
GAME_AT(struct game_win_btn, width, 0x4c4);
GAME_AT(struct game_win_btn, picture, 0xab8);
GAME_AT(struct game_win_scroll, width, 0x4c4);
GAME_AT(struct game_win_scroll, height, 0x4c8);
GAME_AT(struct game_win_scroll, lo, 0xa20);
GAME_AT(struct game_win_scroll, pos, 0xa2c);
GAME_AT(struct game_win_scroll, paging, 0xa40);
GAME_AT(struct game_win_scroll, f2144, 0x2144);
GAME_AT(struct game_win_scroll, f2148, 0x2148);

// The function in a Win's vtable at byte offset `slot`.
static inline void *game_win_slot(const struct game_win *w, int slot) { return *(void **)((uint8_t *)w->vtable + slot); }

// The Win class's globals. Modal windows are a stack: the window, and next to it what the
// game kept of the one before (modal_prev holds the top entry's). The parent list is the
// top-level windows, front first, and the z-order list the visible ones the game draws, back
// to front; `opaque` is a window that hides all those under it.
extern struct game_win **game_win_modal;      // the topmost modal window, or NULL
extern int32_t *game_win_modal_n;
extern struct game_win **game_win_modals;     // [modal_n]
extern int32_t *game_win_modal_aux;           // [modal_n], the same stack's second column
extern int32_t *game_win_modal_prev;
extern struct game_win **game_win_modal_focus;
extern struct game_win **game_win_focus, **game_win_def_focus, **game_win_key;
extern struct game_win **game_win_parents;    // [parent_n]
extern int32_t *game_win_parent_n;
extern struct game_win **game_win_zorder;
extern int32_t *game_win_z_n;
extern struct game_win **game_win_opaque;
extern int32_t *game_win_opaque_flag;
extern int32_t *game_win_nc_margin;           // what WIN_F_MARGIN_H and WIN_F_MARGIN_W add
extern struct game_win **game_win_cur;        // the window being sized (or a scroll bar's parent)
extern struct game_win **game_win_capture;    // the window that has the mouse captured
extern struct game_win **game_win_hover;      // the window the mouse is over
extern struct game_win **game_win_press1, **game_win_press2;  // the windows a mouse button went down in
extern struct game_win **game_win_prev_focus; // the dialog focus before the last change
extern uint32_t *game_win_hwnd;               // the game's one HWND
extern int32_t *game_win_screen_w, *game_win_screen_h;
extern int32_t *game_win_pic_normal, *game_win_pic_zoomed;  // the caption button's picture, window normal or maximized
extern int32_t *game_win_mouse_pos;           // [2] (0x9b6628): where the mouse was when the window under it was looked for
extern int32_t *game_win_hit_nc;              // 0x9b7aa4: set when the last hit was on a window's frame
// The Buffers the screen is drawn through. These are objects, not pointers: the back buffer the
// windows are drawn into (0x9b7490) and the one that keeps what is under the overlay (0x9b6f08,
// the overlay: a cursor or the like), where the overlay is on. `back_alt` (0x9b7a68), when set, is another
// Buffer a draw is sent to, with its offset (0x9b7a70, 0x9b7a74).
extern struct game_buffer *game_win_back, *game_win_overlay;
extern struct game_buffer **game_win_back_alt;
extern int32_t *game_win_back_off;            // [2]: x and y (0x9b7a70, 0x9b7a74)
extern int32_t *game_win_overlay_on;          // 0x9b7a2c
extern int32_t *game_win_overlay_pos, *game_win_overlay_size;  // [2] each: where it lies and how big (0x9b7a30, 0x9b7a40)

struct font;
// ---- The screen: how a Buffer reaches the window (screen.c) ----
// The Windows calls it makes, as imported (stdcall).
#define GAME_STD __attribute__((stdcall))
extern void *(GAME_STD *game_get_dc)(void *hwnd);
extern int(GAME_STD *game_release_dc)(void *hwnd, void *dc);
extern int(GAME_STD *game_bit_blt)(void *dst, int x, int y, int w, int h, void *src, int sx, int sy, unsigned rop);
extern int(GAME_STD *game_validate_rect)(void *hwnd, const int32_t *rect);
extern void *(GAME_STD *game_create_solid_brush)(uint32_t color);
extern int(GAME_STD *game_fill_rect)(void *dc, const int32_t *rect, void *brush);
extern void *(GAME_STD *game_create_pen_indirect)(const int32_t *logpen);
extern int(GAME_STD *game_move_to_ex)(void *dc, int x, int y, void *old);
extern int(GAME_STD *game_line_to)(void *dc, int x, int y);
extern void *(GAME_STD *game_select_palette)(void *dc, void *palette, int background);
extern unsigned(GAME_STD *game_realize_palette)(void *dc);
extern int(GAME_STD *game_animate_palette)(void *palette, unsigned start, unsigned n, const void *entries);
extern int(GAME_STD *game_invalidate_rect)(void *hwnd, const int32_t *rect, int erase);
// The window's device context is counted: the first user gets it (from the DirectDraw surface if the
// game has one, else for the window `hwnd`) and the last gives it back.
extern int32_t *game_dc_count;     // 0x9b3ab0
extern void **game_dc;             // 0x9b7b2c
struct game_com {
    void **vtable;  // a COM object: its methods, stdcall with the object first
};
extern struct game_com **game_dd_surface;  // 0x9bc498: the primary surface, or NULL
extern struct game_com **game_dd_palette;  // 0x9bc4a0: the DirectDraw palette
extern uint32_t *game_palette_serial, *game_palette_hold;  // 0x9b8184: the serial of the palette last given to Windows; 0x9b8188
extern int32_t *game_copy_own_palette;  // 0x696bf4: zero lets a Buffer copy use its own palette
// What the screen flip draws over the picture: a callback, a text (lines separated by ^) in a box with
// a border, and its font.
extern void (**game_flip_hook)(void);  // 0x9b7a48
extern char **game_flip_text;          // 0x9b7a50
extern int32_t *game_flip_box;         // 0x9b6e38: left, top, right, bottom
extern int32_t *game_flip_border, *game_flip_text_color, *game_flip_fill;  // 0x696d30, 0x696d2c, 0x9b7a64
extern struct font **game_flip_font;   // 0x9b7a54

// ---- The button classes (button.c) ----
// Every button is a Win (0x444 bytes) with a Buffer behind it (0x444, 0x588 bytes) and the rest of
// a GraphicWin up to 0xa18, where BaseButton's own fields start. PushButton, FlatButton and
// ImageButton derive from BaseButton and add little (ImageButton a picture at 0xab8).
struct game_button_group;
struct game_button {
    struct game_win win;
    struct game_buffer buf;  // 0x444
    GAME_PAD(0x9cc, 0xa18);
    int32_t state;                // 0xa18: 0 up, 1 down (or what the group makes of it)
    uint8_t timer_hold[0x28];     // 0xa1c: a Time object, started to show the bubble text
    int32_t repeat_first, repeat_next;  // 0xa44, 0xa48: the delays of the auto-repeat, -1 for none
    uint8_t timer_repeat[0x28];   // 0xa4c: the auto-repeat's Time object
    int32_t changed;              // 0xa74: set when the group changes it, cleared at the end of a group click
    int32_t id;                   // 0xa78: what the button tells its parent (-1 and -2 call the globals' functions)
    char *name;                   // 0xa7c
    char *bubble;                 // 0xa80: the text shown when the mouse rests on it
    int32_t fill, border_a, border_b, border_w;  // 0xa84: the fill colour, the two border colours and the border's width
    int32_t focus_a, focus_b;     // 0xa94, 0xa98: the colour of the focus frame (-1 for none), and one more
    int32_t pressed;              // 0xa9c: the mouse is down in it
    int32_t press_ms;             // 0xaa0: how long a press shows at least
    uint32_t press_time;          // 0xaa4: timeGetTime when it went down
    struct game_button_group *group;  // 0xaa8
    int32_t disabled;             // 0xaac: the parent isn't told of a click
    void(__attribute__((cdecl)) * on_click)(int id);  // 0xab0
    void(__attribute__((cdecl)) * on_press)(int id);  // 0xab4
    void *picture;                // 0xab8: ImageButton's picture
};
GAME_AT(struct game_button, buf, 0x444);
GAME_AT(struct game_button, state, 0xa18);
GAME_AT(struct game_button, timer_hold, 0xa1c);
GAME_AT(struct game_button, repeat_first, 0xa44);
GAME_AT(struct game_button, timer_repeat, 0xa4c);
GAME_AT(struct game_button, changed, 0xa74);
GAME_AT(struct game_button, id, 0xa78);
GAME_AT(struct game_button, name, 0xa7c);
GAME_AT(struct game_button, bubble, 0xa80);
GAME_AT(struct game_button, fill, 0xa84);
GAME_AT(struct game_button, border_w, 0xa90);
GAME_AT(struct game_button, focus_a, 0xa94);
GAME_AT(struct game_button, pressed, 0xa9c);
GAME_AT(struct game_button, press_ms, 0xaa0);
GAME_AT(struct game_button, press_time, 0xaa4);
GAME_AT(struct game_button, group, 0xaa8);
GAME_AT(struct game_button, disabled, 0xaac);
GAME_AT(struct game_button, on_click, 0xab0);
GAME_AT(struct game_button, on_press, 0xab4);
GAME_AT(struct game_button, picture, 0xab8);

// A PushButton is a BaseButton with a frame of sprites for the button as it lies and as it is pressed (the
// first is `picture`, at 0xab8): the four edges and corners (the ones for the pressed button at 0xadc and
// 0xaec), and a tile for the middle (0xad8, 0xafc).
struct game_push_button {
    struct game_button base;  // (up to and including picture, the left edge, at 0xab8)
    struct game_sprite *right, *top, *bottom;       // 0xabc
    struct game_sprite *tl, *tr, *bl, *br, *mid;    // 0xac8
    struct game_sprite *pleft, *pright, *ptop, *pbottom;  // 0xadc: pressed
    struct game_sprite *ptl, *ptr, *pbl, *pbr, *pmid;     // 0xaec
};
GAME_AT(struct game_push_button, right, 0xabc);
GAME_AT(struct game_push_button, tl, 0xac8);
GAME_AT(struct game_push_button, mid, 0xad8);
GAME_AT(struct game_push_button, pleft, 0xadc);
GAME_AT(struct game_push_button, ptl, 0xaec);
GAME_AT(struct game_push_button, pmid, 0xafc);

// A CheckButton: a Win with a Buffer behind it like the others, a checked state, and two pictures.
struct game_check_button {
    struct game_win win;
    struct game_buffer buf;  // 0x444
    GAME_PAD(0x9cc, 0xa14);
    int32_t state;  // 0xa14: checked
    int32_t id;     // 0xa18
    void(__attribute__((cdecl)) * on_click)(int id);         // 0xa1c
    void(__attribute__((cdecl)) * on_double_click)(int id);  // 0xa20
    void *pic_off, *pic_on;                                  // 0xa24, 0xa28
};
GAME_AT(struct game_check_button, state, 0xa14);
GAME_AT(struct game_check_button, id, 0xa18);
GAME_AT(struct game_check_button, on_click, 0xa1c);
GAME_AT(struct game_check_button, on_double_click, 0xa20);
GAME_AT(struct game_check_button, pic_on, 0xa28);

// A ButtonGroup: up to 32 buttons, of which at most one is selected (by its id).
struct game_button_group {
    struct game_button *buttons[32];
    int32_t count;   // 0x80
    int32_t sel;     // 0x84: the selected button's id, -1 for none, -2 at the start
    int32_t flags;   // 0x88: 1 lets a click on the selected one be a click, 2 deselects it
    void *user;      // 0x8c: passed to the parent's handler
    int32_t silent;  // 0x90: the parent isn't told while set
};
_Static_assert(sizeof(struct game_button_group) == 0x94, "ButtonGroup");
GAME_AT(struct game_button_group, count, 0x80);
GAME_AT(struct game_button_group, sel, 0x84);
GAME_AT(struct game_button_group, flags, 0x88);
GAME_AT(struct game_button_group, user, 0x8c);
GAME_AT(struct game_button_group, silent, 0x90);

// The Dialog class's list of items, as CheckBox and RadioButton hold it as a virtual base: the
// positions are from the start of the Dialog object. The list is a ring of nodes with the
// item's id at 0x4. `sel` is the selected position (RadioButton) or a mask of the checked ones
// (CheckBox); `hover` is the item under the mouse, or -1.
struct game_dlg_node {
    int32_t pad0, key, pad8;
    struct game_dlg_node *next, *prev;
};
struct game_dlg {
    GAME_PAD(0x0, 0xc4);
    struct game_dlg_node *head;  // 0xc4
    struct game_dlg_node *cur;   // 0xc8
    int32_t count;               // 0xcc
    int32_t idx;                 // 0xd0: where `cur` is
    GAME_PAD(0xd4, 0xec);
    int32_t sel;    // 0xec
    int32_t hover;  // 0xf0
};
GAME_AT(struct game_dlg, head, 0xc4);
GAME_AT(struct game_dlg, count, 0xcc);
GAME_AT(struct game_dlg, idx, 0xd0);
GAME_AT(struct game_dlg, sel, 0xec);
GAME_AT(struct game_dlg, hover, 0xf0);

// The defaults a new button takes (0x69704c): the fill, the two border colours and the border
// width, how long a press shows, and the text colours for the four Buffer colour slots, each
// with the three the Buffer takes. (set_def_text_color sets the first of each, ..._2 the second.)
struct game_button_defaults {
    int32_t fill, border_a, border_b, border_w, press_ms;
    int32_t text[4][3];
};
_Static_assert(sizeof(struct game_button_defaults) == 0x44, "button defaults");
extern struct game_button_defaults *game_button_defaults;
// What a new button takes (0x9b8e2c): the two numbers its ctor gives (focus_a, focus_b), and the
// three Fonts init gives its Buffer (the first and the two after it, set by set_def_font).
struct game_button_fonts {
    int32_t focus_a, focus_b;
    void *def, *def_a, *def_b;
};
extern struct game_button_fonts *game_button_fonts;
// The functions called when a button with id -1 or -2 is clicked.
extern void(__attribute__((cdecl)) * *game_button_hooks)(void);  // [2]
// Whether the mouse is inside the window that has it pressed.
extern int32_t *game_win_press_inside;
// A window being moved or sized with the mouse: the window (0x9b7aac), the frame hit code that
// started it (0x9b7aa8), and where the mouse was (0x9b7ab0, two numbers).
extern struct game_win **game_win_track;
extern int32_t *game_win_track_code, *game_win_track_pos;
// The functions the game may have set to hear of input (cdecl, each NULL if unset): [0] after every
// key or button event (no arguments), [1] a key went down (its code), [2] the left and [3] the right
// button went down (x, y). The array starts at 0x9b7a88.
extern void **game_win_hooks;
// What the window procedure (0x5f0650) uses besides the above: BeginPaint, EndPaint, GetWindowLong and SetWindowLong
// as imported (stdcall); the Win whose mouse-move event was told last (0x9b7a7c); a flag the mouse move clears
// (0x9b7b3c); the Time a 0x401 message is let through for (0x9b90d0, or NULL for any); and the functions it
// calls that are left with the original: update_cursor (0x5f1820, cdecl) and do_tracking (0x5f7580, stdcall).
extern void *(__attribute__((stdcall)) * game_begin_paint)(void *hwnd, void *paintstruct);
extern int(__attribute__((stdcall)) * game_end_paint)(void *hwnd, const void *paintstruct);
extern long(__attribute__((stdcall)) * game_get_window_long)(void *hwnd, int index);
extern long(__attribute__((stdcall)) * game_set_window_long)(void *hwnd, int index, long value);
extern struct game_win **game_win_over;
extern int32_t *game_win_moved;
extern void **game_time_current;
// A list of rectangles a window keeps behind its base fields at 0x8f4: the index of the last one
// that holds the point, or -1; its fields at 0x14 and 0x10 go to the two pointers if given.
extern int(__attribute__((thiscall)) * game_spot_check)(void *list, int x, int y, int *field14, int *field10);
// The colour table (or NULL) that a window copied from its parent is darkened with (0x9b3390).
extern const uint8_t **game_win_tint;

// ---- FlatButton, CheckBox, RadioButton and ListBox drawing (button.c, scroll.c) ----
// What a Dialog's string list node's item points to: the text at 0x4, and the number of lines the text
// wraps to at 0x8 (the CheckBox's and ListBox's layout calculation sets it for a RadioButton).
struct game_list_item {
    int32_t f0;
    char *text;
    int32_t lines;
    int32_t rows;  // 0xc: how many rows the item takes in a ListBox
};
_Static_assert(sizeof(struct game_list_item) == 0x10, "layout of a Dialog item");
// A FlatButton is a BaseButton whose `picture` field (0xab8) says which of three looks to draw (0 as it lies,
// 1 changed, 2 pressed or down; anything else lets the button work it out), and which has a text colour, a
// frame, a picture and the eight pieces of a frame of sprites for each look, in this order.
struct game_flat_button {
    struct game_button base;  // (`picture` is the look, at 0xab8)
    int32_t text_color[3];    // 0xabc: the name's colour, or -1
    struct game_box_frame *frame[3];  // 0xac8
    struct game_sprite *pic[3];       // 0xad4
    struct game_sprite *left[3], *right[3], *top[3], *bottom[3];  // 0xae0
    struct game_sprite *tl[3], *tr[3], *bl[3], *br[3], *mid[3];   // 0xb10
};
_Static_assert(sizeof(struct game_flat_button) == 0xb4c, "layout of the game's FlatButton");
GAME_AT(struct game_flat_button, text_color, 0xabc);
GAME_AT(struct game_flat_button, frame, 0xac8);
GAME_AT(struct game_flat_button, pic, 0xad4);
GAME_AT(struct game_flat_button, left, 0xae0);
GAME_AT(struct game_flat_button, tl, 0xb10);
GAME_AT(struct game_flat_button, mid, 0xb40);
// The fill colour of a FlatButton (a byte at 0x696d14), and the two strings it ends a name too long for it
// with (the first is measured, the second written), at 0x697090 and 0x697094.
extern uint8_t *game_flat_fill;
extern char *game_flat_dots_a, *game_flat_dots_b;
// The ListBox's draw counter (0x9b8ee8: how many items its redraw has drawn), the box an item's
// function (0x30) was last called for (0x9b8ee4), and the width the scroll bar takes from a box (0x9b8dd4).
extern int32_t *game_listbox_row, *game_listbox_cur, *game_listbox_bar;

// ---- The Scroll, ListBox and StringBox classes (scroll.c) ----
// A scroll bar is a Win with a Buffer behind it like a button (GraphicWin, up to 0xa14), its own fields,
// and two ImageButtons for the arrows (0xaac and 0x15f8, 0xb4c bytes each), then two functions a
// screen may set for the bar (the clicks, and the dragging; Win_UNK8 and Win_UNK9 set them). A bar is
// horizontal when its Buffer is wider than it is high. The position runs from `lo` to `hi`; an
// inverted bar (a range given high to low) keeps the mirror of what its parent is told.
struct game_scroll {
    struct game_win win;
    struct game_buffer buf;  // 0x444
    GAME_PAD(0x9cc, 0xa14);
    uint32_t flags;       // 0xa14: 1 lets it be seen through, 2 gives the thumb the bar's own width
    int32_t id;           // 0xa18: what it tells its parent
    int32_t border;       // 0xa1c: the colour of the frame, or -1
    int32_t lo, hi;       // 0xa20
    int32_t inverted;     // 0xa28
    int32_t pos;          // 0xa2c
    int32_t delay;        // 0xa30: how long a button is held before it repeats (ms)
    int32_t rate;         // 0xa34: repeats a second
    uint32_t press_time;  // 0xa38: timeGetTime when a track click began, 0 for none
    int32_t drag;         // 0xa3c: where in the bar the mouse took the thumb, -1 for none
    int32_t paging;       // 0xa40: what Page Up and Page Down move by
    int32_t dir;          // 0xa44: -1 or 1 while a click on the track is held, else 0
    int32_t f48;          // 0xa48
    int32_t bar[4];       // 0xa4c: the thumb (left, top, right, bottom)
    int32_t button_color; // 0xa5c: the arrows' fill
    int32_t thickness;    // 0xa60: the bar's width
    int32_t back;         // 0xa64: the fill of the bar and the thumb
    int32_t bevel;        // 0xa68: lines of bevel round the thumb
    int32_t bevel_upper;  // 0xa6c
    int32_t bevel_lower;  // 0xa70
    struct game_sprite *grip;  // 0xa74: a picture centred on the thumb
    struct game_sprite *tile;  // 0xa78: a picture tiled over the bar
    int32_t spr[4][3];    // 0xa7c: the pictures for the arrows (left, right, up, down), three numbers each
    uint8_t btn_a[0xb4c];  // 0xaac
    uint8_t btn_b[0xb4c];  // 0x15f8: the second arrow
    void(__attribute__((cdecl)) * on_click)(int id, int pos);  // 0x2144
    void(__attribute__((cdecl)) * on_drag)(int id, int pos);   // 0x2148
    GAME_PAD(0x214c, 0x2150);
};
_Static_assert(sizeof(struct game_scroll) == 0x2150, "layout of the game's Scroll");
GAME_AT(struct game_scroll, flags, 0xa14);
GAME_AT(struct game_scroll, id, 0xa18);
GAME_AT(struct game_scroll, border, 0xa1c);
GAME_AT(struct game_scroll, lo, 0xa20);
GAME_AT(struct game_scroll, inverted, 0xa28);
GAME_AT(struct game_scroll, pos, 0xa2c);
GAME_AT(struct game_scroll, delay, 0xa30);
GAME_AT(struct game_scroll, press_time, 0xa38);
GAME_AT(struct game_scroll, drag, 0xa3c);
GAME_AT(struct game_scroll, paging, 0xa40);
GAME_AT(struct game_scroll, dir, 0xa44);
GAME_AT(struct game_scroll, bar, 0xa4c);
GAME_AT(struct game_scroll, button_color, 0xa5c);
GAME_AT(struct game_scroll, thickness, 0xa60);
GAME_AT(struct game_scroll, back, 0xa64);
GAME_AT(struct game_scroll, bevel, 0xa68);
GAME_AT(struct game_scroll, grip, 0xa74);
GAME_AT(struct game_scroll, spr, 0xa7c);
GAME_AT(struct game_scroll, btn_a, 0xaac);
GAME_AT(struct game_scroll, btn_b, 0x15f8);
GAME_AT(struct game_scroll, on_click, 0x2144);
GAME_AT(struct game_scroll, on_drag, 0x2148);
// The flag the scroll bars and list boxes share (0x9b8ddc): a bar sets it when a key moved it and
// clears it when an arrow or the thumb did; a list box then selects the item its bar moved to.
extern int32_t *game_scroll_by_key;
// The flag ListBox's right click raises while it runs the left click's handler (0x9b8eec).
extern int32_t *game_listbox_right;
// GraphicWin_fill_rect (0x5d5350): fill a rectangle of a window's Buffer, from the parent's if it can be
// seen through; and the two Win functions that tell both scroll bars the wheel turned (0x5f85b0: down,
// 0x5f85e0: up), each with whether to tell the parent.
extern void(__attribute__((thiscall)) * game_graphic_win_fill_rect)(void *win, const int32_t *rect, int color);
extern void(__attribute__((thiscall)) * game_win_wheel_down)(void *win, int notify);
extern void(__attribute__((thiscall)) * game_win_wheel_up)(void *win, int notify);

// A StringBox is a Win (the text of a message, with links) whose scroll offset is at 0xa1c.
struct game_stringbox {
    struct game_win win;
    GAME_PAD(0x444, 0xa1c);
    int32_t scroll;  // 0xa1c
};
GAME_AT(struct game_stringbox, scroll, 0xa1c);

// A ListBox's own part. The object has a pointer to its vbtable at 0; its virtual bases are the Win
// (a GraphicWin, vbtable entry 1, at 0x48) and the Dialog (entry 2, at 0xa60), whose list holds the
// items: `sel` is the selected position, `hover` the one under the mouse. A handler the Win's vtable
// holds gets the Win, 0x48 into the box.
struct game_listbox {
    int32_t *vbptr;        // 0x0
    int32_t f4;
    int32_t force;         // 0x8: select even what is selected already
    int32_t flags;         // 0xc
    int32_t f10, f14;
    int32_t top;           // 0x18: the first item shown
    int32_t visible;       // 0x1c: how many items show
    int32_t f20, f24, f28, f2c, f30, f34;
    void *f38;
    void(__attribute__((cdecl)) * on_select)(int id);  // 0x3c: told of each selection
    void(__attribute__((cdecl)) * on_double)(int id);  // 0x40: told of a double click
    GAME_PAD(0x44, 0x48);
};
GAME_AT(struct game_listbox, force, 0x8);
GAME_AT(struct game_listbox, top, 0x18);
GAME_AT(struct game_listbox, visible, 0x1c);
GAME_AT(struct game_listbox, on_select, 0x3c);
GAME_AT(struct game_listbox, on_double, 0x40);

// ---- The EditBox, EditGroup, StringStruct, StringList, Strings and Time classes (edit.c, strings.c) ----
// A Time object (OpenSMACX's Time): a repeating or one-shot timer a window or button owns. `flags` bit 0
// says a pulse made with SetTimer (see time_pulse_3), bit 1 that it has fired once; `id` is what
// timeSetEvent or SetTimer gave back (0 for none). A delay below 0x32 ms uses a multimedia timer, a longer
// one SetTimer with the object as the id; either way the game's own procedures post message 0x401 to the
// window, whose handler calls `cb` (with `arg`) and `cb2` (with `arg` and `arg2`).
struct game_time {
    uint32_t flags;                                          // 0x0
    uint32_t id;                                             // 0x4
    void(__attribute__((cdecl)) * cb)(void *arg);            // 0x8
    void(__attribute__((cdecl)) * cb2)(void *arg, int arg2);  // 0xc
    int32_t arg2;                                            // 0x10
    void *arg;                                               // 0x14
    uint32_t delay;                                          // 0x18
    int32_t fired;                                           // 0x1c: the message is posted and not yet handled
    int32_t resolution;                                      // 0x20: timeSetEvent's resolution (5 by default)
    int32_t f24;                                             // 0x24
};
_Static_assert(sizeof(struct game_time) == 0x28, "layout of the game's Time");
GAME_AT(struct game_time, id, 0x4);
GAME_AT(struct game_time, cb, 0x8);
GAME_AT(struct game_time, cb2, 0xc);
GAME_AT(struct game_time, arg2, 0x10);
GAME_AT(struct game_time, arg, 0x14);
GAME_AT(struct game_time, delay, 0x18);
GAME_AT(struct game_time, fired, 0x1c);
GAME_AT(struct game_time, resolution, 0x20);
// The function that takes and dispatches the game's own 0x401 messages (0x5fd370), the flag it sets
// (0x9b7b9c), and the two procedures a timer calls (0x616820 for a multimedia timer, 0x6167e0 for SetTimer).
extern void(__attribute__((cdecl)) * game_time_flush)(void);
extern uint32_t *game_time_flush_flag;
extern void *game_time_mm_proc, *game_time_timer_proc;
extern uint32_t(__attribute__((stdcall)) * game_time_set_event)(uint32_t delay, uint32_t res, void *proc, void *user, uint32_t flags);
extern uint32_t(__attribute__((stdcall)) * game_time_kill_event)(uint32_t id);
extern uint32_t(__attribute__((stdcall)) * game_set_timer)(uint32_t hwnd, uint32_t id, uint32_t ms, void *proc);
extern int(__attribute__((stdcall)) * game_kill_timer)(uint32_t hwnd, uint32_t id);
extern int(__attribute__((stdcall)) * game_message_beep)(unsigned type);

// An EditBox: a Win with a Buffer behind it (a GraphicWin, up to 0xa14), a line of text, and a caret.
// `anchor` and `caret` are positions in the text; text between them is selected. `first` is the
// first character shown. `mods` is which of Shift (1) and Ctrl (2) the last key had, `flags` bit 0
// says the box passes the dialog focus on to the parent's next and bit 1 that it shows asterisks.
struct game_editbox {
    struct game_win win;
    struct game_buffer buf;  // 0x444
    GAME_PAD(0x9cc, 0xa14);
    char text[0x100];            // 0xa14
    uint32_t max_len;            // 0xb14
    int32_t first;               // 0xb18
    int32_t color;               // 0xb1c: the text's colour, and the frame's
    int32_t sel_text;            // 0xb20: the selected text's colour (-1 for `color`)
    int32_t sel_back;            // 0xb24: the selection's background (-1 for the Buffer's own)
    int32_t border_a, border_b;  // 0xb28, 0xb2c: the lines of the frame
    int32_t border_w;            // 0xb30: how many
    int32_t pad;                 // 0xb34: space inside the frame
    int32_t mods;                // 0xb38
    int32_t flags;               // 0xb3c
    int32_t anchor;              // 0xb40
    int32_t caret;               // 0xb44
    int32_t blink;               // 0xb48: the caret is drawn
    struct game_time timer;      // 0xb4c: the caret's blink
};
_Static_assert(sizeof(struct game_editbox) == 0xb74, "layout of the game's EditBox");
GAME_AT(struct game_editbox, buf, 0x444);
GAME_AT(struct game_editbox, text, 0xa14);
GAME_AT(struct game_editbox, max_len, 0xb14);
GAME_AT(struct game_editbox, first, 0xb18);
GAME_AT(struct game_editbox, color, 0xb1c);
GAME_AT(struct game_editbox, sel_text, 0xb20);
GAME_AT(struct game_editbox, sel_back, 0xb24);
GAME_AT(struct game_editbox, border_a, 0xb28);
GAME_AT(struct game_editbox, border_w, 0xb30);
GAME_AT(struct game_editbox, pad, 0xb34);
GAME_AT(struct game_editbox, mods, 0xb38);
GAME_AT(struct game_editbox, flags, 0xb3c);
GAME_AT(struct game_editbox, anchor, 0xb40);
GAME_AT(struct game_editbox, caret, 0xb44);
GAME_AT(struct game_editbox, blink, 0xb48);
GAME_AT(struct game_editbox, timer, 0xb4c);
// The two windows that have a mouse button down in an EditBox (0x9b7abc and 0x9b7ac0).
extern struct game_win **game_edit_press;  // [2]

// An EditGroup: up to ten EditBoxes with labels, in one dialog. It has a pointer to its vbtable at 0,
// and a Win (entry 1, at 0x8c when it is by itself) and a Dialog (entry 2, at 0xaa4) as virtual bases.
// The Dialog's list at 0xbc holds the labels. `text` are the boxes' default texts, `limit` their maximum
// lengths; label_w and edit_w the widest label and the widest text with its padding.
struct game_editgroup {
    int32_t *vbptr;                 // 0x0
    struct game_editbox *edit[10];  // 0x4
    char *text[10];                 // 0x2c
    int32_t limit[10];              // 0x54
    int32_t label_w, edit_w;        // 0x7c, 0x80
    int32_t f84;                    // 0x84
};
_Static_assert(sizeof(struct game_editgroup) == 0x88, "layout of the game's EditGroup");
GAME_AT(struct game_editgroup, edit, 0x4);
GAME_AT(struct game_editgroup, text, 0x2c);
GAME_AT(struct game_editgroup, limit, 0x54);
GAME_AT(struct game_editgroup, label_w, 0x7c);
GAME_AT(struct game_editgroup, edit_w, 0x80);

// The string list class (OpenSMACX's StringStruct): a ring of nodes, each with an item and an id.
// `head` is the first node, `cur` the one a walk is at, `idx` where it is (counted from the head),
// `count` how many. A StringList item has its text at 0x4 (and a number each at 0x8 and 0xc). The Dialog
// class (a list box's and an EditGroup's virtual base) has one of these at 0xbc.
struct game_strnode {
    int32_t pad0, id;
    void *item;
    struct game_strnode *next, *prev;
};
_Static_assert(sizeof(struct game_strnode) == 0x14, "layout of a string list node");
GAME_AT(struct game_strnode, id, 0x4);
GAME_AT(struct game_strnode, item, 0x8);
GAME_AT(struct game_strnode, next, 0xc);
GAME_AT(struct game_strnode, prev, 0x10);
struct game_strlist {
    void **vtable;              // 0x0
    void *vbptr;                // 0x4
    struct game_strnode *head;  // 0x8
    struct game_strnode *cur;   // 0xc
    int32_t count;              // 0x10
    int32_t idx;                // 0x14
};
_Static_assert(sizeof(struct game_strlist) == 0x18, "the string list's own part");
GAME_AT(struct game_strlist, head, 0x8);
GAME_AT(struct game_strlist, cur, 0xc);
GAME_AT(struct game_strlist, count, 0x10);
GAME_AT(struct game_strlist, idx, 0x14);
// The string table (Strings, at 0x9b8fcc): `base` and `size` of what it holds.
struct game_strings {
    int32_t f0;
    uint32_t base;  // 0x4
    int32_t f8;
    uint32_t size;  // 0xc
    int32_t f10, f14;
};
GAME_AT(struct game_strings, base, 0x4);
GAME_AT(struct game_strings, size, 0xc);

// Windows calls Win makes (through the import table, so a shadow check's journal sees them).
extern int(__attribute__((stdcall)) * game_set_cursor_pos)(int x, int y);
extern int(__attribute__((stdcall)) * game_get_cursor_pos)(int32_t *point);
extern int(__attribute__((stdcall)) * game_show_window)(uint32_t hwnd, int cmd);
extern int(__attribute__((stdcall)) * game_get_system_metrics)(int index);
extern unsigned(__attribute__((stdcall)) * game_map_virtual_key)(unsigned code, unsigned type);
extern intptr_t(__attribute__((stdcall)) * game_def_window_proc)(uint32_t hwnd, unsigned msg, uintptr_t wparam, intptr_t lparam);
extern int(__attribute__((stdcall)) * game_is_zoomed)(uint32_t hwnd);
extern int(__attribute__((stdcall)) * game_get_window_rect)(uint32_t hwnd, int32_t *rect);

// ---- The Menu (menu bar), PullDown (drop-down menu) and Spot (hit rectangles) classes (menu.c) ----
// A Spot list holds rectangles a screen can be hit in, 0x18 bytes each: the rectangle (left, top, right,
// bottom), a `kind` and an `id` that tell what it is. It has no vtable: a pointer to room for `cap`
// rectangles, and how many are in use. A Menu keeps one for its titles.
struct game_spot_rect {
    int32_t rect[4];
    int32_t kind;  // 0x10
    int32_t id;    // 0x14
};
struct game_spot {
    struct game_spot_rect *rects;
    int32_t cap;
    int32_t count;
};
_Static_assert(sizeof(struct game_spot_rect) == 0x18, "layout of the game's Spot rectangle");

// One line of a drop-down menu (0x14 bytes): the text before a '|', what follows it (the key it is
// shown with), the id, flags (1 shown, 2 greyed, 4 a separator, 8 checked) and, if the text has an '&',
// where the letter after it is (the key that picks the line).
struct game_pd_item {
    char *text;    // 0
    char *tail;    // 4
    int32_t id;    // 8: -1 for a free line
    uint32_t flags;  // 0xc
    char *hotkey;  // 0x10
};
_Static_assert(sizeof(struct game_pd_item) == 0x14, "layout of the game's PullDown line");
// A PullDown is a GraphicWin (a Win and a Buffer, up to 0xa14) with 64 lines. `sel` is the line the
// mouse or the keys are on (counted among the shown lines until the click is read), `shown` how many
// lines are shown, `dirty` that the size has to be worked out again. The Menu that owns it is at 0xf30.
struct game_pulldown {
    struct game_win win;
    struct game_buffer buf;  // 0x444: its `full` rectangle (0x474) is the box a click is looked for in
    GAME_PAD(0x9cc, 0xa14);
    int32_t fa14;
    struct game_pd_item items[64];  // 0xa18
    int32_t ff18;
    int32_t ff1c;
    int32_t count;   // 0xf20: lines in use
    int32_t width;   // 0xf24: the widest line, with its key
    int32_t sel;     // 0xf28: -1 for none
    int32_t shown;   // 0xf2c
    void *menu;      // 0xf30
    uint8_t dirty;   // 0xf34
    GAME_PAD(0xf35, 0xf38);
    int32_t ff38, ff3c;
};
_Static_assert(sizeof(struct game_pulldown) == 0xf40, "layout of the game's PullDown");
GAME_AT(struct game_pulldown, items, 0xa18);
GAME_AT(struct game_pulldown, count, 0xf20);
GAME_AT(struct game_pulldown, width, 0xf24);
GAME_AT(struct game_pulldown, sel, 0xf28);
GAME_AT(struct game_pulldown, shown, 0xf2c);
GAME_AT(struct game_pulldown, menu, 0xf30);
GAME_AT(struct game_pulldown, dirty, 0xf34);
GAME_AT(struct game_pulldown, ff38, 0xf38);

// A title on a menu bar (0x14 bytes): id (-1 for a free one), text, flags (1 shown, 2 greyed, 4 at the
// right end), the letter that picks it and its PullDown.
struct game_menu_entry {
    int32_t id;
    char *text;
    uint8_t flags;
    GAME_PAD(0x9, 0xc);
    char *hotkey;
    struct game_pulldown *pd;
};
_Static_assert(sizeof(struct game_menu_entry) == 0x14, "layout of the game's Menu title");
// A Menu is a GraphicWin with up to 15 titles. `open` is the title whose PullDown is down (-1 for none),
// `proc` the function (cdecl, the id of the line) a pick is told to, `spots` where each title is.
struct game_menu {
    struct game_win win;
    struct game_buffer buf;  // 0x444
    GAME_PAD(0x9cc, 0xa14);
    void(__attribute__((cdecl)) * proc)(int id);  // 0xa14
    int32_t count;  // 0xa18: titles in use
    int32_t fa1c;
    int32_t fa20;
    int32_t open;   // 0xa24
    uint32_t flags;  // 0xa28: 1 keeps the title that is open when the mouse moves, 4 keeps the bar when a pick is made
    struct game_spot spots;  // 0xa2c
    struct game_menu_entry entries[15];  // 0xa38
};
_Static_assert(sizeof(struct game_menu) == 0xb64, "layout of the game's Menu");
GAME_AT(struct game_menu, proc, 0xa14);
GAME_AT(struct game_menu, count, 0xa18);
GAME_AT(struct game_menu, open, 0xa24);
GAME_AT(struct game_menu, flags, 0xa28);
GAME_AT(struct game_menu, spots, 0xa2c);
GAME_AT(struct game_menu, entries, 0xa38);
// The Font a PullDown measures and draws with (0x9b7b68) and a Menu's (0x9b7b94): each is set from the
// game's default font (game_font_default) the first time it is wanted. The gap above a PullDown's first
// line (0x696e1c), and the strings its widths are worked out with (four bytes apart from 0x696e20).
extern void **game_pd_font, **game_menu_font;
extern int32_t *game_pd_margin;
extern const char *game_pd_gap;  // (the strings are 4 bytes apart)
// What a PullDown is drawn with: seven numbers from 0x696e04 (the left space of a line's text, the fill colour, the
// text colours of the selected line and of a greyed one, the frame's two colours and the margin, which is
// game_pd_margin); four from 0x9b7b58 (the two functions that can take over the drawing, cdecl, then the selected
// line's background and the text colour); and the sprite of a checked line's mark. A Menu's: the fill, the title and
// greyed title text colours (0x696e30), the open title's background and the title text colour (0x9b7b8c), and the
// string whose width is the space round the titles (0x696e5c).
extern int32_t *game_pd_style;
extern void **game_pd_hooks;
extern struct game_sprite **game_pd_check;
extern int32_t *game_menu_colors, *game_menu_colors2;
extern const char *game_menu_gap;

// ---- The Dialog, SpriteBox and BasePop (popup) classes (dialog.c) ----
// A node of the list a Dialog or a SpriteBox keeps (the game's StringStruct class): the key (the id)
// at 0x4, the item at 0x8, the next and the previous node of the ring.
struct game_dlist_node {
    void **vtable;
    int32_t key;
    void *item;
    struct game_dlist_node *next, *prev;
};
// The list object itself (0x1c bytes). Its vtable's slot 1 (0x4) is called with each item as the list
// is emptied; the item and the node are then deleted through their own vtables. `idx` says where
// `cur` is, and `buf` is the Dialog's data (or what a caller gave it).
struct game_dlist {
    void **vtable;  // 0x0
    void *vbptr;    // 0x4
    struct game_dlist_node *head, *cur;  // 0x8, 0xc
    int32_t count;  // 0x10
    int32_t idx;    // 0x14
    void *buf;      // 0x18
};
_Static_assert(sizeof(struct game_dlist) == 0x1c, "layout of the game's list");
GAME_AT(struct game_dlist, head, 0x8);
GAME_AT(struct game_dlist, count, 0x10);
GAME_AT(struct game_dlist, buf, 0x18);
// The Dialog class: a data block (the first 0x14 bytes from 0x4: a flag byte, the data, a cursor, its
// size and what is left), the position and size, the fonts and text colours its items take (`color` has
// four rows, one for each of the three text colours' four numbers: row i, colour j at 0x7c + 12 * i +
// 4 * j) and a list of items at 0xbc.
struct game_dialog {
    void **vtable;     // 0x0
    uint8_t data_flag; // 0x4
    GAME_PAD(0x5, 0x8);
    void *data, *cursor;  // 0x8, 0xc
    int32_t data_size, data_left;  // 0x10, 0x14
    void *ext;         // 0x18: a data block a caller gave
    int32_t size;      // 0x1c: bytes to make the data (-1 for none)
    uint32_t flags;    // 0x20
    int32_t x, y, w, h;  // 0x24
    int32_t v34, v38, v3c, v40, v44, v48, v4c, v50;  // 0x34
    int32_t v54[6];    // 0x54
    int32_t v6c;       // 0x6c
    void *font;        // 0x70
    int32_t font_a, font_b;  // 0x74
    int32_t color[4][3];  // 0x7c
    int32_t vac, vb0, vb4, vb8;  // 0xac
    struct game_dlist list;  // 0xbc
    GAME_PAD(0xd8, 0xe4);
    void *ve4;         // 0xe4
    int32_t ve8;       // 0xe8
    int32_t sel, hover;  // 0xec, 0xf0
};
GAME_AT(struct game_dialog, data, 0x8);
GAME_AT(struct game_dialog, ext, 0x18);
GAME_AT(struct game_dialog, size, 0x1c);
GAME_AT(struct game_dialog, flags, 0x20);
GAME_AT(struct game_dialog, v34, 0x34);
GAME_AT(struct game_dialog, v54, 0x54);
GAME_AT(struct game_dialog, font, 0x70);
GAME_AT(struct game_dialog, color, 0x7c);
GAME_AT(struct game_dialog, vac, 0xac);
GAME_AT(struct game_dialog, list, 0xbc);
GAME_AT(struct game_dialog, sel, 0xec);
// What a new Dialog takes: 0x697098 (the width, three more numbers, the colours, the flags) and
// 0x9b8eb4 (the fonts and the rest).
struct game_dialog_defs_a {
    int32_t w, v44, v4c, v50, v6c;  // 0x697098
    int32_t color[4][3];  // 0x6970ac
    int32_t flags;        // 0x6970dc
};
struct game_dialog_defs_b {
    int32_t h, v34, v40;  // 0x9b8eb4
    void *font;           // 0x9b8ec0
    int32_t font_a, font_b;
    int32_t vac, vb0, vb4, vb8;  // 0x9b8ecc
};
_Static_assert(sizeof(struct game_dialog_defs_a) == 0x48, "Dialog defaults");
_Static_assert(sizeof(struct game_dialog_defs_b) == 0x28, "Dialog defaults");
extern struct game_dialog_defs_a *game_dialog_defs_a;
extern struct game_dialog_defs_b *game_dialog_defs_b;
// A new BasePop's defaults: the text colours (four numbers each, in four rows), then the button
// colours (three numbers each, in four rows); and the fonts at 0x9b8d98.
struct game_pop_defs {
    int32_t string[4][4];  // 0x696ee4
    int32_t button[4][3];  // 0x696f24
};
struct game_pop_fonts {
    void *string_font;  // 0x9b8d98
    int32_t string_a, string_b, string_c;
    void *button_font;  // 0x9b8da8
    int32_t button_a, button_b;
};
_Static_assert(sizeof(struct game_pop_defs) == 0x70, "BasePop defaults");
_Static_assert(sizeof(struct game_pop_fonts) == 0x1c, "BasePop fonts");
extern struct game_pop_defs *game_pop_defs;
extern struct game_pop_fonts *game_pop_fonts;
// Whether a popup is up (0x9b8cfc, which its fallout sets when 0x9b8d00 is), the default external window
// (three numbers at 0x9b8d04 and two at 0x696ec4), and the file name popups read their text from
// (game_pop_file, up to twelve characters).
extern int32_t *game_pop_open, *game_pop_fallout_on, *game_pop_ext_a, *game_pop_ext_b;

// The pieces of a popup (BasePop, 0x3230 bytes), as far as the functions in dialog.c reach: its flags
// (0x20, and the popup's own at 0x30a8: 0x100 keeps it from being redrawn by a double click, 0x400 stops
// the screen scaling, 0x4000 takes keys as clicks), the external window it is drawn over (0xa14, with
// four numbers), the answer (0xa40 and 0x3100), a Dialog at 0x21d0 that holds the buttons (its vbase
// through the word at 0x21d0) and a CheckBox at 0x2228 (the same), the default button (0x2350), the
// Spot list of its buttons (0x3098), where it goes (0x30f8, 0x30fc), the string and button fonts and
// text colours it takes (0x3110 and 0x316c, colours at 0x3120 and 0x3178).
struct game_base_pop {
    void **vtable;  // 0x0
    GAME_PAD(0x4, 0x20);
    uint32_t f20;   // 0x20: bit 0 and bit 1 are set by UNK3 and UNK4
    GAME_PAD(0x24, 0xa14);
    void *ext;      // 0xa14
    int32_t ext_a, ext_b, ext_c, ext_d;  // 0xa18
    GAME_PAD(0xa28, 0xa40);
    int32_t answer;  // 0xa40
    GAME_PAD(0xa44, 0x21d0);
    int32_t *dlg_vbtable;  // 0x21d0
    GAME_PAD(0x21d4, 0x2228);
    int32_t *chk_vbtable;  // 0x2228
    GAME_PAD(0x222c, 0x2350);
    int32_t def_button;    // 0x2350
    GAME_PAD(0x2354, 0x3098);
    struct game_spot spots;  // 0x3098
    GAME_PAD(0x30a4, 0x30a8);
    uint32_t flags;          // 0x30a8
    GAME_PAD(0x30ac, 0x30f8);
    int32_t x, y, result;    // 0x30f8
    GAME_PAD(0x3104, 0x3110);
    void *string_font;       // 0x3110
    int32_t string_a, string_b, string_c;  // 0x3114
    int32_t string_color[4][4];  // 0x3120
    GAME_PAD(0x3160, 0x316c);
    void *button_font;       // 0x316c
    int32_t button_a, button_b;  // 0x3170
    uint8_t button_flag[3];  // 0x3178
    GAME_PAD(0x317b, 0x317c);
    int32_t button_color[3][3];  // 0x317c
    GAME_PAD(0x31a0, 0x3230);
};
_Static_assert(sizeof(struct game_base_pop) == 0x3230, "layout of the game's BasePop");
GAME_AT(struct game_base_pop, ext, 0xa14);
GAME_AT(struct game_base_pop, answer, 0xa40);
GAME_AT(struct game_base_pop, dlg_vbtable, 0x21d0);
GAME_AT(struct game_base_pop, chk_vbtable, 0x2228);
GAME_AT(struct game_base_pop, def_button, 0x2350);
GAME_AT(struct game_base_pop, spots, 0x3098);
GAME_AT(struct game_base_pop, flags, 0x30a8);
GAME_AT(struct game_base_pop, x, 0x30f8);
GAME_AT(struct game_base_pop, string_font, 0x3110);
GAME_AT(struct game_base_pop, string_color, 0x3120);
GAME_AT(struct game_base_pop, button_font, 0x316c);
GAME_AT(struct game_base_pop, button_flag, 0x3178);
GAME_AT(struct game_base_pop, button_color, 0x317c);
// A SpriteBox (a Dialog-like list of sprites): a Spot list at 0x10, its sprites in a list at 0x24, the functions
// a screen set to hear of the mouse (cdecl, given the id of the sprite; 0x68 left click, 0x6c left down, 0x70
// left up, 0x74 right click, 0x78 right down, 0x7c right up, 0x80 and 0x84 the double clicks) and its Win at
// 0x8c (the mouse functions are called with that, the way the Win class's events are).
struct game_sprite_box {
    void **vtable;  // 0x0
    GAME_PAD(0x4, 0x10);
    struct game_spot spot;  // 0x10
    GAME_PAD(0x1c, 0x24);
    struct game_dlist list;  // 0x24
    GAME_PAD(0x40, 0x68);
    int(__attribute__((cdecl)) * fn[8])(int id);  // 0x68
    GAME_PAD(0x88, 0x8c);
};
_Static_assert(sizeof(struct game_sprite_box) == 0x8c, "layout of the game's SpriteBox");
GAME_AT(struct game_sprite_box, spot, 0x10);
GAME_AT(struct game_sprite_box, list, 0x24);
GAME_AT(struct game_sprite_box, fn, 0x68);
// The Dialog's data block: closed (freeing it) and made with room for a number of bytes (the answer is 0, or 1
// when there was no memory).
extern void(__attribute__((thiscall)) * game_dialog_data_close)(void *data);
extern int(__attribute__((thiscall)) * game_dialog_data_alloc)(void *data, int size);
extern void *(__attribute__((thiscall)) * game_string_alloc)(void *arena, int size);  // 0x5d4680: room for a text in an arena
extern int(__attribute__((thiscall)) * game_strlist_add)(void *list, int arg);        // StringStruct_add (not reimplemented)

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
