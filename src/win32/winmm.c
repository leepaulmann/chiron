// WINMM: multimedia timers, RIFF file I/O (mmio) and audio-input stubs.
#include <SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "../loader/teb.h"
#include "win32.h"

#define MMSYSERR_NOERROR 0
#define MMSYSERR_ERROR 1
#define MMSYSERR_NODRIVER 6

DWORD WINAPI GetTickCount(void);
BOOL WINAPI SetEvent(HANDLE h);
BOOL WINAPI ResetEvent(HANDLE h);

DWORD WINAPI timeGetTime(void) { return GetTickCount(); }

UINT WINAPI timeGetDevCaps(UINT *caps, UINT size) {
    if (size < 8) return MMSYSERR_ERROR;
    caps[0] = 1;        // wPeriodMin
    caps[1] = 1000000;  // wPeriodMax
    return MMSYSERR_NOERROR;
}

UINT WINAPI timeBeginPeriod(UINT ms) { return MMSYSERR_NOERROR; }
UINT WINAPI timeEndPeriod(UINT ms) { return MMSYSERR_NOERROR; }

// ---- timeSetEvent ---------------------------------------------------------
// Each event runs on its own thread with a TEB, since callbacks are game code.

typedef void(WINAPI *TIMECALLBACK)(UINT id, UINT msg, DWORD user, DWORD r1, DWORD r2);

struct mmtimer {
    UINT id;
    UINT delay;
    int periodic, kind;  // kind: 0 callback, 1 set event, 2 pulse event
    void *target;
    DWORD user;
    volatile int stop;
    pthread_t thread;
    struct mmtimer *next;
};

static struct mmtimer *mmtimers;
static pthread_mutex_t mmtimer_lock = PTHREAD_MUTEX_INITIALIZER;
static UINT next_timer_id = 1;

static void *mmtimer_thread(void *arg) {
    struct mmtimer *t = arg;
    teb_attach_current_thread(0);
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    do {
        next.tv_nsec += (long)t->delay * 1000000L;
        while (next.tv_nsec >= 1000000000L) next.tv_sec++, next.tv_nsec -= 1000000000L;
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        if (t->stop) break;
        if (t->kind == 0) {
            ((TIMECALLBACK)t->target)(t->id, 0, t->user, 0, 0);
        } else {
            SetEvent(t->target);
            if (t->kind == 2) ResetEvent(t->target);
        }
    } while (t->periodic && !t->stop);
    return NULL;
}

UINT WINAPI timeSetEvent(UINT delay, UINT resolution, void *target, DWORD user, UINT flags) {
    struct mmtimer *t = calloc(1, sizeof(*t));
    t->delay = delay ? delay : 1;
    t->periodic = flags & 1;  // TIME_PERIODIC
    t->kind = (flags & 0x20) ? 2 : (flags & 0x10) ? 1 : 0;
    t->target = target;
    t->user = user;
    pthread_mutex_lock(&mmtimer_lock);
    t->id = next_timer_id++;
    t->next = mmtimers;
    mmtimers = t;
    pthread_mutex_unlock(&mmtimer_lock);
    if (pthread_create(&t->thread, NULL, mmtimer_thread, t)) return 0;
    return t->id;
}

UINT WINAPI timeKillEvent(UINT id) {
    pthread_mutex_lock(&mmtimer_lock);
    struct mmtimer *t = NULL;
    for (struct mmtimer **pp = &mmtimers; *pp; pp = &(*pp)->next)
        if ((*pp)->id == id) {
            t = *pp;
            *pp = t->next;
            break;
        }
    pthread_mutex_unlock(&mmtimer_lock);
    if (!t) return 97;  // MMSYSERR_INVALPARAM
    t->stop = 1;
    // A callback may kill its own timer; it cannot wait for itself.
    if (pthread_equal(t->thread, pthread_self())) pthread_detach(t->thread);
    else pthread_join(t->thread, NULL), free(t);
    return MMSYSERR_NOERROR;
}

