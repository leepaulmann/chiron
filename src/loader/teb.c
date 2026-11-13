#include "teb.h"

#include <asm/ldt.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "../win32/win32.h"

// Minimal PEB: ImageBaseAddress at 0x08, ProcessHeap at 0x18.
static uint32_t *peb;
static int next_ldt_entry = 32;

// A thread that ends hands its LDT entry, TEB and signal stack to the next thread made: the LDT has
// 8192 entries, and every multimedia timer runs on a thread of its own.
struct teb_slot {
    int entry;
    struct teb *teb;
    stack_t alt;
    struct teb_slot *next;
};
static struct teb_slot *free_slots;
static pthread_mutex_t slot_lock = PTHREAD_MUTEX_INITIALIZER;
static __thread struct teb_slot *own_slot;

struct teb *teb_current(void) {
    struct teb *t;
    __asm__ volatile("movl %%fs:0x18, %0" : "=r"(t));
    return t;
}

void teb_set_image_base(uint32_t base) {
    if (!peb) peb = calloc(1, 0x1000);
    peb[2] = base;
}

uint32_t teb_new_thread_id(void) {
    static uint32_t next = 0x100;
    return __atomic_fetch_add(&next, 4, __ATOMIC_RELAXED);
}

struct teb *teb_attach_current_thread(uint32_t tid) {
    if (!peb) peb = calloc(1, 0x1000);
    pthread_mutex_lock(&slot_lock);
    struct teb_slot *slot = free_slots;
    if (slot) free_slots = slot->next;
    pthread_mutex_unlock(&slot_lock);
    if (!slot) {
        slot = calloc(1, sizeof(*slot));
        slot->entry = __atomic_fetch_add(&next_ldt_entry, 1, __ATOMIC_RELAXED);
        slot->teb = aligned_alloc(0x1000, sizeof(struct teb) + 0x1000);
    }
    own_slot = slot;
    struct teb *t = slot->teb;
    memset(t, 0, sizeof(*t));
    t->exception_list = 0xffffffffu;
    t->self = (uint32_t)t;
    t->pid = getpid();
    t->tid = tid ? tid : teb_new_thread_id();
    t->peb = (uint32_t)peb;

    pthread_attr_t attr;
    void *stack_addr;
    size_t stack_size;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        pthread_attr_getstack(&attr, &stack_addr, &stack_size);
        pthread_attr_destroy(&attr);
        t->stack_limit = (uint32_t)stack_addr;
        t->stack_base = (uint32_t)stack_addr + stack_size;
    }

    // i386 glibc keeps its own TLS in %gs, so %fs is ours. Give this thread a
    // private LDT descriptor covering its TEB.
    struct user_desc desc = {
        .entry_number = slot->entry,
        .base_addr = (uint32_t)t,
        .limit = sizeof(struct teb) - 1,
        .seg_32bit = 1,
        .useable = 1,
    };
    if (syscall(SYS_modify_ldt, 1, &desc, sizeof(desc)) != 0) {
        perror("chiron: modify_ldt");
        abort();
    }
    uint16_t sel = (desc.entry_number << 3) | 7;  // LDT, ring 3
    __asm__ volatile("movw %0, %%fs" ::"r"(sel));
    // Faults are handled on a stack of their own: the handler builds the
    // game's exception records just below the faulting esp, where the
    // kernel would otherwise have put the signal frame (and its saved FPU
    // state, whose size depends on the CPU).
    if (!slot->alt.ss_sp) {
        size_t alt_size = 256 * 1024;
        long min = sysconf(_SC_SIGSTKSZ);
        if (min > 0 && (size_t)min * 4 > alt_size) alt_size = (size_t)min * 4;
        slot->alt = (stack_t){.ss_sp = malloc(alt_size), .ss_size = alt_size};
    }
    stack_t alt = slot->alt;
    if (!alt.ss_sp || sigaltstack(&alt, NULL) != 0) {
        perror("chiron: sigaltstack");
        abort();
    }
    // Windows starts threads with the x87 unit at 53-bit precision; Linux
    // uses 64-bit. MSVC-compiled code expects the Windows setting.
    uint16_t cw = 0x027f;
    __asm__ volatile("fninit; fldcw %0" ::"m"(cw));
    return t;
}

void teb_detach_current_thread(void) {
    struct teb_slot *slot = own_slot;
    if (!slot) return;
    own_slot = NULL;
    stack_t off = {.ss_flags = SS_DISABLE};
    sigaltstack(&off, NULL);
    pthread_mutex_lock(&slot_lock);
    slot->next = free_slots;
    free_slots = slot;
    pthread_mutex_unlock(&slot_lock);
}

void win32_set_last_error(DWORD err) { teb_current()->last_error = err; }
DWORD win32_get_last_error(void) { return teb_current()->last_error; }
