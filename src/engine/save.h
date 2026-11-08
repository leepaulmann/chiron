// Saved games, reimplemented from terranx.exe 2.0: the data path that
// writes and reads a .SAV file (save_daemon, load_daemon, game_data), the
// map part of it (map_data, map_write, map_read), and the light encryption
// used for ironman, e-mail and network games. The Save Game and Load Game
// dialogs stay the game's own; they call these. See save.c.
#pragma once

#include <stddef.h>

// The game's C runtime FILE, never Linux's: its files come from the game's
// fopen, and are only passed to the game's fread, fwrite and so on.
typedef struct game_file game_file;

int save_encrypt_write(const void *src, size_t len, size_t count, game_file *fp);
int save_encrypt_read(void *dst, size_t len, size_t count, game_file *fp);
// One map window's view (zoom, centre and the like): 0 if read or written.
int save_game_io(void *map_win, game_file *fp);
// The whole game state, written (write non-zero) or read: 0 if all went well.
int save_game_data(game_file *fp, int write);
// A map file, or (extended) the map in a saved game: 0 if all went well.
int save_map_data(game_file *fp, int write, int extended);
int save_map_write(game_file *fp);
int save_map_read(game_file *fp);
// The "TERRAN" header line, read up to its NUL and the byte after.
int save_header_check(char *dst, game_file *fp);
int save_header_write(const char *src, game_file *fp);
// A saved game to and from a file name: 0 if all went well, else the
// game's error codes (1 not opened, 2 not a saved game, 3 too old,
// 4 damaged, 5 security).
int save_daemon(const char *name);
int load_daemon(const char *name, int linearize);
