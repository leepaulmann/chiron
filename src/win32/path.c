// Windows paths onto the host file system. Drive C: is two folders layered
// like an overlay: the game folder, which Chiron never writes to, and a user
// folder on top that receives everything the game writes (saves, its .ini,
// temporary files). Reads see the user folder's copy of a file if it has
// one, and the game folder's otherwise. Every path component is matched
// case-insensitively against what is on disk.
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "win32.h"

static char root[4096];       // the game folder
static char user_root[4096];  // the user folder, or empty to write into the game folder
static char cwd[260] = "C:\\";

static void set_root(char *dst, const char *src) {
    snprintf(dst, 4096, "%s", src ? src : "");
    size_t n = strlen(dst);
    while (n > 1 && dst[n - 1] == '/') dst[--n] = 0;
}

void path_init(const char *game_root, const char *user) {
    set_root(root, game_root);
    set_root(user_root, user);
}

const char *path_game_root(void) { return root; }
const char *path_user_root(void) { return user_root; }
const char *path_get_cwd(void) { return cwd; }

// Build an absolute, normalised Windows path ("C:\a\b") from `in`.
static int absolutize(const char *in, char *out, size_t outlen) {
    char tmp[1024];
    if (isalpha((unsigned char)in[0]) && in[1] == ':') {
        if (toupper((unsigned char)in[0]) != 'C') return -1;  // only drive C: exists
        snprintf(tmp, sizeof(tmp), "%s", in + 2);
    } else if (in[0] == '\\' || in[0] == '/') {
        snprintf(tmp, sizeof(tmp), "%s", in);
    } else {
        snprintf(tmp, sizeof(tmp), "%s\\%s", cwd + 2, in);
    }

    // Split on separators, resolving "." and "..".
    char *parts[128];
    int n = 0;
    for (char *s = tmp, *tok; (tok = strsep(&s, "\\/"));) {
        if (!*tok || !strcmp(tok, ".")) continue;
        if (!strcmp(tok, "..")) {
            if (n) n--;
            continue;
        }
        if (n == 128) return -1;
        parts[n++] = tok;
    }
    size_t len = snprintf(out, outlen, "C:");
    for (int i = 0; i < n && len < outlen; i++) len += snprintf(out + len, outlen - len, "\\%s", parts[i]);
    if (!n) snprintf(out + len, outlen - len, "\\");
    return len < outlen ? 0 : -1;
}

// Split an absolute Windows path into its components, in place. Windows
// ignores trailing dots and spaces in names.
static int components(char *abs, char *parts[128]) {
    int n = 0;
    char *s = abs + 3, *comp;
    while (s && (comp = strsep(&s, "\\")) && *comp) {
        size_t cl = strlen(comp);
        while (cl && (comp[cl - 1] == '.' || comp[cl - 1] == ' ')) comp[--cl] = 0;
        if (n == 128) return -1;
        parts[n++] = comp;
    }
    return n;
}

// Find `name` in directory `dir`, ignoring case; its on-disk spelling goes
// to `out`. Returns 1 if found.
static int find_entry(const char *dir, const char *name, char *out, size_t outlen) {
    DIR *d = opendir(dir);
    struct dirent *e;
    int found = 0;
    while (d && (e = readdir(d)))
        if (!strcasecmp(e->d_name, name)) {
            snprintf(out, outlen, "%s", e->d_name);
            found = 1;
            break;
        }
    if (d) closedir(d);
    return found;
}

static int is_dir(const char *p) {
    struct stat st;
    return !stat(p, &st) && S_ISDIR(st.st_mode);
}

// Resolve `abs` under one layer. Returns -1 if a directory on the way is
// missing, otherwise 0, with *exists telling whether the last part exists.
static int resolve(const char *base, const char *abs, char *out, size_t outlen, int *exists) {
    char copy[1024], *parts[128], name[256];
    snprintf(copy, sizeof(copy), "%s", abs);
    int n = components(copy, parts);
    if (n < 0) return -1;
    size_t len = snprintf(out, outlen, "%s", base);
    *exists = is_dir(out);
    for (int i = 0; i < n; i++) {
        *exists = find_entry(out, parts[i], name, sizeof(name));
        len += snprintf(out + len, outlen - len, "/%s", *exists ? name : parts[i]);
        if (len >= outlen) return -1;
        if (i < n - 1 && !(*exists && is_dir(out))) return -1;
    }
    return 0;
}

static int under_user(const char *host) {
    size_t n = strlen(user_root);
    return n && !strncmp(host, user_root, n) && (host[n] == '/' || !host[n]);
}

int path_in_user_layer(const char *host) { return under_user(host); }

