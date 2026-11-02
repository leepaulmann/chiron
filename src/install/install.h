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

// Default game folder: $CHIRON_GAME_DIR, else ~/.local/share/chiron/game.
void install_default_dir(char *out, size_t outlen);
