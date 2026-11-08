// Saved games: what writes a .SAV file and reads it back, reimplemented from
// terranx.exe 2.0. Addresses and names follow Thinker (savegame.cpp,
// Copyright (C) 2019-2025 Induktio, GPL-3.0) and OpenSMACX; the behaviour is
// taken from the disassembly, quirks included.
//
// A saved game is "TERRAN", NUL, 0x1a, the format version 0x56, then
// game_data's records in a fixed order, then the clock's tick count at the
// time of saving (read back as the random seed of an ironman game). The
// records are the game's tables copied out of memory as they are, pointers
// and all; ironman, e-mail and network games pass them through a light
// byte-swapping cipher (encrypt_write and encrypt_read).
//
// These functions do file I/O and allocate from the game's heap, so shadow
// mode can't check them by running them twice: shadow mode runs the
// originals, untouched. They are checked instead by round trips
// (CHIRON_SAVE_CHECK, at the end of this file): the same game saved by the
// original and by Chiron, compared byte for byte, and each file loaded by
// each, compared in memory.
#include "save.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "game.h"
#include "hooks.h"
#include "map.h"
#include "mapgen.h"
#include "shadow.h"
#include "tech.h"

// ---- The game's globals ------------------------------------------------------

// Every global used here is in the game's writable memory, which game_data
// points at (a unit test points it at a made-up copy).
static inline void *at(uint32_t addr) { return game_data + (addr - 0x682000); }
#define I32(addr) (*(int32_t *)at(addr))
#define U32(addr) (*(uint32_t *)at(addr))
#define U8(addr) (*(uint8_t *)at(addr))

enum {
    SAVE_WRITE = 0x945d6c,    // non-zero while writing
    SAVE_ENCRYPT = 0x945d74,  // non-zero while records pass through the cipher
    SAVE_MENU = 0x945d78,     // loading from the main menu
    SAVE_STATUS = 0x945d7c,   // 1: refused, a human faction's game was tampered with
    PREFS = 0x9a6490,         // the game's settings and counters, 0x398 bytes
    SAVE_VERSION = 0x9a6810,  // in PREFS
    GAME_RULES = 0x9a649c,
    GAME_STATE = 0x9a64c0,
    NUM_VEHS = 0x9a64c8,
    NUM_BASES = 0x9a64cc,
    HUMANS = 0x9a64e8,  // bit per faction
    ALIVE = 0x9a64e9,
    MORE_RULES = 0x9a681c,
    PBEM = 0x93a95c,  // an e-mail (or hotseat) game
    NET = 0x93f660,   // a network game
    NET_PLAYERS = 0x93e978,
    NET_SEED = 0x93f798,
    TIME_CONTROLS = 0x94f1b8,  // 6 entries of 0x20, a name pointer first
    MAP_OWNER = 0x939284,      // the faction whose map the player sees
    FACTIONS = 0x96c9e0,       // 8 of 0x20cc
    FACTION_DEFS = 0x946a50,
    RANKINGS = 0x945dd8,
    REPLAY_SIZE = 0x9469f8,
    REPLAY = 0x950324,
    VEHS = 0x952828,  // 0x34 each
    BASES = 0x97d040,  // 0x134 each
    PROTOS = 0x9ab868,  // 0x34 each
    TECHS = 0x94f358,   // 0x2c each
    MAP_WINS = 0x7d3c3c,  // 8 map window pointers
    WORLD_WIN_MAP = 0x8eb48c,
    STRING_BUF = 0x9b86a0,
};

enum {
    RULES_IRONMAN = 0x400,
    STATE_SCENARIO = 0x10,
    STATE_GAME_DONE = 0x1,
    STATE_ENCRYPTED = 0x10000000,
    MORE_RULES_PBEM = 0x20,
    FACTION_MAP_REVEALED = 0x200,
    TECH_REVEALS_MAP = 0x8,
};

// A map window's fields saved (offsets into the window object).
enum { WIN_DRAW_B = 0x1dd78, WIN_DRAW_A = 0x1dd74, WIN_FLAGS = 0x1dd70, WIN_ZOOM = 0x1dd98, WIN_X = 0x1dd9c, WIN_Y = 0x1dda0 };

// The trampolines to the originals, filled in by hooks_apply.
static int (*orig_game_data)(game_file *fp, int write);

// ---- The cipher ---------------------------------------------------------------

// Bytes go out in pairs, swapped, each xored with a counter (one counting
// up from 0x81, one down from 0x7f) unless it is 0 or equal to the counter.
// For an odd length the original reads one byte past the end of the data:
// it tests the pair's index, not the byte's, against the length. That byte
// is written to the file (and ignored when read back).
int save_encrypt_write(const void *src, size_t len, size_t count, game_file *fp) {
    uint32_t total = (uint32_t)(len * count), size = total + (total & 1);
    uint8_t *buf = game_mem_get(size);
    if (!buf) return 0;
    const uint8_t *s = src;
    uint8_t *d = buf, up = 0x80, down = 0x80;
    for (uint32_t i = 0; i < size / 2; i++) {
        up++, down--;
        uint8_t a = *s++, b = 0;
        if (i < total) b = *s++;
        if (a && (uint8_t)(a ^ up)) a ^= up;
        if (b && (uint8_t)(b ^ down)) b ^= down;
        *d++ = b;
        *d++ = a;
    }
    int written = (int)game_fwrite(buf, size, 1, fp);
    game_free(buf);
    return written;
}

int save_encrypt_read(void *dst, size_t len, size_t count, game_file *fp) {
    uint32_t total = (uint32_t)(len * count), size = total + (total & 1);
    uint8_t *buf = game_mem_get(size);
    if (!buf) return 0;
    if (!game_fread(buf, size, 1, fp)) {
        game_free(buf);
        return 0;
    }
    const uint8_t *s = buf;
    uint8_t *d = dst, up = 0x80, down = 0x80;
    uint32_t left = total;
    for (uint32_t i = 0; i < size / 2; i++, s += 2, left -= 2) {
        up++, down--;
        uint8_t b = s[0], a = s[1];
        if (a && (uint8_t)(a ^ up)) a ^= up;
        if (b && (uint8_t)(b ^ down)) b ^= down;
        *d++ = a;
        if (left > 1) *d++ = b;
    }
    game_free(buf);
    return 1;
}

