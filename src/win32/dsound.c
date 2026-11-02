// DSOUND: DirectSound buffers mixed in software into an SDL audio device.
// Covers what soundx.dll uses: secondary buffers with volume, pan, frequency
// and looping, duplicated buffers for overlapping effects, and position
// notifications for streaming. 3D sound is played as plain stereo.
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../loader/modules.h"
#include "win32.h"

#define DS_OK 0
#define DSERR_INVALIDPARAM 0x80070057u
#define DSERR_NODRIVER 0x88780078u
#define DSERR_INVALIDCALL 0x88780032u
#define DSERR_OUTOFMEMORY 0x8007000eu
#define E_NOINTERFACE 0x80004002u

#define DSBCAPS_PRIMARYBUFFER 0x01
#define DSBPLAY_LOOPING 0x01
#define DSBSTATUS_PLAYING 0x01
#define DSBSTATUS_LOOPING 0x04
#define DSBLOCK_FROMWRITECURSOR 0x01
#define DSBLOCK_ENTIREBUFFER 0x02
#define DSBPN_OFFSETSTOP 0xffffffffu

#define MIX_RATE 44100

BOOL WINAPI SetEvent(HANDLE h);

typedef struct {
    WORD format_tag, channels;
    DWORD rate, avg_bytes;
    WORD block_align, bits, extra;
} WAVEFORMATEX;

typedef struct {
    DWORD size, flags, bytes, reserved;
    const WAVEFORMATEX *format;
} DSBUFFERDESC;

// Sample data shared by a buffer and its duplicates.
struct sound_data {
    int refs;
    uint8_t *bytes;
    DWORD size;
};

struct notify_pos {
    DWORD offset;
    HANDLE event;
};

struct buffer {
    const void *vtbl;
    const void *notify_vtbl;  // IDirectSoundNotify, sharing this object
    int refs;
    int primary;
    struct sound_data *data;
    WAVEFORMATEX fmt;
    DWORD frequency;  // playback rate; differs from fmt.rate after SetFrequency
    LONG volume, pan;
    float gain_l, gain_r;
    int playing, looping;
    double pos;          // in frames: everything before this has been mixed
    double chunk_start;  // pos at the start of the last mixer callback
    struct notify_pos *notifies;
    int nnotifies;
    struct buffer *next;
};

static struct buffer *buffers;  // secondary buffers, guarded by the audio lock
static SDL_AudioDeviceID device;
static Uint64 mix_time;    // performance counter at the last mixer callback
static double mix_period;  // seconds of audio that callback produced
int audio_volume = 100;  // master volume in percent (--volume)

// ---- Mixer ----------------------------------------------------------------

static void notify_crossed(struct buffer *b, DWORD from, DWORD to, int wrapped) {
    for (int i = 0; i < b->nnotifies; i++) {
        DWORD off = b->notifies[i].offset;
        if (off == DSBPN_OFFSETSTOP) continue;
        int hit = wrapped ? (off >= from || off < to) : (off >= from && off < to);
        if (hit) SetEvent(b->notifies[i].event);
    }
}

static void notify_stop(struct buffer *b) {
    for (int i = 0; i < b->nnotifies; i++)
        if (b->notifies[i].offset == DSBPN_OFFSETSTOP) SetEvent(b->notifies[i].event);
}

static inline int frame_sample(const struct buffer *b, DWORD frame, int channel) {
    if (frame >= b->data->size / b->fmt.block_align) frame = 0;  // interpolating past the end of a loop
    const uint8_t *p = b->data->bytes + frame * b->fmt.block_align;
    int ch = b->fmt.channels > 1 ? channel : 0;
    if (b->fmt.bits == 8) return ((int)p[ch] - 128) << 8;
    return ((const int16_t *)p)[ch];
}

