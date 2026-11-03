#include "install.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

// Executables Chiron knows how to run. Each comes from a retail or digital
// release that players buy; Chiron never ships any of them.
static const struct known_build builds[] = {
    {"01901cbf7196b0c5d0df9540a029520f5df8fd9a6b343deef8b5663872805fcf",
     "Alien Crossfire 2.0 (GOG Planetary Pack)"},
};

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t h[8], const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | p[4 * i + 1] << 16 | p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = k + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += k;
}

int install_sha256_file(const char *path, char out_hex[65]) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buf[64 * 1024 + 128];
    uint64_t total = 0;
    size_t n;
    while ((n = fread(buf, 1, 64 * 1024, f)) == 64 * 1024) {
        for (size_t i = 0; i < n; i += 64) sha256_block(h, buf + i);
        total += n;
    }
    fclose(f);
    total += n;
    size_t full = n & ~63u;
    for (size_t i = 0; i < full; i += 64) sha256_block(h, buf + i);
    size_t rest = n - full;
    memmove(buf, buf + full, rest);
    buf[rest++] = 0x80;
    size_t padded = rest + 8 <= 64 ? 64 : 128;
    memset(buf + rest, 0, padded - rest);
    for (int i = 0; i < 8; i++) buf[padded - 1 - i] = (uint8_t)((total * 8) >> (8 * i));
    for (size_t i = 0; i < padded; i += 64) sha256_block(h, buf + i);
    for (int i = 0; i < 8; i++) sprintf(out_hex + 8 * i, "%08x", h[i]);
    return 0;
}

const struct known_build *install_identify(const char *exe_host_path, char out_hex[65]) {
    if (install_sha256_file(exe_host_path, out_hex)) return NULL;
    for (size_t i = 0; i < sizeof(builds) / sizeof(builds[0]); i++)
        if (!strcmp(builds[i].sha256, out_hex)) return &builds[i];
    return NULL;
}

// ---- Folders and the config file -------------------------------------------

static void xdg_dir(const char *env, const char *fallback, char *out, size_t outlen) {
    const char *v = getenv(env);
    if (v && *v == '/') snprintf(out, outlen, "%s/chiron", v);
    else snprintf(out, outlen, "%s/%s/chiron", getenv("HOME") ? getenv("HOME") : ".", fallback);
}

void install_data_dir(char *out, size_t outlen) { xdg_dir("XDG_DATA_HOME", ".local/share", out, outlen); }

static void config_path(char *out, size_t outlen) {
    char dir[3072];
    xdg_dir("XDG_CONFIG_HOME", ".config", dir, sizeof(dir));
    snprintf(out, outlen, "%s/chiron.conf", dir);
}

static int mkdirs(const char *path) {
    char p[4096];
    snprintf(p, sizeof(p), "%s", path);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') {
            *s = 0;
            if (mkdir(p, 0755) && errno != EEXIST) return -1;
            *s = '/';
        }
    return mkdir(p, 0755) && errno != EEXIST ? -1 : 0;
}

int install_config_get(const char *key, char *out, size_t outlen) {
    char path[4096], line[4200];
    config_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    int found = 0;
    size_t klen = strlen(key);
    while (f && !found && fgets(line, sizeof(line), f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (strncmp(s, key, klen)) continue;
        s += klen;
        while (*s == ' ' || *s == '\t') s++;
        if (*s++ != '=') continue;
        while (*s == ' ' || *s == '\t') s++;
        size_t n = strcspn(s, "\r\n");
        snprintf(out, outlen, "%.*s", (int)n, s);
        found = 1;
    }
    if (f) fclose(f);
    return found;
}

int install_config_set(const char *key, const char *value) {
    char path[4096], tmp[4200], line[4200], dir[4096];
    xdg_dir("XDG_CONFIG_HOME", ".config", dir, sizeof(dir));
    if (mkdirs(dir)) return -1;
    config_path(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    FILE *in = fopen(path, "r"), *out = fopen(tmp, "w");
    if (!out) {
        if (in) fclose(in);
        return -1;
    }
    if (!in) fprintf(out, "# Chiron settings\n");
    size_t klen = strlen(key);
    int written = 0;
    while (in && fgets(line, sizeof(line), in)) {
        const char *s = line + strspn(line, " \t");
        if (!strncmp(s, key, klen) && strchr(" \t=", s[klen])) {
            if (!written) fprintf(out, "%s = %s\n", key, value);
            written = 1;
        } else {
            fputs(line, out);
        }
    }
    if (!written) fprintf(out, "%s = %s\n", key, value);
    if (in) fclose(in);
    if (fclose(out)) return -1;
    return rename(tmp, path);
}

// ---- Finding the game --------------------------------------------------------

// The file `name` in `dir`, ignoring case. Returns 1 if found.
static int find_in_dir(const char *dir, const char *name, char *out, size_t outlen) {
    DIR *d = opendir(dir);
    struct dirent *e;
    int found = 0;
    while (d && !found && (e = readdir(d)))
        if (!strcasecmp(e->d_name, name)) {
            snprintf(out, outlen, "%s/%s", dir, e->d_name);
            found = 1;
        }
    if (d) closedir(d);
    return found;
}

int install_check_dir(const char *dir, const struct known_build **build, char hash[65]) {
    char exe[8192];
    *build = NULL;
    if (!find_in_dir(dir, "terranx.exe", exe, sizeof(exe))) return 0;
    *build = install_identify(exe, hash);
    return 1;
}

// Search `dir` and the folders below it, `depth` levels deep, for a supported
// copy of the game. Wine prefixes are searched too, but not Windows itself.
static int search(const char *dir, int depth, char *out, size_t outlen, char *unknown, size_t unknown_len) {
    const struct known_build *build;
    char hash[65];
    if (install_check_dir(dir, &build, hash)) {
        if (build) {
            snprintf(out, outlen, "%s", dir);
            return 1;
        }
        if (unknown && !*unknown) snprintf(unknown, unknown_len, "%s (sha256 %s)", dir, hash);
    }
    if (depth <= 0) return 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d))) {
        if (e->d_name[0] == '.' && strcmp(e->d_name, ".wine")) continue;
        if (!strcasecmp(e->d_name, "windows") || !strcasecmp(e->d_name, "node_modules")) continue;
        char sub[4096];
        struct stat st;
        if ((size_t)snprintf(sub, sizeof(sub), "%s/%s", dir, e->d_name) >= sizeof(sub)) continue;
        if (lstat(sub, &st) || !S_ISDIR(st.st_mode)) continue;  // no symlinks: they can loop
        found = search(sub, depth - 1, out, outlen, unknown, unknown_len);
    }
    closedir(d);
    return found;
}