// One record, through the cipher or not, in the direction being taken.
// Non-zero if it was read or written whole.
static int feed(void *p, size_t len, game_file *fp) {
    if (I32(SAVE_ENCRYPT)) return I32(SAVE_WRITE) ? save_encrypt_write(p, len, 1, fp) : save_encrypt_read(p, len, 1, fp);
    return I32(SAVE_WRITE) ? (int)game_fwrite(p, len, 1, fp) : (int)game_fread(p, len, 1, fp);
}

int save_game_io(void *win, game_file *fp) {
    static const uint32_t fields[] = {WIN_DRAW_B, WIN_DRAW_A, WIN_FLAGS, WIN_ZOOM, WIN_X, WIN_Y};
    uint8_t *w = win;
    for (int i = 0; i < 5; i++)
        if (!feed(w + fields[i], 4, fp)) return 1;
    return !feed(w + fields[5], 4, fp);
}

// ---- The map --------------------------------------------------------------------

enum { MAP_HEADER = 0x949870, MAP_HEADER_SIZE = 0xaa4, MAP_PTR = 0x94a30c, ABSTRACT_PTR = 0x94a310 };

// The map's settings (0xaa4 bytes from its width, ending with the map and
// abstract map pointers, which are written as they are), then the squares
// and the abstract map.
int save_map_write(game_file *fp) {
    if (!game_fwrite(at(MAP_HEADER), MAP_HEADER_SIZE, 1, fp)) return 1;
    if (!game_fwrite(*game_map, (size_t)*game_map_area * sizeof(struct map_tile), 1, fp)) return 1;
    return !game_fwrite(*game_abstract_map, (size_t)*game_abstract_area, 1, fp);
}

int save_map_read(game_file *fp) {
    if (*game_map) game_free(*game_map);
    if (*game_abstract_map) game_free(*game_abstract_map);
    *game_map = NULL, *game_abstract_map = NULL;
    if (!game_fread(at(MAP_HEADER), MAP_HEADER_SIZE, 1, fp)) return 1;
    *game_map = NULL, *game_abstract_map = NULL;  // the stale pointers just read
    if (game_map_init()) return 1;
    if (!game_fread(*game_map, (size_t)*game_map_area * sizeof(struct map_tile), 1, fp)) return 1;
    if (!game_fread(*game_abstract_map, (size_t)*game_abstract_area, 1, fp)) return 1;
    game_fixup_landmarks();
    return 0;
}

// A map file (.MP), or with `extended` the map in a saved game, read by the
// map editor: the saved game's records before the map are skipped. The
// original reads the settings as 0x398 bytes, as they are in current saved
// games; a version is never checked, so older saves are misread (Thinker's
// note), and a faction table of the size of a version 2 one is skipped when
// the settings' version is below 3.
int save_map_data(game_file *fp, int write, int extended) {
    I32(SAVE_WRITE) = write;
    I32(SAVE_ENCRYPT) = 0;
    if (extended) {
        static uint8_t prefs[0x398], factions[0x10660], defs[0x2ce0], rankings[0x20];
        if (!(write ? (int)game_fwrite(prefs, sizeof(prefs), 1, fp) : (int)game_fread(prefs, sizeof(prefs), 1, fp))) return 1;
        if (!feed(&write, 4, fp)) return 1;  // the map owner, into the argument
        int32_t version;
        memcpy(&version, prefs + (SAVE_VERSION - PREFS), 4);
        if (!feed(factions, version >= 3 ? 0x10660 : 0xe5e0, fp)) return 1;
        if (!feed(defs, sizeof(defs), fp)) return 1;
        if (!feed(rankings, sizeof(rankings), fp)) return 1;
    }
    return (I32(SAVE_WRITE) ? save_map_write(fp) : save_map_read(fp)) ? 1 : 0;
}

// ---- E-mail games' messages ------------------------------------------------------

// The game's string lists (offsets into the list object): a linked list of
// nodes, each with an id and a text object.
enum { LIST_HEAD = 0x8, LIST_CURSOR = 0xc, LIST_COUNT = 0x10, LIST_POS = 0x14, LIST_FLAGS = 0x18,
       LIST_SRC = 0x1c, LIST_F20 = 0x20, LIST_F24 = 0x24 };
enum { NODE_ID = 0x4, NODE_DATA = 0x8, NODE_NEXT = 0xc, NODE_PREV = 0x10, DATA_TEXT = 0x4 };
#define FIELD(p, off, type) (*(type *)((uint8_t *)(p) + (off)))

// An object's scalar deleting destructor, through its virtual base.
static void delete_object(void *obj) {
    uint8_t *base = (uint8_t *)obj + (*(int32_t **)obj)[1];
    void(__attribute__((thiscall)) * dtor)(void *, int) = **(void ***)base;
    dtor(base, 1);
}

// Write: the count, then each entry's id, length and text (without its NUL;
// nothing for an empty one). Each entry is found as the list's seek_pos
// does it, leaving the list's cursor and position at the last.
static int list_write(uint8_t *list, game_file *fp) {
    char *buf = at(STRING_BUF);
    int32_t n = FIELD(list, LIST_COUNT, int32_t);
    if (!game_fwrite(&n, 4, 1, fp)) return 1;
    for (int32_t j = 0; j < n; j++) {
        int32_t count = FIELD(list, LIST_COUNT, int32_t);
        if (j <= count - 1) {
            int32_t pos = j;
            FIELD(list, LIST_CURSOR, uint8_t *) = FIELD(list, LIST_HEAD, uint8_t *);
            if (j < 0) {
                if (abs(j) > count) goto seeked;
                for (int k = abs(j); k > 0; k--) FIELD(list, LIST_CURSOR, uint8_t *) = FIELD(FIELD(list, LIST_CURSOR, uint8_t *), NODE_PREV, uint8_t *);
                pos = count + j;
            } else {
                for (int k = j; k > 0; k--) FIELD(list, LIST_CURSOR, uint8_t *) = FIELD(FIELD(list, LIST_CURSOR, uint8_t *), NODE_NEXT, uint8_t *);
            }
            FIELD(list, LIST_POS, int32_t) = pos;
        }
    seeked:;
        int32_t id = FIELD(list, LIST_HEAD, void *) ? FIELD(FIELD(list, LIST_CURSOR, uint8_t *), NODE_ID, int32_t) : 0;
        if (!game_fwrite(&id, 4, 1, fp)) return 1;
        const char *text = NULL;
        if (FIELD(list, LIST_HEAD, void *)) text = FIELD(FIELD(FIELD(list, LIST_CURSOR, uint8_t *), NODE_DATA, uint8_t *), DATA_TEXT, char *);
        strcpy(buf, text);
        int32_t len = (int32_t)strlen(buf);
        if (!game_fwrite(&len, 4, 1, fp)) return 1;
        if (len && !game_fwrite(buf, (size_t)len, 1, fp)) return 1;
    }
    return 0;
}

