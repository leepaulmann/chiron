// Windows paths onto the host file system. The game folder is drive C:, and
// every component is matched case-insensitively against what is on disk.
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "win32.h"

static char root[4096];
static char cwd[260] = "C:\\";

void path_init(const char *game_root) {
    snprintf(root, sizeof(root), "%s", game_root);
    size_t n = strlen(root);
    while (n > 1 && root[n - 1] == '/') root[--n] = 0;
}

const char *path_game_root(void) { return root; }
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

int path_to_host(const char *winpath, char *out, size_t outlen) {
    char abs[1024];
    if (absolutize(winpath, abs, sizeof(abs))) return -1;
    size_t len = snprintf(out, outlen, "%s", root);
    char *s = abs + 3, *comp;
    while ((comp = strsep(&s, "\\")) && *comp) {
        // Windows ignores trailing dots and spaces in names.
        size_t cl = strlen(comp);
        while (cl && (comp[cl - 1] == '.' || comp[cl - 1] == ' ')) comp[--cl] = 0;
        const char *match = comp;
        DIR *d = opendir(out);
        struct dirent *e;
        if (d) {
            while ((e = readdir(d)))
                if (!strcasecmp(e->d_name, comp)) {
                    match = e->d_name;
                    break;
                }
        }
        len += snprintf(out + len, outlen - len, "/%s", match);
        if (d) closedir(d);
        if (len >= outlen) return -1;
        // A missing directory in the middle means the path cannot exist.
        if (s && *s) {
            struct stat st;
            if (stat(out, &st) || !S_ISDIR(st.st_mode)) return -1;
        }
    }
    return 0;
}

int path_set_cwd(const char *winpath) {
    char abs[260], host[4096];
    struct stat st;
    if (absolutize(winpath, abs, sizeof(abs)) || path_to_host(abs, host, sizeof(host)) ||
        stat(host, &st) || !S_ISDIR(st.st_mode))
        return -1;
    snprintf(cwd, sizeof(cwd), "%s", abs);
    return 0;
}
