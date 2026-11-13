// The Windows-call journal for shadow checks (see journal.c).
#pragma once

struct pe_image;

// Put the journal's wrappers in the import table (shadow mode, before game_bind reads it).
void journal_install(struct pe_image *exe);
// A check's phases: before the original runs, between the runs, and after Chiron's run (the
// first difference in Windows calls, or NULL); or given up.
void journal_start(void);
void journal_middle(void);
const char *journal_end(void);
void journal_abandon(void);
