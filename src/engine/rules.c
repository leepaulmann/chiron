// The rules: the readers of alpha(x).txt and the faction files,
// reimplemented from terranx.exe 2.0. Names follow Thinker (config.cpp,
// Copyright (C) 2013-2025 Induktio, GPL-3.0) and OpenSMACX; the behaviour is
// taken from the disassembly, quirks included, where Thinker's version
// differs in places (noted below).
//
// The game's text library stays its own and is called through game_*
// pointers: text_open finds a file and a "#LABEL" line in it, text_get
// reads the next line, and text_item and its kin take the line's next
// comma-separated item (as text, as a number, as binary digits, or kept in
// the string table). Every tables' text the readers keep is a pointer into
// that string table, which read_rules starts afresh.
//
// The readers open files and allocate (the string table, the labels, the
// text index, button help, faction art), so shadow mode can't check them by
// running them twice on a rewound copy of the game: shadow mode runs the
// originals, untouched. CHIRON_RULES_CHECK=1 checks them instead whenever
// the game reads its rules or a faction file (at the end of this file):
// each read by the original and by Chiron, and the game's memory compared.
#include "rules.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "events.h"
#include "game.h"
#include "hooks.h"
#include "shadow.h"
#include "veh.h"

// ---- The game's globals ------------------------------------------------------

static inline void *at(uint32_t addr) { return game_at(addr); }
#define I32(addr) (*(int32_t *)at(addr))
#define I16(addr) (*(int16_t *)at(addr))
#define U8(addr) (*(uint8_t *)at(addr))
#define PTR(addr) (*(char **)at(addr))
static inline char *text(uint32_t addr) { return (char *)at(addr); }

enum {
    ALPHA_FILE = 0x691afc,   // char *: the rules file's name
    SCRIPT_FILE = 0x691b0c,  // char *: script.txt's
    TEXT_FILE_PATH = 0x9b7bf0,  // the text reader's: the file it has open
    TEXT_LINE = 0x9b7d00,       // char *: the line read last
    TEXT_ITEM = 0x9b7d04,       // char *: the item taken last
    FOUND_PATH = 0x9b8398,      // the path of the file the game's folders found last
    STR_BUF = 0x9b86a0,         // the game's scratch text
    LANGUAGE = 0x9bc054,        // non-zero: not English, names compared by their first 4 letters
    EXPANSION = 0x9a6488,       // Alien Crossfire
    STRINGS = 0x9b90d8,         // the string table (base at +4, next free at +8)
    LABELS = 0x9b90f8,          // char **: labels.txt's lines; their count follows

    RULES = 0x949738,            // #RULES
    TECH_VALID = 0x949730,       // techs not disabled
    TECH_COMMERCE = 0x949734,    // of those, the ones that raise commerce
    WORLDBUILDER = 0x9502a8,     // #WORLDBUILDER
    TECHS = 0x94f358,            // 0x2c each
    TERRAFORM = 0x691878,        // 0x20 each
    ORDERS = 0x96c878,           // 0xc each: text on land, at sea, and the key
    RES_INFO = 0x945f50,         // 4 numbers per line
    TIME_CONTROLS = 0x94f1b8,    // 0x20 each
    CHASSIS = 0x94a330,          // 0x90 each
    REACTORS = 0x9527f8,         // 0xc each
    WEAPONS = 0x94ae60,          // 0x10 each
    ARMOR = 0x94f278,            // 0x10 each
    ABILITIES = 0x9ab538,        // 0x1c each
    MORALE = 0x952328,           // 8 each
    DEFENSE_MODES = 0x946a00,    // 0x10 each
    OFFENSE_MODES = 0x946178,    // 0x10 each
    PROTOS = 0x9ab868,           // 0x34 each
    FACILITIES = 0x9a4b68,       // 0x30 each
    COMPASS = 0x945d48,
    PLANS_SHORT = 0x945fe0, PLANS_FULL = 0x952360,
    TRIAD = 0x94f1a8,
    RESOURCES = 0x946158,        // 8 each
    ENERGY = 0x94a318,           // 8 each
    CITIZENS = 0x946020,         // 0x1c each
    SOCIAL_PARAMS = 0x946580,    // 0x68 each: the effect's key (24 bytes), then its other name
    SOCIAL_FIELDS = 0x94b000,    // 0xd4 each: name, 4 tech, 4 model names, 4 effect lists of 11
    DIFFICULTY = 0x96c85c,
    MANDATES = 0x94b4a0,         // 8 each
    MOODS = 0x94c9e4,
    REPUTE = 0x946a30,
    MIGHT = 0x94c558,            // 8 each
    PROPOSALS = 0x9a6828,        // 0xc each
    NATURAL = 0x94ade0,          // 8 each
    BONUS_NAMES = 0x9461a8,      // 24 bytes each
    FACTION_DEFS = 0x946a50,     // 0x59c each
    FLAT_BUTTONS = 0x7bb62c,     // the map editor's terraforming buttons, 0xb4c each
    BUBBLE_TEXT = 0xa80,         // in a button: its help text, allocated
};

// The section labels and other words the readers use, in the game's memory.
enum {
    L_NONE = 0x68f268, L_DISABLE = 0x68f270, L_BADTECHKEY = 0x68f278,
    L_NONE_2 = 0x68f284, L_DISABLE_2 = 0x68f28c, L_BADCHASKEY = 0x68f294,
    L_NONE_3 = 0x68f2a0, L_DISABLE_3 = 0x68f2a8, L_BADWEAPKEY = 0x68f2b0,
    L_NONE_4 = 0x68f2bc, L_DISABLE_4 = 0x68f2c4, L_BADARMKEY = 0x68f2cc,
    L_RULES = 0x68f2d8, L_WORLDBUILDER = 0x68f2e0, L_TECHNOLOGY = 0x68f2f0, L_DUPLICATETECH = 0x68f2fc,
    L_TECHNOLOGY_2 = 0x68f30c, L_PLAYERFILE = 0x68f318, L_BONUSNAMES = 0x68f324, L_NEWFACTIONS = 0x68f330,
    L_FACTIONS = 0x68f33c, L_CUSTOMFACTIONS = 0x68f348, L_RANDOM = 0x68f358, L_FACTIONS_2 = 0x68f360,
    L_NEWFACTIONS_2 = 0x68f36c, L_CUSTOMFACTIONS_2 = 0x68f378, L_UNITS = 0x68f388, L_TERRAIN = 0x68f390,
    L_RESOURCEINFO = 0x68f398, L_TIMECONTROLS = 0x68f3a8, L_CHASSIS = 0x68f3b8, L_REACTORS = 0x68f3c0,
    L_WEAPONS = 0x68f3cc, L_DEFENSES = 0x68f3d4, L_ABILITIES = 0x68f3e0, L_MORALE = 0x68f3ec,
    L_DEFENSEMODES = 0x68f3f4, L_OFFENSEMODES = 0x68f404, L_FACILITIES = 0x68f414, L_ORDERS = 0x68f420,
    L_COMPASS = 0x68f428, L_PLANS = 0x68f430, L_TRIAD = 0x68f438, L_RESOURCES = 0x68f440, L_ENERGY = 0x68f44c,
    L_CITIZENS = 0x68f454, L_SOCIO = 0x68f460, L_DIFF = 0x68f468, L_MANDATE = 0x68f470, L_MOOD = 0x68f478,
    L_REPUTE = 0x68f480, L_MIGHT = 0x68f488, L_PROPOSALS = 0x68f490, L_NATURAL = 0x68f49c,
};

enum { TECHS_N = 89, TERRAFORM_N = 20, BONUS_N = 41, SOCIAL_EFFECTS = 11 };

// The faction bonuses, by their place in #BONUSNAMES.
enum {
    BN_TECH, BN_MORALE, BN_PSI, BN_FACILITY, BN_RESEARCH, BN_DRONE, BN_TALENT, BN_ENERGY, BN_INTEREST,
    BN_COMMERCE, BN_POPULATION, BN_HURRY, BN_UNIT, BN_TECHCOST, BN_SHARETECH, BN_TERRAFORM, BN_SOCIAL,
    BN_ROBUST, BN_IMMUNITY, BN_IMPUNITY, BN_PENALTY, BN_FUNGNUTRIENT, BN_FUNGMINERALS, BN_FUNGENERGY,
    BN_COMMFREQ, BN_MINDCONTROL, BN_FANATIC, BN_VOTES, BN_FREEPROTO, BN_AQUATIC, BN_ALIEN, BN_FREEFAC,
    BN_REVOLT, BN_NODRONE, BN_WORMPOLICE, BN_FREEABIL, BN_PROBECOST, BN_DEFENSE, BN_OFFENSE, BN_TECHSHARE,
    BN_TECHSTEAL,
};

// A faction definition's fields by offset (struct faction_def names some).
enum {
    FD_FEMALE = 0x0, FD_FILENAME = 0x4, FD_SEARCH_KEY = 0x1c, FD_LEADER = 0x34, FD_TITLE = 0x4c,
    FD_ADJ_LEADER = 0x64, FD_ADJ_INSULT_LEADER = 0xe4, FD_ADJ_FACTION = 0x164, FD_ADJ_INSULT_FACTION = 0x1e4,
    FD_NOUN = 0x2e4, FD_NOUN_GENDER = 0x2fc, FD_NOUN_PLURAL = 0x300, FD_ADJ_NAME = 0x304, FD_FORMAL_NAME = 0x384,
    FD_INSULT_LEADER = 0x3ac, FD_DESC_NAME = 0x3c4, FD_ASSISTANT = 0x3dc, FD_SCIENTIST = 0x3f4,
    FD_ASSISTANT_CITY = 0x40c, FD_TECH_SELECTED = 0x4d4, FD_MORALE = 0x4d8, FD_RESEARCH = 0x4dc, FD_DRONE = 0x4e0,
    FD_TALENT = 0x4e4, FD_ENERGY = 0x4e8, FD_INTEREST = 0x4ec, FD_POPULATION = 0x4f0, FD_HURRY = 0x4f4,
    FD_TECHCOST = 0x4f8, FD_PSI = 0x4fc, FD_SHARETECH = 0x500, FD_COMMERCE = 0x504, FD_FLAGS = 0x508,
    FD_BONUS_COUNT = 0x50c, FD_BONUS_ID = 0x510, FD_BONUS_VAL1 = 0x530, FD_BONUS_VAL2 = 0x550,
    FD_AI_FIGHT = 0x570, FD_AI_GROWTH = 0x574, FD_AI_TECH = 0x578, FD_AI_WEALTH = 0x57c, FD_AI_POWER = 0x580,
    FD_IDEOLOGY_CATEGORY = 0x584, FD_IDEOLOGY_MODEL = 0x58c, FD_IDEOLOGY_EFFECT = 0x594,
};

