// KERNEL32: files, directories, file mappings and .ini profiles.
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../loader/modules.h"
#include "kernel32.h"

static DWORD errno_to_win(int e) {
    switch (e) {
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EACCES:
    case EPERM:
    case EROFS: return ERROR_ACCESS_DENIED;
    case EEXIST: return ERROR_FILE_EXISTS;
    case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
    default: return ERROR_ACCESS_DENIED;
    }
}

static int host_path(const char *winpath, char *out, size_t outlen) {
    if (!winpath || path_to_host(winpath, out, outlen)) {
        win32_set_last_error(ERROR_PATH_NOT_FOUND);
        return -1;
    }
    return 0;
}

// ---- Files ----------------------------------------------------------------

struct file {
    struct kobj obj;
    int fd;
};

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
enum { CREATE_NEW = 1, CREATE_ALWAYS, OPEN_EXISTING, OPEN_ALWAYS, TRUNCATE_EXISTING };

HANDLE WINAPI CreateFileA(const char *name, DWORD access, DWORD share, void *sa, DWORD disp,
                          DWORD attrs, HANDLE tmpl) {
    char path[4096];
    if (host_path(name, path, sizeof(path))) return INVALID_HANDLE_VALUE;

    int flags = (access & GENERIC_WRITE) ? ((access & GENERIC_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
    struct stat st;
    int existed = stat(path, &st) == 0;
    switch (disp) {
    case CREATE_NEW: flags |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS: flags |= O_CREAT | O_TRUNC; break;
    case OPEN_ALWAYS: flags |= O_CREAT; break;
    case TRUNCATE_EXISTING: flags |= O_TRUNC; break;
    }
    if (existed && S_ISDIR(st.st_mode)) {
        win32_set_last_error(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    int fd = open(path, flags | O_CLOEXEC, 0644);
    if (modules_trace)
        fprintf(stderr, "[kernel32] CreateFileA %s access=%c%c disp=%u -> %s\n", name, (access & GENERIC_READ) ? 'r' : '-',
                (access & GENERIC_WRITE) ? 'w' : '-', disp, fd < 0 ? strerror(errno) : "ok");
    if (fd < 0) {
        win32_set_last_error(errno_to_win(errno));
        return INVALID_HANDLE_VALUE;
    }
    struct file *f = malloc(sizeof(*f));
    f->obj.type = KOBJ_FILE;
    f->fd = fd;
    // Opening an existing file with these dispositions reports ALREADY_EXISTS.
    win32_set_last_error(existed && (disp == CREATE_ALWAYS || disp == OPEN_ALWAYS) ? ERROR_ALREADY_EXISTS
                                                                                   : ERROR_SUCCESS);
    return handle_new(f);
}

static int handle_fd(HANDLE h) {
    if (IS_STD_HANDLE(h)) return STD_FD(h);
    struct file *f = handle_get(h, KOBJ_FILE);
    return f ? f->fd : -1;
}

BOOL WINAPI ReadFile(HANDLE h, void *buf, DWORD n, DWORD *done, void *ovl) {
    int fd = handle_fd(h);
    if (done) *done = 0;
    if (fd < 0) return FALSE;
    DWORD total = 0;
    while (total < n) {
        ssize_t r = read(fd, (char *)buf + total, n - total);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) {
            win32_set_last_error(errno_to_win(errno));
            return FALSE;
        }
        if (r == 0) break;
        total += r;
    }
    if (done) *done = total;
    return TRUE;
}

BOOL WINAPI WriteFile(HANDLE h, const void *buf, DWORD n, DWORD *done, void *ovl) {
    int fd = handle_fd(h);
    if (done) *done = 0;
    if (fd < 0) return FALSE;
    DWORD total = 0;
    while (total < n) {
        ssize_t r = write(fd, (const char *)buf + total, n - total);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) {
            win32_set_last_error(errno_to_win(errno));
            return FALSE;
        }
        total += r;
    }
    if (done) *done = total;
    return TRUE;
}

DWORD WINAPI SetFilePointer(HANDLE h, LONG lo, LONG *hi, DWORD method) {
    int fd = handle_fd(h);
    if (fd < 0) return 0xffffffffu;
    off_t off = hi ? ((off_t)*hi << 32) | (uint32_t)lo : lo;
    off_t r = lseek(fd, off, method == 0 ? SEEK_SET : method == 1 ? SEEK_CUR : SEEK_END);
    if (r < 0) {
        win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0xffffffffu;
    }
    if (hi) *hi = (LONG)(r >> 32);
    win32_set_last_error(ERROR_SUCCESS);
    return (DWORD)r;
}

DWORD WINAPI GetFileSize(HANDLE h, DWORD *hi) {
    int fd = handle_fd(h);
    struct stat st;
    if (fd < 0 || fstat(fd, &st)) return 0xffffffffu;
    if (hi) *hi = (DWORD)((uint64_t)st.st_size >> 32);
    return (DWORD)st.st_size;
}

BOOL WINAPI SetEndOfFile(HANDLE h) {
    int fd = handle_fd(h);
    off_t pos = fd < 0 ? -1 : lseek(fd, 0, SEEK_CUR);
    return pos >= 0 && ftruncate(fd, pos) == 0;
}

BOOL WINAPI FlushFileBuffers(HANDLE h) { return handle_fd(h) >= 0; }

#define FILE_TYPE_DISK 1
#define FILE_TYPE_CHAR 2

DWORD WINAPI GetFileType(HANDLE h) {
    if (IS_STD_HANDLE(h)) return FILE_TYPE_CHAR;
    return handle_get(h, KOBJ_FILE) ? FILE_TYPE_DISK : 0;
}

HANDLE WINAPI GetStdHandle(DWORD which) {
    // STD_INPUT_HANDLE = -10, OUTPUT = -11, ERROR = -12
    int fd = -10 - (int)which;
    return fd >= 0 && fd < 3 ? (HANDLE)(uintptr_t)(STD_HANDLE_BASE + fd) : INVALID_HANDLE_VALUE;
}

BOOL WINAPI SetStdHandle(DWORD which, HANDLE h) { return TRUE; }
UINT WINAPI SetHandleCount(UINT n) { return n; }

BOOL WINAPI DeleteFileA(const char *name) {
    char path[4096];
    if (host_path(name, path, sizeof(path))) return FALSE;
    if (unlink(path)) {
        win32_set_last_error(errno_to_win(errno));
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI MoveFileA(const char *from, const char *to) {
    char a[4096], b[4096];
    struct stat st;
    if (host_path(from, a, sizeof(a)) || host_path(to, b, sizeof(b))) return FALSE;
    if (stat(b, &st) == 0) {  // MoveFile never overwrites
        win32_set_last_error(ERROR_ALREADY_EXISTS);
        return FALSE;
    }
    if (rename(a, b)) {
        win32_set_last_error(errno_to_win(errno));
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI CreateDirectoryA(const char *name, void *sa) {
    char path[4096];
    if (host_path(name, path, sizeof(path))) return FALSE;
    if (mkdir(path, 0755)) {
        win32_set_last_error(errno == EEXIST ? ERROR_ALREADY_EXISTS : errno_to_win(errno));
        return FALSE;
    }
    return TRUE;
}

DWORD WINAPI GetCurrentDirectoryA(DWORD size, char *buf) {
    const char *cwd = path_get_cwd();
    DWORD n = strlen(cwd);
    // Windows reports "C:\" for the root but no trailing slash elsewhere.
    if (size <= n) return n + 1;
    memcpy(buf, cwd, n + 1);
    return n;
}

BOOL WINAPI SetCurrentDirectoryA(const char *dir) {
    if (path_set_cwd(dir)) {
        win32_set_last_error(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    return TRUE;
}

UINT WINAPI GetDriveTypeA(const char *root) {
    if (!root || toupper((unsigned char)root[0]) == 'C') return 3;  // DRIVE_FIXED
    return 1;                                                       // DRIVE_NO_ROOT_DIR
}

// ---- Directory search -----------------------------------------------------

struct find {
    struct kobj obj;
    DIR *dir;
    char host_dir[4096];
    char pattern[260];
};

struct win32_find_data {
    DWORD attrs;
    DWORD ctime[2], atime[2], mtime[2];
    DWORD size_hi, size_lo;
    DWORD reserved[2];
    char name[260];
    char alt_name[14];
};

static void unix_to_filetime(time_t t, DWORD *ft) {
    uint64_t v = ((uint64_t)t + 11644473600ull) * 10000000ull;
    ft[0] = (DWORD)v;
    ft[1] = (DWORD)(v >> 32);
}

// Windows wildcard matching: "*.*" matches everything, case is ignored.
static int win_match(const char *pattern, const char *name) {
    if (!strcmp(pattern, "*.*") || !strcmp(pattern, "*")) return 1;
    return fnmatch(pattern, name, FNM_CASEFOLD) == 0;
}

static int find_next(struct find *f, struct win32_find_data *out) {
    struct dirent *e;
    while ((e = readdir(f->dir))) {
        if (!win_match(f->pattern, e->d_name)) continue;
        char full[8192];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", f->host_dir, e->d_name);
        if (stat(full, &st)) continue;
        memset(out, 0, sizeof(*out));
        out->attrs = S_ISDIR(st.st_mode) ? 0x10 : 0x80;  // DIRECTORY : NORMAL
        unix_to_filetime(st.st_ctime, out->ctime);
        unix_to_filetime(st.st_atime, out->atime);
        unix_to_filetime(st.st_mtime, out->mtime);
        out->size_hi = (DWORD)((uint64_t)st.st_size >> 32);
        out->size_lo = (DWORD)st.st_size;
        snprintf(out->name, sizeof(out->name), "%s", e->d_name);
        return 1;
    }
    return 0;
}

HANDLE WINAPI FindFirstFileA(const char *spec, struct win32_find_data *data) {
    char dir[260];
    const char *sep = NULL;
    for (const char *p = spec; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':') sep = p;
    if (sep) snprintf(dir, sizeof(dir), "%.*s", (int)(sep - spec + 1), spec);
    else snprintf(dir, sizeof(dir), ".");

    struct find *f = calloc(1, sizeof(*f));
    f->obj.type = KOBJ_FIND;
    snprintf(f->pattern, sizeof(f->pattern), "%s", sep ? sep + 1 : spec);
    if (host_path(dir, f->host_dir, sizeof(f->host_dir)) || !(f->dir = opendir(f->host_dir))) {
        free(f);
        win32_set_last_error(ERROR_PATH_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    if (!find_next(f, data)) {
        closedir(f->dir);
        free(f);
        win32_set_last_error(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    return handle_new(f);
}

BOOL WINAPI FindNextFileA(HANDLE h, struct win32_find_data *data) {
    struct find *f = handle_get(h, KOBJ_FIND);
    if (!f) return FALSE;
    if (!find_next(f, data)) {
        win32_set_last_error(ERROR_NO_MORE_FILES);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI FindClose(HANDLE h) {
    struct find *f = handle_get(h, KOBJ_FIND);
    if (!f) return FALSE;
    handle_take(h);
    closedir(f->dir);
    free(f);
    return TRUE;
}

// ---- File mappings --------------------------------------------------------

struct mapping {
    struct kobj obj;
    int fd;  // -1 for page-file backed (anonymous) mappings
    uint32_t size;
    int writable;
};

HANDLE WINAPI CreateFileMappingA(HANDLE file, void *sa, DWORD prot, DWORD size_hi, DWORD size_lo,
                                 const char *name) {
    struct mapping *m = calloc(1, sizeof(*m));
    m->obj.type = KOBJ_MAPPING;
    m->fd = -1;
    m->writable = (prot & 0x04) != 0;  // PAGE_READWRITE
    m->size = size_lo;
    if (file != INVALID_HANDLE_VALUE) {
        int fd = handle_fd(file);
        if (fd < 0) {
            free(m);
            return NULL;
        }
        m->fd = dup(fd);
        struct stat st;
        if (!m->size && fstat(fd, &st) == 0) m->size = st.st_size;
        else if (m->writable && fstat(fd, &st) == 0 && st.st_size < m->size && ftruncate(fd, m->size)) {}
    }
    if (name) fprintf(stderr, "chiron: named mapping '%s' treated as private\n", name);
    return handle_new(m);
}

void *WINAPI MapViewOfFile(HANDLE h, DWORD access, DWORD off_hi, DWORD off_lo, DWORD bytes) {
    struct mapping *m = handle_get(h, KOBJ_MAPPING);
    if (!m) return NULL;
    if (!bytes) bytes = m->size - off_lo;
    int prot = PROT_READ | ((access & 0x02) ? PROT_WRITE : 0);  // FILE_MAP_WRITE
    int copy = access == 0x01;                                    // FILE_MAP_COPY
    void *p = m->fd < 0 ? mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)
                        : mmap(NULL, bytes, copy ? PROT_READ | PROT_WRITE : prot,
                               copy ? MAP_PRIVATE : MAP_SHARED, m->fd, off_lo);
    if (p == MAP_FAILED) {
        win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    return p;
}

BOOL WINAPI UnmapViewOfFile(void *p) {
    // The view size is not recorded; mappings are only unmapped at exit, so
    // releasing the first page is enough to keep the address space tidy.
    return munmap(p, 0x1000) == 0;
}

// ---- Handles --------------------------------------------------------------

BOOL WINAPI CloseHandle(HANDLE h) {
    if (IS_STD_HANDLE(h) || h == (HANDLE)-1 || h == (HANDLE)-2) return TRUE;
    struct kobj *o = handle_take(h);
    if (!o) return FALSE;
    switch (o->type) {
    case KOBJ_FILE: close(((struct file *)o)->fd); free(o); break;
    case KOBJ_MAPPING:
        if (((struct mapping *)o)->fd >= 0) close(((struct mapping *)o)->fd);
        free(o);
        break;
    case KOBJ_FIND: closedir(((struct find *)o)->dir); free(o); break;
    case KOBJ_EVENT:
    case KOBJ_THREAD: kernel32_close_waitable(o); break;
    case KOBJ_HEAP: free(o); break;
    }
    return TRUE;
}

// ---- .ini profiles --------------------------------------------------------
// Profiles are small, so each call reads the whole file. A bare file name
// lives in the Windows directory on real Windows; here it is the game folder.

static int profile_path(const char *file, char *out, size_t outlen) {
    if (!strpbrk(file, "\\/:")) {
        char win[300];
        snprintf(win, sizeof(win), "C:\\%s", file);
        return host_path(win, out, outlen);
    }
    return host_path(file, out, outlen);
}

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(n + 1);
    n = fread(buf, 1, n, f);
    buf[n] = 0;
    fclose(f);
    if (len) *len = n;
    return buf;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

// Copy a double-NUL-terminated list into buf, truncating like Windows does.
static DWORD put_list(char *buf, DWORD size, const char *list, size_t listlen) {
    if (size < 2) return 0;
    if (listlen + 1 <= size) {
        memcpy(buf, list, listlen);
        buf[listlen] = 0;
        return listlen ? listlen : 0;
    }
    memcpy(buf, list, size - 2);
    buf[size - 2] = buf[size - 1] = 0;
    return size - 2;
}

DWORD WINAPI GetPrivateProfileStringA(const char *app, const char *key, const char *def, char *buf,
                                      DWORD size, const char *file) {
    char path[4096];
    if (!size) return 0;
    char *data = profile_path(file, path, sizeof(path)) ? NULL : slurp(path, NULL);
    char list[8192];
    size_t listlen = 0;
    int in_section = 0;
    const char *found = NULL;

    for (char *s = data, *line; s && (line = strsep(&s, "\n"));) {
        line = trim(line);
        if (*line == '[') {
            char *end = strchr(line, ']');
            if (end) *end = 0;
            in_section = app && !strcasecmp(line + 1, app);
            if (!app && listlen + strlen(line + 1) + 2 < sizeof(list)) {
                strcpy(list + listlen, line + 1);
                listlen += strlen(line + 1) + 1;
            }
            continue;
        }
        if (!in_section || !*line || *line == ';') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(line), *v = trim(eq + 1);
        if (!key) {
            if (listlen + strlen(k) + 2 < sizeof(list)) {
                strcpy(list + listlen, k);
                listlen += strlen(k) + 1;
            }
        } else if (!strcasecmp(k, key)) {
            found = v;
            break;
        }
    }

    DWORD n;
    if (!app || !key) {
        n = put_list(buf, size, list, listlen);
    } else {
        const char *v = found ? found : (def ? def : "");
        snprintf(buf, size, "%s", v);
        n = strlen(buf);
    }
    free(data);
    return n;
}

UINT WINAPI GetPrivateProfileIntA(const char *app, const char *key, int def, const char *file) {
    char buf[64];
    if (!GetPrivateProfileStringA(app, key, "", buf, sizeof(buf), file)) return def;
    return (UINT)strtol(buf, NULL, 10);
}

BOOL WINAPI WritePrivateProfileStringA(const char *app, const char *key, const char *value,
                                       const char *file) {
    char path[4096];
    if (!app || profile_path(file, path, sizeof(path))) return FALSE;
    size_t len = 0;
    char *data = slurp(path, &len);
    FILE *out = NULL;
    char tmp[4200];
    snprintf(tmp, sizeof(tmp), "%s.chiron-tmp", path);
    if (!(out = fopen(tmp, "wb"))) {
        free(data);
        return FALSE;
    }

    // Rewrite the file line by line, replacing or dropping the target entry
    // and appending it at the end of its section when it was missing.
    int in_section = 0, seen_section = 0, written = 0;
    const char *eol = data && strstr(data, "\r\n") ? "\r\n" : "\n";
    for (char *s = data, *raw; s && (raw = strsep(&s, "\n"));) {
        if (!s && !*raw) break;  // trailing newline
        size_t rl = strlen(raw);
        if (rl && raw[rl - 1] == '\r') raw[--rl] = 0;
        char copy[2048];
        snprintf(copy, sizeof(copy), "%s", raw);
        char *line = trim(copy);
        if (*line == '[') {
            if (in_section && !written && key && value) {
                fprintf(out, "%s=%s%s", key, value, eol);
                written = 1;
            }
            char *end = strchr(line, ']');
            if (end) *end = 0;
            in_section = !strcasecmp(line + 1, app);
            if (in_section) seen_section = 1;
            if (in_section && !key) continue;  // deleting the whole section
        } else if (in_section) {
            if (!key) continue;
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = 0;
                if (!strcasecmp(trim(line), key)) {
                    if (value && !written) fprintf(out, "%s=%s%s", key, value, eol);
                    written = 1;
                    continue;
                }
            }
        }
        fprintf(out, "%s%s", raw, eol);
    }
    if (key && value && !written) {
        if (!seen_section) fprintf(out, "[%s]%s", app, eol);
        fprintf(out, "%s=%s%s", key, value, eol);
    }
    fclose(out);
    free(data);
    return rename(tmp, path) == 0;
}

// ---- Process launching ----------------------------------------------------

UINT WINAPI WinExec(const char *cmd, UINT show) {
    fprintf(stderr, "chiron: WinExec(\"%s\") ignored\n", cmd);
    return 2;  // ERROR_FILE_NOT_FOUND
}

// ---- File information and times -------------------------------------------

typedef struct {
    DWORD attrs;
    DWORD ctime[2], atime[2], mtime[2];
    DWORD volume_serial, size_hi, size_lo, links, index_hi, index_lo;
} BY_HANDLE_FILE_INFORMATION;

BOOL WINAPI GetFileInformationByHandle(HANDLE h, BY_HANDLE_FILE_INFORMATION *info) {
    int fd = handle_fd(h);
    struct stat st;
    if (fd < 0 || fstat(fd, &st)) return FALSE;
    memset(info, 0, sizeof(*info));
    info->attrs = S_ISDIR(st.st_mode) ? 0x10 : 0x80;
    unix_to_filetime(st.st_ctime, info->ctime);
    unix_to_filetime(st.st_atime, info->atime);
    unix_to_filetime(st.st_mtime, info->mtime);
    info->volume_serial = (DWORD)st.st_dev;
    info->size_hi = (DWORD)((uint64_t)st.st_size >> 32);
    info->size_lo = (DWORD)st.st_size;
    info->links = st.st_nlink;
    info->index_hi = (DWORD)((uint64_t)st.st_ino >> 32);
    info->index_lo = (DWORD)st.st_ino;
    return TRUE;
}

BOOL WINAPI PeekNamedPipe(HANDLE h, void *buf, DWORD size, DWORD *read, DWORD *avail, DWORD *left) {
    win32_set_last_error(ERROR_INVALID_HANDLE);
    return FALSE;
}

static int64_t filetime_to_unix(const DWORD *ft) {
    uint64_t v = (uint64_t)ft[1] << 32 | ft[0];
    return (int64_t)(v / 10000000ull) - 11644473600ll;
}

BOOL WINAPI FileTimeToLocalFileTime(const DWORD *in, DWORD *out) {
    time_t t = filetime_to_unix(in);
    struct tm tm;
    localtime_r(&t, &tm);
    uint64_t v = (uint64_t)in[1] << 32 | in[0];
    v += (int64_t)tm.tm_gmtoff * 10000000ll;
    out[0] = (DWORD)v;
    out[1] = (DWORD)(v >> 32);
    return TRUE;
}

BOOL WINAPI FileTimeToSystemTime(const DWORD *ft, WORD *st) {
    uint64_t v = (uint64_t)ft[1] << 32 | ft[0];
    time_t t = filetime_to_unix(ft);
    struct tm tm;
    gmtime_r(&t, &tm);
    st[0] = tm.tm_year + 1900;
    st[1] = tm.tm_mon + 1;
    st[2] = tm.tm_wday;
    st[3] = tm.tm_mday;
    st[4] = tm.tm_hour;
    st[5] = tm.tm_min;
    st[6] = tm.tm_sec;
    st[7] = (WORD)((v / 10000) % 1000);
    return TRUE;
}

DWORD WINAPI GetTimeZoneInformation(void *tzi) {
    // Report UTC with no daylight saving; only the C runtime's localtime
    // uses this, and Chiron's own timestamps come from the host.
    memset(tzi, 0, 172);
    return 0;  // TIME_ZONE_ID_UNKNOWN
}
