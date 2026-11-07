// Unit test for the reimplemented technology queries, on a small made-up
// tech tree: no game files needed.
#include <stdio.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/tech.h"

static struct rules_tech tree[TECH_COUNT];
static uint8_t achieved[TECH_COUNT];
static int32_t expansion, turn, discovery_rate = 100, difficulty = 3, area_sqrt = 56, cloud, language;
static int32_t gender, plural;
static uint32_t rules;
static uint8_t humans;
static struct faction_data factions[FACTION_COUNT];
static struct faction_def defs[FACTION_COUNT];
static struct unit_proto protos[PROTO_COUNT];
static struct rules_weapon weapons[WEAPON_COUNT];
static struct rules_armor armor[ARMOR_COUNT];
static struct rules_reactor reactors[REACTOR_COUNT];
static struct rules_chassis chassis[CHASSIS_COUNT];
static struct rules_facility facilities[FACILITY_COUNT];
static struct continent continents[REGION_COUNT];
static char *label_table[700], **labels = label_table, tech_name_buf[80];
static int failures;

// The string table hands back the strings it is given.
static char *__attribute__((thiscall)) strings_get(void *table, char *s) { return s; }
static int no_climax(void) { return 0; }
static int peaceful(int faction, int other, int odds) { return 0; }
static uint32_t __attribute__((stdcall)) upper_stub(char *s) {
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z') *s -= 32;
    return 0;
}
static void *upper_ptr = (void *)upper_stub;
static char said[64], said_s[64], popped[64];
static int parse_say_stub(int slot, int text, int gender, int plural) {
    snprintf(said, sizeof(said), "%s", (char *)(intptr_t)text);
    return 0;
}
static int parse_says_stub(int slot, char *text, int gender, int plural) {
    snprintf(said_s, sizeof(said_s), "%s", text);
    return 0;
}
static void x_pop_stub(char *label, int unk) { snprintf(popped, sizeof(popped), "%s", label); }
static int rand_zero(void) { return 0; }
static int random_top(int min, int max) { return max - 1; }

