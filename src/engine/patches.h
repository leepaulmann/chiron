// Fixes for bugs in the original game, applied in memory after loading.
#pragma once

#include "../loader/pe.h"

// Apply every patch. Call this only for a recognised build of terranx.exe;
// each patch also checks the original bytes before changing anything.
void patches_apply(struct pe_image *exe);