static void SDLCALL mix(void *userdata, Uint8 *stream, int len) {
    int16_t *out = (int16_t *)stream;
    int frames = len / 4;
    mix_time = SDL_GetPerformanceCounter();
    mix_period = (double)frames / MIX_RATE;
    int32_t *acc = SDL_stack_alloc(int32_t, frames * 2);
    memset(acc, 0, frames * 2 * sizeof(int32_t));

    for (struct buffer *b = buffers; b; b = b->next) {
        b->chunk_start = b->pos;
        if (!b->playing || !b->data->size || !b->fmt.block_align) continue;
        DWORD total = b->data->size / b->fmt.block_align;
        double step = (double)b->frequency / MIX_RATE;
        DWORD start_byte = (DWORD)b->pos * b->fmt.block_align;
        int wrapped = 0;
        for (int i = 0; i < frames; i++) {
            DWORD f = (DWORD)b->pos;
            if (f >= total) {
                if (!b->looping) {
                    b->playing = 0;
                    b->pos = 0;
                    break;
                }
                b->pos -= total;
                f = (DWORD)b->pos;
                wrapped = 1;
            }
            // Linear interpolation between neighbouring source frames.
            float t = (float)(b->pos - f);
            DWORD g = f + 1 < total || b->looping ? f + 1 : f;
            float l = frame_sample(b, f, 0) * (1 - t) + frame_sample(b, g, 0) * t;
            float r = frame_sample(b, f, 1) * (1 - t) + frame_sample(b, g, 1) * t;
            acc[2 * i] += (int32_t)(l * b->gain_l);
            acc[2 * i + 1] += (int32_t)(r * b->gain_r);
            b->pos += step;
        }
        if (b->nnotifies) {
            DWORD end_byte = (DWORD)b->pos * b->fmt.block_align % b->data->size;
            notify_crossed(b, start_byte, end_byte, wrapped || end_byte < start_byte);
            if (!b->playing) notify_stop(b);
        }
    }
    for (int i = 0; i < frames * 2; i++) {
        int32_t v = acc[i] * audio_volume / 100;
        out[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
    }
    SDL_stack_free(acc);
    // CHIRON_AUDIO_DUMP=file records the mix as raw 44.1 kHz stereo s16.
    static FILE *dump;
    static int dump_checked;
    if (!dump_checked) {
        dump_checked = 1;
        const char *path = getenv("CHIRON_AUDIO_DUMP");
        if (path && *path) dump = fopen(path, "wb");
    }
    if (dump) fwrite(stream, 1, len, dump);
}

static int open_device(void) {
    if (device) return 0;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "chiron: SDL audio: %s\n", SDL_GetError());
        return -1;
    }
    SDL_AudioSpec want = {.freq = MIX_RATE, .format = AUDIO_S16SYS, .channels = 2, .samples = 1024, .callback = mix};
    device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (!device) {
        fprintf(stderr, "chiron: SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return -1;
    }
    SDL_PauseAudioDevice(device, 0);
    return 0;
}

// ---- IDirectSoundBuffer ---------------------------------------------------

static void update_gain(struct buffer *b) {
    // Volume and pan are in hundredths of a decibel of attenuation.
    float vol = b->volume <= -10000 ? 0.0f : powf(10.0f, b->volume / 2000.0f);
    float left = b->pan > 0 ? powf(10.0f, -b->pan / 2000.0f) : 1.0f;
    float right = b->pan < 0 ? powf(10.0f, b->pan / 2000.0f) : 1.0f;
    if (b->pan >= 10000) left = 0;
    if (b->pan <= -10000) right = 0;
    b->gain_l = vol * left;
    b->gain_r = vol * right;
}

static const void *buffer_vtbl[];
static const void *notify_vtbl[];

static struct buffer *new_buffer(void) {
    struct buffer *b = calloc(1, sizeof(*b));
    b->vtbl = buffer_vtbl;
    b->notify_vtbl = notify_vtbl;
    b->refs = 1;
    b->gain_l = b->gain_r = 1.0f;
    return b;
}

static const uint32_t IID_IDirectSoundNotify_d1 = 0xb0210783;

