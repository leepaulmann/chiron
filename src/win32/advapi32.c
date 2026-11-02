// ADVAPI32: a small registry persisted to ~/.local/share/chiron/registry.txt
// (or $CHIRON_REGISTRY). Keys start empty: the game recreates the values it
// needs, just as on a fresh Windows install.
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "../loader/modules.h"
#include "win32.h"

struct reg_value {
    char name[256];
    DWORD type;
    DWORD size;
    uint8_t *data;
    struct reg_value *next;
};

struct reg_key {
    char path[512];  // e.g. "HKEY_LOCAL_MACHINE\Software\Firaxis Games"
    struct reg_value *values;
    struct reg_key *next;
};

#define KEY_MAGIC 0x4b474552u  // "REGK"

struct key_handle {
    uint32_t magic;
    struct reg_key *key;
};

static struct reg_key *keys;
static int loaded;

static const char *root_name(uintptr_t h) {
    switch (h) {
    case 0x80000000: return "HKEY_CLASSES_ROOT";
    case 0x80000001: return "HKEY_CURRENT_USER";
    case 0x80000002: return "HKEY_LOCAL_MACHINE";
    case 0x80000003: return "HKEY_USERS";
    }
    return NULL;
}

static void store_path(char *out, size_t len) {
    const char *env = getenv("CHIRON_REGISTRY");
    if (env && *env) snprintf(out, len, "%s", env);
    else snprintf(out, len, "%s/.local/share/chiron/registry.txt", getenv("HOME") ? getenv("HOME") : ".");
}

static struct reg_key *find_key(const char *path, int create) {
    for (struct reg_key *k = keys; k; k = k->next)
        if (!strcasecmp(k->path, path)) return k;
    if (!create) return NULL;
    struct reg_key *k = calloc(1, sizeof(*k));
    snprintf(k->path, sizeof(k->path), "%s", path);
    k->next = keys;
    keys = k;
    return k;
}

static struct reg_value *find_value(struct reg_key *k, const char *name, int create) {
    if (!name) name = "";
    for (struct reg_value *v = k->values; v; v = v->next)
        if (!strcasecmp(v->name, name)) return v;
    if (!create) return NULL;
    struct reg_value *v = calloc(1, sizeof(*v));
    snprintf(v->name, sizeof(v->name), "%s", name);
    v->next = k->values;
    k->values = v;
    return v;
}

// File format: "[key path]" lines, then "name=type:hexbytes" lines.
static void load(void) {
    if (loaded) return;
    loaded = 1;
    char path[4096], line[8192];
    store_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return;
    struct reg_key *k = NULL;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') {
            char *end = strrchr(line, ']');
            if (end) *end = 0;
            k = find_key(line + 1, 1);
        } else if (k) {
            char *eq = strrchr(line, '='), *colon = eq ? strchr(eq, ':') : NULL;
            if (!colon) continue;
            *eq = 0;
            struct reg_value *v = find_value(k, line, 1);
            v->type = strtoul(eq + 1, NULL, 10);
            const char *hex = colon + 1;
            v->size = strlen(hex) / 2;
            v->data = malloc(v->size + 1);
            for (DWORD i = 0; i < v->size; i++) sscanf(hex + 2 * i, "%2hhx", &v->data[i]);
        }
    }
    fclose(f);
}

