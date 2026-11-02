// Kernel object handles. Every object struct starts with `struct kobj`.
#pragma once

#include "win32.h"

enum kobj_type { KOBJ_FILE = 1, KOBJ_FIND, KOBJ_MAPPING, KOBJ_EVENT, KOBJ_THREAD, KOBJ_HEAP };

struct kobj {
    enum kobj_type type;
};

// Events and threads can be waited on. `signaled` is protected by the global
// wait lock in kernel32_proc.c.
struct waitable {
    struct kobj obj;
    int signaled;
    int manual_reset;
};

HANDLE handle_new(void *obj);
void *handle_get(HANDLE h, enum kobj_type type);  // NULL + ERROR_INVALID_HANDLE on mismatch
void *handle_take(HANDLE h);                       // remove from table, return object

// Standard handles, which are not in the table.
#define STD_HANDLE_BASE 0x10
#define STD_FD(h) ((int)(intptr_t)(h) - STD_HANDLE_BASE)
#define IS_STD_HANDLE(h) ((uintptr_t)(h) >= STD_HANDLE_BASE && (uintptr_t)(h) < STD_HANDLE_BASE + 3)
