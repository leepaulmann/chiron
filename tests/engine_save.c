// Unit test for the saved game data path (save.c): made-up game memory
// saved and read back, plain and encrypted, the cipher on known bytes, the
// map alone and the file header. No game files needed; the game's C runtime
// is stood in for by Linux's.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/engine/game.h"
#include "../src/engine/save.h"

static int failures;
#define EXPECT(expr, want)                                                    \
    do {                                                                      \
        long got_ = (long)(expr);                                             \
        if (got_ != (long)(want)) {                                           \
            printf("FAIL %s = %ld, expected %ld\n", #expr, got_, (long)(want)); \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static void *at(uint32_t addr) { return game_data + (addr - 0x682000); }
#define I32(addr) (*(int32_t *)at(addr))

static size_t t_fread(void *b, size_t l, size_t c, void *f) { return fread(b, l, c, f); }
static size_t t_fwrite(const void *b, size_t l, size_t c, void *f) { return fwrite(b, l, c, f); }
static int t_fseek(void *f, long o, int w) { return fseek(f, o, w); }
static int t_fgetc(void *f) { return fgetc(f); }
static int t_fputc(int c, void *f) { return fputc(c, f); }
static int allocs, frees, fixups;
static void *t_mem_get(size_t n) { return allocs++, malloc(n); }
static void t_free(void *p) { frees++, free(p); }
static void t_fixup(void) { fixups++; }
static int t_map_init(void) {
    *game_map = calloc((size_t)*game_map_area, sizeof(struct map_tile));
    *game_abstract_map = calloc((size_t)*game_abstract_area, 1);
    return 0;
}

static unsigned seed = 1;
static uint8_t rnd8(void) { return (uint8_t)((seed = seed * 1103515245 + 12345) >> 16); }
static void scramble(uint32_t addr, size_t n) {
    for (size_t i = 0; i < n; i++) ((uint8_t *)at(addr))[i] = rnd8();
}

// What game_data writes: address, size (the counts in the settings) and
// when (1: encrypted games only, 2: e-mail games only).
static const uint32_t regions[][3] = {
    {0x93e978, 0xce0, 1}, {0x93f798, 4, 1}, {0x939284, 4}, {0x96c9e0, 0x10660}, {0x946a50, 0x2ce0}, {0x945dd8, 0x20},
    {0x9a68ac, 0x3e80}, {0x950324, 37}, {0x9aa730, 0xe00}, {0x9ab868, 0x6800}, {0x94b558, 0x1000},
    {0x952828, 5 * 0x34}, {0x97d040, 3 * 0x134}, {0x9b2078, 0x100}, {0x94b4c0, 0x98}, {0x94ca08, 0x27a0},
    {0x946138, 0x20}, {0x939288, 8}, {0x7388d8, 0x2400, 2}, {0x93f698, 0x100, 2}, {0x93a96c, 0x40, 2}, {0x6ff69c, 0x10, 2},
    {0x6ff6d4, 0x40, 2}, {0x703de8, 0x44, 2}, {0x93a9ac, 0x4c, 2},
};
enum { N_REGIONS = sizeof(regions) / sizeof(regions[0]) };
static uint8_t *wins[8];

static void make_game(uint32_t state, uint32_t more_rules) {
    for (int i = 0; i < N_REGIONS; i++) scramble(regions[i][0], regions[i][1]);
    scramble(0x9a6490, 0x398);
    I32(0x9a6810) = 12;          // version
    I32(0x9a64c0) = (int32_t)state;
    I32(0x9a681c) = (int32_t)more_rules;
    I32(0x9a64c8) = 5;           // units
    I32(0x9a64cc) = 3;           // bases
    I32(0x9469f8) = 37;          // replay bytes, odd for the cipher
    I32(0x9a64e8) = 0;           // no humans, no tamper check
    I32(0x9392b8) = 3;           // cursor index
    scramble(0x9392c0 + 12, 4), scramble(0x939340 + 12, 4);
    for (int i = 0; i < 8; i++) memset(wins[i] + 0x1dd70, 0, 0x40), wins[i][0x1dd9c] = (uint8_t)(i + 1);
    // The e-mail message lists, empty.
    for (int i = 0; i < 7; i++) memset(at(0x93a7e0 + 0x30 * i), 0, 0x30);
    for (int i = 0; i < 64; i++) memset(at(0x737cd8 + 0x30 * i), 0, 0x30);
    I32(0x939e5c) = 1024, I32(0x939e58) = 768;
    // A 6 x 4 map.
    I32(0x949870) = 6, I32(0x949874) = 4, *game_map_area = 12, *game_abstract_area = 3;
    t_map_init();
    for (int i = 0; i < 12 * (int)sizeof(struct map_tile); i++) ((uint8_t *)*game_map)[i] = rnd8();
    for (int i = 0; i < 3; i++) (*game_abstract_map)[i] = rnd8();
}

static uint8_t *copy_of(uint32_t addr, size_t n) {
    uint8_t *c = malloc(n);
    memcpy(c, at(addr), n);
    return c;
}

static void round_trip(const char *what, uint32_t state, uint32_t more_rules) {
    make_game(state, more_rules);
    uint8_t *before[N_REGIONS];
    for (int i = 0; i < N_REGIONS; i++) before[i] = copy_of(regions[i][0], regions[i][1]);
    uint8_t *prefs = copy_of(0x9a6490, 0x398), map[12 * sizeof(struct map_tile)], abstract[3];
    memcpy(map, *game_map, sizeof(map));
    memcpy(abstract, *game_abstract_map, 3);
    int32_t win_x[8];
    for (int i = 0; i < 8; i++) memcpy(&win_x[i], wins[i] + 0x1dd9c, 4);

    FILE *f = tmpfile();
    EXPECT(save_game_data((game_file *)f, 1), 0);
    long size = ftell(f);
    // Wipe what was saved, then read it back.
    for (int i = 0; i < N_REGIONS; i++) memset(at(regions[i][0]), 0, regions[i][1]);
    for (int i = 0; i < 8; i++) memset(wins[i] + 0x1dd70, 0, 0x40);
    I32(0x9a64c8) = 0, I32(0x9a64cc) = 0;
    rewind(f);
    int ok = save_game_data((game_file *)f, 0);
    EXPECT(ok, 0);
    EXPECT(ftell(f), size);
    for (int i = 0; i < N_REGIONS; i++)
        if ((regions[i][2] != 1 || (state & 0x10000000)) && (regions[i][2] != 2 || (more_rules & 0x20)) &&
            memcmp(before[i], at(regions[i][0]), regions[i][1])) {
            printf("FAIL %s: %#x not read back\n", what, regions[i][0]);
            failures++;
        }
    if (memcmp(prefs, at(0x9a6490), 0x398)) printf("FAIL %s: settings not read back\n", what), failures++;
    if (memcmp(map, *game_map, sizeof(map)) || memcmp(abstract, *game_abstract_map, 3))
        printf("FAIL %s: map not read back\n", what), failures++;
    for (int i = 0; i < 8; i++) EXPECT(memcmp(&win_x[i], wins[i] + 0x1dd9c, 4), 0);
    for (int i = 0; i < N_REGIONS; i++) free(before[i]);
    free(prefs);
    fclose(f);
}

static void test_cipher(void) {
    // Bytes in pairs, swapped, xored with counters from 0x81 up and 0x7f
    // down, except 0 and a byte equal to its counter; the sixth byte, past
    // the five given, is read as the original does.
    const uint8_t in[6] = {0x00, 0x81, 0x7f, 0x05, 0x06, 0x7d}, want[6] = {0xfe, 0x00, 0x7b, 0xfd, 0x7d, 0x85};
    FILE *f = tmpfile();
    EXPECT(save_encrypt_write(in, 5, 1, (game_file *)f), 1);
    EXPECT(ftell(f), 6);
    uint8_t out[6] = {0};
    rewind(f);
    EXPECT(fread(out, 1, 6, f), 6);
    EXPECT(memcmp(out, want, 6), 0);
    uint8_t back[6];
    memset(back, 0xaa, sizeof(back));
    rewind(f);
    EXPECT(save_encrypt_read(back, 5, 1, (game_file *)f), 1);
    EXPECT(memcmp(back, in, 5), 0);
    EXPECT(back[5], 0xaa);  // nothing past the end
    // A byte equal to its counter is left as it is, both ways.
    const uint8_t same[2] = {0x81, 0x7f};
    rewind(f);
    EXPECT(save_encrypt_write(same, 1, 2, (game_file *)f), 1);
    rewind(f);
    EXPECT(fread(out, 1, 2, f), 2);
    EXPECT(out[0], 0x7f);
    EXPECT(out[1], 0x81);
    rewind(f);
    EXPECT(save_encrypt_read(back, 2, 1, (game_file *)f), 1);
    EXPECT(back[0], 0x81);
    EXPECT(back[1], 0x7f);
    // Too short to read.
    fclose(f);
    f = tmpfile();
    EXPECT(save_encrypt_write(same, 1, 2, (game_file *)f), 1);
    rewind(f);
    EXPECT(save_encrypt_read(back, 4, 1, (game_file *)f), 0);
    fclose(f);
}

static void test_map_and_header(void) {
    make_game(0, 0);
    uint8_t map[12 * sizeof(struct map_tile)];
    memcpy(map, *game_map, sizeof(map));
    FILE *f = tmpfile();
    EXPECT(save_header_write("TERRANMAP", (game_file *)f), 0x1a);
    EXPECT(save_map_data((game_file *)f, 1, 0), 0);
    memset(*game_map, 0, sizeof(map));
    rewind(f);
    char header[0x110];
    EXPECT(save_header_check(header, (game_file *)f), 0x1a);
    EXPECT(strcmp(header, "TERRANMAP"), 0);
    int fix = fixups;
    EXPECT(save_map_data((game_file *)f, 0, 0), 0);
    EXPECT(fixups, fix + 1);
    EXPECT(memcmp(map, *game_map, sizeof(map)), 0);
    EXPECT(save_map_data((game_file *)f, 0, 0), 1);  // at the end: nothing to read
    fclose(f);
    // The records before the map in a saved game, skipped. (The map
    // editor passes the file from the old settings record on, so the
    // original, and this, misread a current saved game.)
    make_game(0, 0);
    memcpy(map, *game_map, sizeof(map));
    f = tmpfile();
    EXPECT(save_game_data((game_file *)f, 1), 0);
    memset(*game_map, 0, sizeof(map));
    rewind(f);
    char skip[0x31c];
    EXPECT(fread(skip, sizeof(skip), 1, f), 1);
    EXPECT(save_map_data((game_file *)f, 0, 1), 0);
    EXPECT(memcmp(map, *game_map, sizeof(map)), 0);
    fclose(f);
}

int main(void) {
    game_data_size = 0x9ca000 - 0x682000;
    game_data = calloc(1, game_data_size);
    game_map = at(0x94a30c);
    game_abstract_map = at(0x94a310);
    game_map_area = at(0x949884);
    game_abstract_area = at(0x94a29c);
    game_fread = t_fread, game_fwrite = t_fwrite, game_fseek = t_fseek, game_fgetc = t_fgetc, game_fputc = t_fputc;
    game_mem_get = t_mem_get, game_free = t_free, game_map_init = t_map_init, game_fixup_landmarks = t_fixup;
    for (int i = 0; i < 8; i++) {
        wins[i] = calloc(1, 0x1ddb0);
        *(uint8_t **)at(0x7d3c3c + 4 * i) = wins[i];
    }

    test_cipher();
    round_trip("plain", 0, 0);
    round_trip("encrypted", 0x10000000, 0);
    round_trip("e-mail", 0x10000000, 0x20);
    round_trip("scenario", 0x10, 0x20);
    test_map_and_header();
    if (failures) printf("%d failure(s)\n", failures);
    else printf("engine_save: all passed\n");
    return failures != 0;
}