// The trampolines to the originals, filled in by hooks_apply.
static int (*orig_entry_tech_name)(char *), (*orig_entry_chas_name)(char *), (*orig_entry_weap_name)(char *),
    (*orig_entry_arm_name)(char *), (*orig_entry_text_get_number)(int, int), (*orig_entry_tech_item)(void),
    (*orig_entry_read_basic_rules)(void), (*orig_entry_read_tech)(void), (*orig_entry_read_factions)(void),
    (*orig_entry_read_units)(void), (*orig_entry_read_rules)(int);
static void (*orig_entry_noun_item)(int32_t *, int32_t *), (*orig_entry_read_faction)(struct faction_def *, int),
    (*orig_entry_read_faction2)(int);

// ---- Small helpers ------------------------------------------------------------

static void text_get(void) { game_text_get(); }
static char *text_item(void) { return game_text_item(); }
static char *item_string(void) { return game_text_item_string(); }
static int item_number(void) { return game_text_item_number(); }
static int item_binary(void) { return game_text_item_binary(); }
static int open_alpha(uint32_t label) { return game_text_open(PTR(ALPHA_FILE), text(label)); }
static char *strings_get(char *s) { return game_strings_get(game_strings, s); }
static char *strings_put(char *s) { return game_strings_put(game_strings, s); }
static int stricmp_(const char *a, const char *b) { return game_stricmp(a, b); }

// The game's range(): min when below it (or when max is below min), max
// when above.
static int range(int v, int min, int max) {
    if (v < min || max < min) return min;
    return v > max ? max : v;
}

// strncpy, with the last byte made the end.
static void copy_n(char *dst, const char *src, size_t n) {
    strncpy(dst, src, n);
    dst[n - 1] = 0;
}

// Trailing spaces cut from a name.
static void trim(char *s) {
    while (strlen(s) && s[strlen(s) - 1] == ' ') s[strlen(s) - 1] = 0;
}

// An unknown name: the file, the name and the line, and the label's
// message.
static void bad_key(char *name, uint32_t label) {
    game_parse_says(0, text(TEXT_FILE_PATH), -1, -1);
    game_parse_says(1, name, -1, -1);
    game_parse_says(2, PTR(TEXT_LINE), -1, -1);
    game_x_pop(text(label), 0);
}

// ---- Names to indexes ---------------------------------------------------------

int rules_tech_name(char *name) {
    trim(name);
    if (!stricmp_(name, text(L_NONE))) return -1;
    if (!stricmp_(name, text(L_DISABLE))) return -2;
    for (int i = 0; i < TECHS_N; i++)
        if (!stricmp_(name, text(TECHS + 0x2c * i + 8))) return i;
    bad_key(name, L_BADTECHKEY);
    return -2;
}

// A part's name among `count` names, each the first pointer of an entry of
// `size` bytes from `table`.
static int part_name(char *name, uint32_t none, uint32_t disable, uint32_t table, int size, int count, uint32_t bad) {
    trim(name);
    if (!stricmp_(name, text(none))) return -1;
    if (!stricmp_(name, text(disable))) return -2;
    for (int i = 0; i < count; i++)
        if (!stricmp_(name, strings_get(PTR(table + (uint32_t)(size * i))))) return i;
    bad_key(name, bad);
    return 0;
}

int chas_name(char *name) { return part_name(name, L_NONE_2, L_DISABLE_2, CHASSIS, 0x90, CHASSIS_COUNT, L_BADCHASKEY); }
// The weapon's and armor's short names.
int weap_name(char *name) { return part_name(name, L_NONE_3, L_DISABLE_3, WEAPONS + 4, 0x10, WEAPON_COUNT, L_BADWEAPKEY); }
int arm_name(char *name) { return part_name(name, L_NONE_4, L_DISABLE_4, ARMOR + 4, 0x10, ARMOR_COUNT, L_BADARMKEY); }

int text_get_number(int min, int max) {
    text_get();
    return range(item_number(), min, max);
}

int tech_item(void) {
    text_get();
    return rules_tech_name(text_item());
}

// M or m male, F or f female, N or n neuter, else male; the second
// character 2 for plural (read even after an empty item's end, where the
// buffer holds what an earlier item left).
void noun_item(int32_t *gender, int32_t *plural) {
    char *s = text_item();
    *gender = 0, *plural = 0;
    if (s[0] == 'M' || s[0] == 'm') *gender = 0;
    if (s[0] == 'F' || s[0] == 'f') *gender = 1;
    if (s[0] == 'N' || s[0] == 'n') *gender = 2;
    if (s[1] == '1') *plural = 0;
    if (s[1] == '2') *plural = 1;
}

// ---- #RULES and #WORLDBUILDER ----------------------------------------------------
//
// One number per line, each kept within its range, but for a few lines of
// two and the techs. The ranges are the original's; Thinker's differ for a
// few (the yes/no rules here allow -1, retooling strictness goes to 2).

static void num_at(uint32_t addr, int min, int max) { I32(addr) = range(item_number(), min, max); }
static void line_num(uint32_t addr, int min, int max) { text_get(), num_at(addr, min, max); }
static void line_tech(uint32_t addr) { I32(addr) = tech_item(); }

int read_basic_rules(void) {
    if (open_alpha(L_RULES)) return 1;
    line_num(0x949738, 1, 100);                                   // movement points along roads
    line_num(0x94973c, 0, 100);                                   // nutrients a citizen eats
    text_get(), num_at(0x949748, 1, 1000), num_at(0x94974c, 1, 1000);  // artillery damage, as a fraction
    line_num(0x949744, 1, 8);                                     // artillery range
    line_num(0x949740, 1, 500);                                   // airdrop range without Orbital Insertion
    line_num(0x949750, 1, 100);                                   // nutrient costs
    line_num(0x949754, 1, 100);                                   // mineral costs
    line_num(0x949758, 0, 1000);                                  // the rate of discovery
    line_num(0x94975c, 0, 100);                                   // minerals limit for a mine without a road
    line_num(0x949760, -1, 0);                                    // nutrients from a mine's square
    line_num(0x949764, 0, 100);                                   // the base size for specialists
    line_num(0x949768, 0, 100);                                   // drones from a Genejack Factory
    line_num(0x94976c, 1, 100);                                   // population limit without a Hab Complex
    line_num(0x949770, 1, 100);                                   // and without a Habitation Dome
    line_num(0x949790, 0, 500);                                   // a prototype's extra cost: land,
    line_num(0x949794, 0, 500);                                   // sea
    line_num(0x949798, 0, 500);                                   // and air
    for (int t = 0; t < 3; t++)                                   // psi combat odds by triad
        text_get(), num_at(0x94979c + 4 * t, 1, 1000), num_at(0x9497a8 + 4 * t, 1, 1000);
    line_num(0x9497b4, 0, 1000);                                  // the energy reserve to start with
    // The combat modifiers.
    static const uint32_t combat[] = {0x9497b8, 0x9497bc, 0x9497c0, 0x9497c4, 0x9497f4, 0x9497f8, 0x949804,
                                      0x9497e4, 0x9497e8, 0x9497ec, 0x9497f0, 0x9497fc, 0x949800, 0x949808,
                                      0x94980c, 0x949810, 0x949814, 0x949818, 0x94981c, 0x949820, 0x949824};
    for (size_t i = 0; i < sizeof(combat) / sizeof(combat[0]); i++) line_num(combat[i], -100, 1000);
    line_num(0x94982c, 0, 100);   // the retooling penalty
    line_num(0x949828, 0, 2);     // retooling strictness
    line_num(0x949830, 0, 1000);  // retooling exemption
    line_num(0x9497cc, 0, 1000);  // turns between Councils
    line_num(0x9497d0, 0, 100);   // minerals from harvesting a forest
    line_num(0x9497d4, 0, 100);   // a base's territory
    line_num(0x9497d8, 1, 100);   // turns to corner the energy market
    // The techs for: fungus improvements, moving through fungus, roads in
    // fungus, two special abilities, three nutrients, minerals and energy
    // a square, Orbital Insertion without a Space Elevator, mining
    // platforms' bonus, and an economic victory.
    static const uint32_t techs[] = {0x949774, 0x949778, 0x94977c, 0x949780, 0x949784,
                                     0x949788, 0x94978c, 0x9497c8, 0x9497dc, 0x9497e0};
    for (size_t i = 0; i < sizeof(techs) / sizeof(techs[0]); i++) line_tech(techs[i]);
    line_num(0x949834, -1, 1);    // probe teams steal techs
    line_num(0x949838, -1, 1);    // humans always in contact: network games,
    line_num(0x94983c, -1, 1);    // e-mail games
    line_num(0x949840, 10, 100);  // the most damage artillery does: to a base or bunker,
    line_num(0x949844, 10, 100);  // in the open,
    line_num(0x949848, 10, 100);  // at sea
    text_get(), num_at(0x94984c, 0, 1000), num_at(0x949850, 1, 1000);  // global warming's frequency
    line_num(0x949854, 0, 999999);  // the starting year,
    line_num(0x949858, 0, 999999);  // the end year at the lower three levels,
    line_num(0x94985c, 0, 999999);  // and at the upper three
    line_num(0x949860, -1, 1);      // obliterating a base is an atrocity
    line_num(0x949864, 1, 999);     // the base size for a Subspace Generator,
    line_num(0x949868, 1, 999);     // and the number of them needed
    if (open_alpha(L_WORLDBUILDER)) return 1;
    static const int32_t world[][3] = {
        {0x9502a8, 50, 4000}, {0x9502ac, 0, 2000}, {0x9502b0, 5, 1000}, {0x9502b4, 5, 1000}, {0x9502b8, 0, 100},
        {0x9502bc, 0, 100},   {0x9502c0, 0, 500},  {0x9502c4, 0, 500},  {0x9502c8, 0, 100},  {0x9502cc, 0, 100},
        {0x9502d0, 1, 64},    {0x9502d4, 1, 64},   {0x9502d8, 1, 64},   {0x9502dc, 1, 64},   {0x9502e0, 1, 100},
        {0x9502e4, 0, 20},    {0x9502e8, 0, 20},   {0x9502ec, 0, 8},    {0x9502f0, -100, 100}, {0x9502f4, -100, 100},
        {0x9502f8, -100, 100}, {0x9502fc, -100, 100}, {0x950300, 0, 100}, {0x950304, -100, 100}, {0x950308, 0, 5},
    };
    for (size_t i = 0; i < sizeof(world) / sizeof(world[0]); i++) line_num((uint32_t)world[i][0], world[i][1], world[i][2]);
    text_get();  // the continents' size ratios, as they are
    for (int i = 0; i < 5; i++) I32(0x95030c + 4 * i) = item_number();
    line_num(0x950320, 1, 500);  // islands
    return 0;
}

