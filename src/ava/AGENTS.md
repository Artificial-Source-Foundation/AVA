## Project Specific Programming Knowledge

Every `.cpp` file beneath `src/ava/` must do `#include "sys.h"` as very first include.

Only use `#include "debug.h"` when required for things that are typically
declared by `cwds/debug.h`. e.g. `Dout`, `Debug`, `DoutEntering`, `ASSERT`.

If a header file defines a class that used `AVA_DEBUG_PRINT_MEMBERS_ON` then
use `#include "ava/debug/print_members_on.h"` to get that macro.

If you get a compile error in a generated `print_members.cpp`, aka somewhere
in `$BUILDDIR/generated/print_members/` then the first thing to try is to
regenerate those headers using `--target generate-print-members`.

If you get a compile error like:
`error: function 'ava::*::(anonymous namespace)::*::print_members' has internal linkage but is not defined`
then use `AVA_DEBUG_PRINT_MEMBERS_OPT_OUT` in that struct/class: you can currently not
use `AVA_DEBUG_PRINT_MEMBERS_ON` in a struct/class that is in an anonymous namespace.
