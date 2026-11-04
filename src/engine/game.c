#include "game.h"

#include "code.h"

struct rules_tech *game_tech;
uint8_t *game_tech_achieved;
int32_t *game_expansion;
int32_t *game_turn;

struct faction_data *game_factions;
struct faction_def *game_faction_defs;
uint8_t *game_human_factions;
struct unit_proto *game_protos;
struct rules_weapon *game_weapons;
struct rules_armor *game_armor;
struct rules_reactor *game_reactors;
struct rules_chassis *game_chassis;
struct rules_facility *game_facilities;
struct continent *game_continents;
int32_t *game_tech_discovery_rate;
uint32_t *game_rules;
int32_t *game_difficulty;
int32_t *game_map_area_sqrt;
int32_t *game_cloud_cover;
int32_t *game_language;
int32_t *game_parse_gender, *game_parse_plural;
char ***game_labels;
void *game_strings;
char *game_tech_name_buffer;

struct map_tile **game_map;
int32_t *game_map_width, *game_map_height, *game_map_half_width;
uint32_t *game_map_flat;
int32_t *game_map_seed, *game_sea_level, *game_elev_detail, *game_alt_natural, *game_goody_mult;
uint8_t **game_abstract_map;
int32_t *game_abstract_width, *game_abstract_height;
int32_t *game_landmark_count;
struct landmark *game_landmarks;
struct base *game_bases;
int32_t *game_base_count;
int32_t *game_radius_near_x, *game_radius_near_y, *game_radius_x, *game_radius_y, *game_radius_count;

char *(__attribute__((thiscall)) * game_strings_get)(void *table, char *s);
int (*game_climactic_battle)(void);
int (*game_weapon_budget)(int faction, int condition, int check_mode);
int (*game_wants_to_attack)(int faction, int other, int odds);

void game_bind(struct pe_image *exe) {
    game_tech = (struct rules_tech *)code_at(exe, 0x94f358);
    game_tech_achieved = code_at(exe, 0x9a6670);
    game_expansion = (int32_t *)code_at(exe, 0x9a6488);
    game_turn = (int32_t *)code_at(exe, 0x9a64d4);

    game_factions = (struct faction_data *)code_at(exe, 0x96c9e0);
    game_faction_defs = (struct faction_def *)code_at(exe, 0x946a50);
    game_human_factions = code_at(exe, 0x9a64e8);
    game_protos = (struct unit_proto *)code_at(exe, 0x9ab868);
    game_weapons = (struct rules_weapon *)code_at(exe, 0x94ae60);
    game_armor = (struct rules_armor *)code_at(exe, 0x94f278);
    game_reactors = (struct rules_reactor *)code_at(exe, 0x9527f8);
    game_chassis = (struct rules_chassis *)code_at(exe, 0x94a330);
    game_facilities = (struct rules_facility *)code_at(exe, 0x9a4b68);
    game_continents = (struct continent *)code_at(exe, 0x9aa730);
    game_tech_discovery_rate = (int32_t *)code_at(exe, 0x949758);
    game_rules = (uint32_t *)code_at(exe, 0x9a649c);
    game_difficulty = (int32_t *)code_at(exe, 0x9a64c4);
    game_map_area_sqrt = (int32_t *)code_at(exe, 0x949888);
    game_cloud_cover = (int32_t *)code_at(exe, 0x94a2b4);
    game_language = (int32_t *)code_at(exe, 0x9bc054);
    game_parse_gender = (int32_t *)code_at(exe, 0x9bbfec);
    game_parse_plural = (int32_t *)code_at(exe, 0x9bbff0);
    game_labels = (char ***)code_at(exe, 0x9b90f8);
    game_strings = code_at(exe, 0x9b90d8);
    game_tech_name_buffer = (char *)code_at(exe, 0x945e80);

    game_map = (struct map_tile **)code_at(exe, 0x94a30c);
    game_map_width = (int32_t *)code_at(exe, 0x949870);
    game_map_height = (int32_t *)code_at(exe, 0x949874);
    game_map_half_width = (int32_t *)code_at(exe, 0x68faf0);
    game_map_flat = (uint32_t *)code_at(exe, 0x94988c);
    game_map_seed = (int32_t *)code_at(exe, 0x949878);
    game_sea_level = (int32_t *)code_at(exe, 0x94987c);
    game_elev_detail = (int32_t *)code_at(exe, 0x68fb20);
    game_alt_natural = (int32_t *)code_at(exe, 0x68fb4c);
    game_goody_mult = (int32_t *)code_at(exe, 0x68fb78);
    game_abstract_map = (uint8_t **)code_at(exe, 0x94a310);
    game_abstract_width = (int32_t *)code_at(exe, 0x94a294);
    game_abstract_height = (int32_t *)code_at(exe, 0x94a298);
    game_landmark_count = (int32_t *)code_at(exe, 0x949890);
    game_landmarks = (struct landmark *)code_at(exe, 0x949894);
    game_bases = (struct base *)code_at(exe, 0x97d040);
    game_base_count = (int32_t *)code_at(exe, 0x9a64cc);
    game_radius_near_x = (int32_t *)code_at(exe, 0x66ef50);
    game_radius_near_y = (int32_t *)code_at(exe, 0x66ef74);
    game_radius_x = (int32_t *)code_at(exe, 0x66efbc);
    game_radius_y = (int32_t *)code_at(exe, 0x66f440);
    game_radius_count = (int32_t *)code_at(exe, 0x66f8c4);

    game_strings_get = (void *)code_at(exe, 0x6169a0);
    game_climactic_battle = (void *)code_at(exe, 0x539e40);
    game_weapon_budget = (void *)code_at(exe, 0x57f0b0);
    game_wants_to_attack = (void *)code_at(exe, 0x55bc80);
}
