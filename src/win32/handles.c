#include "handles.h"

#include <pthread.h>

#define MAX_HANDLES 4096
#define HANDLE_FROM_INDEX(i) ((HANDLE)(uintptr_t)(0x100 + (i) * 4))
#define INDEX_FROM_HANDLE(h) (((intptr_t)(h) - 0x100) / 4)

static struct kobj *table[MAX_HANDLES];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

HANDLE handle_new(void *obj) {
    pthread_mutex_lock(&lock);
    for (int i = 0; i < MAX_HANDLES; i++)
        if (!table[i]) {
            table[i] = obj;
            pthread_mutex_unlock(&lock);
            return HANDLE_FROM_INDEX(i);
        }
    pthread_mutex_unlock(&lock);
    return NULL;
}

static struct kobj **slot(HANDLE h) {
    intptr_t v = (intptr_t)h;
    if (v < 0x100 || (v & 3)) return NULL;
    intptr_t i = INDEX_FROM_HANDLE(h);
    return i < MAX_HANDLES ? &table[i] : NULL;
}

void *handle_get(HANDLE h, enum kobj_type type) {
    pthread_mutex_lock(&lock);
    struct kobj **s = slot(h);
    struct kobj *o = s ? *s : NULL;
    pthread_mutex_unlock(&lock);
    if (!o || (type && o->type != type)) {
        win32_set_last_error(ERROR_INVALID_HANDLE);
        return NULL;
    }
    return o;
}

void *handle_take(HANDLE h) {
    pthread_mutex_lock(&lock);
    struct kobj **s = slot(h);
    struct kobj *o = s ? *s : NULL;
    if (o) *s = NULL;
    pthread_mutex_unlock(&lock);
    if (!o) win32_set_last_error(ERROR_INVALID_HANDLE);
    return o;
}
