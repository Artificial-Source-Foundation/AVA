## Project Specific Programming Knowledge

Only use `#include "debug.h"` when required for things that are typically
declare by `cwds/debug.h`. e.g. `Dout`, `Debug`, `DoutEntering`, `ASSERT`.

If a header file defines a class that used `AVA_DEBUG_PRINT_MEMBERS_ON` then
use `#include "ava/debug/print_members_on.h"` to get that macro.

If you get a compile error in a generated `print_members.cpp`, aka somewhere
in `$BUILDDIR/generated/print_members/` then the first thing to try is to
regenerate those headers using `--target generate-print-members`.