// ---- mmio -----------------------------------------------------------------
// Unbuffered file I/O plus RIFF chunk navigation. The direct-buffer API
// (mmioGetInfo/mmioAdvance) is served from a private read buffer.

typedef struct {
    DWORD flags, fcc_io;
    void *io_proc;
    UINT error_ret;
    void *task;
    LONG buffer_size;
    char *buffer, *next, *end_read, *end_write;
    LONG buf_offset, disk_offset;
    DWORD info[3];
    DWORD reserved1, reserved2;
    void *mmio;
} MMIOINFO;

typedef struct {
    DWORD ckid, cksize, fcc_type, data_offset, flags;
} MMCKINFO;

#define FOURCC(a, b, c, d) ((DWORD)(a) | (DWORD)(b) << 8 | (DWORD)(c) << 16 | (DWORD)(d) << 24)
#define FOURCC_RIFF FOURCC('R', 'I', 'F', 'F')
#define FOURCC_LIST FOURCC('L', 'I', 'S', 'T')

struct mmio {
    FILE *f;
    char buf[64 * 1024];
    LONG buf_pos;  // file offset of buf[0]
    LONG buf_len;
};

int path_to_host(const char *winpath, char *out, size_t outlen);

void *WINAPI mmioOpenA(const char *name, MMIOINFO *info, DWORD flags) {
    char host[4096];
    const char *mode = (flags & 0x1000) ? "w+b" : (flags & 3) == 2 ? "r+b" : (flags & 3) == 1 ? "r+b" : "rb";
    if (!name || path_to_host(name, host, sizeof(host))) {
        if (info) info->error_ret = 257;  // MMIOERR_FILENOTFOUND
        return NULL;
    }
    FILE *f = fopen(host, mode);
    if (!f) {
        if (info) info->error_ret = 257;
        return NULL;
    }
    struct mmio *m = calloc(1, sizeof(*m));
    m->f = f;
    if (info) info->error_ret = 0;
    return m;
}

UINT WINAPI mmioClose(struct mmio *m, UINT flags) {
    if (!m) return 0;
    fclose(m->f);
    free(m);
    return 0;
}

LONG WINAPI mmioRead(struct mmio *m, char *buf, LONG n) { return (LONG)fread(buf, 1, n, m->f); }
LONG WINAPI mmioWrite(struct mmio *m, const char *buf, LONG n) { return (LONG)fwrite(buf, 1, n, m->f); }

LONG WINAPI mmioSeek(struct mmio *m, LONG off, int origin) {
    if (fseek(m->f, off, origin)) return -1;
    return ftell(m->f);
}

UINT WINAPI mmioSetBuffer(struct mmio *m, char *buf, LONG size, UINT flags) { return 0; }

DWORD WINAPI mmioStringToFOURCCA(const char *s, UINT flags) {
    char c[4] = {' ', ' ', ' ', ' '};
    for (int i = 0; i < 4 && s[i]; i++) c[i] = (flags & 0x10) ? toupper((unsigned char)s[i]) : s[i];
    return FOURCC(c[0], c[1], c[2], c[3]);
}

UINT WINAPI mmioDescend(struct mmio *m, MMCKINFO *ck, const MMCKINFO *parent, UINT flags) {
    long end = parent ? (long)(parent->data_offset + parent->cksize) : 0x7fffffff;
    DWORD want_id = ck->ckid, want_type = ck->fcc_type;
    for (;;) {
        long pos = ftell(m->f);
        if (pos + 8 > end) return 265;  // MMIOERR_CHUNKNOTFOUND
        DWORD hdr[2];
        if (fread(hdr, 4, 2, m->f) != 2) return 265;
        ck->ckid = hdr[0];
        ck->cksize = hdr[1];
        ck->data_offset = pos + 8;
        ck->flags = 0;
        ck->fcc_type = 0;
        if (hdr[0] == FOURCC_RIFF || hdr[0] == FOURCC_LIST) {
            if (fread(&ck->fcc_type, 4, 1, m->f) != 1) return 265;
        }
        int ok;
        if (flags & 0x20) ok = hdr[0] == FOURCC_RIFF && ck->fcc_type == want_type;       // MMIO_FINDRIFF
        else if (flags & 0x40) ok = hdr[0] == FOURCC_LIST && ck->fcc_type == want_type;  // MMIO_FINDLIST
        else if (flags & 0x10) ok = hdr[0] == want_id;                                   // MMIO_FINDCHUNK
        else ok = 1;
        if (ok) return 0;
        fseek(m->f, ck->data_offset + ((ck->cksize + 1) & ~1u), SEEK_SET);
    }
}

