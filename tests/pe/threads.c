// Threads and multimedia timers for Chiron: more of each, one after another, than the LDT has
// entries (8192). Each thread gets an LDT entry for its TEB, which must be reused once the thread
// has ended, and a one-shot timer's thread must go away once it has fired without timeKillEvent.
#include <windows.h>
#include <mmsystem.h>

static void put(const char *s) {
    DWORD n = 0, w;
    while (s[n]) n++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &w, NULL);
}

#define CHECK(code, cond)            \
    do {                             \
        if (!(cond)) {               \
            put("FAIL " #code "\n"); \
            ExitProcess(code);       \
        }                            \
    } while (0)

enum { ROUNDS = 10000 };

static volatile LONG fired, ran;

static DWORD WINAPI thread_start(void *p) {
    InterlockedIncrement(&ran);
    return 0;
}

static void CALLBACK on_timer(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR r1, DWORD_PTR r2) {
    InterlockedIncrement(&fired);
}

void __stdcall start(void) {
    for (int i = 0; i < ROUNDS; i++) {
        HANDLE h = CreateThread(NULL, 0, thread_start, (void *)(UINT_PTR)i, 0, NULL);
        CHECK(1, h != NULL);
        CHECK(2, WaitForMultipleObjects(1, &h, TRUE, INFINITE) == WAIT_OBJECT_0);
        CHECK(3, ran == i + 1);
        CloseHandle(h);
    }
    for (int i = 0; i < ROUNDS; i++) {
        CHECK(4, timeSetEvent(1, 0, on_timer, 0, TIME_ONESHOT) != 0);
        while (fired <= i) Sleep(0);
    }
    put("PASS\n");
    ExitProcess(0);
}