static DWORD WINAPI buf_QueryInterface(struct buffer *b, const uint8_t *iid, void **out) {
    if (*(const uint32_t *)iid == IID_IDirectSoundNotify_d1 && !b->primary) {
        b->refs++;
        *out = &b->notify_vtbl;
        return DS_OK;
    }
    fprintf(stderr, "chiron: IDirectSoundBuffer::QueryInterface {%08x-...} not supported\n", *(const uint32_t *)iid);
    *out = NULL;
    return E_NOINTERFACE;
}

static DWORD WINAPI buf_AddRef(struct buffer *b) { return ++b->refs; }

static DWORD WINAPI buf_Release(struct buffer *b) {
    if (--b->refs > 0) return b->refs;
    SDL_LockAudioDevice(device);
    for (struct buffer **pp = &buffers; *pp; pp = &(*pp)->next)
        if (*pp == b) {
            *pp = b->next;
            break;
        }
    SDL_UnlockAudioDevice(device);
    if (b->data && --b->data->refs == 0) {
        free(b->data->bytes);
        free(b->data);
    }
    free(b->notifies);
    free(b);
    return 0;
}

static DWORD WINAPI buf_GetCaps(struct buffer *b, DWORD *caps) {
    caps[1] = b->primary ? DSBCAPS_PRIMARYBUFFER : 0x0e0;  // CTRLVOLUME|CTRLPAN|CTRLFREQUENCY
    caps[2] = b->data ? b->data->size : 0;
    caps[3] = 0;
    caps[4] = 0;
    return DS_OK;
}

static DWORD bytes_ahead(struct buffer *b) {
    // Report a write cursor about 20 ms ahead of the play cursor.
    return (b->frequency / 50) * b->fmt.block_align;
}

static DWORD WINAPI buf_GetCurrentPosition(struct buffer *b, DWORD *play, DWORD *write) {
    if (b->primary || !b->data->size) {
        if (play) *play = 0;
        if (write) *write = 0;
        return DS_OK;
    }
    // Real hardware moves the play cursor sample by sample. Ours advances a
    // whole callback at a time, which makes exact collisions with the game's
    // own block boundaries common; terranx.exe's movie player reads an equal
    // play and write position as an empty ring and overwrites unplayed audio.
    // So interpolate between the last two callbacks, which also never reports
    // a position that has not been mixed yet.
    SDL_LockAudioDevice(device);
    DWORD total = b->data->size / b->fmt.block_align;
    double est = b->pos;
    if (b->playing && mix_period > 0) {
        double elapsed = (double)(SDL_GetPerformanceCounter() - mix_time) / SDL_GetPerformanceFrequency();
        double frac = elapsed / mix_period;
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        double advanced = b->pos - b->chunk_start;
        if (advanced < 0) advanced += total;  // wrapped during the callback
        est = b->chunk_start + advanced * frac;
        if (est >= total) est -= total;
    }
    DWORD p = ((DWORD)est * b->fmt.block_align) % b->data->size;
    SDL_UnlockAudioDevice(device);
    if (play) *play = p;
    if (write) *write = b->playing ? (p + bytes_ahead(b)) % b->data->size : p;
    return DS_OK;
}

static DWORD WINAPI buf_GetFormat(struct buffer *b, WAVEFORMATEX *fmt, DWORD size, DWORD *written) {
    DWORD n = size < sizeof(WAVEFORMATEX) ? size : sizeof(WAVEFORMATEX);
    if (fmt) memcpy(fmt, &b->fmt, n);
    if (written) *written = sizeof(WAVEFORMATEX);
    return DS_OK;
}

static DWORD WINAPI buf_GetVolume(struct buffer *b, LONG *v) { *v = b->volume; return DS_OK; }
static DWORD WINAPI buf_GetPan(struct buffer *b, LONG *v) { *v = b->pan; return DS_OK; }
static DWORD WINAPI buf_GetFrequency(struct buffer *b, DWORD *v) { *v = b->frequency; return DS_OK; }