// Read: the list emptied (its remove_all, inlined), then each entry added.
// An empty text can't be read back: reading 0 bytes counts as a failure,
// and the whole load fails.
static int list_read(uint8_t *list, game_file *fp) {
    char *buf = at(STRING_BUF);
    if (FIELD(list, LIST_HEAD, void *)) {
        for (int k = 0; k < FIELD(list, LIST_COUNT, int32_t); k++) {
            uint8_t *head = FIELD(list, LIST_HEAD, uint8_t *);
            FIELD(list, LIST_CURSOR, uint8_t *) = FIELD(head, NODE_NEXT, uint8_t *);
            void *data = FIELD(head, NODE_DATA, void *);
            void(__attribute__((thiscall)) * release)(void *, void *) = (*(void ***)list)[1];
            release(list, data);
            if (data) delete_object(data);
            FIELD(FIELD(list, LIST_HEAD, uint8_t *), NODE_DATA, void *) = NULL;
            if (FIELD(list, LIST_HEAD, void *)) delete_object(FIELD(list, LIST_HEAD, void *));
            FIELD(list, LIST_HEAD, uint8_t *) = FIELD(list, LIST_CURSOR, uint8_t *);
        }
        FIELD(list, LIST_HEAD, void *) = NULL;
        FIELD(list, LIST_POS, int32_t) = 0;
        FIELD(list, LIST_COUNT, int32_t) = 0;
    }
    FIELD(list, LIST_POS, int32_t) = 0;
    FIELD(list, LIST_FLAGS, int32_t) = 0;
    int32_t n;
    if (!game_fread(&n, 4, 1, fp)) return 1;
    for (int32_t k = 0; k < n; k++) {
        int32_t id, len;
        if (!game_fread(&id, 4, 1, fp)) return 1;
        if (!game_fread(&len, 4, 1, fp)) return 1;
        if (!game_fread(buf, (size_t)len, 1, fp)) return 1;
        buf[len] = 0;
        FIELD(list, LIST_F20, int32_t) = 0;
        FIELD(list, LIST_F24, int32_t) = 0;
        FIELD(list, LIST_SRC, char *) = buf;
        game_string_list_add(list, id);
    }
    return 0;
}

// The diplomacy messages of an e-mail game: a list per faction 1 to 7, and
// one per pair of factions (i below j).
static int pbem_messages(int write, game_file *fp) {
    for (int i = 1; i < FACTION_COUNT; i++) {
        uint8_t *list = at(0x93a7e0 + 0x30 * (i - 1));
        if (write ? list_write(list, fp) : list_read(list, fp)) return 1;
    }
    for (int j = 2; j < FACTION_COUNT; j++)
        for (int i = 1; i < j; i++) {
            uint8_t *list = at(0x737cd8 + 0x30 * (8 * i + j));
            if (write ? list_write(list, fp) : list_read(list, fp)) return 1;
        }
    return 0;
}

// ---- The game ----------------------------------------------------------------------

enum { LEGACY_SIZE = 0x31c, LEGACY_VERSION = 0x304, PREFS_SIZE = 0x398 };

// Reading a game saved by a version before 11 (or with a header that says
// so) takes the original's conversions of old faction, unit and base
// layouts, which no file the game writes today exercises. Such a file is
// handed to the original whole: the first records are looked at, the file
// is put back where it was, and the original reads it. Non-zero if it did.
static int legacy_read(game_file *fp, int *result) {
    uint8_t peek[LEGACY_SIZE + PREFS_SIZE];
    size_t got = game_fread(peek, 1, sizeof(peek), fp);
    game_fseek(fp, -(long)got, SEEK_CUR);
    if (got < LEGACY_SIZE || !orig_game_data) return 0;
    int32_t header_version, version = 11;
    memcpy(&header_version, peek + LEGACY_VERSION, 4);
    if (got == sizeof(peek)) memcpy(&version, peek + LEGACY_SIZE + (SAVE_VERSION - PREFS), 4);
    if (header_version >= 11 && version >= 11) return 0;
    *result = orig_game_data(fp, 0);
    return 1;
}

