// Windows thread environment block, reachable through %fs as the game's
// compiled code (and its MSVC runtime) expects.
#pragma once

#include <stdint.h>

#define TEB_TLS_SLOTS 64

struct teb {
    uint32_t exception_list;  // 0x00  head of the SEH chain, -1 when empty
    uint32_t stack_base;      // 0x04  highest stack address
    uint32_t stack_limit;     // 0x08
    uint32_t reserved0[3];    // 0x0c
    uint32_t self;            // 0x18  linear address of this TEB
    uint32_t env;             // 0x1c
    uint32_t pid;             // 0x20
    uint32_t tid;             // 0x24
    uint32_t rpc;             // 0x28
    uint32_t tls_pointer;     // 0x2c
    uint32_t peb;             // 0x30
    uint32_t last_error;      // 0x34
    uint8_t pad[0xe10 - 0x38];
    uint32_t tls_slots[TEB_TLS_SLOTS];  // 0xe10
};

_Static_assert(__builtin_offsetof(struct teb, tls_slots) == 0xe10, "TEB layout");

// Allocate a TEB for the calling thread and point %fs at it. The PEB is
// shared and created on first use. `tid` is the Windows thread id; pass 0 to
// allocate one.
struct teb *teb_attach_current_thread(uint32_t tid);
// Called last on a thread that is ending: its LDT entry, TEB and signal stack go to the next thread
// attached. The thread must not run game code after this.
void teb_detach_current_thread(void);
uint32_t teb_new_thread_id(void);
struct teb *teb_current(void);
void teb_set_image_base(uint32_t base);