static DWORD WINAPI buf_GetStatus(struct buffer *b, DWORD *status) {
    *status = b->playing ? DSBSTATUS_PLAYING | (b->looping ? DSBSTATUS_LOOPING : 0) : 0;
    return DS_OK;
}

static DWORD WINAPI buf_Initialize(struct buffer *b, void *ds, const DSBUFFERDESC *desc) { return DSERR_INVALIDCALL; }

static DWORD WINAPI buf_Lock(struct buffer *b, DWORD offset, DWORD bytes, void **p1, DWORD *n1, void **p2, DWORD *n2,
                             DWORD flags) {
    if (b->primary || !b->data->size) return DSERR_INVALIDCALL;
    DWORD size = b->data->size;
    if (flags & DSBLOCK_FROMWRITECURSOR) buf_GetCurrentPosition(b, NULL, &offset);
    if (flags & DSBLOCK_ENTIREBUFFER) bytes = size;
    if (offset >= size || bytes > size) return DSERR_INVALIDPARAM;
    *p1 = b->data->bytes + offset;
    if (offset + bytes <= size) {
        *n1 = bytes;
        if (p2) *p2 = NULL;
        if (n2) *n2 = 0;
    } else {
        *n1 = size - offset;
        if (p2) *p2 = b->data->bytes;
        if (n2) *n2 = bytes - *n1;
    }
    return DS_OK;
}

static DWORD WINAPI buf_Play(struct buffer *b, DWORD reserved, DWORD priority, DWORD flags) {
    if (b->primary) return DS_OK;
    if (modules_trace) fprintf(stderr, "[dsound] Play %p (%u bytes)%s\n", (void *)b, b->data->size, (flags & 1) ? " looping" : "");
    SDL_LockAudioDevice(device);
    b->playing = 1;
    b->looping = (flags & DSBPLAY_LOOPING) != 0;
    SDL_UnlockAudioDevice(device);
    return DS_OK;
}

static DWORD WINAPI buf_SetCurrentPosition(struct buffer *b, DWORD pos) {
    if (b->primary || !b->fmt.block_align) return DS_OK;
    SDL_LockAudioDevice(device);
    b->pos = b->chunk_start = (double)((pos % (b->data->size ? b->data->size : 1)) / b->fmt.block_align);
    SDL_UnlockAudioDevice(device);
    return DS_OK;
}

static DWORD WINAPI buf_SetFormat(struct buffer *b, const WAVEFORMATEX *fmt) {
    // The primary buffer's format only matters to hardware; we always mix
    // at MIX_RATE and convert each buffer.
    if (b->primary && fmt) b->fmt = *fmt;
    return b->primary ? DS_OK : DSERR_INVALIDCALL;
}

static DWORD WINAPI buf_SetVolume(struct buffer *b, LONG v) {
    if (modules_trace) fprintf(stderr, "[dsound] SetVolume %s %d\n", b->primary ? "primary" : "secondary", v);
    if (v > 0 || v < -10000) return DSERR_INVALIDPARAM;
    SDL_LockAudioDevice(device);
    b->volume = v;
    update_gain(b);
    SDL_UnlockAudioDevice(device);
    return DS_OK;
}

static DWORD WINAPI buf_SetPan(struct buffer *b, LONG v) {
    if (v > 10000 || v < -10000) return DSERR_INVALIDPARAM;
    SDL_LockAudioDevice(device);
    b->pan = v;
    update_gain(b);
    SDL_UnlockAudioDevice(device);
    return DS_OK;
}

static DWORD WINAPI buf_SetFrequency(struct buffer *b, DWORD f) {
    SDL_LockAudioDevice(device);
    b->frequency = f ? f : b->fmt.rate;  // DSBFREQUENCY_ORIGINAL = 0
    SDL_UnlockAudioDevice(device);
    return DS_OK;
}