int save_game_data(game_file *fp, int write) {
    int legacy;
    if (!write && legacy_read(fp, &legacy)) return legacy;
    I32(SAVE_WRITE) = write;
    I32(SAVE_ENCRYPT) = 0;
    I32(SAVE_STATUS) = 0;
    // First the settings as they were laid out before version 11. The
    // original writes this record from a local it never sets, so the file
    // gets whatever the stack held; a version below 11 there would make it
    // copy those bytes into the settings on writing, and read the file the
    // old way. Under Chiron that stack holds zeros and a 12 where the version
    // goes, and so does this record here, always.
    uint8_t header[LEGACY_SIZE];
    if (write) {
        memset(header, 0, sizeof(header));
        int32_t twelve = 12;
        memcpy(header + LEGACY_VERSION, &twelve, 4);
    }
    if (!(write ? (int)game_fwrite(header, sizeof(header), 1, fp) : (int)game_fread(header, sizeof(header), 1, fp))) return 1;
    if (!feed(at(PREFS), PREFS_SIZE, fp)) return 1;
    if (I32(SAVE_VERSION) < 4) {
        I32(SAVE_ENCRYPT) = 0;
    } else {
        // An encrypted game repeats the settings, encrypted, then has the
        // network game's players and seed, and the time controls (whose name
        // pointers are kept from memory).
        I32(SAVE_ENCRYPT) = U32(GAME_STATE) & STATE_ENCRYPTED;
        if (I32(SAVE_ENCRYPT)) {
            if (!feed(at(PREFS), PREFS_SIZE, fp)) return 1;
            if (!feed(at(NET_PLAYERS), 0xce0, fp)) return 1;
            if (!feed(at(NET_SEED), 4, fp)) return 1;
            uint32_t names[6];
            if (!write)
                for (int i = 0; i < 6; i++) names[i] = U32(TIME_CONTROLS + 0x20 * i);
            if (!feed(at(TIME_CONTROLS), 0xc0, fp)) return 1;
            if (!write)
                for (int i = 0; i < 6; i++) U32(TIME_CONTROLS + 0x20 * i) = names[i];
        }
    }
    if (!feed(at(MAP_OWNER), 4, fp)) return 1;
    // A buffer for converting old faction tables, allocated whatever the
    // version (and not freed if the factions can't be read).
    void *convert = game_mem_get(0x105e0);
    if (!feed(at(FACTIONS), 0x10660, fp)) return 1;
    if (convert) game_free(convert);
    if (!feed(at(FACTION_DEFS), 0x2ce0, fp)) return 1;
    if (!feed(at(RANKINGS), 0x20, fp)) return 1;
    if (!I32(SAVE_WRITE) && !I32(SAVE_MENU) && !(U32(GAME_STATE) & STATE_GAME_DONE) &&
        game_bit_count((uint32_t)(U8(HUMANS) & U8(ALIVE))) > 1) {
        // A game with more than one human (e-mail or hotseat) loaded during
        // play: refused if a living human faction's tamper flag is set.
        for (int f = 1; f < FACTION_COUNT; f++) {
            uint32_t bit = 1u << f;
            if ((U8(ALIVE) & bit) && (U8(HUMANS) & (uint8_t)bit) && I32(FACTIONS + 0x20cc * f + 0x2088)) {
                I32(SAVE_STATUS) = 1;
                return 1;
            }
        }
    }
    if (I32(SAVE_WRITE) ? save_map_write(fp) : save_map_read(fp)) return 1;
    if (!feed(at(0x9a68ac), 0x3e80, fp)) return 1;  // the factions' might, turn by turn
    if (!feed(at(REPLAY_SIZE), 4, fp)) return 1;
    if (I32(REPLAY_SIZE) && !feed(at(REPLAY), (size_t)I32(REPLAY_SIZE), fp)) return 1;
    if (!feed(at(0x9aa730), 0xe00, fp)) return 1;  // continents
    if (!feed(at(PROTOS), 0x6800, fp)) return 1;
    if (!feed(at(0x94b558), 0x1000, fp)) return 1;  // the AI's prototype ideas
    if (I32(NUM_VEHS) && !feed(at(VEHS), (size_t)I32(NUM_VEHS) * 0x34, fp)) return 1;
    if (I32(NUM_BASES) && !feed(at(BASES), (size_t)I32(NUM_BASES) * 0x134, fp)) return 1;
    if ((U32(GAME_STATE) & STATE_SCENARIO) && !feed(at(0x9b2178), 0x100, fp)) return 1;
    if (!feed(at(0x9b2078), 0x100, fp)) return 1;  // the scenario's name
    if (!feed(at(0x94b4c0), 0x98, fp)) return 1;   // the scenario's objectives
    if (!feed(at(0x94ca08), 0x27a0, fp)) return 1;  // the monuments
    if (I32(SAVE_VERSION) >= 12) {
        if (!feed(at(0x946138), 0x20, fp)) return 1;  // tectonic payloads used
    } else {
        memset(at(0x946138), 0, 0x20);
    }
    // The player's map window: unit mode, the unit, the cursor.
    if (!feed(at(0x939288), 4, fp)) return 1;
    if (!feed(at(0x93928c), 4, fp)) return 1;
    if (!feed(at(0x9392c0 + 4 * I32(0x9392b8)), 4, fp)) return 1;
    if (!feed(at(0x939340 + 4 * I32(0x9392b8)), 4, fp)) return 1;
    if (I32(SAVE_WRITE)) I32(0x9393d0) = 0;
    if (!feed(at(0x9393d0), 4, fp)) return 1;
    for (int i = 0; i < 8; i++)
        if (save_game_io(*(void **)at(MAP_WINS + 4 * i), fp)) return 1;
    if (save_game_io(at(WORLD_WIN_MAP), fp)) return 1;
    if (U32(MORE_RULES) & MORE_RULES_PBEM) {
        // E-mail games: diplomacy and council state and the messages.
        if (!feed(at(0x7388d8), 0x2400, fp)) return 1;
        if (!feed(at(0x93f698), 0x100, fp)) return 1;
        if (!feed(at(0x93a96c), 0x20, fp)) return 1;
        if (!feed(at(0x93a98c), 0x20, fp)) return 1;
        if (pbem_messages(write, fp)) return 1;
        static const uint32_t council[][2] = {{0x6ff69c, 4}, {0x6ff6a0, 4}, {0x6ff6a4, 4}, {0x6ff6a8, 4}, {0x6ff6d4, 0x20},
                                              {0x6ff6f4, 0x20}, {0x703de8, 4}, {0x703dec, 0x20}, {0x703e0c, 0x20},
                                              {0x93a9ac, 4}, {0x93a9b0, 4}, {0x93a9b4, 4}, {0x93a9b8, 0x20}, {0x93a9d8, 0x20}};
        for (size_t i = 0; i < sizeof(council) / sizeof(council[0]); i++)
            if (!feed(at(council[i][0]), council[i][1], fp)) return 1;
    }
    // The screen size the game was saved at: on another, the extra map
    // windows are closed.
    if (write) {
        if (!game_fwrite(at(0x939e5c), 4, 1, fp)) return 1;
        if (!game_fwrite(at(0x939e58), 4, 1, fp)) return 1;
    } else {
        int32_t w, h;
        if (!game_fread(&w, 4, 1, fp)) return 1;
        if (!game_fread(&h, 4, 1, fp)) return 1;
        if (w != I32(0x939e5c) || h != I32(0x939e58))
            for (int i = 1; i < 8; i++) {
                uint8_t *win = *(uint8_t **)at(MAP_WINS + 4 * i);
                if (win) FIELD(win, WIN_DRAW_A, int32_t) = 0;
            }
    }
    return 0;
}

// ---- Files ---------------------------------------------------------------------------