// ---- #TECHNOLOGY ------------------------------------------------------------------
//
// Read twice: first the short names alone, each checked against those
// before it (a duplicate is reported, and reading goes on), then the rest.

int read_tech(void) {
    if (open_alpha(L_TECHNOLOGY)) return 1;
    for (int i = 0; i < TECHS_N; i++) {
        char *id = text(TECHS + 0x2c * i + 8);
        text_get();
        text_item();
        text_item();
        copy_n(id, PTR(TEXT_ITEM), 8);
        for (int j = 0; j < i; j++) {
            if (strcmp(id, text(TECHS + 0x2c * j + 8))) continue;
            game_parse_num(0, i);
            game_parse_num(1, j);
            game_parse_says(0, id, -1, -1);
            game_parse_says(1, text(FOUND_PATH), -1, -1);
            game_parse_says(2, PTR(TEXT_LINE), -1, -1);
            game_x_pop(text(L_DUPLICATETECH), 0);
        }
    }
    if (open_alpha(L_TECHNOLOGY_2)) return 1;
    I32(TECH_VALID) = 0, I32(TECH_COMMERCE) = 0;
    for (int i = 0; i < TECHS_N; i++) {
        uint32_t t = TECHS + 0x2c * (uint32_t)i;
        text_get();
        PTR(t + 4) = item_string();
        text_item();
        I32(t + 0x20) = item_number();  // the AI's weights: power,
        I32(t + 0x18) = item_number();  // tech,
        I32(t + 0x1c) = item_number();  // wealth,
        I32(t + 0x14) = item_number();  // growth
        I32(t + 0x24) = rules_tech_name(text_item());
        I32(t + 0x28) = rules_tech_name(text_item());
        uint32_t flags = (uint32_t)item_binary();
        I32(t) = (int32_t)flags;
        if (I32(t + 0x24) >= -1 && I32(t + 0x28) >= -1) {
            I32(TECH_VALID)++;
            if (flags & 4) I32(TECH_COMMERCE)++;
        }
    }
    return 0;
}

// ---- Faction files ------------------------------------------------------------------

// The +/- signs before a name: one up for each +, one down for each -.
static int signs(char **s) {
    int n = 0;
    for (;; (*s)++) {
        if (**s == '+') n++;
        else if (**s == '-') n--;
        else return n;
    }
}

static int is_bonus(const char *rule, int bonus) { return !stricmp_(rule, text(BONUS_NAMES + 24 * (uint32_t)bonus)); }

// Compares a name to the start of a social engineering name: whole in
// English, the first 4 letters in other languages.
static int social_match(const char *s, char *name) {
    return I32(LANGUAGE) ? !game_strnicmp(s, strings_get(name), 4) : !stricmp_(s, strings_get(name));
}

// The bonuses on the faction file's second line: name and value pairs,
// until an empty name. Unknown names are ignored. Thinker's version clamps
// some values and stops after a first match where this one goes on.
static void read_bonuses(uint8_t *d) {
#define FI32(off) (*(int32_t *)(d + (off)))
#define ADD_BONUS(id, v1)                         \
    do {                                          \
        int n_ = FI32(FD_BONUS_COUNT);            \
        FI32(FD_BONUS_ID + 4 * n_) = (id);        \
        FI32(FD_BONUS_VAL1 + 4 * n_) = (v1);      \
        FI32(FD_BONUS_COUNT)++;                   \
    } while (0)
    static const struct {
        uint8_t bonus;
        uint8_t id;
    } listed[] = {
        // Bonuses that are just added to the list, with their value.
        {BN_FACILITY, 2}, {BN_UNIT, 1}, {BN_FUNGNUTRIENT, 7}, {BN_FUNGMINERALS, 8}, {BN_FUNGENERGY, 9},
        {BN_VOTES, 0xb}, {BN_FREEFAC, 0xc}, {BN_REVOLT, 0xd}, {BN_NODRONE, 0xe}, {BN_FREEABIL, 0xf},
        {BN_PROBECOST, 0x10}, {BN_DEFENSE, 0x11}, {BN_OFFENSE, 0x12},
    };
    static const struct {
        uint8_t bonus;
        uint16_t off;
    } values[] = {
        // Bonuses that set a rule to their value.
        {BN_RESEARCH, FD_RESEARCH}, {BN_DRONE, FD_DRONE},       {BN_TALENT, FD_TALENT},
        {BN_ENERGY, FD_ENERGY},     {BN_COMMERCE, FD_COMMERCE}, {BN_POPULATION, FD_POPULATION},
        {BN_TECHCOST, FD_TECHCOST}, {BN_PSI, FD_PSI},           {BN_SHARETECH, FD_SHARETECH},
    };
    static const struct {
        uint8_t bonus;
        uint16_t flag;
    } flags[] = {
        // Bonuses that set a flag.
        {BN_TERRAFORM, 0x2000}, {BN_COMMFREQ, 0x1000}, {BN_MINDCONTROL, 0x800}, {BN_FANATIC, 0x400},
        {BN_FREEPROTO, 0x200},  {BN_AQUATIC, 0x100},   {BN_ALIEN, 0x80},        {BN_WORMPOLICE, 0x40},
        {BN_TECHSTEAL, 0x10},
    };
    // The original tests the names in this order, which matters only for
    // names listed twice in #BONUSNAMES.
    static const uint8_t order[] = {
        BN_TECH,        BN_MORALE,       BN_FACILITY,     BN_RESEARCH,     BN_DRONE,       BN_TALENT,
        BN_ENERGY,      BN_INTEREST,     BN_COMMERCE,     BN_POPULATION,   BN_HURRY,       BN_TECHCOST,
        BN_UNIT,        BN_PSI,          BN_SHARETECH,    BN_TERRAFORM,    BN_SOCIAL,      BN_ROBUST,
        BN_IMMUNITY,    BN_IMPUNITY,     BN_PENALTY,      BN_FUNGNUTRIENT, BN_FUNGMINERALS, BN_FUNGENERGY,
        BN_COMMFREQ,    BN_MINDCONTROL,  BN_FANATIC,      BN_VOTES,        BN_FREEPROTO,   BN_AQUATIC,
        BN_ALIEN,       BN_FREEFAC,      BN_REVOLT,       BN_NODRONE,      BN_WORMPOLICE,  BN_FREEABIL,
        BN_PROBECOST,   BN_DEFENSE,      BN_OFFENSE,      BN_TECHSHARE,    BN_TECHSTEAL,
    };
    text_get();
    if (!strlen(text_item())) return;
    do {
        char rule[1024];
        strcpy(rule, PTR(TEXT_ITEM));
        char *param = text_item();
        int b = -1;
        for (size_t i = 0; i < sizeof(order) && b < 0; i++)
            if (is_bonus(rule, order[i])) b = order[i];
        if (b < 0) continue;
        int full = FI32(FD_BONUS_COUNT) >= 8;
        if (b == BN_TECH) {
            // A number: free techs of the player's choice; a name: that tech.
            int n = game_atoi(param);
            if (n) FI32(FD_TECH_SELECTED) = n;
            else if (!full) ADD_BONUS(0, rules_tech_name(param));
        } else if (b == BN_MORALE) {
            if (*param == '+') param++;
            FI32(FD_MORALE) = game_atoi(param);
            if (!FI32(FD_MORALE)) FI32(FD_FLAGS) |= 0x8000;
        } else if (b == BN_INTEREST) {
            FI32(FD_FLAGS) |= 0x4000;
            FI32(FD_INTEREST) = game_atoi(param);
        } else if (b == BN_HURRY) {
            FI32(FD_HURRY) = range(game_atoi(param), 1, 1000);
        } else if (b == BN_TECHSHARE) {
            FI32(FD_FLAGS) |= 0x20;
            FI32(FD_SHARETECH) = game_atoi(param);
        } else if (b == BN_SOCIAL || b == BN_ROBUST || b == BN_IMMUNITY) {
            // An effect, with +/- signs for how much (none: 1).
            int v = signs(&param);
            if (!v) v = 1;
            int e = 0;
            while (e < SOCIAL_EFFECTS && stricmp_(param, text(SOCIAL_PARAMS + 0x68 * (uint32_t)e))) e++;
            if (e == SOCIAL_EFFECTS || full) continue;
            int n = FI32(FD_BONUS_COUNT);
            FI32(FD_BONUS_ID + 4 * n) = is_bonus(rule, BN_ROBUST) ? 0xa : is_bonus(rule, BN_SOCIAL) ? 3 : 4;
            FI32(FD_BONUS_VAL1 + 4 * n) = e;
            FI32(FD_BONUS_VAL2 + 4 * n) = v;
            FI32(FD_BONUS_COUNT)++;
        } else if (b == BN_IMPUNITY || b == BN_PENALTY) {
            // A social model: each model of that name (any category) is
            // listed, while there is room.
            for (int c = 0; c < SOCIAL_CATEGORIES; c++)
                for (int m = 0; m < SOCIAL_MODELS; m++) {
                    if (stricmp_(param, strings_get(PTR(SOCIAL_FIELDS + 0xd4 * (uint32_t)c + 0x14 + 4 * (uint32_t)m))))
                        continue;
                    int n = FI32(FD_BONUS_COUNT);
                    if (n >= 8) continue;
                    FI32(FD_BONUS_ID + 4 * n) = is_bonus(rule, BN_IMPUNITY) ? 5 : 6;
                    FI32(FD_BONUS_VAL1 + 4 * n) = c;
                    FI32(FD_BONUS_VAL2 + 4 * n) = m;
                    FI32(FD_BONUS_COUNT)++;
                }
        } else {
            int done = 0;
            for (size_t i = 0; i < sizeof(listed) / sizeof(listed[0]) && !done; i++)
                if (listed[i].bonus == b) {
                    if (!full) ADD_BONUS(listed[i].id, game_atoi(param));
                    done = 1;
                }
            for (size_t i = 0; i < sizeof(values) / sizeof(values[0]) && !done; i++)
                if (values[i].bonus == b) FI32(values[i].off) = game_atoi(param), done = 1;
            for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]) && !done; i++)
                if (flags[i].bonus == b) FI32(FD_FLAGS) |= flags[i].flag, done = 1;
        }
    } while (strlen(text_item()));
#undef ADD_BONUS
#undef FI32
}