static DWORD WINAPI buf_Stop(struct buffer *b) {
    if (b->primary) return DS_OK;
    SDL_LockAudioDevice(device);
    int was = b->playing;
    b->playing = 0;
    if (was) notify_stop(b);
    SDL_UnlockAudioDevice(device);
    return DS_OK;
}

static DWORD WINAPI buf_Unlock(struct buffer *b, void *p1, DWORD n1, void *p2, DWORD n2) { return DS_OK; }
static DWORD WINAPI buf_Restore(struct buffer *b) { return DS_OK; }

static const void *buffer_vtbl[] = {
    buf_QueryInterface, buf_AddRef, buf_Release, buf_GetCaps, buf_GetCurrentPosition, buf_GetFormat,
    buf_GetVolume, buf_GetPan, buf_GetFrequency, buf_GetStatus, buf_Initialize, buf_Lock,
    buf_Play, buf_SetCurrentPosition, buf_SetFormat, buf_SetVolume, buf_SetPan, buf_SetFrequency,
    buf_Stop, buf_Unlock, buf_Restore,
};

// ---- IDirectSoundNotify ---------------------------------------------------
// The interface pointer is &buffer->notify_vtbl, one word into the buffer.

#define NOTIFY_OWNER(p) ((struct buffer *)((char *)(p) - offsetof(struct buffer, notify_vtbl)))

static DWORD WINAPI notify_QueryInterface(void *self, const uint8_t *iid, void **out) {
    return buf_QueryInterface(NOTIFY_OWNER(self), iid, out);
}
static DWORD WINAPI notify_AddRef(void *self) { return buf_AddRef(NOTIFY_OWNER(self)); }
static DWORD WINAPI notify_Release(void *self) { return buf_Release(NOTIFY_OWNER(self)); }

static DWORD WINAPI notify_SetNotificationPositions(void *self, DWORD n, const struct notify_pos *pos) {
    struct buffer *b = NOTIFY_OWNER(self);
    struct notify_pos *copy = n ? malloc(n * sizeof(*copy)) : NULL;
    if (n) memcpy(copy, pos, n * sizeof(*copy));
    SDL_LockAudioDevice(device);
    free(b->notifies);
    b->notifies = copy;
    b->nnotifies = n;
    SDL_UnlockAudioDevice(device);
    return DS_OK;
}

static const void *notify_vtbl[] = {notify_QueryInterface, notify_AddRef, notify_Release,
                                    notify_SetNotificationPositions};

// ---- IDirectSound ---------------------------------------------------------

struct dsound {
    const void *vtbl;
    int refs;
};

static DWORD WINAPI ds_QueryInterface(struct dsound *ds, const uint8_t *iid, void **out) {
    fprintf(stderr, "chiron: IDirectSound::QueryInterface {%08x-...} not supported\n", *(const uint32_t *)iid);
    *out = NULL;
    return E_NOINTERFACE;
}

static DWORD WINAPI ds_AddRef(struct dsound *ds) { return ++ds->refs; }

static DWORD WINAPI ds_Release(struct dsound *ds) {
    if (--ds->refs > 0) return ds->refs;
    free(ds);
    return 0;
}

static DWORD WINAPI ds_CreateSoundBuffer(struct dsound *ds, const DSBUFFERDESC *desc, struct buffer **out,
                                         void *outer) {
    if (!desc || !out) return DSERR_INVALIDPARAM;
    struct buffer *b = new_buffer();
    if (desc->flags & DSBCAPS_PRIMARYBUFFER) {
        b->primary = 1;
        b->fmt = (WAVEFORMATEX){1, 2, MIX_RATE, MIX_RATE * 4, 4, 16, 0};
        b->data = calloc(1, sizeof(*b->data));
        b->data->refs = 1;
        *out = b;
        return DS_OK;
    }
    if (!desc->format || (desc->format->format_tag != 1 /* WAVE_FORMAT_PCM */) ||
        (desc->format->bits != 8 && desc->format->bits != 16)) {
        fprintf(stderr, "chiron: CreateSoundBuffer: unsupported format\n");
        free(b);
        return DSERR_INVALIDPARAM;
    }
    b->fmt = *desc->format;
    b->fmt.extra = 0;
    if (modules_trace)
        fprintf(stderr, "[dsound] CreateSoundBuffer %u bytes, %u Hz, %u ch, %u bit, flags 0x%x\n", desc->bytes,
                b->fmt.rate, b->fmt.channels, b->fmt.bits, desc->flags);
    b->frequency = b->fmt.rate;
    b->data = calloc(1, sizeof(*b->data));
    b->data->refs = 1;
    b->data->size = desc->bytes;
    b->data->bytes = calloc(1, desc->bytes + 4);
    if (!b->data->bytes) {
        free(b->data);
        free(b);
        return DSERR_OUTOFMEMORY;
    }
    SDL_LockAudioDevice(device);
    b->next = buffers;
    buffers = b;
    SDL_UnlockAudioDevice(device);
    *out = b;
    return DS_OK;
}

