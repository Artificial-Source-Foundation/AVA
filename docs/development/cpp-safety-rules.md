# AVA C++ Safety Rules

AVA is a native C++ agentic coding tool. The core runtime will execute commands,
edit files, manage sessions, stream model output, and handle untrusted text. C++
gives us speed and control, but only if we keep the codebase strict.

These rules define the C++ subset we will use.

## Baseline

- Use C++23 for new code.
- Use CMake as the only build system.
- Treat compiler warnings as errors in CI.
- Format all code with `clang-format`.
- Run `clang-tidy` on core code.
- Run AddressSanitizer and UndefinedBehaviorSanitizer in the test preset.
- Prefer small modules with narrow interfaces over framework-style abstractions.

## Ownership

- Never use raw owning pointers.
- Never use manual `new` or `delete` in application code.
- Prefer values, RAII types, and `std::unique_ptr` for explicit ownership.
- Use `std::shared_ptr` only when shared ownership is genuinely required.
- Document every use of `std::shared_ptr` with the ownership reason.
- Do not store references in long-lived structs/classes unless the lifetime is obvious and documented.
- Do not store `std::string_view`, `std::span`, or iterators in long-lived objects.

## Errors

- Core APIs should return explicit result types for fallible operations.
- Prefer `std::expected<T, Error>` or an AVA `Result<T>` alias once established.
- Do not throw exceptions across subsystem boundaries.
- Preserve actionable error context: operation, path/provider/tool name, and underlying cause.
- Do not collapse distinct failures into generic `false`, `nullptr`, or empty string returns.

## Assertions

- Use `ASSERT` (cwds, via `debug.h`) only for programmer errors and internal invariants.
- Never assert on recoverable conditions, untrusted input, or runtime failures; those belong on explicit `Result<T>`/`VoidResult` error paths.
- Keep `ASSERT` predicates side-effect-free: release builds may omit assertions, and correctness must never depend on their evaluation.
- Every `ASSERT` that checks for an API contract violation must have an immediately preceding, actionable comment explaining what the developer did wrong and how to fix it. Write one such comment per `ASSERT`, and keep assertion-specific recovery guidance next to the assertion rather than in public headers.
- Use internal-invariant `ASSERT`s sparingly and never as a substitute for reasoning about or testing the code. They may be useful in complex code when an invariant violation would otherwise be difficult to detect. Precede each such `ASSERT` with a comment describing the invariant and why it must hold. The comment may begin with `// Paranoia check:` to emphasize that the assertion is believed to be impossible to trigger in correct code.
- Verify comment placement with `python3 scripts/verify-assert-comments.py .`; the focused CTests are `ava_tests.assert_comments_checker` and `ava_tests.assert_comments_source`. Reviewers still judge whether contract comments are actionable and invariant comments explain why the checked property must hold.

## State

- Avoid global mutable state.
- Keep mutable state owned by one component.
- Prefer immutable config snapshots passed into runtime components.
- Make session state explicit and serializable.
- Do not hide state transitions behind callbacks with implicit side effects.

## Concurrency

- Prefer message passing over shared mutable state.
- Use RAII handles for threads, processes, timers, and cancellation scopes.
- Every background task must have an owner and a shutdown path.
- Every subprocess must support timeout, cancellation, and output limits.
- Do not detach threads.
- Do not share mutable state across threads without a clearly owned synchronization strategy.

## Filesystem And Process Safety

- All filesystem writes must go through one path-safe file layer.
- All shell/process execution must go through one permissioned executor.
- Do not build shell commands through ad hoc string concatenation.
- Prefer argv-style process execution over shell interpretation.
- Require explicit working directories for tool execution.
- Normalize and validate paths before writes, deletes, or moves.
- Keep destructive operations behind explicit policy checks.

## Strings And Views

- Use `std::string` for owned text.
- Use `std::string_view` only for short-lived, non-stored parameters.
- Do not return views into temporary or mutable buffers.
- Be careful with model output, terminal escape sequences, paths, and JSON strings; treat them as untrusted input.

## Data Modeling

- Use strong types for concepts that carry risk: paths, command args, session ids, model ids, provider ids, tool names.
- Use `enum class` instead of unscoped enums.
- Use `std::variant` for closed sets of event/result types.
- Avoid boolean parameter pairs; use named option structs when meaning is not obvious.
- Keep DTOs simple and serializable.

## Dependencies

- Add dependencies slowly and deliberately, and ask before introducing a new production dependency.
- Prefer mature, small, well-maintained libraries.
- Do not add framework-scale dependencies for isolated convenience.
- Dependencies must be usable from CMake without custom fragile setup.
- Pin every dependency exactly and review its license and provenance; shipped dependencies must be covered by `THIRD_PARTY_NOTICES.md`.
- Every dependency should have a clear owner and purpose.

## Testing

- Add tests from the first implementation milestone.
- Prioritize tests for file edits, process execution, JSON parsing, config loading, session persistence, and permission decisions.
- Add regression tests for every safety bug.
- Keep fast unit tests separate from integration tests that spawn subprocesses or hit networks.
- Keep default tests credential-free and offline. Networked provider tests must be opt-in, and validation for normal development must never require paid live-provider calls.
- Match completion checks to the change's scope: focused CTest filters for the touched subsystem, clang-format on changed C/C++ files, and only the repository gates the change can affect.

### Running Tests

Normal development uses a `BetaTest` build with `EnableDebug=ON`, keeping Release-style optimization and assertions together with AVA's libcwd instrumentation:

```sh
export GITACHE_ROOT="${GITACHE_ROOT:-$HOME/.cache/ava/gitache}"
mkdir -p "$GITACHE_ROOT"
cmake -S . -B build -DAVA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=BetaTest -DEnableDebug=ON
scripts/build.sh --build-dir build
scripts/run-tests.sh --build-dir build
```

### Sanitizer Builds

ASan/UBSan are test diagnostics, not a production security boundary:

```sh
cmake -S . -B build-sanitize -DAVA_ENABLE_SANITIZERS=ON -DAVA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=BetaTest -DEnableDebug=ON
scripts/build.sh --build-dir build-sanitize --jobs 2
scripts/run-tests.sh --build-dir build-sanitize --jobs 2
```

## Review Checklist

Before merging C++ code, check:

- No raw owning pointers or manual memory management.
- No hidden global mutable state.
- No long-lived dangling-prone views/references.
- Fallible operations return explicit errors.
- Assertions are side-effect-free and have preceding contract or internal-invariant comments appropriate to what they check.
- File writes and process execution use the approved layers.
- Background work has cancellation and shutdown.
- Tests cover the risky behavior introduced.
- The change keeps AVA smaller and more understandable than the code it replaces.

## Authoritative References

These external documents motivate the assertion and sanitizer rules above; they are rationale, not a wholesale import of external rule sets:

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) (assertions express programmer-error expectations, not runtime error handling).
- [CERT MSC11-C](https://wiki.sei.cmu.edu/confluence/display/c/MSC11-C.+Incorporate+diagnostic+tests+using+assertions) (assertions are diagnostic and must be side-effect-free).
- [cppreference `assert`](https://en.cppreference.com/w/cpp/error/assert.html) (standard assertion semantics; disabled in `NDEBUG` builds).
- [Clang AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html) and [Clang UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) (test-time diagnostics).

## Principle

AVA C++ should use explicit ownership, bounded interfaces, and fail-closed
safety constraints. We will encode safety in APIs, tooling, tests, and review.