int install_find_game(const char *start, int depth, char *out, size_t outlen, char *unknown, size_t unknown_len) {
    if (unknown && unknown_len) *unknown = 0;
    return search(start, depth, out, outlen, unknown, unknown_len);
}

int install_autodetect(char *out, size_t outlen) {
    const char *home = getenv("HOME");
    if (!home) return 0;
    // Where GOG installs usually end up on Linux: Wine prefixes made by hand,
    // by Lutris or by Heroic, or an installer unpacked into the home folder.
    static const char *const roots[] = {"Games", "GOG Games", ".wine/drive_c", "Downloads"};
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        char root[4096];
        snprintf(root, sizeof(root), "%s/%s", home, roots[i]);
        if (install_find_game(root, 6, out, outlen, NULL, 0)) return 1;
    }
    return 0;
}

// Unpack a GOG offline installer with innoextract into `dest`.
static int unpack_installer(const char *installer, const char *dest) {
    if (mkdirs(dest)) {
        fprintf(stderr, "chiron: cannot create %s: %s\n", dest, strerror(errno));
        return -1;
    }
    fprintf(stderr, "chiron: unpacking %s into %s\n", installer, dest);
    pid_t pid = fork();
    if (pid == 0) {
        execlp("innoextract", "innoextract", "--silent", "--progress", "--output-dir", dest, installer, (char *)NULL);
        _exit(127);
    }
    int status;
    if (pid < 0 || waitpid(pid, &status, 0) < 0) return -1;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        fprintf(stderr, "chiron: innoextract is needed to unpack the installer; install it and try again\n");
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        fprintf(stderr, "chiron: innoextract failed\n");
        return -1;
    }
    return 0;
}

int install_setup(const char *path, char *game_dir, size_t game_dir_len) {
    struct stat st;
    char unknown[4200], search_from[4096];
    if (stat(path, &st)) {
        fprintf(stderr, "chiron: %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        snprintf(search_from, sizeof(search_from), "%s", path);
    } else {
        // An installer: unpack it into Chiron's own folder.
        char data[3072];
        install_data_dir(data, sizeof(data));
        snprintf(search_from, sizeof(search_from), "%s/game", data);
        DIR *d = opendir(search_from);
        int empty = 1;
        struct dirent *e;
        while (d && empty && (e = readdir(d))) empty = !strcmp(e->d_name, ".") || !strcmp(e->d_name, "..");
        if (d) closedir(d);
        if (!empty) {
            fprintf(stderr, "chiron: %s is not empty; move it away first, or run --setup on a game folder\n",
                    search_from);
            return -1;
        }
        if (unpack_installer(path, search_from)) return -1;
    }
    if (!install_find_game(search_from, 6, game_dir, game_dir_len, unknown, sizeof(unknown))) {
        if (*unknown)
            fprintf(stderr, "chiron: found terranx.exe in %s, but not a version Chiron supports.\n", unknown);
        else
            fprintf(stderr, "chiron: no terranx.exe found in %s\n", search_from);
        return -1;
    }
    if (install_config_set("game", game_dir)) {
        fprintf(stderr, "chiron: cannot write the config file: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}