int path_to_host(const char *winpath, char *out, size_t outlen) {
    char abs[1024], game[4096];
    int in_user = 0, in_game = 0, user_ok = -1;
    if (absolutize(winpath, abs, sizeof(abs))) return -1;
    if (*user_root) {
        user_ok = resolve(user_root, abs, out, outlen, &in_user);
        if (!user_ok && in_user) return 0;
    }
    int game_ok = resolve(root, abs, game, sizeof(game), &in_game);
    // A file that exists nowhere: name it in a layer where its folder exists,
    // so a later open fails with "file not found" rather than "path not found".
    if (!game_ok && (in_game || user_ok)) {
        snprintf(out, outlen, "%s", game);
        return 0;
    }
    return user_ok;
}

int path_copy_file(const char *from, const char *to) {
    int in = open(from, O_RDONLY | O_CLOEXEC);
    if (in < 0) return -1;
    int out = open(to, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out < 0) {
        close(in);
        return -1;
    }
    char buf[65536];
    ssize_t r;
    int err = 0;
    while ((r = read(in, buf, sizeof(buf))) > 0)
        if (write(out, buf, r) != r) err = 1;
    if (r < 0) err = 1;
    close(in);
    if (close(out) || err) {
        unlink(to);
        return -1;
    }
    return 0;
}

int path_to_host_write(const char *winpath, char *out, size_t outlen, int copy_up) {
    if (!*user_root) return path_to_host(winpath, out, outlen);
    char abs[1024], copy[1024], *parts[128], name[256], game[4096];
    if (absolutize(winpath, abs, sizeof(abs))) return -1;
    snprintf(copy, sizeof(copy), "%s", abs);
    int n = components(copy, parts);
    if (n < 0) return -1;
    if (mkdir(user_root, 0755) && errno != EEXIST) return -1;
    size_t len = snprintf(out, outlen, "%s", user_root);
    size_t glen = snprintf(game, sizeof(game), "%s", root);
    int game_has = 1;  // the game folder has this path so far
    for (int i = 0; i < n; i++) {
        // Keep the spelling already on disk: the user folder's, else the game's.
        const char *spell = parts[i];
        char gname[256];
        int in_game = game_has && find_entry(game, parts[i], gname, sizeof(gname));
        if (find_entry(out, parts[i], name, sizeof(name))) spell = name;
        else if (in_game) spell = gname;
        len += snprintf(out + len, outlen - len, "/%s", spell);
        if (in_game) glen += snprintf(game + glen, sizeof(game) - glen, "/%s", gname);
        game_has = in_game;
        if (len >= outlen || glen >= sizeof(game)) return -1;
        if (i == n - 1) break;
        // Folders on the way: create them in the user folder if the game
        // folder has them; a folder neither layer has means no such path.
        if (!is_dir(out)) {
            if (!(game_has && is_dir(game))) return -1;
            if (mkdir(out, 0755)) return -1;
        }
    }
    struct stat st;
    if (copy_up && game_has && stat(out, &st) && !stat(game, &st) && S_ISREG(st.st_mode)) {
        if (path_copy_file(game, out)) return -1;
    }
    return 0;
}

int path_set_cwd(const char *winpath) {
    char abs[260], host[4096];
    if (absolutize(winpath, abs, sizeof(abs)) || path_to_host(abs, host, sizeof(host)) || !is_dir(host)) return -1;
    snprintf(cwd, sizeof(cwd), "%s", abs);
    return 0;
}

static int by_name_nocase(const void *a, const void *b) { return strcasecmp(*(char **)a, *(char **)b); }

int path_list_dir(const char *winpath, char ***names_out) {
    char abs[1024], dir[4096];
    const char *layers[2] = {user_root, root};
    char **names = NULL;
    int count = 0, cap = 0, found = 0;
    if (absolutize(winpath, abs, sizeof(abs))) return -1;
    for (int l = 0; l < 2; l++) {
        int exists;
        if (!*layers[l] || resolve(layers[l], abs, dir, sizeof(dir), &exists) || !exists || !is_dir(dir)) continue;
        found = 1;
        DIR *d = opendir(dir);
        struct dirent *e;
        while (d && (e = readdir(d))) {
            int dup = 0;
            for (int i = 0; i < count && !dup; i++) dup = !strcasecmp(names[i], e->d_name);
            if (dup) continue;  // the user folder's copy hides the game's
            if (count == cap) names = realloc(names, (cap = cap ? cap * 2 : 64) * sizeof(*names));
            names[count++] = strdup(e->d_name);
        }
        if (d) closedir(d);
    }
    if (!found) return -1;
    qsort(names, count, sizeof(*names), by_name_nocase);
    *names_out = names;
    return count;
}