UINT WINAPI mmioAscend(struct mmio *m, MMCKINFO *ck, UINT flags) {
    fseek(m->f, ck->data_offset + ((ck->cksize + 1) & ~1u), SEEK_SET);
    return 0;
}

static void fill_info(struct mmio *m, MMIOINFO *info) {
    info->buffer = m->buf;
    info->buffer_size = sizeof(m->buf);
    info->next = m->buf;
    info->end_read = m->buf + m->buf_len;
    info->end_write = m->buf + sizeof(m->buf);
    info->disk_offset = m->buf_pos + m->buf_len;
    info->buf_offset = m->buf_pos;
}

UINT WINAPI mmioGetInfo(struct mmio *m, MMIOINFO *info, UINT flags) {
    memset(info, 0, sizeof(*info));
    long pos = ftell(m->f);
    m->buf_pos = pos;
    m->buf_len = (LONG)fread(m->buf, 1, sizeof(m->buf), m->f);
    fseek(m->f, pos, SEEK_SET);
    fill_info(m, info);
    info->mmio = m;
    return 0;
}

UINT WINAPI mmioSetInfo(struct mmio *m, const MMIOINFO *info, UINT flags) {
    // Everything up to info->next has been consumed.
    fseek(m->f, m->buf_pos + (info->next - m->buf), SEEK_SET);
    return 0;
}

UINT WINAPI mmioAdvance(struct mmio *m, MMIOINFO *info, UINT flags) {
    if (flags & 2) return 0;  // MMIO_WRITE is not supported
    long pos = m->buf_pos + (info ? (info->next - m->buf) : m->buf_len);
    fseek(m->f, pos, SEEK_SET);
    m->buf_pos = pos;
    m->buf_len = (LONG)fread(m->buf, 1, sizeof(m->buf), m->f);
    fseek(m->f, pos, SEEK_SET);
    if (info) fill_info(m, info);
    return 0;
}

// ---- MCI and audio input --------------------------------------------------

DWORD WINAPI mciSendCommandA(UINT device, UINT msg, DWORD flags, void *params) {
    static int warned;
    if (!warned++) fprintf(stderr, "chiron: MCI is not supported (command 0x%x)\n", msg);
    return 275;  // MCIERR_DEVICE_NOT_INSTALLED
}

UINT WINAPI waveInGetNumDevs(void) { return 0; }
UINT WINAPI waveInGetDevCapsA(UINT id, void *caps, UINT size) { return MMSYSERR_NODRIVER; }
UINT WINAPI waveInOpen(void **h, UINT id, const void *fmt, DWORD cb, DWORD inst, DWORD flags) { return MMSYSERR_NODRIVER; }
UINT WINAPI waveInClose(void *h) { return MMSYSERR_NODRIVER; }
UINT WINAPI waveInPrepareHeader(void *h, void *hdr, UINT size) { return MMSYSERR_NODRIVER; }
UINT WINAPI waveInUnprepareHeader(void *h, void *hdr, UINT size) { return MMSYSERR_NODRIVER; }
UINT WINAPI waveInAddBuffer(void *h, void *hdr, UINT size) { return MMSYSERR_NODRIVER; }
UINT WINAPI waveInStart(void *h) { return MMSYSERR_NODRIVER; }
UINT WINAPI waveInStop(void *h) { return MMSYSERR_NODRIVER; }
UINT WINAPI waveInReset(void *h) { return MMSYSERR_NODRIVER; }