// The leader's social preferences, twice (the ideology, then the model it
// shuns): a category (the first that matches), a model in it (the last
// that matches), and an effect. The effect is always left at -1: the
// original searches for it, and on a match sets -1 again (Thinker keeps
// the match).
static void read_ideology(uint8_t *d) {
    for (int i = 0; i < 2; i++) {
        int32_t *cat = (int32_t *)(d + FD_IDEOLOGY_CATEGORY + 4 * i);
        int32_t *model = (int32_t *)(d + FD_IDEOLOGY_MODEL + 4 * i);
        int32_t *effect = (int32_t *)(d + FD_IDEOLOGY_EFFECT + 4 * i);
        char *buf = text(STR_BUF);
        *cat = -1, *model = 0, *effect = -1;
        text_get();
        buf[0] = 0;
        strcat(buf, text_item());
        for (int c = 0; c < SOCIAL_CATEGORIES; c++)
            if (social_match(buf, PTR(SOCIAL_FIELDS + 0xd4 * (uint32_t)c))) {
                *cat = c;
                break;
            }
        buf[0] = 0;
        strcat(buf, text_item());
        if (*cat >= 0)
            for (int m = 0; m < SOCIAL_MODELS; m++)
                if (social_match(buf, PTR(SOCIAL_FIELDS + 0xd4 * (uint32_t)*cat + 0x14 + 4 * (uint32_t)m))) *model = m;
        buf[0] = 0;
        strcat(buf, text_item());
        for (int e = 0; e < SOCIAL_EFFECTS; e++)
            if (!stricmp_(buf, text(SOCIAL_PARAMS + 0x68 * (uint32_t)e))) {
                *effect = -1;
                break;
            }
    }
}

void read_faction(struct faction_def *def, int toggle) {
    uint8_t *d = (uint8_t *)def;
    clear_faction(def);
    if (game_text_open((char *)d + FD_FILENAME, (char *)d + FD_SEARCH_KEY)
        && game_text_open((char *)d + FD_FILENAME, (char *)d + FD_FILENAME)) {
        game_parse_says(0, (char *)d + FD_SEARCH_KEY, -1, -1);
        game_parse_says(1, (char *)d + FD_FILENAME, -1, -1);
        game_x_pop(text(L_PLAYERFILE), 0);
        return;
    }
    // The names: formal, descriptive, the noun with its gender and number
    // (1 or 2), the leader and theirs.
    text_get();
    copy_n((char *)d + FD_FORMAL_NAME, text_item(), 40);
    copy_n((char *)d + FD_DESC_NAME, text_item(), 24);
    copy_n((char *)d + FD_NOUN, text_item(), 24);
    text_item();
    char c = PTR(TEXT_ITEM)[0];
    *(int32_t *)(d + FD_NOUN_GENDER) = 0;
    if (c == 'F' || c == 'f') *(int32_t *)(d + FD_NOUN_GENDER) = 1;
    c = PTR(TEXT_ITEM)[0];
    if (c == 'N' || c == 'n') *(int32_t *)(d + FD_NOUN_GENDER) = 2;
    *(int32_t *)(d + FD_NOUN_PLURAL) = range((int)((unsigned)item_number() - 1u), 0, 1);
    copy_n((char *)d + FD_LEADER, text_item(), 24);
    text_item();
    c = PTR(TEXT_ITEM)[0];
    *(int32_t *)(d + FD_FEMALE) = c == 'F' || c == 'f';
    if (toggle == 2) return;
    // The AI's priorities, as they are (Thinker clamps them).
    *(int32_t *)(d + FD_AI_FIGHT) = item_number();
    *(int32_t *)(d + FD_AI_POWER) = item_number();
    *(int32_t *)(d + FD_AI_TECH) = item_number();
    *(int32_t *)(d + FD_AI_WEALTH) = item_number();
    *(int32_t *)(d + FD_AI_GROWTH) = item_number();
    read_bonuses(d);
    read_ideology(d);
    text_get();
    copy_n((char *)d + FD_ADJ_NAME, text_item(), 128);
    text_get();
    copy_n((char *)d + FD_ASSISTANT, text_item(), 24);
    copy_n((char *)d + FD_SCIENTIST, text_item(), 24);
    copy_n((char *)d + FD_ASSISTANT_CITY, text_item(), 24);
    text_get();
    copy_n((char *)d + FD_TITLE, text_item(), 24);
    copy_n((char *)d + FD_ADJ_LEADER, text_item(), 128);
    copy_n((char *)d + FD_ADJ_INSULT_LEADER, text_item(), 128);
    copy_n((char *)d + FD_ADJ_FACTION, text_item(), 128);
    copy_n((char *)d + FD_ADJ_INSULT_FACTION, text_item(), 128);
    text_get();
    copy_n((char *)d + FD_INSULT_LEADER, text_item(), 24);
}

void read_faction2(int faction) {
    if (!faction) return;
    read_faction(&game_faction_defs[faction], 0);
    game_load_faction_art(faction);
}

static uint8_t *def_at(int faction) { return (uint8_t *)at(FACTION_DEFS + 0x59c * (uint32_t)faction); }

// The bonus names, the files of the factions playing (Alien Crossfire's
// unless the original game is played; the expansion's settings may choose
// others), and each faction read with its art. A faction left "random"
// (filename JENN282) is drawn from all the factions listed, including
// #CUSTOMFACTIONS: a draw of a faction already playing is drawn again. A
// drawn faction's search key is its filename.
int read_factions(void) {
    if (open_alpha(L_BONUSNAMES)) return 1;
    text_get();
    for (int i = 0, k = 0; i < BONUS_N; i++) {
        strcpy(text(BONUS_NAMES + 24 * (uint32_t)i), text_item());
        if (++k >= 8) k = 0, text_get();
    }
    if (open_alpha(I32(EXPANSION) ? L_NEWFACTIONS : L_FACTIONS)) return 1;
    for (int f = 1; f < FACTION_COUNT; f++) {
        text_get();
        strcpy((char *)def_at(f) + FD_FILENAME, text_item());
        strcpy((char *)def_at(f) + FD_SEARCH_KEY, text_item());
    }
    if (I32(EXPANSION)) game_prefs_fac_load();
    int listed = 14;
    if (!open_alpha(L_CUSTOMFACTIONS)) {
        text_get();
        if (*text_item())
            do {
                listed++;
                text_get();
            } while (*text_item());
    }
    for (int f = 1; f < FACTION_COUNT; f++) {
        char *name = (char *)def_at(f) + FD_FILENAME;
        if (strcmp(name, text(L_RANDOM))) continue;
        int again;
        do {
            int r = game_rand() % listed;
            // Seven to a list; past the third, the lines after the last.
            static const uint32_t lists[] = {L_FACTIONS_2, L_NEWFACTIONS_2, L_CUSTOMFACTIONS_2};
            if (r / 7 < 3 && open_alpha(lists[r / 7])) return 1;
            again = 0;
            text_get();
            for (int k = r % 7; k > 0; k--) text_get();
            strcpy(name, text_item());
            memmove(name + 0x18, name, strlen(name) + 1);  // strcpy over the search key
            for (int g = 1; g < FACTION_COUNT; g++)
                if (g != f && !strcmp(name, (char *)def_at(g) + FD_FILENAME)) {
                    again = 1;
                    break;
                }
            if (!again) {
                read_faction((struct faction_def *)def_at(f), 0);
                game_load_faction_art(f);
            }
        } while (again);
    }
    for (int f = 1; f < FACTION_COUNT; f++) {
        read_faction((struct faction_def *)def_at(f), 0);
        game_load_faction_art(f);
    }
    return 0;
}

// ---- #UNITS -----------------------------------------------------------------------
//
// The basic designs: each made with make_proto, then its plan, cost and
// capacity as listed where they are set (plan -1 and cost or capacity 0
// leave make_proto's), and its icon. The three Battle Ogres get reactors
// 1, 2 and 3 by their place in the list; every other design reactor 1.
// Unlike Thinker's version, None and Disable are taken as part names
// (-1 and -2), as the original does.

int read_units(void) {
    if (open_alpha(L_UNITS)) return 1;
    text_get();
    int n = range(item_number(), 0, 64);
    for (int p = 0; p < n; p++) {
        uint32_t u = PROTOS + 0x34 * (uint32_t)p;
        text_get();
        strcpy(text(u), text_item());
        int chassis = chas_name(text_item());
        int weapon = weap_name(text_item());
        int armor = arm_name(text_item());
        int plan = item_number(), cost = item_number(), carry = item_number();
        I16(u + 0x32) = (int16_t)rules_tech_name(text_item());
        int icon = item_number();
        uint32_t abilities = (uint32_t)item_binary();
        make_proto(p, chassis, weapon, armor, abilities, p == 17 ? 2 : p == 18 ? 3 : 1);
        if (plan >= 0) U8(u + 0x2a) = (uint8_t)plan;
        if (cost) U8(u + 0x29) = (uint8_t)cost;
        if (carry) U8(u + 0x28) = (uint8_t)carry;
        U8(u + 0x2e) = (uint8_t)icon;
    }
    return 0;
}

// ---- Everything -------------------------------------------------------------------

// Reads `count` lines of `per` strings each (kept in the string table) to
// entries of `size` bytes from `table`.
static int read_strings(uint32_t label, uint32_t table, int count, int per, int size) {
    if (open_alpha(label)) return 1;
    for (int i = 0; i < count; i++) {
        text_get();
        for (int k = 0; k < per; k++) PTR(table + (uint32_t)(size * i + 4 * k)) = item_string();
    }
    return 0;
}

static void chassis_name(uint32_t c, int name, int gender, int plural) {
    PTR(c + (uint32_t)name) = item_string();
    noun_item((int32_t *)at(c + (uint32_t)gender), (int32_t *)at(c + (uint32_t)plural));
}

// The map editor's buttons get their help: fifteen terraforming buttons
// the terraforming names, the original's way of skipping terraforms with
// no bit of their own (a count bumped for each such one at or before it);
// four more some landmarks' short names.
static void button_help(void) {
    for (int k = 0; k < 15; k++) {
        int n = k;
        for (int t = 0; t < TERRAFORM_N; t++)
            if (!I32(TERRAFORM + 0x20 * (uint32_t)t + 0x10) && n >= t) n++;
        game_button_set_bubble_text(at(FLAT_BUTTONS + 0xb4c * (uint32_t)k), strings_get(PTR(TERRAFORM + 0x20 * (uint32_t)n)));
    }
    static const uint32_t more[][2] = {{0x7c5fa0, 2}, {0x7c6aec, 6}, {0x7c7638, 3}, {0x7c8184, 10}};
    for (int i = 0; i < 4; i++) game_button_set_bubble_text(at(more[i][0]), strings_get(PTR(NATURAL + 8 * more[i][1] + 4)));
}

