// Internal interface between the KERNEL32 shim files.
#pragma once

#include "handles.h"

void kernel32_set_command_line(const char *cmdline);
void kernel32_close_waitable(struct kobj *o);

// Holds GetTickCount() (and so timeGetTime()) at its current value for the
// calling thread, or releases it. Shadow-mode checks use it so the original
// and Chiron's code read the same time.
void kernel32_hold_ticks(int hold);
// Whether the clock is held: timers don't come due meanwhile.
int kernel32_ticks_held(void);
// Whether any thread holds its clock.
int kernel32_ticks_held_anywhere(void);
// A multimedia timer callback may run (1, and kernel32_timer_leave() after
// it) only while no thread holds its clock.
int kernel32_timer_enter(void);
void kernel32_timer_leave(void);

// WIN32_FIND_DATAA.
struct win32_find_data {
    DWORD attrs;
    DWORD ctime[2], atime[2], mtime[2];
    DWORD size_hi, size_lo;
    DWORD reserved[2];
    char name[260];
    char alt_name[14];
};

// Win32 entry points that other shim files call directly.
HANDLE WINAPI FindFirstFileA(const char *spec, struct win32_find_data *data);
BOOL WINAPI FindNextFileA(HANDLE h, struct win32_find_data *data);
BOOL WINAPI FindClose(HANDLE h);
BOOL WINAPI SetCurrentDirectoryA(const char *dir);
