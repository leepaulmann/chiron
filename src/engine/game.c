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

    game_strings_get = (void *)code_at(exe, 0x6169a0);
    game_climactic_battle = (void *)code_at(exe, 0x539e40);
    game_weapon_budget = (void *)code_at(exe, 0x57f0b0);
    game_wants_to_attack = (void *)code_at(exe, 0x55bc80);
}