int read_rules(int all) {
    game_strings_init(game_strings, 0xc320);
    if (game_labels_init()) return 1;
    game_text_clear_index();
    game_text_make_index(PTR(SCRIPT_FILE));
    game_text_make_index(PTR(ALPHA_FILE));
    if (read_tech() || read_basic_rules() || open_alpha(L_TERRAIN)) return 1;
    // Terraforming, land and sea: names, techs, rate, the order's text
    // (made from a pattern with each name) and keys.
    for (int t = 0; t < TERRAFORM_N; t++) {
        uint32_t T = TERRAFORM + 0x20 * (uint32_t)t, O = ORDERS + 0xc * (uint32_t)(t + 4);
        char pattern[1024];
        text_get();
        for (int k = 0; k < 2; k++) {
            PTR(T + 4 * (uint32_t)k) = item_string();
            I32(T + 8 + 4 * (uint32_t)k) = rules_tech_name(text_item());
        }
        I32(T + 0x18) = item_number();
        strcpy(pattern, text_item());
        for (int k = 0; k < 2; k++) {
            game_parse_say(0, (int)(uintptr_t)PTR(T + 4 * (uint32_t)k), -1, -1);
            text(STR_BUF)[0] = 0;
            game_parse_string(pattern, text(STR_BUF));
            PTR(O + 4 * (uint32_t)k) = strings_put(text(STR_BUF));
        }
        PTR(O + 8) = item_string();
        PTR(T + 0x1c) = item_string();
    }
    if (open_alpha(L_RESOURCEINFO)) return 1;
    for (int i = 0; i < 9; i++) {
        text_get();
        text_item();
        for (int k = 0; k < 4; k++) I32(RES_INFO + 16 * (uint32_t)i + 4 * (uint32_t)k) = item_number();
    }
    if (open_alpha(L_TIMECONTROLS)) return 1;
    for (int i = 0; i < 6; i++) {
        uint32_t t = TIME_CONTROLS + 0x20 * (uint32_t)i;
        text_get();
        PTR(t) = item_string();
        for (int k = 1; k <= 7; k++) I32(t + 4 * (uint32_t)k) = item_number();
    }
    if (open_alpha(L_CHASSIS)) return 1;
    for (int i = 0; i < CHASSIS_COUNT; i++) {
        uint32_t c = CHASSIS + 0x90 * (uint32_t)i;
        text_get();
        chassis_name(c, 0x0, 0x18, 0x30);   // offensive,
        chassis_name(c, 0x4, 0x1c, 0x34);   // its second form,
        chassis_name(c, 0xc, 0x24, 0x3c);   // defensive,
        chassis_name(c, 0x10, 0x28, 0x40);  // its second form
        U8(c + 0x48) = (uint8_t)item_number();  // speed
        U8(c + 0x49) = (uint8_t)item_number();  // triad
        U8(c + 0x4a) = (uint8_t)item_number();  // range
        U8(c + 0x4d) = (uint8_t)item_number();  // missile
        U8(c + 0x4b) = (uint8_t)item_number();  // cargo
        U8(c + 0x4c) = (uint8_t)item_number();  // cost
        I16(c + 0x8e) = (int16_t)rules_tech_name(text_item());
        chassis_name(c, 0x8, 0x20, 0x38);   // offensive, large,
        chassis_name(c, 0x14, 0x2c, 0x44);  // defensive, large
    }
    // Reactors' power is left as it was.
    if (open_alpha(L_REACTORS)) return 1;
    for (int i = 0; i < REACTOR_COUNT; i++) {
        uint32_t r = REACTORS + 0xc * (uint32_t)i;
        text_get();
        PTR(r) = item_string();
        PTR(r + 4) = item_string();
        text_item();
        I16(r + 8) = (int16_t)rules_tech_name(text_item());
    }
    if (open_alpha(L_WEAPONS)) return 1;
    for (int i = 0; i < WEAPON_COUNT; i++) {
        uint32_t w = WEAPONS + 0x10 * (uint32_t)i;
        text_get();
        PTR(w) = item_string();
        PTR(w + 4) = item_string();
        U8(w + 8) = (uint8_t)item_number();    // offense
        U8(w + 0xa) = (uint8_t)item_number();  // mode
        U8(w + 0xb) = (uint8_t)item_number();  // cost
        U8(w + 9) = (uint8_t)item_number();    // icon
        I16(w + 0xc) = (int16_t)rules_tech_name(text_item());
    }
    if (open_alpha(L_DEFENSES)) return 1;
    for (int i = 0; i < ARMOR_COUNT; i++) {
        uint32_t a = ARMOR + 0x10 * (uint32_t)i;
        text_get();
        PTR(a) = item_string();
        PTR(a + 4) = item_string();
        U8(a + 8) = (uint8_t)item_number();    // defense
        U8(a + 9) = (uint8_t)item_number();    // mode
        U8(a + 0xa) = (uint8_t)item_number();  // cost
        I16(a + 0xc) = (int16_t)rules_tech_name(text_item());
    }
    if (open_alpha(L_ABILITIES)) return 1;
    for (int i = 0; i < 29; i++) {
        uint32_t a = ABILITIES + 0x1c * (uint32_t)i;
        text_get();
        PTR(a) = item_string();
        I32(a + 0xc) = item_number();  // cost
        I16(a + 0x18) = (int16_t)rules_tech_name(text_item());
        PTR(a + 8) = item_string();  // abbreviation
        I32(a + 0x14) = item_binary();
        PTR(a + 4) = item_string();  // description
    }
    if (read_strings(L_MORALE, MORALE, 7, 2, 8) || read_strings(L_DEFENSEMODES, DEFENSE_MODES, 3, 4, 0x10)
        || read_strings(L_OFFENSEMODES, OFFENSE_MODES, 3, 4, 0x10))
        return 1;
    // The basic designs, for a new game: every design's name, flags and a
    // byte at +0x2b cleared first.
    if (all) {
        for (int p = 0; p < PROTO_COUNT; p++) {
            uint32_t u = PROTOS + 0x34 * (uint32_t)p;
            U8(u) = 0, I16(u + 0x30) = 0, U8(u + 0x2b) = 0;
        }
        if (read_units()) return 1;
    }
    // Facilities (from 1): the tech that would make one free is read and
    // then set to -2 regardless, as the original does. Secret projects
    // (from 70) have the AI's weights.
    if (open_alpha(L_FACILITIES)) return 1;
    for (int i = 1; i < FACILITY_COUNT; i++) {
        uint32_t f = FACILITIES + 0x30 * (uint32_t)i;
        text_get();
        PTR(f) = item_string();
        I32(f + 0xc) = item_number();  // cost
        I32(f + 0x10) = item_number();  // maintenance
        I32(f + 0x14) = rules_tech_name(text_item());
        I32(f + 0x18) = rules_tech_name(text_item());
        PTR(f + 4) = item_string();  // effect
        I32(f + 0x18) = -2;
        if (i >= 70) {
            I32(f + 0x1c) = item_number();  // fight,
            I32(f + 0x2c) = item_number();  // power,
            I32(f + 0x24) = item_number();  // tech,
            I32(f + 0x28) = item_number();  // wealth,
            I32(f + 0x20) = item_number();  // growth
        }
    }
    // The orders other than terraforming's (0 to 3 and 24 on): the same
    // text on land and at sea.
    if (open_alpha(L_ORDERS)) return 1;
    for (int i = 0; i < 30; i++) {
        if (i >= 4 && i < 24) continue;
        uint32_t o = ORDERS + 0xc * (uint32_t)i;
        text_get();
        PTR(o) = PTR(o + 4) = item_string();
        PTR(o + 8) = item_string();
    }
    if (read_strings(L_COMPASS, COMPASS, 8, 1, 4)) return 1;
    if (open_alpha(L_PLANS)) return 1;
    for (int i = 0; i < 15; i++) {
        text_get();
        PTR(PLANS_SHORT + 4 * (uint32_t)i) = item_string();
        PTR(PLANS_FULL + 4 * (uint32_t)i) = item_string();
    }
    if (read_strings(L_TRIAD, TRIAD, 3, 1, 4) || read_strings(L_RESOURCES, RESOURCES, 4, 2, 8)
        || read_strings(L_ENERGY, ENERGY, 3, 2, 8) || open_alpha(L_CITIZENS))
        return 1;
    for (int i = 0; i < 10; i++) {
        uint32_t c = CITIZENS + 0x1c * (uint32_t)i;
        text_get();
        PTR(c) = item_string();
        PTR(c + 4) = item_string();
        if (i < 7) {
            I32(c + 8) = rules_tech_name(text_item());
            I32(c + 0xc) = rules_tech_name(text_item());
            for (int k = 0; k < 3; k++) I32(c + 0x10 + 4 * (uint32_t)k) = item_number();
        }
    }
    // Social engineering: the effects' two sets of names (copied, not kept
    // in the string table), the categories, and each model's name, tech and
    // effects. An effect is a name after +/- signs (as many steps as
    // signs); it is added to every effect of that name (Thinker's version
    // sets the first).
    if (open_alpha(L_SOCIO)) return 1;
    for (int k = 0; k < 2; k++) {
        text_get();
        for (int e = 0; e < SOCIAL_EFFECTS; e++) strcpy(text(SOCIAL_PARAMS + 0x68 * (uint32_t)e + 0x18 * (uint32_t)k), text_item());
    }
    text_get();
    for (int c = 0; c < SOCIAL_CATEGORIES; c++) PTR(SOCIAL_FIELDS + 0xd4 * (uint32_t)c) = item_string();
    for (int c = 0; c < SOCIAL_CATEGORIES; c++)
        for (int m = 0; m < SOCIAL_MODELS; m++) {
            uint32_t s = SOCIAL_FIELDS + 0xd4 * (uint32_t)c;
            int32_t *effects = (int32_t *)at(s + 0x24 + 0x2c * (uint32_t)m);
            text_get();
            PTR(s + 0x14 + 4 * (uint32_t)m) = item_string();
            I32(s + 4 + 4 * (uint32_t)m) = rules_tech_name(text_item());
            memset(effects, 0, 0x2c);
            if (!strlen(text_item())) continue;
            do {
                char *name = PTR(TEXT_ITEM);
                int v = signs(&name);
                for (int e = 0; e < SOCIAL_EFFECTS; e++)
                    if (!stricmp_(name, text(SOCIAL_PARAMS + 0x68 * (uint32_t)e))) effects[e] += v;
            } while (strlen(text_item()));
        }
    if (read_strings(L_DIFF, DIFFICULTY, 6, 1, 4)) return 1;
    if (all && read_factions()) return 1;
    if (read_strings(L_MANDATE, MANDATES, 4, 2, 8) || read_strings(L_MOOD, MOODS, 9, 1, 4)
        || read_strings(L_REPUTE, REPUTE, 8, 1, 4) || read_strings(L_MIGHT, MIGHT, 7, 2, 8))
        return 1;
    if (open_alpha(L_PROPOSALS)) return 1;
    for (int i = 0; i < 11; i++) {
        uint32_t p = PROPOSALS + 0xc * (uint32_t)i;
        text_get();
        PTR(p) = item_string();
        I32(p + 8) = rules_tech_name(text_item());
        PTR(p + 4) = item_string();
    }
    if (read_strings(L_NATURAL, NATURAL, 16, 2, 8)) return 1;
    button_help();
    return 0;
}

