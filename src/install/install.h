// Locating and verifying the player's own copy of the game.
#pragma once

#include <stddef.h>

struct known_build {
    const char *sha256;
    const char *description;
};

// Compute the SHA-256 of a file as lowercase hex. Returns 0 on success.
int install_sha256_file(const char *path, char out_hex[65]);

// Check `exe_host_path` against the builds Chiron supports. Returns the
// matching build, or NULL for an unknown file.
const struct known_build *install_identify(const char *exe_host_path, char out_hex[65]);

// Chiron's data folder: $XDG_DATA_HOME/chiron, else ~/.local/share/chiron.
void install_data_dir(char *out, size_t outlen);

// Settings in $XDG_CONFIG_HOME/chiron/chiron.conf ("key = value" lines).
// get returns 1 if the key is set; set returns 0 on success.
int install_config_get(const char *key, char *out, size_t outlen);
int install_config_set(const char *key, const char *value);

// Whether `dir` holds terranx.exe. *build is the supported build it matches,
// or NULL with `hash` set for an unknown one.
int install_check_dir(const char *dir, const struct known_build **build, char hash[65]);

// Search `start` and up to `depth` levels below it for a supported copy of
// the game. On success the folder goes to `out`. An unsupported terranx.exe
// seen on the way is described in `unknown`, if given.
int install_find_game(const char *start, int depth, char *out, size_t outlen, char *unknown, size_t unknown_len);

// Look for the game where GOG installs usually are (~/Games, ~/GOG Games,
// ~/.wine, ~/Downloads). Returns 1 and the folder if found.
int install_autodetect(char *out, size_t outlen);

// First-run setup from a game folder (or a folder above it, such as a Wine
// prefix) or a GOG offline installer, which is unpacked with innoextract.
// Saves the game folder in the config file and returns 0 on success.
int install_setup(const char *path, char *game_dir, size_t game_dir_len);
