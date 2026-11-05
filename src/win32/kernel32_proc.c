// KERNEL32: process, modules, memory, TLS, synchronisation, threads, strings.
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "../loader/modules.h"
#include "../loader/thunks.h"
#include "../loader/teb.h"
#include "handles.h"
#include "kernel32.h"

// ---- Process --------------------------------------------------------------

static char command_line[1024] = "terranx.exe";

void kernel32_set_command_line(const char *cmdline) {
    snprintf(command_line, sizeof(command_line), "%s", cmdline);
}

DWORD WINAPI GetVersion(void) { return (2600u << 16) | 0x0105;  /* Windows XP */ }
char *WINAPI GetCommandLineA(void) { return command_line; }

void WINAPI GetStartupInfoA(void *info) {
    memset(info, 0, 68);
    *(DWORD *)info = 68;
}

void WINAPI ExitProcess(UINT code) {
    if (code) {
        char where[128];
        modules_describe_addr((uint32_t)__builtin_return_address(0), where, sizeof(where));
        fprintf(stderr, "chiron: exit code %u from %s\n", code, where);
        thunk_trace_recent();
    }
    fflush(NULL);
    exit(code);
}

BOOL WINAPI TerminateProcess(HANDLE process, UINT code) {
    if (process != (HANDLE)-1) return FALSE;
    ExitProcess(code);
    return TRUE;
}

HANDLE WINAPI GetCurrentProcess(void) { return (HANDLE)-1; }
HANDLE WINAPI GetCurrentThread(void) { return (HANDLE)-2; }
DWORD WINAPI GetCurrentThreadId(void) { return teb_current()->tid; }
DWORD WINAPI GetCurrentProcessId(void) { return teb_current()->pid; }
BOOL WINAPI SetThreadPriority(HANDLE thread, int prio) { return TRUE; }

DWORD WINAPI GetLastError(void) { return win32_get_last_error(); }
void WINAPI SetLastError(DWORD err) { win32_set_last_error(err); }

typedef struct {
    DWORD size, major, minor, build, platform;
    char csd[128];
} OSVERSIONINFOA;

BOOL WINAPI GetVersionExA(OSVERSIONINFOA *v) {
    if (v->size < 20) return FALSE;
    v->major = 5, v->minor = 1, v->build = 2600, v->platform = 2;  // Windows XP, VER_PLATFORM_WIN32_NT
    if (v->size >= sizeof(*v)) v->csd[0] = 0;
    return TRUE;
}

void WINAPI FatalAppExitA(UINT action, const char *msg) {
    fprintf(stderr, "chiron: FatalAppExit: %s\n", msg);
    ExitProcess(255);
}

BOOL WINAPI SetEnvironmentVariableA(const char *name, const char *value) { return TRUE; }
BOOL WINAPI SetConsoleCtrlHandler(void *handler, BOOL add) { return TRUE; }

void WINAPI OutputDebugStringA(const char *s) { fprintf(stderr, "[debug] %s", s); }

BOOL WINAPI IsBadReadPtr(const void *p, UINT n) { return p == NULL && n; }
BOOL WINAPI IsBadWritePtr(void *p, UINT n) { return p == NULL && n; }
BOOL WINAPI IsBadCodePtr(void *p) { return p == NULL; }


// Environment: the game gets an empty block.
static char empty_env_a[2];
static WCHAR empty_env_w[2];
char *WINAPI GetEnvironmentStrings(void) { return empty_env_a; }
WCHAR *WINAPI GetEnvironmentStringsW(void) { return empty_env_w; }
BOOL WINAPI FreeEnvironmentStringsA(char *p) { return TRUE; }
BOOL WINAPI FreeEnvironmentStringsW(WCHAR *p) { return TRUE; }

// ---- Modules --------------------------------------------------------------

HMODULE WINAPI GetModuleHandleA(const char *name) { return modules_get_handle(name); }
HMODULE WINAPI LoadLibraryA(const char *name) { return modules_load_library(name); }
BOOL WINAPI FreeLibrary(HMODULE h) { return TRUE; }

void *WINAPI GetProcAddress(HMODULE h, const char *name) {
    if ((uintptr_t)name <= 0xffff) return modules_get_proc(h, NULL, (int)(uintptr_t)name);
    return modules_get_proc(h, name, 0);
}