static void save(void) {
    char path[4096], tmp[4200];
    store_path(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    char dir[4096];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0, mkdir(dir, 0755);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    for (struct reg_key *k = keys; k; k = k->next) {
        fprintf(f, "[%s]\n", k->path);
        for (struct reg_value *v = k->values; v; v = v->next) {
            fprintf(f, "%s=%u:", v->name, v->type);
            for (DWORD i = 0; i < v->size; i++) fprintf(f, "%02x", v->data[i]);
            fputc('\n', f);
        }
    }
    fclose(f);
    rename(tmp, path);
}

// Resolve (parent handle, subkey) to a full key path.
static int full_path(void *h, const char *sub, char *out, size_t len) {
    const char *root = root_name((uintptr_t)h);
    struct key_handle *kh = h;
    const char *base = root;
    if (!base) {
        if (!kh || (uintptr_t)kh < 0x10000 || kh->magic != KEY_MAGIC) return -1;
        base = kh->key->path;
    }
    if (sub && *sub) snprintf(out, len, "%s\\%s", base, sub);
    else snprintf(out, len, "%s", base);
    // Normalise separators and drop trailing backslashes.
    size_t n = strlen(out);
    while (n && out[n - 1] == '\\') out[--n] = 0;
    return 0;
}

static void *open_handle(struct reg_key *k) {
    struct key_handle *kh = malloc(sizeof(*kh));
    kh->magic = KEY_MAGIC;
    kh->key = k;
    return kh;
}

LONG WINAPI RegOpenKeyExA(void *h, const char *sub, DWORD opts, DWORD sam, void **out) {
    char path[512];
    load();
    if (full_path(h, sub, path, sizeof(path))) return ERROR_INVALID_HANDLE;
    struct reg_key *k = find_key(path, 0);
    if (modules_trace) fprintf(stderr, "[advapi32] RegOpenKeyExA %s: %s\n", path, k ? "found" : "not found");
    if (!k) return ERROR_FILE_NOT_FOUND;
    *out = open_handle(k);
    return ERROR_SUCCESS;
}

LONG WINAPI RegCreateKeyExA(void *h, const char *sub, DWORD reserved, char *cls, DWORD opts, DWORD sam, void *sa,
                            void **out, DWORD *disposition) {
    char path[512];
    load();
    if (full_path(h, sub, path, sizeof(path))) return ERROR_INVALID_HANDLE;
    int existed = find_key(path, 0) != NULL;
    struct reg_key *k = find_key(path, 1);
    if (disposition) *disposition = existed ? 2 : 1;  // REG_OPENED_EXISTING_KEY : REG_CREATED_NEW_KEY
    if (!existed) save();
    *out = open_handle(k);
    return ERROR_SUCCESS;
}

LONG WINAPI RegCloseKey(void *h) {
    struct key_handle *kh = h;
    if (root_name((uintptr_t)h)) return ERROR_SUCCESS;
    if (!kh || kh->magic != KEY_MAGIC) return ERROR_INVALID_HANDLE;
    kh->magic = 0;
    free(kh);
    return ERROR_SUCCESS;
}

static struct reg_key *key_of(void *h) {
    char path[512];
    if (full_path(h, NULL, path, sizeof(path))) return NULL;
    return find_key(path, root_name((uintptr_t)h) != NULL);
}

LONG WINAPI RegQueryValueExA(void *h, const char *name, DWORD *reserved, DWORD *type, uint8_t *data, DWORD *size) {
    load();
    struct reg_key *k = key_of(h);
    if (!k) return ERROR_INVALID_HANDLE;
    struct reg_value *v = find_value(k, name, 0);
    if (!v) return ERROR_FILE_NOT_FOUND;
    if (type) *type = v->type;
    if (!size) return data ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    DWORD have = *size;
    *size = v->size;
    if (!data) return ERROR_SUCCESS;
    if (have < v->size) return 234;  // ERROR_MORE_DATA
    memcpy(data, v->data, v->size);
    return ERROR_SUCCESS;
}

LONG WINAPI RegSetValueExA(void *h, const char *name, DWORD reserved, DWORD type, const uint8_t *data, DWORD size) {
    load();
    struct reg_key *k = key_of(h);
    if (!k) return ERROR_INVALID_HANDLE;
    struct reg_value *v = find_value(k, name, 1);
    free(v->data);
    v->type = type;
    v->size = size;
    v->data = malloc(size + 1);
    if (size) memcpy(v->data, data, size);
    save();
    return ERROR_SUCCESS;
}
