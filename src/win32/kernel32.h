// Internal interface between the KERNEL32 shim files.
#pragma once

#include "handles.h"

void kernel32_set_command_line(const char *cmdline);
void kernel32_close_waitable(struct kobj *o);