DWORD WINAPI GetModuleFileNameA(HMODULE h, char *buf, DWORD size) {
    char tmp[300];
    int n = modules_get_filename(h, tmp, sizeof(tmp));
    if (n < 0 || !size) return 0;
    snprintf(buf, size, "%s", tmp);
    return (DWORD)n < size ? (DWORD)n : size;
}

// ---- Heaps ----------------------------------------------------------------
// Heap blocks carry a size header so HeapSize can answer exactly.

#define HEAP_ZERO_MEMORY 0x08
#define HEAP_REALLOC_IN_PLACE_ONLY 0x10
#define HDR 16

HANDLE WINAPI HeapCreate(DWORD opts, DWORD initial, DWORD max) {
    struct kobj *h = malloc(sizeof(*h));
    h->type = KOBJ_HEAP;
    return handle_new(h);
}

BOOL WINAPI HeapDestroy(HANDLE h) {
    // Blocks are not tracked per heap, so they are leaked; the CRT only
    // destroys its heap at exit.
    free(handle_take(h));
    return TRUE;
}

void *WINAPI HeapAlloc(HANDLE h, DWORD flags, DWORD size) {
    uint8_t *p = calloc(1, size + HDR);
    if (!p) {
        fprintf(stderr, "chiron: HeapAlloc(%u bytes) failed\n", size);
        win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    *(DWORD *)p = size;
    return p + HDR;
}

BOOL WINAPI HeapFree(HANDLE h, DWORD flags, void *mem) {
    if (mem) free((uint8_t *)mem - HDR);
    return TRUE;
}

DWORD WINAPI HeapSize(HANDLE h, DWORD flags, const void *mem) {
    return *(const DWORD *)((const uint8_t *)mem - HDR);
}

void *WINAPI HeapReAlloc(HANDLE h, DWORD flags, void *mem, DWORD size) {
    DWORD old = HeapSize(h, flags, mem);
    if (flags & HEAP_REALLOC_IN_PLACE_ONLY) {
        if (size > old) {
            win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }
        *(DWORD *)((uint8_t *)mem - HDR) = size;
        return mem;
    }
    uint8_t *p = realloc((uint8_t *)mem - HDR, size + HDR);
    if (!p) {
        win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if ((flags & HEAP_ZERO_MEMORY) && size > old) memset(p + HDR + old, 0, size - old);
    *(DWORD *)p = size;
    return p + HDR;
}

// ---- Virtual memory -------------------------------------------------------
// The MSVC small-block heap reserves regions and then commits pages inside
// them, so reservations are tracked to support MEM_RELEASE.

#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_DECOMMIT 0x4000
#define MEM_RELEASE 0x8000
#define PAGE_ROUND_DOWN(x) ((x) & ~0xfffu)
#define PAGE_ROUND_UP(x) (((x) + 0xfffu) & ~0xfffu)

struct region {
    uint32_t base, size;
    struct region *next;
};
static struct region *regions;
static pthread_mutex_t region_lock = PTHREAD_MUTEX_INITIALIZER;

static int win_prot(DWORD p) {
    if (p & 0xf0) return PROT_READ | PROT_WRITE | PROT_EXEC;  // PAGE_EXECUTE_*
    if (p & 0x04) return PROT_READ | PROT_WRITE;              // PAGE_READWRITE
    if (p & 0x02) return PROT_READ;                           // PAGE_READONLY
    return PROT_NONE;
}

void *WINAPI VirtualAlloc(void *addr, DWORD size, DWORD type, DWORD prot) {
    uint32_t a = (uint32_t)addr;
    if (type & MEM_RESERVE) {
        uint32_t base = PAGE_ROUND_DOWN(a), len = PAGE_ROUND_UP(a + size) - base;
        int p = (type & MEM_COMMIT) ? win_prot(prot) : PROT_NONE;
        void *m = mmap((void *)base, len, p,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | (base ? MAP_FIXED_NOREPLACE : 0),
                       -1, 0);
        if (m == MAP_FAILED) {
            fprintf(stderr, "chiron: VirtualAlloc(%p, %u, 0x%x) failed: %s\n", addr, size, type, strerror(errno));
            win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }
        struct region *r = malloc(sizeof(*r));
        *r = (struct region){(uint32_t)m, len, NULL};
        pthread_mutex_lock(&region_lock);
        r->next = regions;
        regions = r;
        pthread_mutex_unlock(&region_lock);
        return m;
    }
    if (type & MEM_COMMIT) {
        uint32_t base = PAGE_ROUND_DOWN(a), len = PAGE_ROUND_UP(a + size) - base;
        if (!a || mprotect((void *)base, len, win_prot(prot))) {
            fprintf(stderr, "chiron: VirtualAlloc commit (%p, %u) failed\n", addr, size);
            win32_set_last_error(ERROR_INVALID_PARAMETER);
            return NULL;
        }
        return addr;
    }
    win32_set_last_error(ERROR_INVALID_PARAMETER);
    return NULL;
}

BOOL WINAPI VirtualFree(void *addr, DWORD size, DWORD type) {
    uint32_t a = (uint32_t)addr;
    if (type & MEM_DECOMMIT) {
        uint32_t base = PAGE_ROUND_DOWN(a), len = PAGE_ROUND_UP(a + size) - base;
        madvise((void *)base, len, MADV_DONTNEED);
        return mprotect((void *)base, len, PROT_NONE) == 0;
    }
    if (type & MEM_RELEASE) {
        pthread_mutex_lock(&region_lock);
        for (struct region **pp = &regions; *pp; pp = &(*pp)->next)
            if ((*pp)->base == a) {
                struct region *r = *pp;
                *pp = r->next;
                pthread_mutex_unlock(&region_lock);
                munmap((void *)r->base, r->size);
                free(r);
                return TRUE;
            }
        pthread_mutex_unlock(&region_lock);
    }
    win32_set_last_error(ERROR_INVALID_PARAMETER);
    return FALSE;
}

// ---- Thread-local storage -------------------------------------------------

static uint64_t tls_used;  // bitmap of TEB_TLS_SLOTS slots

DWORD WINAPI TlsAlloc(void) {
    for (int i = 0; i < TEB_TLS_SLOTS; i++) {
        uint64_t bit = 1ull << i, old = __atomic_fetch_or(&tls_used, bit, __ATOMIC_ACQ_REL);
        if (!(old & bit)) return i;
    }
    return 0xffffffffu;  // TLS_OUT_OF_INDEXES
}

BOOL WINAPI TlsFree(DWORD i) {
    __atomic_fetch_and(&tls_used, ~(1ull << i), __ATOMIC_ACQ_REL);
    return TRUE;
}

void *WINAPI TlsGetValue(DWORD i) {
    if (i >= TEB_TLS_SLOTS) {
        win32_set_last_error(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    win32_set_last_error(ERROR_SUCCESS);  // callers check this when NULL is stored
    return (void *)teb_current()->tls_slots[i];
}

BOOL WINAPI TlsSetValue(DWORD i, void *v) {
    if (i >= TEB_TLS_SLOTS) {
        win32_set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    teb_current()->tls_slots[i] = (uint32_t)v;
    return TRUE;
}

// ---- Critical sections and interlocked operations -------------------------
// A CRITICAL_SECTION is 24 bytes of game memory; we keep a pointer to a
// recursive pthread mutex in its first word (DebugInfo, unused by callers).

void WINAPI InitializeCriticalSection(void *cs) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_t *m = malloc(sizeof(*m));
    pthread_mutex_init(m, &attr);
    pthread_mutexattr_destroy(&attr);
    memset(cs, 0, 24);
    *(pthread_mutex_t **)cs = m;
}

void WINAPI DeleteCriticalSection(void *cs) {
    pthread_mutex_t *m = *(pthread_mutex_t **)cs;
    if (m) {
        pthread_mutex_destroy(m);
        free(m);
    }
    *(pthread_mutex_t **)cs = NULL;
}

void WINAPI EnterCriticalSection(void *cs) { pthread_mutex_lock(*(pthread_mutex_t **)cs); }
void WINAPI LeaveCriticalSection(void *cs) { pthread_mutex_unlock(*(pthread_mutex_t **)cs); }

LONG WINAPI InterlockedIncrement(volatile LONG *p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
LONG WINAPI InterlockedDecrement(volatile LONG *p) { return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST); }
LONG WINAPI InterlockedExchange(volatile LONG *p, LONG v) { return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST); }

// ---- Events, threads and waiting ------------------------------------------
// All waitable state sits behind one lock and one condition variable. The
// game has a handful of threads, so simplicity beats throughput here.

#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 0x102
#define WAIT_FAILED 0xffffffffu
#define INFINITE 0xffffffffu

static pthread_mutex_t wait_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wait_cond = PTHREAD_COND_INITIALIZER;

static void signal_waitable(struct waitable *w, int on) {
    pthread_mutex_lock(&wait_lock);
    w->signaled = on;
    pthread_cond_broadcast(&wait_cond);
    pthread_mutex_unlock(&wait_lock);
}

HANDLE WINAPI CreateEventA(void *sa, BOOL manual, BOOL initial, const char *name) {
    if (name) fprintf(stderr, "chiron: named event '%s' treated as private\n", name);
    struct waitable *e = calloc(1, sizeof(*e));
    e->obj.type = KOBJ_EVENT;
    e->manual_reset = manual;
    e->signaled = initial;
    return handle_new(e);
}

BOOL WINAPI SetEvent(HANDLE h) {
    struct waitable *e = handle_get(h, KOBJ_EVENT);
    if (!e) return FALSE;
    signal_waitable(e, 1);
    return TRUE;
}

BOOL WINAPI ResetEvent(HANDLE h) {
    struct waitable *e = handle_get(h, KOBJ_EVENT);
    if (!e) return FALSE;
    signal_waitable(e, 0);
    return TRUE;
}

DWORD WINAPI WaitForMultipleObjects(DWORD n, const HANDLE *hs, BOOL all, DWORD ms) {
    struct waitable *w[64];
    if (n == 0 || n > 64) return WAIT_FAILED;
    for (DWORD i = 0; i < n; i++) {
        w[i] = handle_get(hs[i], 0);
        if (!w[i] || (w[i]->obj.type != KOBJ_EVENT && w[i]->obj.type != KOBJ_THREAD))
            return WAIT_FAILED;
    }
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += ms / 1000;
    deadline.tv_nsec += (ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) deadline.tv_sec++, deadline.tv_nsec -= 1000000000L;

    DWORD result = WAIT_TIMEOUT;
    pthread_mutex_lock(&wait_lock);
    for (;;) {
        DWORD ready = 0, first = n;
        for (DWORD i = 0; i < n; i++)
            if (w[i]->signaled) {
                ready++;
                if (first == n) first = i;
            }
        if (all ? ready == n : ready > 0) {
            // Satisfying a wait consumes auto-reset events.
            for (DWORD i = 0; i < n; i++)
                if ((all || i == first) && w[i]->obj.type == KOBJ_EVENT && !w[i]->manual_reset)
                    w[i]->signaled = 0;
            result = WAIT_OBJECT_0 + (all ? 0 : first);
            break;
        }
        if (ms == 0) break;
        int rc = ms == INFINITE ? pthread_cond_wait(&wait_cond, &wait_lock)
                                : pthread_cond_timedwait(&wait_cond, &wait_lock, &deadline);
        if (rc == ETIMEDOUT) break;
    }
    pthread_mutex_unlock(&wait_lock);
    return result;
}

struct thread {
    struct waitable w;
    pthread_t pt;
    DWORD(WINAPI *start)(void *);
    void *param;
    int suspended;
    DWORD exit_code;
    pthread_mutex_t start_lock;
    pthread_cond_t start_cond;
    DWORD tid;
};

static __thread struct thread *current_thread;

static void *thread_main(void *arg) {
    struct thread *t = arg;
    current_thread = t;
    teb_attach_current_thread(t->tid);
    pthread_mutex_lock(&t->start_lock);
    while (t->suspended) pthread_cond_wait(&t->start_cond, &t->start_lock);
    pthread_mutex_unlock(&t->start_lock);
    t->exit_code = t->start(t->param);
    signal_waitable(&t->w, 1);
    return NULL;
}

HANDLE WINAPI CreateThread(void *sa, DWORD stack, DWORD(WINAPI *start)(void *), void *param,
                           DWORD flags, DWORD *tid) {
    struct thread *t = calloc(1, sizeof(*t));
    t->w.obj.type = KOBJ_THREAD;
    t->w.manual_reset = 1;
    t->start = start;
    t->param = param;
    t->suspended = (flags & 4) != 0;  // CREATE_SUSPENDED
    t->tid = teb_new_thread_id();
    pthread_mutex_init(&t->start_lock, NULL);
    pthread_cond_init(&t->start_cond, NULL);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stack > (4u << 20) ? stack : (4u << 20));
    int rc = pthread_create(&t->pt, &attr, thread_main, t);
    pthread_attr_destroy(&attr);
    if (rc) {
        free(t);
        win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    pthread_detach(t->pt);
    if (tid) *tid = t->tid;
    return handle_new(t);
}

DWORD WINAPI ResumeThread(HANDLE h) {
    struct thread *t = handle_get(h, KOBJ_THREAD);
    if (!t) return 0xffffffffu;
    pthread_mutex_lock(&t->start_lock);
    DWORD prev = t->suspended;
    t->suspended = 0;
    pthread_cond_broadcast(&t->start_cond);
    pthread_mutex_unlock(&t->start_lock);
    return prev;
}

void WINAPI ExitThread(DWORD code) {
    if (current_thread) {
        current_thread->exit_code = code;
        signal_waitable(&current_thread->w, 1);
    }
    pthread_exit(NULL);
}

void kernel32_close_waitable(struct kobj *o) {
    // Threads keep running after their handle is closed, so only events are
    // freed here.
    if (o->type == KOBJ_EVENT) free(o);
}

// ---- Time -----------------------------------------------------------------

static _Thread_local int ticks_held;
static _Thread_local DWORD ticks_held_at;

static DWORD ticks_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)(ts.tv_sec * 1000ull + ts.tv_nsec / 1000000);
}

DWORD WINAPI GetTickCount(void) { return ticks_held ? ticks_held_at : ticks_now(); }

// Threads holding their clock, for timer threads to wait on.
static volatile int ticks_holders;

void kernel32_hold_ticks(int hold) {
    if (hold && !ticks_held++) ticks_held_at = ticks_now(), __atomic_add_fetch(&ticks_holders, 1, __ATOMIC_SEQ_CST);
    if (!hold && ticks_held && !--ticks_held) __atomic_sub_fetch(&ticks_holders, 1, __ATOMIC_SEQ_CST);
}

int kernel32_ticks_held(void) { return ticks_held > 0; }
int kernel32_ticks_held_anywhere(void) { return __atomic_load_n(&ticks_holders, __ATOMIC_SEQ_CST) > 0; }

void WINAPI Sleep(DWORD ms) {
    if (!ms) {
        sched_yield();
        return;
    }
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) && errno == EINTR) {}
}