// ---- Hooks ------------------------------------------------------------------------------

// Non-zero while the check runs the originals: the game's calls to these
// functions (from inside an original) then go to the originals.
static int side_original;

#define RULES_ENTRY(ret, name, ours, params, args)                                                \
    static struct hook_stats stats_entry_##name = {#name};                                       \
    static ret entry_##name params { return side_original ? orig_entry_##name args : ours args; } \
    static ret shadow_entry_##name params {                                                      \
        stats_entry_##name.calls++;                                                              \
        return orig_entry_##name args;                                                           \
    }

RULES_ENTRY(int, tech_name, rules_tech_name, (char *s), (s))
RULES_ENTRY(int, chas_name, chas_name, (char *s), (s))
RULES_ENTRY(int, weap_name, weap_name, (char *s), (s))
RULES_ENTRY(int, arm_name, arm_name, (char *s), (s))
RULES_ENTRY(int, text_get_number, text_get_number, (int a, int b), (a, b))
RULES_ENTRY(int, tech_item, tech_item, (void), ())
RULES_ENTRY(int, read_basic_rules, read_basic_rules, (void), ())
RULES_ENTRY(int, read_tech, read_tech, (void), ())
RULES_ENTRY(int, read_factions, read_factions, (void), ())
RULES_ENTRY(int, read_units, read_units, (void), ())

static struct hook_stats stats_entry_noun_item = {"noun_item"};
static void entry_noun_item(int32_t *g, int32_t *p) {
    if (side_original) orig_entry_noun_item(g, p);
    else noun_item(g, p);
}
static void shadow_entry_noun_item(int32_t *g, int32_t *p) {
    stats_entry_noun_item.calls++;
    orig_entry_noun_item(g, p);
}

// The game's calls of the readers, when CHIRON_RULES_CHECK is set (and no
// check is running already: the originals' own calls of the readers, in
// shadow mode, go straight to the originals).
static int check_enabled(void);
static int check_busy;
static int check_read_rules(int all, int shadow);
static void check_read_faction(struct faction_def *def, int toggle, int shadow);
static void check_read_faction2(int faction, int shadow);

static struct hook_stats stats_entry_read_rules = {"read_rules"};
static int entry_read_rules(int all) {
    if (side_original) return orig_entry_read_rules(all);
    if (!check_enabled()) return read_rules(all);
    check_busy = 1;
    int r = check_read_rules(all, 0);
    check_busy = 0;
    return r;
}
static int shadow_entry_read_rules(int all) {
    if (side_original || !check_enabled()) return stats_entry_read_rules.calls++, orig_entry_read_rules(all);
    stats_entry_read_rules.calls++;
    check_busy = 1;
    int r = check_read_rules(all, 1);
    check_busy = 0;
    return r;
}

static struct hook_stats stats_entry_read_faction = {"read_faction"};
static void entry_read_faction(struct faction_def *d, int t) {
    if (side_original) orig_entry_read_faction(d, t);
    else if (!check_enabled()) read_faction(d, t);
    else check_busy = 1, check_read_faction(d, t, 0), check_busy = 0;
}
static void shadow_entry_read_faction(struct faction_def *d, int t) {
    stats_entry_read_faction.calls++;
    if (side_original || !check_enabled()) orig_entry_read_faction(d, t);
    else check_busy = 1, check_read_faction(d, t, 1), check_busy = 0;
}

static struct hook_stats stats_entry_read_faction2 = {"read_faction2"};
static void entry_read_faction2(int f) {
    if (side_original) orig_entry_read_faction2(f);
    else if (!check_enabled()) read_faction2(f);
    else check_busy = 1, check_read_faction2(f, 0), check_busy = 0;
}
static void shadow_entry_read_faction2(int f) {
    stats_entry_read_faction2.calls++;
    if (side_original || !check_enabled()) orig_entry_read_faction2(f);
    else check_busy = 1, check_read_faction2(f, 1), check_busy = 0;
}

// ---- The check (CHIRON_RULES_CHECK) ------------------------------------------------------
//
// CHIRON_RULES_CHECK=1: whenever the game reads its rules (read_rules: at
// startup and for each new game) or a faction file (read_faction or
// read_faction2, as the game's setup screens and some events do), the read
// is made by the original and by Chiron in turn, from the same state, and
// the game's writable memory (0x682000-0x9ca000) after each is compared
// byte for byte.
//
// Before each run the tables the readers write (check_tables, or the one
// faction definition) are poisoned, filled with the same pattern, so a
// field one of them leaves unwritten shows; spared are only the bytes the
// readers read there without writing (the terraforming bits, a faction's
// file names). rand()'s state and the game's own generator's are put back
// too. Memory the original wrote outside the poisoned regions is listed
// ("also written") for the record. After the comparison the tables are put
// back and the read made once more, by Chiron (or in shadow mode the
// original), for the game to go on with.
//
// The rest of the game's memory can't be put back between the runs: the C
// runtime's heap keeps its bookkeeping there, and the string table is
// freed and made anew by each read. So pointers are compared for what they
// point at:
// - text kept in the string table (most names in the tables, the labels):
//   each run's pointer must be at the same offset in that run's table, and
//   the two tables' used bytes the same;
// - the labels' list and the map editor's button help (allocated anew by
//   each run): the lists and texts compared, not their addresses.
// Memory left out of the comparison entirely is listed in check_ignored,
// with why.
//
// With read_rules(1) (a new game), the rules are also read with the other
// game's factions (Alien Crossfire's #NEWFACTIONS or the original's
// #FACTIONS, by switching the expansion flag), then as the game asked.
// After each read_rules, every faction file listed in #FACTIONS,
// #NEWFACTIONS and #CUSTOMFACTIONS is read by both readers into a spare
// faction definition (faction 0's, put back after), whole and names only;
// for a new game read_faction2 is checked for factions 1 to 7; and with
// CHIRON_USER_DIR set, the small helpers the game itself never calls
// (text_get_number, tech_item, noun_item) are run on a file of test lines
// made there (rulechk.txt) from the game's own tech names.

enum { MEM_BASE = 0x682000, MEM_END = 0x9ca000, BUTTONS_N = 19 };

static int check_on = -1, check_count, check_failures;

static int check_enabled(void) {
    if (check_on < 0) {
        const char *v = getenv("CHIRON_RULES_CHECK");
        check_on = v && *v && *v != '0';
    }
    return check_on && !check_busy && !hooks_protecting();
}