static DWORD WINAPI ds_GetCaps(struct dsound *ds, DWORD *caps) {
    DWORD size = caps[0];
    memset(caps + 1, 0, size - 4);
    caps[1] = 0x0f | 0x0f00;  // primary/secondary mono, stereo, 8 and 16 bit
    caps[2] = 100;            // dwMinSecondarySampleRate
    caps[3] = 200000;         // dwMaxSecondarySampleRate
    caps[4] = 1;              // dwPrimaryBuffers
    return DS_OK;
}

static DWORD WINAPI ds_DuplicateSoundBuffer(struct dsound *ds, struct buffer *src, struct buffer **out) {
    if (src->primary) return DSERR_INVALIDCALL;
    struct buffer *b = new_buffer();
    b->data = src->data;
    b->data->refs++;
    b->fmt = src->fmt;
    b->frequency = src->frequency;
    b->volume = src->volume;
    b->pan = src->pan;
    update_gain(b);
    SDL_LockAudioDevice(device);
    b->next = buffers;
    buffers = b;
    SDL_UnlockAudioDevice(device);
    *out = b;
    return DS_OK;
}

static DWORD WINAPI ds_SetCooperativeLevel(struct dsound *ds, void *hwnd, DWORD level) { return DS_OK; }
static DWORD WINAPI ds_Compact(struct dsound *ds) { return DS_OK; }

static DWORD WINAPI ds_GetSpeakerConfig(struct dsound *ds, DWORD *config) {
    *config = 4;  // DSSPEAKER_STEREO
    return DS_OK;
}

static DWORD WINAPI ds_SetSpeakerConfig(struct dsound *ds, DWORD config) { return DS_OK; }
static DWORD WINAPI ds_Initialize(struct dsound *ds, const void *guid) { return DS_OK; }

static const void *dsound_vtbl[] = {
    ds_QueryInterface, ds_AddRef, ds_Release, ds_CreateSoundBuffer, ds_GetCaps, ds_DuplicateSoundBuffer,
    ds_SetCooperativeLevel, ds_Compact, ds_GetSpeakerConfig, ds_SetSpeakerConfig, ds_Initialize,
};

// ---- Exports --------------------------------------------------------------

// ordinal: DirectSoundCreate 1
DWORD WINAPI DirectSoundCreate(const void *guid, struct dsound **out, void *outer) {
    if (!out) return DSERR_INVALIDPARAM;
    *out = NULL;
    if (open_device()) return DSERR_NODRIVER;
    struct dsound *ds = calloc(1, sizeof(*ds));
    ds->vtbl = dsound_vtbl;
    ds->refs = 1;
    *out = ds;
    return DS_OK;
}

typedef BOOL(WINAPI *DSENUMCALLBACKA)(void *guid, const char *desc, const char *module, void *ctx);

// ordinal: DirectSoundEnumerateA 2
DWORD WINAPI DirectSoundEnumerateA(DSENUMCALLBACKA callback, void *context) {
    callback(NULL, "Primary Sound Driver", "", context);
    return DS_OK;
}
