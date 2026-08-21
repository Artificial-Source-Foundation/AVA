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
- Every `ASSERT` that checks for an API contract violation must have its own
  immediately preceding, actionable comment explaining what the developer did
  wrong and how to fix it. Keep assertion-specific recovery guidance next to
  the assertion rather than in public headers such as `Window.h`.
- Use internal-invariant `ASSERT`s sparingly and never as a substitute for
  reasoning about or testing the code. They may be useful in complex code when
  an invariant violation would otherwise be difficult to detect. Precede each
  such `ASSERT` with a comment describing the invariant and why it must hold.
  The comment may begin with `// Paranoia check:` to emphasize that the
  assertion is believed to be impossible to trigger in correct code.
- Verify the placement rule with `python3 scripts/verify-assert-comments.py .`
  from the repository root; the focused CTests are
  `ava_tests.assert_comments_checker` and `ava_tests.assert_comments_source`.
  See `docs/development/cpp-safety-rules.md` for the full assertion policy.
