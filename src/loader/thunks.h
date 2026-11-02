// Small machine-code stubs placed in the game's import tables.
#pragma once

// A stub that reports "DLL!name is not implemented" with the caller's address
// and aborts. Used for every import we do not shim yet.
void *thunk_trap(const char *dll, const char *name, int ordinal);

// A stub that counts and logs each call, then jumps to `target`. Used when
// tracing is on, so a run lists every Win32 function the game touches.
void *thunk_trace(const char *dll, const char *name, int ordinal, void *target);

// Print per-function call counts gathered by trace stubs.
void thunk_trace_summary(void);
