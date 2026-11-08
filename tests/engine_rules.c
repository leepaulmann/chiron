// Unit test for the rules readers (rules.c): a made-up rules file and
// faction files, written below, read through stand-ins for the game's text
// library, string table and C runtime. No game files needed.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../src/engine/game.h"
#include "../src/engine/rules.h"

static int failures;
#define EXPECT(expr, want)                                                    \
    do {                                                                      \
        long got_ = (long)(expr);                                             \
        if (got_ != (long)(want)) {                                           \
            printf("FAIL %s = %ld, expected %ld\n", #expr, got_, (long)(want)); \
            failures++;                                                       \
        }                                                                     \
    } while (0)
#define EXPECT_STR(expr, want)                                                   \
    do {                                                                         \
        const char *got_ = (expr);                                               \
        if (!got_ || strcmp(got_, (want))) {                                     \
            printf("FAIL %s = \"%s\", expected \"%s\"\n", #expr, got_ ? got_ : "(null)", (want)); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static void *at(uint32_t addr) { return game_at(addr); }
#define I32(addr) (*(int32_t *)at(addr))
#define I16(addr) (*(int16_t *)at(addr))
#define U8(addr) (*(uint8_t *)at(addr))
#define PTR(addr) (*(char **)at(addr))

// ---- The made-up files -------------------------------------------------------------

static char alpha[200000], gaians[2048], hive[2048], custom[2048];
static size_t alpha_len;

static void out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void out(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    alpha_len += (size_t)vsnprintf(alpha + alpha_len, sizeof(alpha) - alpha_len, fmt, ap);
    va_end(ap);
}

static const char *effect_keys[11] = {"ECONOMY", "EFFIC", "SUPPORT", "TALENT", "MORALE", "POLICE",
                                      "GROWTH", "PLANET", "PROBE", "INDUSTRY", "RESEARCH"};
static const char *bonus_keys[41] = {
    "TECH", "MORALE", "PSI", "FACILITY", "RESEARCH", "DRONE", "TALENT", "ENERGY", "INTEREST", "COMMERCE",
    "POPULATION", "HURRY", "UNIT", "TECHCOST", "SHARETECH", "TERRAFORM", "SOCIAL", "ROBUST", "IMMUNITY",
    "IMPUNITY", "PENALTY", "FUNGNUTRIENT", "FUNGMINERALS", "FUNGENERGY", "COMMFREQ", "MINDCONTROL", "FANATIC",
    "VOTES", "FREEPROTO", "AQUATIC", "ALIEN", "FREEFAC", "REVOLT", "NODRONE", "WORMPOLICE", "FREEABIL",
    "PROBECOST", "DEFENSE", "OFFENSE", "TECHSHARE", "TECHSTEAL"};

static void make_alpha(int duplicate_tech) {
    alpha_len = 0;
    out("; made up for the test\n#TECHNOLOGY\n");
    for (int i = 0; i < 89; i++) {
        // Tech 3 needs tech 1; tech 4 is disabled; tech 5 raises commerce.
        const char *p1 = i == 3 ? "T01" : i == 4 ? "Disable" : "None";
        out("Tech %d, T%02d, %d, %d, %d, %d, %s, None, %s\n", i, duplicate_tech && i == 7 ? 6 : i, i, i + 1, i + 2, i + 3,
            p1, i == 5 ? "100" : "0");
    }
    out("\n#RULES\n200\n-3\n0, 2000\n");  // moves (clamped to 100), nutrients (to 0), artillery
    for (int i = 0; i < 14; i++) out("5\n");
    out("0, 1\n9, 9\n2000, 7\n");  // psi odds by triad
    out("5\n");
    for (int i = 0; i < 21; i++) out("%d\n", i == 0 ? -500 : 25);
    out("5\n9\n5\n5\n5\n5\n5\n");  // retooling strictness 9: clamped to 2
    out("T02\nNone\nDisable\nt03\nT04   \nT05\nT06\nT07\nT08\nT09\n");
    out("-7\n1\n0\n5\n500\n50\n2000, 0\n2000\n3000\n4000\n1\n1000\n0\n");
    out("\n#WORLDBUILDER\n");
    for (int i = 0; i < 25; i++) out("%d\n", i == 0 ? 10 : i == 18 ? -500 : 3);
    out("1, 2, 3, 4, 5\n0\n");
    out("\n#TERRAIN\n");
    for (int i = 0; i < 20; i++) out("Land %d, %s, Sea %d, T02, %d, Do $TEXT0, k%d, s%d\n", i, i == 1 ? "T01" : "None", i, i, i, i);
    out("\n#RESOURCEINFO\n");
    for (int i = 0; i < 9; i++) out("r%d, %d, %d, %d, %d\n", i, i, i + 1, i + 2, i + 3);
    out("\n#TIMECONTROLS\n");
    for (int i = 0; i < 6; i++) out("Time %d, 1, 2, 3, 4, 5, 6, %d\n", i, i);
    out("\n#CHASSIS\n");
    for (int i = 0; i < 9; i++)
        out("C%da, m1, C%db, f2, C%dc, n1, C%dd, M2, %d, %d, 0, %d, %d, %d, None, C%de, F, C%df, N2\n", i, i, i, i, i + 1,
            i == 2 ? 1 : 0, i == 4, i == 2 ? 1 : 0, i + 1, i, i);
    out("\n#REACTORS\n");
    for (int i = 0; i < 4; i++) out("Reactor %d, R%d, 99, None\n", i, i);
    out("\n#WEAPONS\n");
    for (int i = 0; i < 26; i++) out("Weapon %d, W%d, %d, %d, %d, %d, None\n", i, i, i + 2, i == 1 ? 7 : 0, i + 1, i);
    out("\n#DEFENSES\n");
    for (int i = 0; i < 14; i++) out("Armor %d, A%d, %d, 0, %d, T0%d\n", i, i, i + 1, i + 1, i % 10);
    out("\n#ABILITIES\n");
    for (int i = 0; i < 29; i++) out("Ability %d, %d, None, AB%d, 101, Does %d\n", i, i % 3, i, i);
    out("\n#MORALE\n");
    for (int i = 0; i < 7; i++) out("Morale %d, Life %d\n", i, i);
    out("\n#DEFENSEMODES\n");
    for (int i = 0; i < 3; i++) out("D%d, D-%d, d%d, x\n", i, i, i);
    out("\n#OFFENSEMODES\n");
    for (int i = 0; i < 3; i++) out("O%d, O-%d, o%d, y\n", i, i, i);
    out("\n#UNITS\n19\n");
    out("Scout, C0a, W0, A0, -1, 0, 0, T02, 5, 0\n");
    out("Boat, C2a, W1, A0, 3, 9, 4, None, 6, 1\n");  // a transport at sea, plan 3, cost 9, capacity 4
    out("Tank, C1a, W3, None, -1, 0, 0, None, -2, 10\n");
    for (int i = 3; i < 19; i++) out("Ogre %d, C0a, W2, A1, -1, 0, 0, None, 0, 0\n", i);
    out("\n#FACILITIES\n");
    for (int i = 1; i < 134; i++) {
        out("Facility %d, %d, %d, T%02d, T01, Effect %d", i, i, i % 3, i % 80, i);
        if (i >= 70) out(", 1, 2, 3, 4, 5");
        out("\n");
    }
    out("\n#ORDERS\n");
    for (int i = 0; i < 10; i++) out("Order %d, %c\n", i, 'a' + i);
    out("\n#COMPASS\n");
    for (int i = 0; i < 8; i++) out("Dir %d\n", i);
    out("\n#PLANS\n");
    for (int i = 0; i < 15; i++) out("P%d, Plan %d\n", i, i);
    out("\n#TRIAD\nLand\nSea\nAir\n\n#RESOURCES\n");
    for (int i = 0; i < 4; i++) out("Res %d, Ress %d\n", i, i);
    out("\n#ENERGY\n");
    for (int i = 0; i < 3; i++) out("E%d, Energy %d\n", i, i);
    out("\n#CITIZENS\n");
    for (int i = 0; i < 10; i++) out("Cit %d, Cits %d, T0%d, None, %d, %d, %d\n", i, i, i % 10, i, i + 1, i + 2);
    out("\n#SOCIO\n");
    for (int i = 0; i < 11; i++) out("%s%s", effect_keys[i], i < 10 ? ", " : "\n");
    for (int i = 0; i < 11; i++) out("Other %d%s", i, i < 10 ? ", " : "\n");
    out("Politics, Economics, Values, Future\n");
    for (int c = 0; c < 4; c++)
        for (int m = 0; m < 4; m++) {
            out("Model %d%d, %s", c, m, m ? "T03" : "None");
            if (c == 0 && m == 1) out(", ++ECONOMY, -EFFIC, economy, +-+POLICE, ++NOTHING");
            out("\n");
        }
    // A model name repeated in two categories, for the faction's impunity.
    out("\n#DIFF\n");
    for (int i = 0; i < 6; i++) out("Level %d\n", i);
    out("\n#BONUSNAMES\n");
    for (int i = 0; i < 41; i++) out("%s%s", bonus_keys[i], i % 8 == 7 || i == 40 ? "\n" : ", ");
    out("\n#FACTIONS\n");
    for (int i = 1; i < 8; i++) out("%s, %s\n", i == 1 ? "GAIANS" : "HIVE", i == 1 ? "GAIANS" : "HIVE");
    out("\n#NEWFACTIONS\n");
    for (int i = 1; i < 8; i++) out("%s, %s\n", i == 2 ? "JENN282" : i == 1 ? "HIVE" : "GAIANS", "X");
    out("\n#CUSTOMFACTIONS\nCUSTOM1, CUSTOM1\n\n#MANDATE\n");
    for (int i = 0; i < 4; i++) out("Mandate %d, MANDATE %d\n", i, i);
    out("\n#MOOD\n");
    for (int i = 0; i < 9; i++) out("Mood %d\n", i);
    out("\n#REPUTE\n");
    for (int i = 0; i < 8; i++) out("Repute %d\n", i);
    out("\n#MIGHT\n");
    for (int i = 0; i < 7; i++) out("Might %d, mighty %d\n", i, i);
    out("\n#PROPOSALS\n");
    for (int i = 0; i < 11; i++) out("Proposal %d, %s, Text %d\n", i, i == 2 ? "T02" : "None", i);
    out("\n#NATURAL\n");
    for (int i = 0; i < 16; i++) out("Natural %d, N%d\n", i, i);

    snprintf(gaians, sizeof(gaians),
             "#GAIANS\n"
             "The Formal Name That Is Rather Long Indeed, Desc, Gaians, f, 3, Leader, F, 1, 2, 3, 4, 5\n"
             "TECH, T02, TECH, 2, MORALE, +0, SOCIAL, ++EFFIC, ROBUST, economy, IMMUNITY, -POLICE, "
             "IMPUNITY, Model 01, PENALTY, Model 32, HURRY, 5000, FREEPROTO, 1, TECHSHARE, 3, UNKNOWN, 1, "
             "FUNGNUTRIENT, 1, PSI, 25\n"
             "Politics, Model 02, MORALE\n"
             "Economix, Model 11, PLANET\n"
             "Gaian, Gaian\n"
             "Assistant, Scientist, City\n"
             "Lady, a, b, c, d\n"
             "Insult\n");
    snprintf(hive, sizeof(hive),
             "; no label of its own: found by its filename\n#HIVE\n"
             "Hive, Hive, Hive, n, 1, Yang, m, -1, 0, 0, 0, 1\n"
             "\n"
             "Values, Model 22, NOTHING\n"
             "Future, Model 33, EFFIC\n"
             "Hivish\n"
             "A, B, C\n"
             "Chairman, e, f, g, h\n"
             "Ins\n");
    snprintf(custom, sizeof(custom), "#CUSTOM1\nCustom, Custom, Customs, m, 2, Boss, m, 0, 0, 0, 0, 0\n\n"
                                     "x, x, x\nx, x, x\nCustomary\nA, B, C\nBoss, a, b, c, d\nI\n");
}

// ---- Stand-ins for the game's text library ----------------------------------------

static const char *cur;
static char line_buf[1024], item_buf[1024], *pos = line_buf;
static int opens;

static const char *file_text(const char *name) {
    if (!strcasecmp(name, "ALPHAX")) return alpha;
    if (!strcasecmp(name, "GAIANS")) return gaians;
    if (!strcasecmp(name, "HIVE")) return hive;
    if (!strcasecmp(name, "CUSTOM1")) return custom;
    return NULL;
}

static int t_open(char *file, char *label) {
    opens++;
    const char *t = file_text(file);
    if (!t) return 1;
    size_t n = strlen(label);
    for (const char *p = t; *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : p + strlen(p)) {
        if (*p == '#' && !strncasecmp(p + 1, label, n) && (p[1 + n] == '\n' || !p[1 + n])) {
            cur = strchr(p, '\n') ? strchr(p, '\n') + 1 : p + strlen(p);
            return 0;
        }
    }
    return 1;
}
static void t_get(void) {
    size_t n = strcspn(cur, "\n");
    if (n >= sizeof(line_buf)) n = sizeof(line_buf) - 1;
    memcpy(line_buf, cur, n);
    line_buf[n] = 0;
    cur += n + (cur[n] == '\n');
    pos = line_buf;
}
static char *t_item(void) {
    size_t n = strcspn(pos, ",");
    while (*pos == ' ') pos++, n--;
    memcpy(item_buf, pos, n);
    item_buf[n] = 0;
    pos += n + (pos[n] == ',');
    while (n && item_buf[n - 1] == ' ') item_buf[--n] = 0;
    return item_buf;
}
static char arena[200000];
static size_t arena_used = 1;
static char *t_put(const char *s) {
    char *p = arena + arena_used;
    strcpy(p, s);
    arena_used += strlen(s) + 1;
    return p;
}
static char *t_item_string(void) { return t_put(t_item()); }
static int t_item_number(void) { return atoi(t_item()); }
static int t_item_binary(void) { return (int)strtol(t_item(), NULL, 2); }
static void t_nothing(void) {}
static void t_index(char *f) { (void)f; }
static int t_labels(void) { return 0; }
static int __attribute__((thiscall)) t_strings_init(void *t, int n) { return (void)t, (void)n, arena_used = 1, 0; }
static char *__attribute__((thiscall)) t_strings_put(void *t, char *s) { return (void)t, t_put(s); }
static char *__attribute__((thiscall)) t_strings_get(void *t, char *s) { return (void)t, s; }
static const char *bubbles[32];
static int __attribute__((thiscall)) t_bubble(void *b, char *s) {
    for (int i = 0; i < 15; i++)
        if (b == at(0x7bb62c + 0xb4c * (uint32_t)i)) bubbles[i] = s;
    if (b == at(0x7c5fa0)) bubbles[15] = s;
    if (b == at(0x7c8184)) bubbles[18] = s;
    return 0;
}
static int arts[8], prefs_loads;
static void t_art(int f) { arts[f]++; }
static void t_prefs(void) { prefs_loads++; }
static int t_stricmp(const char *a, const char *b) { return strcasecmp(a, b); }
static int t_strnicmp(const char *a, const char *b, size_t n) { return strncasecmp(a, b, n); }
static int t_atoi(const char *s) { return atoi(s); }
static const char *last_pop;
static int pops;
static int t_x_pop(char *label, int unk) { return (void)unk, pops++, last_pop = label, 0; }
static int t_parse_says(int slot, char *text, int g, int p) { return (void)slot, (void)text, (void)g, (void)p, 0; }
static int t_parse_say(int slot, int text, int g, int p) { return (void)slot, (void)text, (void)g, (void)p, 0; }
static int t_parse_num(int slot, int v) { return (void)slot, (void)v, 0; }
static char *say0;
static int t_parse_say_keep(int slot, int text, int g, int p) {
    (void)g, (void)p;
    if (!slot) say0 = (char *)(uintptr_t)text;
    return 0;
}
// parse_string: the pattern with $TEXT0 replaced.
static void t_parse_string(char *pattern, char *buf) {
    char *v = strstr(pattern, "$TEXT0");
    if (!v) {
        strcat(buf, pattern);
        return;
    }
    strncat(buf, pattern, (size_t)(v - pattern));
    strcat(buf, say0);
    strcat(buf, v + 6);
}
static int rolls[8], roll_n;
static int t_rand(void) { return roll_n ? rolls[--roll_n] : 0; }
static uint32_t rand_state;

static void setup(void) {
    game_text_open = t_open, game_text_get = t_get, game_text_item = t_item, game_text_item_string = t_item_string;
    game_text_item_number = t_item_number, game_text_item_binary = t_item_binary;
    game_text_clear_index = t_nothing, game_text_make_index = t_index, game_labels_init = t_labels;
    game_strings_init = t_strings_init, game_strings_put = t_strings_put, game_strings_get = t_strings_get;
    game_button_set_bubble_text = t_bubble, game_load_faction_art = t_art, game_prefs_fac_load = t_prefs;
    game_stricmp = t_stricmp, game_strnicmp = t_strnicmp, game_atoi = t_atoi;
    game_x_pop = t_x_pop, game_parse_says = t_parse_says, game_parse_say = t_parse_say_keep, game_parse_num = t_parse_num;
    game_parse_string = t_parse_string, game_rand = t_rand;
    (void)t_parse_say, (void)rand_state;
    game_tech = at(0x94f358), game_weapons = at(0x94ae60), game_armor = at(0x94f278), game_reactors = at(0x9527f8);
    game_chassis = at(0x94a330), game_protos = at(0x9ab868), game_abilities = at(0x9ab538);
    game_faction_defs = at(0x946a50), game_strings = at(0x9b90d8);
    // The text reader's line and item, where the readers look for them.
    PTR(0x9b7d00) = line_buf, PTR(0x9b7d04) = item_buf;
    // The words and labels the readers use.
    static const struct {
        uint32_t addr;
        const char *s;
    } words[] = {
        {0x691b58, "ALPHAX"}, {0x68f268, "None"}, {0x68f270, "Disable"}, {0x68f278, "BADTECHKEY"},
        {0x68f284, "None"}, {0x68f28c, "Disable"}, {0x68f294, "BADCHASKEY"}, {0x68f2a0, "None"},
        {0x68f2a8, "Disable"}, {0x68f2b0, "BADWEAPKEY"}, {0x68f2bc, "None"}, {0x68f2c4, "Disable"},
        {0x68f2cc, "BADARMKEY"}, {0x68f2d8, "RULES"}, {0x68f2e0, "WORLDBUILDER"}, {0x68f2f0, "TECHNOLOGY"},
        {0x68f2fc, "DUPLICATETECH"}, {0x68f30c, "TECHNOLOGY"}, {0x68f318, "PLAYERFILE"}, {0x68f324, "BONUSNAMES"},
        {0x68f330, "NEWFACTIONS"}, {0x68f33c, "FACTIONS"}, {0x68f348, "CUSTOMFACTIONS"}, {0x68f358, "JENN282"},
        {0x68f360, "FACTIONS"}, {0x68f36c, "NEWFACTIONS"}, {0x68f378, "CUSTOMFACTIONS"}, {0x68f388, "UNITS"},
        {0x68f390, "TERRAIN"}, {0x68f398, "RESOURCEINFO"}, {0x68f3a8, "TIMECONTROLS"}, {0x68f3b8, "CHASSIS"},
        {0x68f3c0, "REACTORS"}, {0x68f3cc, "WEAPONS"}, {0x68f3d4, "DEFENSES"}, {0x68f3e0, "ABILITIES"},
        {0x68f3ec, "MORALE"}, {0x68f3f4, "DEFENSEMODES"}, {0x68f404, "OFFENSEMODES"}, {0x68f414, "FACILITIES"},
        {0x68f420, "ORDERS"}, {0x68f428, "COMPASS"}, {0x68f430, "PLANS"}, {0x68f438, "TRIAD"},
        {0x68f440, "RESOURCES"}, {0x68f44c, "ENERGY"}, {0x68f454, "CITIZENS"}, {0x68f460, "SOCIO"},
        {0x68f468, "DIFF"}, {0x68f470, "MANDATE"}, {0x68f478, "MOOD"}, {0x68f480, "REPUTE"}, {0x68f488, "MIGHT"},
        {0x68f490, "PROPOSALS"}, {0x68f49c, "NATURAL"},
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) strcpy(at(words[i].addr), words[i].s);
    PTR(0x691afc) = at(0x691b58);
    // Terraforms with a bit of their own (+0x10): all but 2, 5 and 6.
    for (int t = 0; t < 20; t++) I32(0x691878 + 0x20 * (uint32_t)t + 0x10) = t != 2 && t != 5 && t != 6;
}

// ---- The tests ------------------------------------------------------------------------

static uint32_t tech(int i) { return 0x94f358 + 0x2c * (uint32_t)i; }
static uint8_t *def(int f) { return at(0x946a50 + 0x59c * (uint32_t)f); }
#define D32(f, off) (*(int32_t *)(def(f) + (off)))

static void test_rules(void) {
    make_alpha(0);
    I32(0x9a6488) = 1;  // Alien Crossfire
    roll_n = 2, rolls[1] = 2, rolls[0] = 14;  // the random faction: HIVE (playing already), then CUSTOM1
    pops = 0;
    // Leftovers the read must clear or keep.
    memset(at(0x9ab868), 0x55, 0x34 * 512);
    EXPECT(read_rules(1), 0);
    EXPECT(pops, 0);

    // Techs.
    EXPECT_STR(at(tech(3) + 8), "T03");
    EXPECT_STR(PTR(tech(3) + 4), "Tech 3");
    EXPECT(I32(tech(3) + 0x20), 3);  // power, first
    EXPECT(I32(tech(3) + 0x18), 4);
    EXPECT(I32(tech(3) + 0x1c), 5);
    EXPECT(I32(tech(3) + 0x14), 6);  // growth, last
    EXPECT(I32(tech(3) + 0x24), 1);
    EXPECT(I32(tech(4) + 0x24), -2);
    EXPECT(I32(tech(5)), 4);
    EXPECT(I32(0x949730), 88);  // all but the disabled one
    EXPECT(I32(0x949734), 1);   // tech 5

    // #RULES: ranges.
    EXPECT(I32(0x949738), 100);
    EXPECT(I32(0x94973c), 0);
    EXPECT(I32(0x949748), 1);
    EXPECT(I32(0x94974c), 1000);
    EXPECT(I32(0x94979c), 1); EXPECT(I32(0x9497a8), 1);
    EXPECT(I32(0x9497a0), 9); EXPECT(I32(0x9497ac), 9);
    EXPECT(I32(0x9497a4), 1000); EXPECT(I32(0x9497b0), 7);
    EXPECT(I32(0x9497b8), -100);
    EXPECT(I32(0x949824), 25);
    EXPECT(I32(0x949828), 2);
    EXPECT(I32(0x949774), 2);
    EXPECT(I32(0x949778), -1);
    EXPECT(I32(0x94977c), -2);
    EXPECT(I32(0x949780), 3);  // "t03": names are compared without case
    EXPECT(I32(0x949784), 4);  // trailing spaces cut
    EXPECT(I32(0x9497e0), 9);
    EXPECT(I32(0x949834), -1);
    EXPECT(I32(0x949840), 10);
    EXPECT(I32(0x949844), 100);
    EXPECT(I32(0x949848), 50);
    EXPECT(I32(0x94984c), 1000); EXPECT(I32(0x949850), 1);
    EXPECT(I32(0x949858), 3000);
    EXPECT(I32(0x949868), 1);
    EXPECT(I32(0x9502a8), 50);
    EXPECT(I32(0x9502f0), -100);
    EXPECT(I32(0x95030c), 1); EXPECT(I32(0x95031c), 5);
    EXPECT(I32(0x950320), 1);

    // Terraforming and its orders.
    EXPECT_STR(PTR(0x691878 + 0x20 * 1), "Land 1");
    EXPECT(I32(0x691878 + 0x20 * 1 + 8), 1);
    EXPECT_STR(PTR(0x691878 + 0x20 * 1 + 4), "Sea 1");
    EXPECT(I32(0x691878 + 0x20 * 1 + 0xc), 2);
    EXPECT(I32(0x691878 + 0x20 * 1 + 0x18), 1);
    EXPECT_STR(PTR(0x691878 + 0x20 * 1 + 0x1c), "s1");
    EXPECT_STR(PTR(0x96c878 + 0xc * 5), "Do Land 1");
    EXPECT_STR(PTR(0x96c878 + 0xc * 5 + 4), "Do Sea 1");
    EXPECT_STR(PTR(0x96c878 + 0xc * 5 + 8), "k1");
    EXPECT(I32(0x945f50 + 16 * 2 + 12), 5);
    EXPECT(I32(0x94f1b8 + 0x20 * 3 + 28), 3);

    // Chassis: names, genders, numbers.
    uint32_t c2 = 0x94a330 + 0x90 * 2;
    EXPECT_STR(PTR(c2), "C2a"); EXPECT(I32(c2 + 0x18), 0); EXPECT(I32(c2 + 0x30), 0);
    EXPECT_STR(PTR(c2 + 4), "C2b"); EXPECT(I32(c2 + 0x1c), 1); EXPECT(I32(c2 + 0x34), 1);
    EXPECT_STR(PTR(c2 + 0xc), "C2c"); EXPECT(I32(c2 + 0x24), 2);
    EXPECT_STR(PTR(c2 + 0x10), "C2d"); EXPECT(I32(c2 + 0x28), 0); EXPECT(I32(c2 + 0x40), 1);
    EXPECT_STR(PTR(c2 + 8), "C2e"); EXPECT(I32(c2 + 0x20), 1); EXPECT(I32(c2 + 0x38), 0);
    EXPECT_STR(PTR(c2 + 0x14), "C2f"); EXPECT(I32(c2 + 0x2c), 2); EXPECT(I32(c2 + 0x44), 1);
    EXPECT(U8(c2 + 0x48), 3); EXPECT(U8(c2 + 0x49), 1); EXPECT(U8(c2 + 0x4b), 1); EXPECT(U8(c2 + 0x4c), 3);
    EXPECT(I16(c2 + 0x8e), -1);
    EXPECT(I16(0x9527f8 + 0xc * 2 + 8), -1);
    EXPECT_STR(PTR(0x9527f8 + 0xc * 2 + 4), "R2");
    EXPECT(U8(0x94ae60 + 0x10 * 1 + 8), 3); EXPECT(U8(0x94ae60 + 0x10 * 1 + 0xa), 7);
    EXPECT(U8(0x94ae60 + 0x10 * 1 + 0xb), 2); EXPECT(U8(0x94ae60 + 0x10 * 1 + 9), 1);
    EXPECT(I16(0x94f278 + 0x10 * 3 + 0xc), 3);
    EXPECT(I32(0x9ab538 + 0x1c * 4 + 0xc), 1); EXPECT(I32(0x9ab538 + 0x1c * 4 + 0x14), 5);
    EXPECT_STR(PTR(0x9ab538 + 0x1c * 4 + 8), "AB4"); EXPECT_STR(PTR(0x9ab538 + 0x1c * 4 + 4), "Does 4");
    EXPECT_STR(PTR(0x946a00 + 0x10 * 2 + 12), "x");

    // The basic designs: made, then plan, cost and capacity where set; the
    // rest of the designs cleared of name, flags and the byte at +0x2b.
    uint32_t u1 = 0x9ab868 + 0x34;
    EXPECT_STR(at(u1), "Boat");
    EXPECT(U8(u1 + 0x24), 2); EXPECT(U8(u1 + 0x25), 1); EXPECT(U8(u1 + 0x27), 1);
    EXPECT(U8(u1 + 0x2a), 3); EXPECT(U8(u1 + 0x29), 9); EXPECT(U8(u1 + 0x28), 4); EXPECT(U8(u1 + 0x2e), 6);
    EXPECT(I32(u1 + 0x20), 1);
    EXPECT(I16(0x9ab868 + 0x32), 2);
    EXPECT((int8_t)U8(0x9ab868 + 0x34 * 2 + 0x2e), -2);
    EXPECT(U8(0x9ab868 + 0x34 * 2 + 0x26), 0xff);  // armor None
    // The Battle Ogres' reactors, by their place.
    EXPECT(U8(0x9ab868 + 0x34 * 15 + 0x27), 1);
    EXPECT(U8(0x9ab868 + 0x34 * 16 + 0x27), 1);
    EXPECT(U8(0x9ab868 + 0x34 * 17 + 0x27), 2);
    EXPECT(U8(0x9ab868 + 0x34 * 18 + 0x27), 3);
    EXPECT(U8(0x9ab868 + 0x34 * 100), 0); EXPECT(I16(0x9ab868 + 0x34 * 100 + 0x30), 0);
    EXPECT(U8(0x9ab868 + 0x34 * 100 + 0x2b), 0); EXPECT(U8(0x9ab868 + 0x34 * 100 + 0x2a), 0x55);

    // Facilities: the free tech always -2; projects' weights.
    uint32_t f9 = 0x9a4b68 + 0x30 * 9, f75 = 0x9a4b68 + 0x30 * 75;
    EXPECT_STR(PTR(f9), "Facility 9"); EXPECT(I32(f9 + 0xc), 9); EXPECT(I32(f9 + 0x10), 0);
    EXPECT(I32(f9 + 0x14), 9); EXPECT(I32(f9 + 0x18), -2); EXPECT_STR(PTR(f9 + 4), "Effect 9");
    EXPECT(I32(f75 + 0x1c), 1); EXPECT(I32(f75 + 0x2c), 2); EXPECT(I32(f75 + 0x24), 3);
    EXPECT(I32(f75 + 0x28), 4); EXPECT(I32(f75 + 0x20), 5);

    // Orders: 0 to 3 and 24 on.
    EXPECT_STR(PTR(0x96c878 + 0xc * 3), "Order 3"); EXPECT_STR(PTR(0x96c878 + 0xc * 3 + 4), "Order 3");
    EXPECT_STR(PTR(0x96c878 + 0xc * 24), "Order 4"); EXPECT_STR(PTR(0x96c878 + 0xc * 29 + 8), "j");
    EXPECT_STR(PTR(0x945d48 + 4 * 7), "Dir 7");
    EXPECT_STR(PTR(0x945fe0 + 4 * 14), "P14"); EXPECT_STR(PTR(0x952360 + 4 * 14), "Plan 14");
    EXPECT_STR(PTR(0x94f1a8 + 8), "Air");
    EXPECT_STR(PTR(0x946158 + 8 * 3 + 4), "Ress 3");
    EXPECT_STR(PTR(0x94a318 + 8 * 2), "E2");
    EXPECT(I32(0x946020 + 0x1c * 6 + 8), 6); EXPECT(I32(0x946020 + 0x1c * 6 + 0x18), 8);
    EXPECT(I32(0x946020 + 0x1c * 8 + 8), 0);  // not read past the seventh

    // Social engineering.
    EXPECT_STR(at(0x946580 + 0x68 * 10), "RESEARCH");
    EXPECT_STR(at(0x946598 + 0x68 * 10), "Other 10");
    EXPECT_STR(PTR(0x94b000 + 0xd4 * 3), "Future");
    EXPECT_STR(PTR(0x94b014 + 0xd4 * 0 + 4), "Model 01");
    EXPECT(I32(0x94b004 + 4), 3);
    int32_t *eff = at(0x94b024 + 0x2c * 1);
    EXPECT(eff[0], 2);   // ++ECONOMY, and economy (no signs: adds 0)
    EXPECT(eff[1], -1);  // -EFFIC
    EXPECT(eff[5], 1);   // +-+POLICE
    EXPECT_STR(PTR(0x96c85c + 4 * 5), "Level 5");

    // The factions: Alien Crossfire's list, with the settings' choice.
    EXPECT_STR(at(0x9461a8 + 24 * 40), "TECHSTEAL");
    EXPECT(prefs_loads, 1);
    EXPECT_STR(PTR(0x94b4a0 + 8 * 3 + 4), "MANDATE 3");
    EXPECT_STR(PTR(0x94c9e4 + 4 * 8), "Mood 8");
    EXPECT_STR(PTR(0x946a30 + 4 * 7), "Repute 7");
    EXPECT_STR(PTR(0x94c558 + 8 * 6 + 4), "mighty 6");
    EXPECT(I32(0x9a6828 + 0xc * 2 + 8), 2); EXPECT_STR(PTR(0x9a6828 + 0xc * 2 + 4), "Text 2");
    EXPECT_STR(PTR(0x94ade0 + 8 * 15 + 4), "N15");

    // The map editor's buttons: terraforms 2, 5 and 6 have no bit: button
    // 2 is terraform 3, button 4 terraform 7.
    EXPECT_STR(bubbles[0], "Land 0"); EXPECT_STR(bubbles[2], "Land 3"); EXPECT_STR(bubbles[4], "Land 7");
    EXPECT_STR(bubbles[15], "N2"); EXPECT_STR(bubbles[18], "N10");
}

static void test_factions(void) {
    // Faction 1 (HIVE in Alien Crossfire's list) and faction 3 (GAIANS).
    EXPECT_STR((char *)def(1) + 4, "HIVE");
    EXPECT_STR((char *)def(3) + 4, "GAIANS");
    // HIVE: found by its filename; names only on its first line.
    EXPECT_STR((char *)def(1) + 0x384, "Hive");
    EXPECT(D32(1, 0x2fc), 2); EXPECT(D32(1, 0x300), 0); EXPECT(D32(1, 0), 0);
    EXPECT(D32(1, 0x570), -1); EXPECT(D32(1, 0x580), 0); EXPECT(D32(1, 0x574), 1);
    EXPECT(D32(1, 0x50c), 0);
    EXPECT(D32(1, 0x584), 2); EXPECT(D32(1, 0x58c), 2); EXPECT(D32(1, 0x594), -1);
    EXPECT(D32(1, 0x588), 3); EXPECT(D32(1, 0x590), 3); EXPECT(D32(1, 0x598), -1);  // effect found: still -1
    EXPECT_STR((char *)def(1) + 0x304, "Hivish");
    EXPECT_STR((char *)def(1) + 0x3ac, "Ins");
    // GAIANS.
    uint8_t *g = def(3);
    EXPECT_STR((char *)g + 0x384, "The Formal Name That Is Rather Long Ind");  // 39 characters kept
    EXPECT(D32(3, 0x2fc), 1); EXPECT(D32(3, 0x300), 1); EXPECT(D32(3, 0), 1);
    EXPECT(D32(3, 0x570), 1); EXPECT(D32(3, 0x580), 2); EXPECT(D32(3, 0x578), 3); EXPECT(D32(3, 0x57c), 4);
    EXPECT(D32(3, 0x574), 5);
    EXPECT(D32(3, 0x4d4), 2);               // TECH, 2: free techs
    EXPECT(D32(3, 0x4d8), 0);               // MORALE +0
    EXPECT(D32(3, 0x508), 0x8000 | 0x200 | 0x20);  // morale exemption, FREEPROTO, TECHSHARE
    EXPECT(D32(3, 0x500), 3);
    EXPECT(D32(3, 0x4f4), 1000);            // HURRY clamped
    EXPECT(D32(3, 0x4fc), 25);
    // Bonuses: TECH T02; SOCIAL ++EFFIC; ROBUST economy; IMMUNITY -POLICE;
    // IMPUNITY Model 01 (category 0, model 1); PENALTY Model 32; FUNGNUTRIENT 1.
    EXPECT(D32(3, 0x50c), 7);
    EXPECT(D32(3, 0x510), 0); EXPECT(D32(3, 0x530), 2);
    EXPECT(D32(3, 0x514), 3); EXPECT(D32(3, 0x534), 1); EXPECT(D32(3, 0x554), 2);
    EXPECT(D32(3, 0x518), 0xa); EXPECT(D32(3, 0x538), 0); EXPECT(D32(3, 0x558), 1);
    EXPECT(D32(3, 0x51c), 4); EXPECT(D32(3, 0x53c), 5); EXPECT(D32(3, 0x55c), -1);
    EXPECT(D32(3, 0x520), 5); EXPECT(D32(3, 0x540), 0); EXPECT(D32(3, 0x560), 1);
    EXPECT(D32(3, 0x524), 6); EXPECT(D32(3, 0x544), 3); EXPECT(D32(3, 0x564), 2);
    EXPECT(D32(3, 0x528), 7); EXPECT(D32(3, 0x548), 1);
    // Ideology: Politics / Model 02; "Economix" is no category.
    EXPECT(D32(3, 0x584), 0); EXPECT(D32(3, 0x58c), 2);
    EXPECT(D32(3, 0x588), -1); EXPECT(D32(3, 0x590), 0);
    EXPECT_STR((char *)g + 0x3dc, "Assistant"); EXPECT_STR((char *)g + 0x40c, "City");
    EXPECT_STR((char *)g + 0x4c, "Lady"); EXPECT_STR((char *)g + 0x1e4, "d");
    EXPECT_STR((char *)g + 0x3ac, "Insult");
    for (int f = 1; f < 8; f++) EXPECT(arts[f] >= 1, 1);
}

static void test_random_faction(void) {
    // Faction 2 was JENN282: drawn from the 15 listed (7 + 7 + 1 custom).
    // The first roll, 2: #FACTIONS' third line, HIVE, plays already
    // (faction 1): drawn again. 14: #CUSTOMFACTIONS' first, CUSTOM1, its
    // search key its filename.
    EXPECT_STR((char *)def(2) + 4, "CUSTOM1");
    EXPECT_STR((char *)def(2) + 0x1c, "CUSTOM1");
    EXPECT_STR((char *)def(2) + 0x384, "Custom");
    EXPECT(D32(2, 0x300), 1);
    EXPECT(roll_n, 0);
}

static void test_names(void) {
    pops = 0;
    char a[] = "Nope", b[] = "C9z", c[] = "W1", d[] = "none", e[] = "DISABLE  ";
    EXPECT(rules_tech_name(a), -2);
    EXPECT(pops, 1);
    EXPECT_STR(last_pop, "BADTECHKEY");
    EXPECT(chas_name(b), 0);
    EXPECT_STR(last_pop, "BADCHASKEY");
    EXPECT(weap_name(c), 1);
    EXPECT(arm_name(d), -1);
    EXPECT(arm_name(e), -2);
    // A duplicate short name is reported, and reading goes on.
    make_alpha(1);
    pops = 0;
    EXPECT(read_tech(), 0);
    EXPECT(pops, 1);
    EXPECT_STR(last_pop, "DUPLICATETECH");
    // text_get_number, tech_item, noun_item.
    t_open("ALPHAX", "WORLDBUILDER");
    EXPECT(text_get_number(50, 60), 50);
    EXPECT(text_get_number(5, 1), 5);  // max below min: min
    t_open("ALPHAX", "RULES");
    for (int i = 0; i < 49; i++) t_get();
    EXPECT(tech_item(), 2);
    t_open("ALPHAX", "CHASSIS");
    t_get();
    int32_t gender = -1, plural = -1;
    t_item();
    noun_item(&gender, &plural);
    EXPECT(gender, 0); EXPECT(plural, 0);
    t_item();
    noun_item(&gender, &plural);
    EXPECT(gender, 1); EXPECT(plural, 1);
    // Names only, and a missing file.
    memset(def(0), 0x77, 0x59c);
    strcpy((char *)def(0) + 4, "GAIANS"), strcpy((char *)def(0) + 0x1c, "GAIANS");
    read_faction((struct faction_def *)def(0), 2);
    EXPECT_STR((char *)def(0) + 0x34, "Leader");
    EXPECT(D32(0, 0x570), 0x77777777);
    EXPECT(D32(0, 0x4f4), 100);  // cleared
    pops = 0;
    strcpy((char *)def(0) + 4, "NOFILE");
    read_faction((struct faction_def *)def(0), 0);
    EXPECT(pops, 1); EXPECT_STR(last_pop, "PLAYERFILE");
    // read_faction2: not for the natives.
    int before = arts[0];
    read_faction2(0);
    EXPECT(arts[0], before);
}

static void test_failures(void) {
    // A section missing: read_rules stops with 1.
    make_alpha(0);
    char *n = strstr(alpha, "#NATURAL");
    n[1] = 'X';
    EXPECT(read_rules(0), 1);
    char *t = strstr(alpha, "#TECHNOLOGY");
    t[1] = 'X';
    EXPECT(read_rules(0), 1);
}

int main(void) {
    game_test_image = calloc(1, 0x9ca000 - 0x400000);
    setup();
    test_rules();
    test_random_faction();
    test_factions();
    test_names();
    test_failures();
    if (failures) printf("%d failure(s)\n", failures);
    else printf("engine_rules: all passed\n");
    return failures != 0;
}