static void report(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void report(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "chiron: rules check: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

struct region {
    uint32_t addr, size;
};

// The tables the readers write.
static const struct region check_tables[] = {
    {TECH_VALID, 8},
    {RULES, 0x949868 + 4 - RULES},
    {WORLDBUILDER, 0x950324 - WORLDBUILDER},
    {TECHS, 0x2c * TECHS_N},
    {TERRAFORM, 0x20 * TERRAFORM_N},
    {ORDERS, 0xc * 30},
    {RES_INFO, 0x90},
    {TIME_CONTROLS, 0x20 * 6},
    {CHASSIS, 0x90 * CHASSIS_COUNT},
    {REACTORS, 0xc * REACTOR_COUNT},
    {WEAPONS, 0x10 * WEAPON_COUNT},
    {ARMOR, 0x10 * ARMOR_COUNT},
    {ABILITIES, 0x1c * 29},
    {MORALE, 8 * 7},
    {DEFENSE_MODES, 0x10 * 3},
    {OFFENSE_MODES, 0x10 * 3},
    {PROTOS, 0x34 * PROTO_COUNT},
    {FACILITIES, 0x30 * FACILITY_COUNT},
    {COMPASS, 4 * 8},
    {PLANS_SHORT, 4 * 15},
    {PLANS_FULL, 4 * 15},
    {TRIAD, 4 * 3},
    {RESOURCES, 8 * 4},
    {ENERGY, 8 * 3},
    {CITIZENS, 0x1c * 10},
    {SOCIAL_PARAMS, 0x68 * SOCIAL_EFFECTS},
    {SOCIAL_FIELDS, 0xd4 * SOCIAL_CATEGORIES},
    {DIFFICULTY, 4 * 6},
    {MANDATES, 8 * 4},
    {MOODS, 4 * 9},
    {REPUTE, 4 * 8},
    {MIGHT, 8 * 7},
    {PROPOSALS, 0xc * 11},
    {NATURAL, 8 * 16},
    {BONUS_NAMES, 24 * BONUS_N},
    {FACTION_DEFS, 0x59c * FACTION_COUNT},
};
enum { CHECK_TABLES = sizeof(check_tables) / sizeof(check_tables[0]) };

static uint32_t button(int i) {
    static const uint32_t more[] = {0x7c5fa0, 0x7c6aec, 0x7c7638, 0x7c8184};
    return i < 15 ? FLAT_BUTTONS + 0xb4c * (uint32_t)i : more[i - 15];
}

// Memory not compared, and why.
static int check_ignored(uint32_t a) {
    // The text index (text_make_index, the game's library): each section's
    // place in alpha(x).txt and script.txt, in heaps allocated anew.
    if (a >= 0x9b7e08 && a < 0x9b8268) return 1;
    // The labels' list and the buttons' help: allocated anew, compared by
    // their contents.
    if (a >= LABELS && a < LABELS + 4) return 1;
    for (int i = 0; i < BUTTONS_N; i++)
        if (a >= button(i) + BUBBLE_TEXT && a < button(i) + BUBBLE_TEXT + 4) return 1;
    // The faction art (load_faction_art, the game's own, called alike by
    // both): each of a faction's 37 sprites (0x2c bytes each, from
    // 0x78e978) holds its pixels' address at +4, and the image buffer the
    // art is loaded through (0x798668) its own at +0x4b0, allocated anew.
    if (a >= 0x78e978 && a < 0x78e978 + 0x2c * 37 * FACTION_COUNT && (a - 0x78e978) % 0x2c >= 4
        && (a - 0x78e978) % 0x2c < 8)
        return 1;
    if (a >= 0x798b18 && a < 0x798b1c) return 1;
    // The C runtime's count of files opened (one more for each fopen), and
    // its table of FILEs (_iob, 20 of 0x20 bytes): the file the text reader
    // has open gets its buffer when first read, and an OS handle, either of
    // which can differ between the runs.
    if (a >= 0x9c0504 && a < 0x9c0508) return 1;
    if (a >= 0x6a5d80 && a < 0x6a5d80 + 20 * 0x20) return 1;
    return 0;
}

static int in_regions(uint32_t a, const struct region *r, int n) {
    for (int i = 0; i < n; i++)
        if (a >= r[i].addr && a < r[i].addr + r[i].size) return 1;
    return 0;
}

// The state after a run.
struct state {
    uint8_t *mem;
    uint32_t heap_base, heap_next;  // the string table: its block and how much is used
    uint8_t *heap;
    size_t heap_cap;
    uint32_t *labels;  // the labels' pointers
    int labels_n, labels_cap;
    char bubble[BUTTONS_N][256];
    int has_bubble[BUTTONS_N];
};

static void state_take(struct state *s) {
    size_t n = MEM_END - MEM_BASE;
    if (!s->mem) s->mem = malloc(n);
    memcpy(s->mem, at(MEM_BASE), n);
    s->heap_base = *(uint32_t *)at(STRINGS + 4), s->heap_next = *(uint32_t *)at(STRINGS + 8);
    size_t used = s->heap_next >= s->heap_base ? s->heap_next - s->heap_base : 0;
    if (used > s->heap_cap) s->heap = realloc(s->heap, used), s->heap_cap = used;
    if (used) memcpy(s->heap, (void *)(uintptr_t)s->heap_base, used);
    s->labels_n = I32(LABELS + 4);
    if (s->labels_n < 0) s->labels_n = 0;
    if (s->labels_n > s->labels_cap) s->labels = realloc(s->labels, 4 * (size_t)s->labels_n), s->labels_cap = s->labels_n;
    uint32_t list = *(uint32_t *)at(LABELS);
    if (list && s->labels_n) memcpy(s->labels, (void *)(uintptr_t)list, 4 * (size_t)s->labels_n);
    else s->labels_n = 0;
    for (int i = 0; i < BUTTONS_N; i++) {
        uint32_t p = *(uint32_t *)at(button(i) + BUBBLE_TEXT);
        s->has_bubble[i] = p != 0;
        s->bubble[i][0] = 0;
        if (p) snprintf(s->bubble[i], sizeof(s->bubble[i]), "%s", (char *)(uintptr_t)p);
    }
}

// Both pointers into their run's string table, at the same place.
static int same_string(const struct state *a, const struct state *b, uint32_t pa, uint32_t pb) {
    return pa >= a->heap_base && pa < a->heap_next && pb >= b->heap_base && pb < b->heap_next
           && pa - a->heap_base == pb - b->heap_base;
}

static uint32_t word_at(const uint8_t *mem, uint32_t a) {
    uint32_t w;
    memcpy(&w, mem + ((a & ~3u) - MEM_BASE), 4);
    return w;
}

// Byte ranges where two copies of memory differ (gaps under 16 bytes
// merged), with `skip` deciding which bytes don't count; the first few
// printed. The count of differing bytes.
static size_t diff_ranges(const char *what, const uint8_t *x, const uint8_t *y, int (*skip)(uint32_t a, const void *ctx),
                          const void *ctx) {
    size_t bytes = 0;
    int ranges = 0;
    char line[2048] = "";
    size_t len = 0;
    uint32_t n = MEM_END - MEM_BASE;
    for (uint32_t i = 0; i < n;) {
        if (x[i] == y[i] || skip(MEM_BASE + i, ctx)) {
            i++;
            continue;
        }
        uint32_t start = i, last = i;
        while (i < n && i - last < 16) {
            if (x[i] != y[i] && !skip(MEM_BASE + i, ctx)) last = i, bytes++;
            i++;
        }
        if (++ranges <= 16 && len < sizeof(line) - 64)
            len += (size_t)snprintf(line + len, sizeof(line) - len, " %#x+%u", MEM_BASE + start, last - start + 1);
        i = last + 1;
    }
    if (bytes) report("%s: %zu bytes in %d ranges:%s%s", what, bytes, ranges, line, ranges > 16 ? " ..." : "");
    return bytes;
}

struct pair {
    const struct state *a, *b;
};
static int skip_compared(uint32_t a, const void *ctx) {
    const struct pair *p = ctx;
    return check_ignored(a) || same_string(p->a, p->b, word_at(p->a->mem, a), word_at(p->b->mem, a));
}

// What a run wrote outside the regions poisoned, for the record.
struct audit {
    const struct region *poisoned;
    int n;
};
static int skip_audited(uint32_t a, const void *ctx) {
    const struct audit *u = ctx;
    return check_ignored(a) || in_regions(a, u->poisoned, u->n);
}

static size_t compare_states(const char *what, const struct state *a, const struct state *b) {
    struct pair p = {a, b};
    char sub[256];
    snprintf(sub, sizeof(sub), "%s: memory differs", what);
    size_t bad = diff_ranges(sub, a->mem, b->mem, skip_compared, &p);
    size_t ua = a->heap_next - a->heap_base, ub = b->heap_next - b->heap_base;
    if (ua != ub || memcmp(a->heap, b->heap, ua)) {
        size_t k = 0;
        while (k < ua && k < ub && a->heap[k] == b->heap[k]) k++;
        report("%s: string tables differ: %zu and %zu bytes used, first difference at %zu", what, ua, ub, k);
        bad++;
    }
    if (a->labels_n != b->labels_n) {
        report("%s: %d and %d labels", what, a->labels_n, b->labels_n);
        bad++;
    } else {
        for (int i = 0; i < a->labels_n; i++)
            if (!same_string(a, b, a->labels[i], b->labels[i])) {
                report("%s: label %d differs", what, i);
                bad++;
                break;
            }
    }
    for (int i = 0; i < BUTTONS_N; i++)
        if (a->has_bubble[i] != b->has_bubble[i] || strcmp(a->bubble[i], b->bubble[i])) {
            report("%s: button %d's help differs: \"%s\" and \"%s\"", what, i, a->bubble[i], b->bubble[i]);
            bad++;
        }
    return bad;
}

static uint8_t *before;
static struct state run_a, run_b;

static void put_back(const struct region *r, int n) {
    for (int i = 0; i < n; i++) memcpy(at(r[i].addr), before + (r[i].addr - MEM_BASE), r[i].size);
}

// Fills the regions with a pattern, but for the bytes `spare` keeps (what
// the readers read there without writing it).
static void poison(const struct region *r, int n, int (*spare)(uint32_t a)) {
    for (int i = 0; i < n; i++)
        for (uint32_t a = r[i].addr; a < r[i].addr + r[i].size; a++)
            if (!spare || !spare(a)) U8(a) = (uint8_t)(0xa5 + 0x3b * a);
}

// What the readers take from the tables without writing it: the
// terraforming's bits (for the buttons' help), and the faction file names
// read_faction is given.
static int spare_rules(uint32_t a) {
    return a >= TERRAFORM && a < TERRAFORM + 0x20 * TERRAFORM_N && (a - TERRAFORM) % 0x20 >= 0x10
           && (a - TERRAFORM) % 0x20 < 0x18;
}
static int spare_names(uint32_t a) {
    return (a - FACTION_DEFS) % 0x59c >= FD_FILENAME && (a - FACTION_DEFS) % 0x59c < FD_LEADER;
}

enum { FINAL_NONE = -1, FINAL_CHIRON = 0, FINAL_ORIGINAL = 1 };

// One comparison: run(ctx, original) by the original and by Chiron, each
// from the same state with the regions it writes poisoned (but for what
// `spare` keeps), so a field one of them leaves unwritten shows; the
// memory after each compared. Then the regions are put back and, unless
// `final` is FINAL_NONE, the read is made once more, by Chiron or the
// original, for the game to go on with. Returns that run's result, or
// Chiron's.
static int check_run(const char *what, int (*run)(void *ctx, int original), void *ctx, const struct region *regions,
                     int n_regions, int (*spare)(uint32_t a), int final) {
    size_t n = MEM_END - MEM_BASE;
    if (!before) before = malloc(n);
    memcpy(before, at(MEM_BASE), n);
    // rand()'s state, and the game's own generator's (the art uses it).
    uint32_t rand0 = *game_rand_state(), seed0 = *game_random_seed;
    poison(regions, n_regions, spare);
    side_original = 1;
    int ra = run(ctx, 1);
    side_original = 0;
    state_take(&run_a);
    struct audit u = {regions, n_regions};
    char sub[256];
    snprintf(sub, sizeof(sub), "%s: also written (not poisoned)", what);
    diff_ranges(sub, before, run_a.mem, skip_audited, &u);

    put_back(regions, n_regions);
    poison(regions, n_regions, spare);
    *game_rand_state() = rand0, *game_random_seed = seed0;
    int rb = run(ctx, 0);
    state_take(&run_b);

    check_count++;
    size_t bad = compare_states(what, &run_a, &run_b);
    if (ra != rb) report("%s: returned %d (original) and %d (Chiron)", what, ra, rb), bad++;
    if (bad) check_failures++;
    else report("%s: identical", what);
    put_back(regions, n_regions);
    *game_rand_state() = rand0, *game_random_seed = seed0;
    if (final == FINAL_NONE) return rb;
    side_original = final == FINAL_ORIGINAL;
    int r = run(ctx, side_original);
    side_original = 0;
    return r;
}

// The runs checked.
static int run_read_rules(void *ctx, int original) {
    int all = *(int *)ctx;
    return original ? orig_entry_read_rules(all) : read_rules(all);
}

struct faction_run {
    struct faction_def *def;
    int toggle;
};
static int run_read_faction(void *ctx, int original) {
    struct faction_run *r = ctx;
    if (original) orig_entry_read_faction(r->def, r->toggle);
    else read_faction(r->def, r->toggle);
    return 0;
}
static int run_read_faction2(void *ctx, int original) {
    int f = *(int *)ctx;
    if (original) orig_entry_read_faction2(f);
    else read_faction2(f);
    return 0;
}

static void check_summary(void) { report("%d comparisons so far, %d failed", check_count, check_failures); }

// A faction definition in the game's memory as a region, or none.
static int def_region(struct faction_def *d, struct region *r) {
    uint8_t *p = (uint8_t *)d, *lo = at(MEM_BASE);
    if (p < lo || p + sizeof *d > lo + (MEM_END - MEM_BASE)) return 0;
    r->addr = MEM_BASE + (uint32_t)(p - lo), r->size = sizeof *d;
    return 1;
}

static void check_read_faction(struct faction_def *def, int toggle, int shadow) {
    struct region r;
    struct faction_run fr = {def, toggle};
    char what[128];
    snprintf(what, sizeof(what), "read_faction(%.24s, %d)", def->filename, toggle);
    if (def_region(def, &r)) {
        check_run(what, run_read_faction, &fr, &r, 1, spare_names, shadow);
        check_summary();
        return;
    }
    // A definition outside the game's memory (on the stack): compared
    // apart, poisoned likewise.
    struct faction_def start = *def, a;
    uint8_t *d = (uint8_t *)def;
    for (size_t k = 0; k < sizeof *def; k++)
        if (k < FD_FILENAME || k >= FD_LEADER) d[k] = (uint8_t)(0xa5 + 0x3b * k);
    struct faction_def poisoned = *def;
    side_original = 1;
    run_read_faction(&fr, 1);
    side_original = 0;
    a = *def;
    *def = poisoned;
    run_read_faction(&fr, 0);
    check_count++;
    if (memcmp(&a, def, sizeof a)) {
        size_t k = 0;
        while (((uint8_t *)&a)[k] == d[k]) k++;
        report("%s: the definition differs, first at offset %#zx", what, k);
        check_failures++;
    } else {
        report("%s: identical", what);
    }
    *def = start;
    side_original = shadow;
    run_read_faction(&fr, shadow);
    side_original = 0;
    check_summary();
}

static void check_read_faction2(int faction, int shadow) {
    struct region r = {FACTION_DEFS + 0x59c * (uint32_t)faction, 0x59c};
    char what[64];
    snprintf(what, sizeof(what), "read_faction2(%d)", faction);
    check_run(what, run_read_faction2, &faction, &r, 1, spare_names, shadow);
}

// Every faction file the rules list, read into faction 0's definition
// (put back after), whole and names only.
static void check_faction_files(void) {
    static const uint32_t lists[] = {L_FACTIONS, L_NEWFACTIONS, L_CUSTOMFACTIONS};
    char files[64][2][24];
    int n = 0;
    for (int l = 0; l < 3; l++) {
        if (open_alpha(lists[l])) continue;
        for (int i = 0; i < (l < 2 ? 7 : 32) && n < 64; i++) {
            text_get();
            char *f = text_item();
            if (!*f) break;
            snprintf(files[n][0], 24, "%s", f);
            snprintf(files[n][1], 24, "%s", text_item());
            int dup = 0;
            for (int k = 0; k < n; k++) dup |= !strcmp(files[k][0], files[n][0]) && !strcmp(files[k][1], files[n][1]);
            if (!dup) n++;
        }
    }
    struct faction_def *d = &game_faction_defs[0], keep = *d;
    struct region r = {FACTION_DEFS, 0x59c};
    for (int i = 0; i < n; i++) {
        strcpy(d->filename, files[i][0]);
        strcpy(d->search_key, files[i][1]);
        if (game_text_open(d->filename, d->search_key) && game_text_open(d->filename, d->filename)) {
            report("faction file %s (%s) not found; not read", files[i][0], files[i][1]);
            continue;
        }
        for (int t = 0; t <= 2; t += 2) {
            struct faction_run fr = {d, t};
            char what[128];
            snprintf(what, sizeof(what), "faction file %.24s, %s", files[i][0], t ? "names" : "whole");
            check_run(what, run_read_faction, &fr, &r, 1, spare_names, FINAL_NONE);
        }
    }
    *d = keep;
}

// The helpers on lines made from the game's own names, in a file in the
// scratch user folder.
static void check_helpers(void) {
    const char *user = getenv("CHIRON_USER_DIR");
    if (!user || !*user) return;
    static const char *numbers[] = {"0", "7", "-5", "100", "1000", "-1000", "abc", "", "12", "99999999999", "+3", "0x10"};
    static const char *nouns[] = {"m1", "f2", "N1", "n2", "F", "M2", "x1", "", "2", "f", "N"};
    enum { N_NUMBERS = sizeof(numbers) / sizeof(numbers[0]), N_NOUNS = sizeof(nouns) / sizeof(nouns[0]) };
    char path[4096];
    snprintf(path, sizeof(path), "%s/rulechk.txt", user);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "#RULECHECK\n");
    for (int i = 0; i < N_NUMBERS; i++) fprintf(f, "%s, 1\n", numbers[i]);
    for (int i = 0; i < TECHS_N; i++) fprintf(f, "%s\n", text(TECHS + 0x2c * (uint32_t)i + 8));
    for (int i = 0; i < TECHS_N; i++) {
        char low[16];
        snprintf(low, sizeof(low), "%s", text(TECHS + 0x2c * (uint32_t)i + 8));
        for (char *c = low; *c; c++)
            if (*c >= 'A' && *c <= 'Z') *c += 'a' - 'A';
        fprintf(f, "%s\n", low);
    }
    fprintf(f, "%s\n%s\n", text(L_NONE), text(L_DISABLE));
    for (int i = 0; i < N_NOUNS; i++) fprintf(f, "%s, x\n", nouns[i]);
    fclose(f);
    static char file[] = "rulechk.txt", label[] = "RULECHECK";
    int bad = 0, count = 0;
    // The file open at line `line` (the first after the label is 1): the
    // next text_get reads it.
#define AT_LINE(line)                                   \
    do {                                                \
        game_text_open(file, label);                    \
        for (int k_ = 1; k_ < (line); k_++) text_get(); \
    } while (0)
    static const int ranges[][2] = {{0, 100}, {-10, 10}, {5, 1}, {1, 1000}};
    for (int i = 0; i < N_NUMBERS; i++)
        for (int r = 0; r < 4; r++) {
            AT_LINE(1 + i);
            int a = orig_entry_text_get_number(ranges[r][0], ranges[r][1]);
            AT_LINE(1 + i);
            int b = text_get_number(ranges[r][0], ranges[r][1]);
            count++;
            if (a != b) report("text_get_number(%d, %d) on \"%s\": %d and %d", ranges[r][0], ranges[r][1], numbers[i], a, b), bad++;
        }
    for (int i = 0; i < 2 * TECHS_N + 2; i++) {
        AT_LINE(1 + N_NUMBERS + i);
        side_original = 1;
        int a = orig_entry_tech_item();
        side_original = 0;
        AT_LINE(1 + N_NUMBERS + i);
        int b = tech_item();
        count++;
        if (a != b) report("tech_item on line %d: %d and %d", 1 + N_NUMBERS + i, a, b), bad++;
    }
    for (int i = 0; i < N_NOUNS; i++) {
        int32_t ga = -9, pa = -9, gb = -9, pb = -9;
        // noun_item takes the next item of the line read last.
        AT_LINE(2 + N_NUMBERS + 2 * TECHS_N + 2 + i);
        orig_entry_noun_item(&ga, &pa);
        AT_LINE(2 + N_NUMBERS + 2 * TECHS_N + 2 + i);
        noun_item(&gb, &pb);
        count++;
        if (ga != gb || pa != pb) report("noun_item on \"%s\": %d %d and %d %d", nouns[i], ga, pa, gb, pb), bad++;
    }
#undef AT_LINE
    check_count++;
    if (bad) check_failures++;
    report("helpers: %d calls compared, %d differ", count, bad);
}

// The checks, then the read made once more as the game asked (by Chiron,
// or in shadow mode the original), from rand()'s state at the call: the
// game goes on as after a plain read, the text reader included.
static int check_read_rules(int all, int shadow) {
    char what[96];
    uint32_t rand0 = *game_rand_state(), seed0 = *game_random_seed;
    if (all) {
        // The other game's factions first.
        int exp = I32(EXPANSION);
        I32(EXPANSION) = !exp;
        snprintf(what, sizeof(what), "read_rules(1), %s factions", exp ? "the original game's" : "Alien Crossfire's");
        check_run(what, run_read_rules, &all, check_tables, CHECK_TABLES, spare_rules, FINAL_NONE);
        I32(EXPANSION) = exp;
    }
    snprintf(what, sizeof(what), "read_rules(%d)%s", all, !all ? "" : I32(EXPANSION) ? ", Alien Crossfire's factions" : ", the original game's factions");
    check_run(what, run_read_rules, &all, check_tables, CHECK_TABLES, spare_rules, shadow);
    check_faction_files();
    if (all)
        for (int f = 1; f < FACTION_COUNT; f++) check_read_faction2(f, shadow);
    check_helpers();
    check_summary();
    *game_rand_state() = rand0, *game_random_seed = seed0;
    side_original = shadow;
    int r = run_read_rules(&all, shadow);
    side_original = 0;
    return r;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
// A hook whose prologue ends in a relative call (re-aimed in the trampoline).
#define HOOK_CALL(addr, name, ...)                                                              \
    {addr, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}), (void *)name, \
     (void *)shadow_##name, (void **)&orig_##name, &stats_##name, 0, 1}
static const struct hook rules_hooks[] = {
    HOOK(0x584d60, entry_tech_name, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56),  // push ebx; push esi
    HOOK(0x584e40, entry_chas_name, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56),
    HOOK(0x584f40, entry_weap_name, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56),
    HOOK(0x585030, entry_arm_name, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56),
    HOOK_CALL(0x585120, entry_text_get_number, PUSH_EBP_MOV_EBP_ESP, 0xe8, 0x48, 0x84, 0x07, 0x00),  // call text_get
    HOOK_CALL(0x585150, entry_tech_item, 0xe8, 0x1b, 0x84, 0x07, 0x00),                              // call text_get
    HOOK(0x585170, entry_read_basic_rules, 0xa1, 0xfc, 0x1a, 0x69, 0x00),                            // mov eax, [0x691afc]
    HOOK(0x585e30, entry_read_tech, PUSH_EBP_MOV_EBP_ESP, 0x51, 0xa1, 0xfc, 0x1a, 0x69, 0x00),       // push ecx; mov eax, [0x691afc]
    HOOK(0x586050, entry_read_faction2, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x8b, 0x75, 0x08),               // push esi; mov esi, [ebp+8]
    HOOK(0x586090, entry_read_faction, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0x04, 0x01, 0x00, 0x00),    // sub esp, 0x104
    HOOK(0x586f30, entry_read_factions, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),                     // sub esp, 8
    HOOK_CALL(0x5871d0, entry_noun_item, PUSH_EBP_MOV_EBP_ESP, 0x56, 0xe8, 0x97, 0x64, 0x07, 0x00),  // push esi; call text_item
    HOOK(0x587240, entry_read_units, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x1c),                        // sub esp, 0x1c
    HOOK(0x5873c0, entry_read_rules, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0x14, 0x01, 0x00, 0x00),      // sub esp, 0x114
};
struct hook_module rules_module = {"rules", rules_hooks, HOOK_COUNT(rules_hooks), NULL, NULL, -1};