// Up to 256 characters to the NUL (then without a NUL of its own), and the
// character after it.
int save_header_check(char *dst, game_file *fp) {
    char c = (char)game_fgetc(fp);
    *dst++ = c;
    for (int n = 1; c && n < 0x100; n++) {
        c = (char)game_fgetc(fp);
        *dst++ = c;
    }
    return game_fgetc(fp);
}

int save_header_write(const char *src, game_file *fp) {
    char c;
    do {
        c = *src++;
        game_fputc(c, fp);
    } while (c);
    return game_fputc(0x1a, fp);
}

int save_daemon(const char *name) {
    if (I32(PBEM)) I32(0x93a9b4)++;  // e-mail games count their saves
    uint32_t seed = game_time_get_time();
    if ((U32(GAME_RULES) & RULES_IRONMAN) && !I32(NET)) game_my_srand((int)seed);
    I32(SAVE_VERSION) = 12;
    if (U32(GAME_RULES) & RULES_IRONMAN) {
        U32(GAME_STATE) |= STATE_ENCRYPTED;
        I32(NET_SEED) = 0;
    }
    if (I32(PBEM)) {
        U32(MORE_RULES) |= MORE_RULES_PBEM;
        U32(GAME_STATE) |= STATE_ENCRYPTED;
        I32(NET_SEED) = 0;
    }
    if (I32(NET)) {
        U32(GAME_STATE) |= STATE_ENCRYPTED;
        I32(NET_SEED) = I32(0x93d4f8);
        memcpy(at(NET_PLAYERS), at(0x93d4fc), 0xce0);
    }
    // The name as given, with "." and the saved game extension if it has no
    // dot. (The original's buffer is 0x108 bytes, unchecked.)
    char path[0x400] = "";
    strncat(path, name, sizeof(path) - 1);
    if (!strchr(path, '.')) {
        strncat(path, at(0x690d80), sizeof(path) - strlen(path) - 1);
        strncat(path, *(char **)at(0x691b34), sizeof(path) - strlen(path) - 1);
    }
    game_file *fp = game_env_open(path, at(0x690d84));  // "wb"
    if (!fp) return 1;
    int status = 2;
    save_header_write(at(0x690d88), fp);  // "TERRAN"
    int32_t version = 0x56;
    if (game_fwrite(&version, 4, 1, fp)) {
        status = 3;
        if (!save_game_data(fp, 1) && game_fwrite(&seed, 4, 1, fp)) status = 0;
    }
    game_fclose(fp);
    if (status) game_remove(path);
    return status;
}

int load_daemon(const char *name, int linearize) {
    game_file *fp = game_env_open(name, at(0x690d90));  // "rb"
    if (!fp) return 1;
    int status = 2;
    char header[0x110];
    save_header_check(header, fp);
    if (strcmp(header, at(0x690d94))) goto done;  // "TERRAN"
    status = 3;
    int32_t version;
    if (!game_fread(&version, 4, 1, fp) || version < 0x56) goto done;
    for (int i = 2; i < 8; i++) {
        uint8_t *win = *(uint8_t **)at(MAP_WINS + 4 * i);
        if (win && FIELD(win, WIN_DRAW_A, int32_t)) {
            FIELD(win, WIN_DRAW_A, int32_t) = 0;
            game_map_win_close(*(void **)at(MAP_WINS + 4 * i));
        }
    }
    I32(0x939294) = 0;
    I32(0x939298) = 0;
    status = 4;
    if (save_game_data(fp, 0)) {
        if (I32(SAVE_STATUS)) status = 5;
        goto done;
    }
    if (U32(GAME_RULES) & RULES_IRONMAN) {
        int32_t seed;
        if (game_fread(&seed, 4, 1, fp)) game_my_srand(seed);
    }
    if (!I32(NET))
        for (int i = 2; i < 8; i++) {
            uint8_t *win = *(uint8_t **)at(MAP_WINS + 4 * i);
            if (win) FIELD(win, WIN_DRAW_A, int32_t) = 0;
        }
    I32(0x689374) = -1;  // the base being computed
    status = 0;
    game_path_init(at(0x945b00));
    if (linearize) world_linearize_contours();
    memcpy(at(0x68fb4c), at(0x68fb20), 11 * 4);  // the natural altitudes, from the detailed ones
    // Whose map is all known: satellites, or a tech that reveals the map.
    for (int f = 1; f < FACTION_COUNT; f++) {
        uint32_t *flags = at(FACTIONS + 0x20cc * f);
        *flags &= ~(uint32_t)FACTION_MAP_REVEALED;
        for (int k = 0; k < 4; k++)
            if (((int32_t *)((uint8_t *)flags + 0x808))[k]) *flags |= FACTION_MAP_REVEALED;
        for (int t = 0; t < TECH_COUNT; t++)
            if ((U8(TECHS + 0x2c * t) & TECH_REVEALS_MAP) && has_tech(t, f)) *flags |= FACTION_MAP_REVEALED;
    }
    // Who is alive: a base, or a colony pod.
    for (int f = 1; f < FACTION_COUNT; f++) {
        int alive = 0;
        for (int b = 0; b < I32(NUM_BASES) && !alive; b++)
            if (U8(BASES + 0x134 * b + 4) == f) alive = 1;
        if (!alive)
            for (int v = 0; v < I32(NUM_VEHS); v++)
                if (U8(VEHS + 0x34 * v + 0xe) == f && U8(PROTOS + 0x34 * *(int16_t *)at(VEHS + 0x34 * v + 0xa) + 0x2a) == PLAN_COLONY)
                    alive = 1;
        if (alive) U8(ALIVE) |= (uint8_t)(1 << f);
        else U8(ALIVE) &= (uint8_t)~(1 << f);
    }
    // An ironman game's file is deleted once loaded (while still open).
    if (U32(GAME_RULES) & RULES_IRONMAN) game_remove(name);
    for (int i = 0; i < 32; i++) {
        I32(0x9392c0 + 4 * i) = I32(0x9392c0 + 4 * I32(0x9392b8));
        I32(0x939340 + 4 * i) = I32(0x939340 + 4 * I32(0x9392b8));
    }
    rebuild_vehicle_bits();
    rebuild_base_bits();
    // A leftover from testing: a file named "multi..." loaded outside a
    // network game turns on the scenario editor and debug modes.
    if (strstr(name, at(0x690d9c)) && !I32(NET)) {
        I32(0x939288) = 0;
        U32(GAME_STATE) |= 0x10c0;
        uint8_t *obj = at(0x7b0cb8);
        void **vt = *(void ***)obj;
        if ((U32(GAME_STATE) & 0x80) && (U32(GAME_STATE) & 0x40))
            ((void(__attribute__((thiscall)) *)(void *, int))vt[1])(obj, 0);
        else
            ((void(__attribute__((thiscall)) *)(void *))vt[2])(obj);
    }
    I32(0x9b2070) = I32(MAP_OWNER);
    if (I32(NET)) {
        I32(PBEM) = 0;
    } else if ((U32(MORE_RULES) & MORE_RULES_PBEM) || game_bit_count(U8(HUMANS)) > 1) {
        I32(PBEM) = 1;
        game_check_tamper();
    } else {
        I32(PBEM) = 0;
    }
    *game_draw_state |= 4;
done:
    game_fclose(fp);
    return status;
}

