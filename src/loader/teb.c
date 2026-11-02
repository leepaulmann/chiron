#include "teb.h"

#include <asm/ldt.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "../win32/win32.h"

// Minimal PEB: ImageBaseAddress at 0x08, ProcessHeap at 0x18.
static uint32_t *peb;
static int next_ldt_entry = 32;

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
    struct teb *t = aligned_alloc(0x1000, sizeof(struct teb) + 0x1000);
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
        .entry_number = __atomic_fetch_add(&next_ldt_entry, 1, __ATOMIC_RELAXED),
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
    return t;
}

void win32_set_last_error(DWORD err) { teb_current()->last_error = err; }
DWORD win32_get_last_error(void) { return teb_current()->last_error; }