#define EXPECT_STR(expr, want)                                                   \
    do {                                                                         \
        const char *got_ = (expr);                                               \
        if (strcmp(got_, (want))) {                                              \
            printf("FAIL %s = \"%s\", expected \"%s\"\n", #expr, got_, want);     \
            failures++;                                                          \
        }                                                                        \
    } while (0)

#define EXPECT(expr, want)                                                       \
    do {                                                                         \
        int got_ = (expr);                                                       \
        if (got_ != (want)) {                                                    \
            printf("FAIL %s = %d, expected %d\n", #expr, got_, (int)(want));    \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static void set(int id, int preq1, int preq2, int growth, int tech, int wealth, int power) {
    tree[id] = (struct rules_tech){.growth = growth, .tech = tech, .wealth = wealth, .power = power,
                                   .preq1 = preq1, .preq2 = preq2};
}

int main(void) {
    for (int i = 0; i < TECH_COUNT; i++) set(i, TECH_NONE, TECH_NONE, 0, 0, 0, 0);
    set(0, TECH_NONE, TECH_NONE, 1, 2, 3, 4);  // power wins
    set(1, 0, TECH_NONE, 2, 2, 2, 2);          // all tied: growth
    set(2, 0, 1, 0, 3, 3, 0);                  // tech and wealth tied: tech
    set(3, TECH_DISABLED, TECH_NONE, 0, 0, 0, 0);
    set(4, 1, TECH_DISABLED, 0, 0, 0, 0);       // second prerequisite disabled
    set(5, TECH_NONE, TECH_DISABLED, 0, 0, 0, 0);  // ...but the first is "none"
    set(TECH_TRANSCEND, 2, TECH_NONE, 0, 0, 0, 0);
    achieved[0] = 1 << 1 | 1 << 2;
    achieved[1] = achieved[3] = achieved[4] = achieved[5] = achieved[TECH_TRANSCEND] = 1 << 1;
    game_tech = tree;
    game_tech_achieved = achieved;
    game_expansion = &expansion;

    EXPECT(has_tech(0, 1), 1);
    EXPECT(has_tech(0, 3), 0);
    EXPECT(has_tech(0, 0), 0);  // natives have nothing
    EXPECT(has_tech(TECH_NONE, 1), 1);
    EXPECT(has_tech(TECH_NONE, 0), 0);
    EXPECT(has_tech(TECH_DISABLED, 1), 0);
    EXPECT(has_tech(3, 1), 0);
    EXPECT(has_tech(4, 1), 0);
    EXPECT(has_tech(5, 1), 1);
    EXPECT(has_tech(TECH_TRANSCEND, 1), 0);
    EXPECT(has_tech(TECH_COUNT, 1), 0);
    EXPECT(has_tech(0, 33), 1);  // shift count wraps, as on x86

    EXPECT(tech_recurse(0, 0), 1);
    EXPECT(tech_recurse(2, 0), 3);  // 2 <- 1 <- 0
    EXPECT(tech_recurse(TECH_NONE, 5), 5);

    EXPECT(tech_category(0), 3);
    EXPECT(tech_category(1), 0);
    EXPECT(tech_category(2), 1);

    EXPECT(tech_avail(1, 2), 1);
    EXPECT(tech_avail(1, 1), 0);  // already had
    EXPECT(tech_avail(2, 1), 1);
    EXPECT(tech_avail(2, 2), 0);  // lacks tech 1
    EXPECT(tech_avail(5, 2), 0);  // a disabled prerequisite
    EXPECT(tech_avail(TECH_FIRST_SMACX, 2), 0);
    expansion = 1;
    EXPECT(tech_avail(TECH_FIRST_SMACX, 2), 1);
    EXPECT(tech_avail(TECH_COUNT, 1), 0);
    EXPECT(tech_avail((uint32_t)-1, 1), 0);

    EXPECT(tech_is_preq(0, 2, 1), 1);
    EXPECT(tech_is_preq(0, 2, 0), 0);
    EXPECT(tech_is_preq(2, 2, 0), 1);
    EXPECT(tech_is_preq(0, TECH_TRANSCEND, 2), 1);
    EXPECT(tech_is_preq(1, 0, 5), 0);
    EXPECT(tech_is_preq(TECH_NONE, 2, 3), 0);

    EXPECT(tech_mil(0), 4);
    EXPECT(tech_mil(TECH_COUNT), 0);
    EXPECT(tech_tech(0), 2);
    EXPECT(tech_tech(TECH_COUNT), 1);
    EXPECT(tech_infra(0), 3);
    EXPECT(tech_infra(TECH_COUNT + 5), 1);
    EXPECT(tech_colonize(0), 1);
    EXPECT(tech_colonize(TECH_COUNT), 1);

    // The faction and rules tables for the rest.
    game_factions = factions;
    game_faction_defs = defs;
    game_human_factions = &humans;
    game_protos = protos;
    game_weapons = weapons;
    game_armor = armor;
    game_reactors = reactors;
    game_chassis = chassis;
    game_facilities = facilities;
    game_continents = continents;
    game_tech_discovery_rate = &discovery_rate;
    game_rules = &rules;
    game_difficulty = &difficulty;
    game_map_area_sqrt = &area_sqrt;
    game_cloud_cover = &cloud;
    game_language = &language;
    game_parse_gender = &gender;
    game_parse_plural = &plural;
    game_labels = &labels;
    game_strings_get = strings_get;
    game_tech_name_buffer = tech_name_buf;
    game_climactic_battle = no_climax;
    game_wants_to_attack = peaceful;
    game_turn = &turn;
    for (int i = 0; i < WEAPON_COUNT; i++) weapons[i].preq_tech = TECH_NONE;
    for (int i = 0; i < REACTOR_COUNT; i++) reactors[i].preq_tech = 50;
    protos[PROTO_FORMERS].preq_tech = 60;
    chassis[CHASSIS_FOIL].preq_tech = 61;

    // valid_tech_leap: tech 2 needs 0 and 1, and faction 1 has what those need.
    EXPECT(valid_tech_leap(2, 1), 1);
    EXPECT(valid_tech_leap(2, 3), 0);  // lacks tech 0, which tech 1 needs
    EXPECT(valid_tech_leap(4, 1), 0);  // disabled
    reactors[1].preq_tech = 2;
    EXPECT(valid_tech_leap(2, 1), 0);  // unlocks a reactor
    reactors[1].preq_tech = 50;
    // weapon_budget() picks weapon 1, the only combat weapon, so offense 3
    // is the most allowed (the original compares with the weapon's number).
    for (int i = 0; i < WEAPON_COUNT; i++) weapons[i].mode = i == 1 ? 0 : 3;
    weapons[5].preq_tech = 2, weapons[5].offense = 3;
    factions[1].ranking = 2;
    EXPECT(valid_tech_leap(2, 1), 1);
    factions[1].ranking = 3;
    EXPECT(valid_tech_leap(2, 1), 0);  // not among the two weakest
    factions[1].ranking = 2, weapons[5].offense = 4;
    EXPECT(valid_tech_leap(2, 1), 0);  // too strong a weapon
    weapons[5].preq_tech = TECH_NONE;

    // say_tech and tech_name.
    label_table[25] = "NONE", label_table[185] = "Prototype", label_table[306] = "World Map";
    label_table[310] = "Not Available", label_table[487] = "Comm Frequency";
    label_table[629] = "E", label_table[630] = "D", label_table[631] = "B", label_table[632] = "C";
    tree[2].name = "Applied Physics";
    snprintf(defs[2].adjective, sizeof(defs[2].adjective), "Gaian");
    snprintf(defs[2].noun, sizeof(defs[2].noun), "Gaians");
    defs[2].noun_gender = 1, defs[2].noun_plural = 1;
    snprintf(protos[3].name, sizeof(protos[3].name), "Scout Patrol");
    char out[128] = "Tech: ";
    say_tech(out, 2, 1);
    EXPECT_STR(out, "Tech: Applied Physics (D3)");
    EXPECT_STR(tech_name(2, 0), "Applied Physics");
    EXPECT_STR(tech_name(TECH_NONE, 0), "NONE");
    EXPECT_STR(tech_name(TECH_DISABLED, 0), "Not Available");
    EXPECT_STR(tech_name(9999, 1), "World Map");
    EXPECT_STR(tech_name(TECH_COUNT + 2, 0), "Gaian Comm Frequency");
    language = 1;
    EXPECT_STR(tech_name(TECH_COUNT + 2, 0), "Comm Frequency (Gaians)");
    EXPECT(gender * 10 + plural, 11);
    language = 0;
    EXPECT_STR(tech_name(TECH_COUNT + FACTION_COUNT + 3, 0), "Scout Patrol Prototype");

    // tech_effects: techs 0 and 1 for faction 1, plus faction-file bonuses.
    tree[0].flags = TECH_FLAG_COMMERCE | TECH_FLAG_FUNGUS_ENERGY;
    tree[1].flags = TECH_FLAG_COMMERCE | TECH_FLAG_FUNGUS_NUTRIENT;
    defs[1].rule_commerce = 1;
    defs[1].bonus_count = 2;
    defs[1].bonus_id[0] = BONUS_FUNGUS_MINERALS, defs[1].bonus_val1[0] = 2;
    defs[1].bonus_id[1] = BONUS_FUNGUS_ENERGY, defs[1].bonus_val1[1] = -3;
    factions[1].se_pending[SE_ECONOMY] = 4;
    factions[1].tech_fungus[FUNGUS_UNUSED] = 7;
    tech_effects(1);
    EXPECT(factions[1].tech_commerce_bonus, 1 + 2 + 2);  // rule, two techs, economy 4
    EXPECT(factions[1].tech_fungus[FUNGUS_NUTRIENT], 1);
    EXPECT(factions[1].tech_fungus[FUNGUS_MINERALS], 2);
    EXPECT(factions[1].tech_fungus[FUNGUS_ENERGY], 0);  // 1 - 3, held at zero
    EXPECT(factions[1].tech_fungus[FUNGUS_UNUSED], 0);
    tree[0].flags = tree[1].flags = 0;
    defs[1].bonus_count = 0;

    // tech_rate, worked through by hand: own progress 10, the leader's 20,
    // AI difficulty 3, turn 100.
    factions[1].tech_cost = -1, factions[1].earned_techs_saved = 5, factions[1].tech_ranking = 10;
    factions[2].earned_techs_saved = 10, factions[2].tech_ranking = 20;
    defs[1].rule_techcost = 100;
    turn = 100;
    EXPECT(tech_rate(1), 180);
    rules = RULES_TECH_STAGNATION;
    EXPECT(tech_rate(1), 300);
    rules = 0;
    discovery_rate = 0;
    EXPECT(tech_rate(1), 999999999);
    discovery_rate = 100;
    factions[1].tech_cost = 42;
    EXPECT(tech_rate(1), 42);

    // tech_val.
    EXPECT(tech_val(9999, 1, 0), 2);
    weapons[2].offense = 3, armor[1].defense = 1, chassis[0].speed = 1;
    protos[3].weapon = 2, protos[3].armor = 1, protos[3].chassis = 0, protos[3].reactor = 1;
    EXPECT(tech_val(TECH_COUNT + FACTION_COUNT + 3, 1, 0), 2 + 1 + 1 + 1 - 2);
    factions[1].ai_fight = 1;
    EXPECT(tech_val(TECH_COUNT + 2, 1, 0), 3);  // neither wants war: 3 / 3 * 3
    factions[1].ai_power = 1;
    EXPECT(tech_val(0, 1, 1), 1 + 2 + 3 + 4 * 2);  // power weighted for this faction
    EXPECT(tech_val(0, 1, 0), 1 + 2 + 3 + 4 * 3);  // ...more so in the full sum; had, so no more
    // Faction 3 lacks tech 0, which two others have: 10, less 2.
    EXPECT(tech_val(0, 3, 1), 8);
    // In full: 10, doubled because tech 0 leads to the (made-up) Hunter-Seeker
    // tech 0 and faction 3 has no probe defence.
    EXPECT(tech_val(0, 3, 0), 20);

    // Facilities, at a made-up base 0 of faction 1 that is never on the coast
    // here. Faction 1 has techs 0 and 1, but not 2.
    static struct base bases[2];
    static int32_t projects[SP_COUNT];
    static uint8_t image[0x300000];  // the game's strings, up to 0x700000
    static char battle[256];
    int32_t base_count = 1, current = 1;
    for (int i = 0; i < SP_COUNT; i++) projects[i] = -1;
    for (int i = 0; i < FACILITY_COUNT; i++) facilities[i].preq_tech = TECH_NONE;
    bases[0].faction = 1;
    game_bases = bases, game_base_count = &base_count, game_secret_projects = projects;
    game_test_image = image;
    strcpy((char *)image + 0x6916e4 - 0x400000, "CARETAKE");
    strcpy((char *)image + 0x6916f0 - 0x400000, "THESECRET");
    game_battle_text = battle, game_char_upper = (void *)&upper_ptr, game_current_faction = &current;
    snprintf(defs[1].filename, sizeof(defs[1].filename), "Gaians");
    snprintf(defs[2].filename, sizeof(defs[2].filename), "Caretake");
    rules = 0, difficulty = 3, expansion = 1;

    EXPECT(redundant(4, 1), 0);
    projects[5] = 0;  // Citizens' Defense Force, at base 0
    EXPECT(redundant(4, 1), 1);
    EXPECT(redundant(4, 2), 0);
    EXPECT(redundant(5, 1), 0);

    EXPECT(facility_avail(0, 1, -1, 0), 0);
    EXPECT(facility_avail(3, 0, -1, 0), 0);  // not for the natives
    EXPECT(facility_avail(3, 1, -1, 0), 1);
    EXPECT_STR(battle, "GAIANS");  // left behind
    facilities[3].preq_tech = 2;
    EXPECT(facility_avail(3, 1, -1, 0), 0);
    facilities[3].preq_tech = 0;
    EXPECT(facility_avail(3, 1, -1, 0), 1);
    EXPECT(facility_avail(3, 3, -1, 0), 0);  // lacks tech 0
    facilities[3].preq_tech = TECH_NONE;
    // Recycling Tanks: not with a Pressure Dome, built or queued.
    EXPECT(facility_avail(3, 1, 0, 0), 1);
    bases[0].facilities_built[26 / 8] = 1 << 26 % 8;
    EXPECT(facility_avail(3, 1, 0, 0), 0);
    memset(bases[0].facilities_built, 0, sizeof(bases[0].facilities_built));
    bases[0].queue[0] = -26;
    EXPECT(facility_avail(3, 1, 0, 0), 1);
    EXPECT(facility_avail(3, 1, 0, 1), 0);
    bases[0].queue[0] = -3;
    EXPECT(facility_avail(3, 1, 0, 1), 0);  // queued already
    bases[0].queue[0] = 0;
    // Perimeter Defense is redundant with the Citizens' Defense Force, which
    // gives the Tachyon Field what it needs.
    EXPECT(facility_avail(4, 1, 0, 0), 0);
    EXPECT(facility_avail(5, 1, 0, 0), 1);
    projects[5] = -1;
    EXPECT(facility_avail(4, 1, 0, 0), 1);
    EXPECT(facility_avail(5, 1, 0, 0), 0);
    bases[0].facilities_built[0] = 1 << 4;
    EXPECT(facility_avail(5, 1, 0, 0), 1);
    bases[0].facilities_built[0] = 0;
    // Skunkworks: not at the lowest difficulties, nor with free prototypes.
    EXPECT(facility_avail(10, 1, 0, 0), 1);
    difficulty = 1;
    EXPECT(facility_avail(10, 1, 0, 0), 0);
    difficulty = 3, defs[1].rule_flags = 0x200;
    EXPECT(facility_avail(10, 1, 0, 0), 0);
    defs[1].rule_flags = 0;
    // A Punishment Sphere and a Paradise Garden don't go together, one way.
    bases[0].facilities_built[12 / 8] = 1 << 12 % 8;
    EXPECT(facility_avail(23, 1, 0, 0), 0);
    bases[0].facilities_built[12 / 8] = 0;
    bases[0].facilities_built[23 / 8] = 1 << 23 % 8;
    EXPECT(facility_avail(12, 1, 0, 0), 1);
    bases[0].facilities_built[23 / 8] = 0;
    // A Habitation Dome needs a Hab Complex, here queued.
    EXPECT(facility_avail(25, 1, 0, 0), 0);
    bases[0].queue[1] = -24;
    EXPECT(facility_avail(25, 1, 0, 2), 1);
    bases[0].queue[1] = 0;
    // In orbit: an Aerospace Complex, or the Space Elevator.
    EXPECT(facility_avail(65, 1, 0, 0), 0);
    projects[SP_SPACE_ELEVATOR] = 0;
    EXPECT(facility_avail(65, 1, 0, 0), 1);
    EXPECT(facility_avail(65, 1, -1, 0), 1);
    projects[SP_SPACE_ELEVATOR] = -1;
    // Alien Crossfire's, and the alien factions' own.
    EXPECT(facility_avail(35, 1, 0, 0), 1);
    EXPECT(facility_avail(40, 1, 0, 0), 0);
    defs[1].rule_flags = RULE_FLAG_ALIEN;
    EXPECT(facility_avail(40, 1, 0, 0), 1);
    expansion = 0;
    EXPECT(facility_avail(35, 1, 0, 0), 0);
    EXPECT(facility_avail(104, 1, -1, 0), 0);
    expansion = 1, defs[1].rule_flags = 0;
    // Secret projects: while unbuilt, and not in scenarios that forbid them.
    EXPECT(facility_avail(70, 1, 0, 0), 1);
    projects[0] = 0;
    EXPECT(facility_avail(70, 1, -1, 0), 0);
    projects[0] = -1, rules = 0x80000000u;
    EXPECT(facility_avail(70, 1, -1, 0), 0);
    rules = 0;
    // The Voice of Planet, not for the Caretakers; Ascent once it is built,
    // for anyone with the transcendence victory.
    EXPECT(facility_avail(101, 1, -1, 0), 1);
    EXPECT(facility_avail(101, 2, -1, 0), 0);
    EXPECT(facility_avail(102, 1, -1, 0), 0);
    projects[SP_VOICE_OF_PLANET] = -2;
    EXPECT(facility_avail(102, 1, -1, 0), 0);
    rules = RULES_VICTORY_TRANSCENDENCE;
    EXPECT(facility_avail(102, 0, -1, 0), 1);
    EXPECT(facility_avail(102, 2, -1, 0), 1);
    rules = 0, projects[SP_VOICE_OF_PLANET] = -1;

    // Terraforming: action 12 needs tech 0 on land and is impossible at sea.
    static struct rules_terraform terra[TERRAFORM_COUNT];
    for (int i = 0; i < TERRAFORM_COUNT; i++) terra[i].preq_tech = terra[i].preq_tech_sea = TECH_NONE;
    terra[12].preq_tech = 0, terra[12].preq_tech_sea = TECH_DISABLED, terra[16].preq_tech = 2;
    game_terraform = terra;
    EXPECT(terrain_avail(12, 0, 1), 1);
    EXPECT(terrain_avail(12, 0, 3), 0);
    EXPECT(terrain_avail(12, 1, 1), 0);
    EXPECT(terrain_avail(0, 0, 0), 0);
    EXPECT(terrain_avail(0, 0, 3), 1);
    EXPECT(terrain_avail(16, 0, 1), 0);
    projects[2] = 0;  // the Weather Paradigm: no tech needed
    EXPECT(terrain_avail(16, 0, 1), 1);
    EXPECT(terrain_avail(16, 0, 2), 0);
    EXPECT(terrain_avail(11, 0, 1), 1);
    rules = 0x800000;  // no raising or lowering land
    EXPECT(terrain_avail(16, 0, 1), 0);
    EXPECT(terrain_avail(18, 0, 1), 1);
    rules = 0, projects[2] = -1;

    // tech_secret's message.
    game_parse_say = parse_say_stub, game_parse_says = parse_says_stub, game_x_pop = x_pop_stub;
    tech_secret(2, 1);
    EXPECT_STR(popped, "THESECRET0");
    EXPECT_STR(said, "Applied Physics");
    EXPECT_STR(said_s, defs[1].adjective);
    tech_secret(2, 3);
    EXPECT_STR(popped, "THESECRET1");

    // tech_ai: the AI's draw (here always 0) takes the first it can
    // research; a human's (here always the most) the most valuable.
    game_rand = rand_zero, game_random = random_top;
    for (int t = 6; t < TECH_COUNT; t++) set(t, TECH_DISABLED, TECH_NONE, 0, 0, 0, 0);
    set(6, 0, TECH_NONE, 1, 0, 0, 0);
    set(7, 0, TECH_NONE, 50, 0, 0, 0);
    set(60, TECH_NONE, TECH_NONE, 0, 0, 0, 0);
    EXPECT(tech_ai(2), 1);  // faction 2 has tech 0 only
    humans = 1 << 2;
    EXPECT(tech_ai(2), 7);
    // With blind research a human's faction that cares for growth goes for
    // the terraforming tech (60).
    rules = RULES_BLIND_RESEARCH, factions[2].ai_growth = 1;
    EXPECT(tech_ai(2), 60);
    humans = 0, rules = 0;
    achieved[0] |= 1 << 4, achieved[1] |= 1 << 4, achieved[2] |= 1 << 4, achieved[6] = achieved[7] = achieved[60] = 1 << 4;
    EXPECT(tech_ai(4), -1);  // nothing left to research

    if (failures) return 1;
    puts("PASS");
    return 0;
}