static void fill_systemtime(WORD *st, int local) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    if (local) localtime_r(&ts.tv_sec, &tm);
    else gmtime_r(&ts.tv_sec, &tm);
    st[0] = tm.tm_year + 1900;
    st[1] = tm.tm_mon + 1;
    st[2] = tm.tm_wday;
    st[3] = tm.tm_mday;
    st[4] = tm.tm_hour;
    st[5] = tm.tm_min;
    st[6] = tm.tm_sec;
    st[7] = ts.tv_nsec / 1000000;
}

void WINAPI GetSystemTime(WORD *st) { fill_systemtime(st, 0); }
void WINAPI GetLocalTime(WORD *st) { fill_systemtime(st, 1); }

// ---- Code pages and string conversion -------------------------------------
// The game is single-byte Windows-1252 text. We treat it as Latin-1, which
// only differs in 0x80-0x9f punctuation.

UINT WINAPI GetACP(void) { return 1252; }
DWORD WINAPI GetUserDefaultLCID(void) { return 0x409; }  // en-US
BOOL WINAPI IsValidCodePage(UINT cp) { return cp == 1252 || cp == 437 || cp == 0; }
BOOL WINAPI IsValidLocale(DWORD lcid, DWORD flags) { return lcid == 0x409 || lcid == 0x400 || lcid == 0x800; }

