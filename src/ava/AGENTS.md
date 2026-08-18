## Project Specific Programming Knowledge

Every `.cpp` file beneath `src/ava/` must do `#include "sys.h"` as very first include.

Only use `#include "debug.h"` when required for things that are typically
declared by `cwds/debug.h`. e.g. `Dout`, `Debug`, `DoutEntering`, `ASSERT`.

If a header file defines a class that used `AVA_DEBUG_PRINT_MEMBERS_ON` then
use `#include "ava/debug/print_members_on.h"` to get that macro unless `"debug.h"`
is already included; never include both headers.

If you get a compile error in a generated `print_members.cpp`, aka somewhere
in `$BUILDDIR/generated/print_members/` then the first thing to try is to
regenerate those headers using `--target generate-print-members`.

If you get a compile error like:
`error: function 'ava::*::(anonymous namespace)::*::print_members' has internal linkage but is not defined`
then use `AVA_DEBUG_PRINT_MEMBERS_OPT_OUT` in that struct/class: you can currently not
use `AVA_DEBUG_PRINT_MEMBERS_ON` in a struct/class that is in an anonymous namespace.

## Assertions

- Use `ASSERT` only for programmer errors and internal invariants. Recoverable
  conditions, untrusted input, and runtime failures belong on explicit
  `Result<T>`/`VoidResult` error paths, never in an assertion.
- Keep `ASSERT` predicates side-effect-free: release builds may omit them, and
  correctness must never depend on assertion evaluation.
- Every individual `ASSERT` gets its own immediately preceding, actionable
  comment that explains what the developer did wrong and how to correct it.
  The comment is normally a `//` line; a one-line `/* ... */` comment is also
  accepted so an assertion inside a continued macro definition can carry the
  required comment. Write one `ASSERT` per line, repeat the comment for
  adjacent assertions, and keep assertion-specific recovery guidance next to
  the assertion rather than in public headers such as `Window.h`.
- Verify the placement rule with `python3 scripts/verify-assert-comments.py .`
  from the repository root; the focused CTests are
  `ava_tests.assert_comments_checker` and `ava_tests.assert_comments_source`.
  See `docs/development/cpp-safety-rules.md` for the full assertion policy.