// ---- Hooks ------------------------------------------------------------------------------

// Non-zero while the round-trip check runs the originals: the game's calls
// to these functions (from inside an original) then go to the originals.
static int save_side_original;

#define SAVE_ENTRY(ret, name, ours, params, args)                        \
    static ret(*orig_##name) params;                                   \
    static struct hook_stats stats_##name = {#name};                   \
    static ret name params {                                           \
        return save_side_original ? orig_##name args : ours args;      \
    }                                                                  \
    static ret shadow_##name params {                                  \
        stats_##name.calls++;                                          \
        return orig_##name args;                                       \
    }

SAVE_ENTRY(int, encrypt_write, save_encrypt_write, (const void *s, size_t l, size_t c, game_file *f), (s, l, c, f))
SAVE_ENTRY(int, encrypt_read, save_encrypt_read, (void *d, size_t l, size_t c, game_file *f), (d, l, c, f))
SAVE_ENTRY(int, game_io, save_game_io, (void *w, game_file *f), (w, f))
SAVE_ENTRY(int, map_data, save_map_data, (game_file * f, int w, int e), (f, w, e))
SAVE_ENTRY(int, map_write, save_map_write, (game_file * f), (f))
SAVE_ENTRY(int, map_read, save_map_read, (game_file * f), (f))
SAVE_ENTRY(int, header_check, save_header_check, (char *d, game_file *f), (d, f))
SAVE_ENTRY(int, header_write, save_header_write, (const char *s, game_file *f), (s, f))
SAVE_ENTRY(int, load_daemon_entry, load_daemon, (const char *n, int l), (n, l))

static struct hook_stats stats_game_data = {"game_data"};
static int game_data_entry(game_file *f, int w) { return save_side_original ? orig_game_data(f, w) : save_game_data(f, w); }
static int shadow_game_data_entry(game_file *f, int w) {
    stats_game_data.calls++;
    return orig_game_data(f, w);
}
#define orig_game_data_entry orig_game_data
#define stats_game_data_entry stats_game_data

static void save_check(void);
static int (*orig_save_daemon_entry)(const char *n);
static struct hook_stats stats_save_daemon_entry = {"save_daemon"};
static int save_daemon_entry(const char *n) {
    if (save_side_original) return orig_save_daemon_entry(n);
    save_check();
    return save_daemon(n);
}
static int shadow_save_daemon_entry(const char *n) {
    stats_save_daemon_entry.calls++;
    save_check();
    return orig_save_daemon_entry(n);
}

// ---- The round-trip check -----------------------------------------------------------------
//
// CHIRON_SAVE_CHECK=N: every N turns, when the game saves (its autosave),
// the game is first saved by the original and by Chiron (save_daemon), plain
// and encrypted with the e-mail records (the game's flags set for the
// saves, then put back), and the map alone (map_data); the files are
// compared byte for byte. With CHIRON_SAVE_CHECK_LOAD=1 each file is then
// loaded by both, crosswise: Chiron's file by the original (A), the
// original's by Chiron (B), the original's by the original (C); the game's
// writable memory and the map after A and B, and after B and C, are
// compared. The game goes on from C, the original's load of its own save.
// The maps are loaded likewise (map_data, from the .MP files and from the
// saved game). The files are kept in the user folder's saves/chk.
//
// Bytes a load sets that are not in the file and may differ are left out
// of the memory comparison (save_check_ignored). The tick count saved last
// is the same in both files: the clock is held still for the saves.

static int check_every = -1, check_loads;
static char check_dir[3800];

static int check_setup(void) {
    if (check_every >= 0) return check_every > 0;
    const char *every = getenv("CHIRON_SAVE_CHECK"), *user = getenv("CHIRON_USER_DIR");
    check_every = every ? atoi(every) : 0;
    if (check_every <= 0) return check_every = 0;
    if (!user || !*user) {
        // Never in the player's own user folder.
        fprintf(stderr, "chiron: CHIRON_SAVE_CHECK needs CHIRON_USER_DIR (a scratch user folder); not checking\n");
        return check_every = 0;
    }
    const char *loads = getenv("CHIRON_SAVE_CHECK_LOAD");
    check_loads = loads && *loads == '1';
    snprintf(check_dir, sizeof(check_dir), "%s/saves", user);
    mkdir(check_dir, 0755);
    snprintf(check_dir, sizeof(check_dir), "%s/saves/chk", user);
    if (mkdir(check_dir, 0755) && errno != EEXIST) {
        fprintf(stderr, "chiron: CHIRON_SAVE_CHECK: can't make %s\n", check_dir);
        return check_every = 0;
    }
    return 1;
}

static unsigned char *slurp(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(n > 0 ? (size_t)n : 1);
    *size = buf && n > 0 ? fread(buf, 1, (size_t)n, f) : 0;
    fclose(f);
    return buf;
}

static int check_failures, check_count;