// CompareStringA: ordinal comparison, optionally ignoring case.
int WINAPI CompareStringA(DWORD lcid, DWORD flags, const char *a, int na, const char *b, int nb) {
    if (na < 0) na = strlen(a);
    if (nb < 0) nb = strlen(b);
    for (int i = 0; i < na && i < nb; i++) {
        int ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (flags & 1) ca = tolower(ca), cb = tolower(cb);  // NORM_IGNORECASE
        if (ca != cb) return ca < cb ? 1 : 3;
    }
    return na == nb ? 2 : na < nb ? 1 : 3;  // CSTR_LESS_THAN, EQUAL, GREATER_THAN
}
UINT WINAPI GetOEMCP(void) { return 437; }

BOOL WINAPI GetCPInfo(UINT cp, BYTE *info) {
    memset(info, 0, 20);
    *(UINT *)info = 1;  // MaxCharSize
    info[4] = '?';      // DefaultChar
    return TRUE;
}

int WINAPI MultiByteToWideChar(UINT cp, DWORD flags, const char *src, int srclen, WCHAR *dst, int dstlen) {
    if (srclen < 0) srclen = strlen(src) + 1;
    if (!dstlen) return srclen;
    if (dstlen < srclen) {
        win32_set_last_error(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (int i = 0; i < srclen; i++) dst[i] = (unsigned char)src[i];
    return srclen;
}

int WINAPI WideCharToMultiByte(UINT cp, DWORD flags, const WCHAR *src, int srclen, char *dst, int dstlen,
                               const char *defchar, BOOL *used_default) {
    if (srclen < 0) {
        srclen = 0;
        while (src[srclen]) srclen++;
        srclen++;
    }
    if (used_default) *used_default = FALSE;
    if (!dstlen) return srclen;
    if (dstlen < srclen) {
        win32_set_last_error(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (int i = 0; i < srclen; i++) {
        if (src[i] > 0xff) {
            dst[i] = defchar ? *defchar : '?';
            if (used_default) *used_default = TRUE;
        } else {
            dst[i] = (char)src[i];
        }
    }
    return srclen;
}

#define LCMAP_LOWERCASE 0x100
#define LCMAP_UPPERCASE 0x200

static unsigned map_case(unsigned c, DWORD flags) {
    if (c >= 0x80) return c;  // leave non-ASCII alone
    if (flags & LCMAP_LOWERCASE) return tolower(c);
    if (flags & LCMAP_UPPERCASE) return toupper(c);
    return c;
}

int WINAPI LCMapStringA(DWORD lcid, DWORD flags, const char *src, int srclen, char *dst, int dstlen) {
    if (srclen < 0) srclen = strlen(src) + 1;
    if (!dstlen) return srclen;
    if (dstlen < srclen) {
        win32_set_last_error(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (int i = 0; i < srclen; i++) dst[i] = map_case((unsigned char)src[i], flags);
    return srclen;
}

int WINAPI LCMapStringW(DWORD lcid, DWORD flags, const WCHAR *src, int srclen, WCHAR *dst, int dstlen) {
    if (srclen < 0) {
        srclen = 0;
        while (src[srclen]) srclen++;
        srclen++;
    }
    if (!dstlen) return srclen;
    if (dstlen < srclen) {
        win32_set_last_error(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (int i = 0; i < srclen; i++) dst[i] = map_case(src[i], flags);
    return srclen;
}

// CT_CTYPE1 classification bits.
static WORD ctype1(unsigned c) {
    WORD t = 0;
    if (c >= 0x80) return isalpha(c) ? 0x100 : 0;  // Latin-1 letters are rare in the data
    if (isupper(c)) t |= 0x001;
    if (islower(c)) t |= 0x002;
    if (isdigit(c)) t |= 0x004;
    if (isspace(c)) t |= 0x008;
    if (ispunct(c)) t |= 0x010;
    if (iscntrl(c)) t |= 0x020;
    if (c == ' ' || c == '\t') t |= 0x040;
    if (isxdigit(c)) t |= 0x080;
    if (isalpha(c)) t |= 0x100;
    return t;
}

BOOL WINAPI GetStringTypeA(DWORD lcid, DWORD type, const char *src, int len, WORD *out) {
    if (len < 0) len = strlen(src) + 1;
    for (int i = 0; i < len; i++) out[i] = type == 1 ? ctype1((unsigned char)src[i]) : 0;
    return TRUE;
}

BOOL WINAPI GetStringTypeW(DWORD type, const WCHAR *src, int len, WORD *out) {
    if (len < 0) {
        len = 0;
        while (src[len]) len++;
        len++;
    }
    for (int i = 0; i < len; i++) out[i] = type == 1 ? ctype1(src[i]) : 0;
    return TRUE;
}
