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

- Add dependencies slowly and deliberately.
- Prefer mature, small, well-maintained libraries.
- Do not add framework-scale dependencies for isolated convenience.
- Dependencies must be usable from CMake without custom fragile setup.
- Every dependency should have a clear owner and purpose.

## Testing

- Add tests from the first implementation milestone.
- Prioritize tests for file edits, process execution, JSON parsing, config loading, session persistence, and permission decisions.
- Add regression tests for every safety bug.
- Keep fast unit tests separate from integration tests that spawn subprocesses or hit networks.
- Networked provider tests must be opt-in.

### Running Tests

```sh
cmake -S . -B build -DAVA_BUILD_TESTS=ON
scripts/build.sh --build-dir build
scripts/run-tests.sh --build-dir build
```

### Sanitizer Builds

```sh
cmake -S . -B build-sanitize -DAVA_ENABLE_SANITIZERS=ON -DAVA_BUILD_TESTS=ON
scripts/build.sh --build-dir build-sanitize --jobs 2
scripts/run-tests.sh --build-dir build-sanitize --jobs 2
```

## Review Checklist

Before merging C++ code, check:

- No raw owning pointers or manual memory management.
- No hidden global mutable state.
- No long-lived dangling-prone views/references.
- Fallible operations return explicit errors.
- File writes and process execution use the approved layers.
- Background work has cancellation and shutdown.
- Tests cover the risky behavior introduced.
- The change keeps AVA smaller and more understandable than the code it replaces.

## Principle

AVA C++ should use explicit ownership, bounded interfaces, and fail-closed
safety constraints. We will encode safety in APIs, tooling, tests, and review.