static void report(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void report(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "chiron: save check turn %d: ", *game_turn);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

// Differing byte ranges of two buffers (gaps under 16 bytes merged), the
// first few printed with `base` added to their offsets. The count of
// differing bytes.
static size_t print_diffs(const char *what, const uint8_t *a, const uint8_t *b, size_t n, uint32_t base,
                          int (*ignored)(uint32_t addr)) {
    size_t bytes = 0;
    int ranges = 0;
    char line[2048] = "";
    size_t len = 0;
    for (size_t i = 0; i < n;) {
        if (a[i] == b[i] || (ignored && ignored(base + (uint32_t)i))) {
            i++;
            continue;
        }
        size_t start = i, last = i;
        while (i < n && i - last < 16) {
            if (a[i] != b[i] && !(ignored && ignored(base + (uint32_t)i))) last = i, bytes++;
            i++;
        }
        if (++ranges <= 12 && len < sizeof(line) - 64)
            len += (size_t)snprintf(line + len, sizeof(line) - len, " %#x+%zu", base + (uint32_t)start, last - start + 1);
        i = last + 1;
    }
    if (bytes) report("%s: %zu bytes differ in %d ranges:%s%s", what, bytes, ranges, line, ranges > 12 ? " ..." : "");
    return bytes;
}

static void compare_files(const char *what, const char *a, const char *b) {
    size_t na = 0, nb = 0;
    unsigned char *fa = slurp(a, &na), *fb = slurp(b, &nb);
    check_count++;
    if (!fa || !fb || !na) {
        report("%s: files missing (%s, %s)", what, a, b);
        check_failures++;
    } else if (na != nb) {
        report("%s: sizes differ, original %zu, Chiron %zu", what, na, nb);
        print_diffs(what, fa, fb, na < nb ? na : nb, 0, NULL);
        check_failures++;
    } else if (print_diffs(what, fa, fb, na, 0, NULL)) {
        check_failures++;
    } else {
        report("%s: files identical (%zu bytes)", what, na);
    }
    free(fa);
    free(fb);
}

// The game's state after a load: its writable memory, the map and the
// abstract map.
struct snapshot {
    uint8_t *mem, *map, *abstract;
    size_t map_size, abstract_size;
};

static void snapshot_take(struct snapshot *s) {
    if (!s->mem) s->mem = malloc(game_data_size);
    memcpy(s->mem, game_data, game_data_size);
    s->map_size = *game_map ? (size_t)*game_map_area * sizeof(struct map_tile) : 0;
    s->abstract_size = *game_abstract_map ? (size_t)*game_abstract_area : 0;
    free(s->map), free(s->abstract);
    s->map = malloc(s->map_size + 1), s->abstract = malloc(s->abstract_size + 1);
    if (s->map_size) memcpy(s->map, *game_map, s->map_size);
    if (s->abstract_size) memcpy(s->abstract, *game_abstract_map, s->abstract_size);
}

// Memory a load sets from outside the file, or that differs between two
// loads for reasons other than the loader's work:
static int save_check_ignored(uint32_t a) {
    // The map and abstract map pointers: each load allocates them anew.
    if (a >= MAP_PTR && a < ABSTRACT_PTR + 4) return 1;
    // The path finder's three tables (0x945b00..0x945b0b), freed and
    // allocated anew by Path_init on every load.
    if (a >= 0x945b00 && a < 0x945b0c) return 1;
    // The C runtime's count of files opened (_cflush, incremented by each
    // fopen): one more for each load.
    if (a >= 0x9c0504 && a < 0x9c0508) return 1;
    // The C runtime's small-block heap rovers (0x9c11ac..0x9c11b7), written
    // only by its allocator.
    if (a >= 0x9c11ac && a < 0x9c11b8) return 1;
    // The path of the file the game's folders found last (env_open): the
    // two loads compared crosswise open files with different names.
    if (a >= 0x9b8398 && a < 0x9b8498) return 1;
    return 0;
}

static void compare_states(const char *what, struct snapshot *a, struct snapshot *b) {
    check_count++;
    size_t bad = print_diffs(what, a->mem, b->mem, game_data_size, 0x682000, save_check_ignored);
    if (a->map_size != b->map_size || a->abstract_size != b->abstract_size) {
        report("%s: map sizes differ", what);
        bad++;
    } else {
        char sub[128];
        snprintf(sub, sizeof(sub), "%s (map, offsets)", what);
        bad += print_diffs(sub, a->map, b->map, a->map_size, 0, NULL);
        snprintf(sub, sizeof(sub), "%s (abstract map, offsets)", what);
        bad += print_diffs(sub, a->abstract, b->abstract, a->abstract_size, 0, NULL);
    }
    if (bad) check_failures++;
    else report("%s: game state identical", what);
}

static void chk_game_path(char *out, size_t n, const char *name) { snprintf(out, n, "saves\\chk\\%s", name); }
static void linux_path(char *out, size_t n, const char *name) { snprintf(out, n, "%s/%s", check_dir, name); }

// Save with the original or with Chiron.
static int check_save(int original, const char *name) {
    char path[256];
    chk_game_path(path, sizeof(path), name);
    if (!original) return save_daemon(path);
    save_side_original = 1;
    int r = orig_save_daemon_entry(path);
    save_side_original = 0;
    return r;
}

static int check_load(int original, const char *name, int linearize) {
    char path[256];
    chk_game_path(path, sizeof(path), name);
    if (!original) return load_daemon(path, linearize);
    save_side_original = 1;
    int r = orig_load_daemon_entry(path, linearize);
    save_side_original = 0;
    return r;
}

// The map alone: written to a .MP file (with the map editor's header), or
// read from a .MP file or (extended) a saved game.
static int check_map(int original, const char *name, int write, int extended) {
    char path[256];
    chk_game_path(path, sizeof(path), name);
    game_file *fp = game_env_open(path, at(write ? 0x690d84 : 0x690d90));
    if (!fp) return -1;
    if (write) {
        save_header_write("TERRANMAP", fp);
        int32_t version = 5;
        game_fwrite(&version, 4, 1, fp);
    } else {
        char header[0x110];
        int32_t version;
        save_header_check(header, fp);
        game_fread(&version, 4, 1, fp);
    }
    save_side_original = original;
    // map_data sets the direction itself: the flag left the other way.
    I32(SAVE_WRITE) = !write;
    int r = original ? orig_map_data(fp, write, extended) : save_map_data(fp, write, extended);
    save_side_original = 0;
    game_fclose(fp);
    return r;
}

static void check_loads_of(const char *what, const char *orig_file, const char *ours_file, int linearize) {
    static struct snapshot a, b, c;
    char label[160];
    int ra = check_load(1, ours_file, linearize);
    snapshot_take(&a);
    int rb = check_load(0, orig_file, linearize);
    snapshot_take(&b);
    int rc = check_load(1, orig_file, linearize);
    snapshot_take(&c);
    if (ra || rb || rc) {
        report("%s: loads returned %d (original, Chiron's file), %d (Chiron, original's file), %d (original, its own)", what, ra, rb, rc);
        check_failures++;
    }
    snprintf(label, sizeof(label), "%s: loaded by original from Chiron's file vs Chiron from original's", what);
    compare_states(label, &a, &b);
    snprintf(label, sizeof(label), "%s: loaded by Chiron vs original, same file", what);
    compare_states(label, &b, &c);
}

static void save_check(void) {
    static int busy, last_turn = -1;
    if (busy || hooks_protecting() || !check_setup() || !*game_map || *game_turn == last_turn || *game_turn % check_every) return;
    busy = 1;
    last_turn = *game_turn;
    const char *ext = *(char **)at(0x691b34);
    char o[64], c[64], ox[64], cx[64], fo[96], fc[96], fox[96], fcx[96], mo[64], mc[64], lo[4096], lc[4096];
    snprintf(o, sizeof(o), "t%03d_o", *game_turn);
    snprintf(c, sizeof(c), "t%03d_c", *game_turn);
    snprintf(ox, sizeof(ox), "t%03d_ox", *game_turn);
    snprintf(cx, sizeof(cx), "t%03d_cx", *game_turn);
    snprintf(mo, sizeof(mo), "t%03d_mo.MP", *game_turn);
    snprintf(mc, sizeof(mc), "t%03d_mc.MP", *game_turn);
    snprintf(fo, sizeof(fo), "%s.%s", o, ext);
    snprintf(fc, sizeof(fc), "%s.%s", c, ext);
    snprintf(fox, sizeof(fox), "%s.%s", ox, ext);
    snprintf(fcx, sizeof(fcx), "%s.%s", cx, ext);

    if (hooks_hold_time) hooks_hold_time(1);
    int r1 = check_save(1, o), r2 = check_save(0, c);
    uint32_t state = U32(GAME_STATE), more = U32(MORE_RULES);
    U32(GAME_STATE) |= STATE_ENCRYPTED;
    U32(MORE_RULES) |= MORE_RULES_PBEM;
    int r3 = check_save(1, ox), r4 = check_save(0, cx);
    U32(GAME_STATE) = state, U32(MORE_RULES) = more;
    int r5 = check_map(1, mo, 1, 0), r6 = check_map(0, mc, 1, 0);
    if (hooks_hold_time) hooks_hold_time(0);
    if (r1 || r2 || r3 || r4 || r5 || r6) {
        report("saves returned %d %d (plain), %d %d (encrypted), %d %d (map)", r1, r2, r3, r4, r5, r6);
        check_failures++;
    }
    linux_path(lo, sizeof(lo), fo), linux_path(lc, sizeof(lc), fc);
    compare_files("saved game", lo, lc);
    linux_path(lo, sizeof(lo), fox), linux_path(lc, sizeof(lc), fcx);
    compare_files("encrypted e-mail game", lo, lc);
    linux_path(lo, sizeof(lo), mo), linux_path(lc, sizeof(lc), mc);
    compare_files("map", lo, lc);

    if (check_loads) {
        if (hooks_hold_time) hooks_hold_time(1);
        int linearize = (*game_turn / check_every) % 2;
        check_loads_of("encrypted e-mail game", fox, fcx, linearize);
        static struct snapshot d, e;
        check_loads_of("saved game", fo, fc, linearize);
        // The maps from the .MP files. (Not the map in a saved game, as the
        // map editor would open one: the original reads a current saved
        // game's records at the wrong offsets, and asks for gigabytes.)
        int rf = check_map(1, mc, 0, 0);
        snapshot_take(&d);
        int rg = check_map(0, mo, 0, 0);
        snapshot_take(&e);
        compare_states("map file, original (Chiron's file) vs Chiron (original's)", &d, &e);
        if (rf || rg) {
            report("map loads returned %d %d", rf, rg);
            check_failures++;
        }
        // The original's load of its own save left the game as it goes on.
        if (hooks_hold_time) hooks_hold_time(0);
    }
    report("%d comparisons so far, %d failed", check_count, check_failures);
    busy = 0;
}

#define PUSH_EBP_MOV_EBP_ESP 0x55, 0x8b, 0xec
static const struct hook save_hooks[] = {
    HOOK(0x5a66e0, encrypt_write, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x10),                        // sub esp, 0x10
    HOOK(0x5a67b0, encrypt_read, PUSH_EBP_MOV_EBP_ESP, 0x83, 0xec, 0x08),                         // sub esp, 8
    HOOK(0x5a6890, game_io, PUSH_EBP_MOV_EBP_ESP, 0x8b, 0x0d, 0x74, 0x5d, 0x94, 0x00),            // mov ecx, [0x945d74]
    HOOK(0x5a6ac0, game_data_entry, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0x74, 0x03, 0x00, 0x00),    // sub esp, 0x374
    HOOK(0x5a9280, map_data, PUSH_EBP_MOV_EBP_ESP, 0xb8, 0xf8, 0x36, 0x01, 0x00),                 // mov eax, 0x136f8
    HOOK(0x5a94f0, save_daemon_entry, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0x08, 0x01, 0x00, 0x00),  // sub esp, 0x108
    HOOK(0x5a9760, load_daemon_entry, PUSH_EBP_MOV_EBP_ESP, 0x81, 0xec, 0x10, 0x01, 0x00, 0x00),  // sub esp, 0x110
    HOOK(0x5910b0, map_write, PUSH_EBP_MOV_EBP_ESP, 0x56, 0x8b, 0x75, 0x08),                      // push esi; mov esi, [ebp+8]
    HOOK(0x591130, map_read, PUSH_EBP_MOV_EBP_ESP, 0xa1, 0x0c, 0xa3, 0x94, 0x00),                 // mov eax, [0x94a30c]
    HOOK(0x57d1f0, header_check, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56, 0x57),                         // push ebx; push esi; push edi
    HOOK(0x57d240, header_write, PUSH_EBP_MOV_EBP_ESP, 0x53, 0x56, 0x8b, 0x75, 0x08),             // push ebx; push esi; mov esi, [ebp+8]
};
struct hook_module save_module = {"save", save_hooks, HOOK_COUNT(save_hooks), NULL, NULL, -1};
