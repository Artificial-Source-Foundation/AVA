# AVA Agent Guide

AVA is a native C++23 terminal coding agent. Treat the codebase as a small systems program: keep interfaces narrow, preserve backend safety boundaries, and make local behavior easy to verify with CMake tests.

## Current Scope

- AVA agent work in this repo is backend-only unless the user explicitly says otherwise.
- Carlo owns frontend/TUI planning and implementation. Do not create or follow frontend/TUI plans; backend work may expose semantic events, RPC, session, provider, and tool contracts that Carlo's frontend can consume.

## Source Map

- `src/main.cpp`: application entry point, CLI argument handling, OpenAI connect flow, TUI startup, and non-TTY line shell wiring.
- `src/ava/core/`: shared primitives such as `Result<T>`, errors, JSON helpers, and IDs.
- `src/ava/config/`: XDG paths, auth storage, model configuration, prompt configuration, and OpenAI OAuth support.
- `src/ava/provider/`: provider contracts plus the OpenAI provider and `curl` transport.
- `src/ava/agent/`: agent loop, mode handling, tool dispatch, and user-question plumbing.
- `src/ava/permissions/`: backend permission policy and prompt/decision types.
- `src/ava/tools/`: built-in file, search, and shell tools. Keep filesystem and process safety checks here or in clearly permissioned call paths.
- `src/ava/session/`: append-only JSONL session storage and session-level formatting/lifecycle helpers.
- `src/ava/context/`: project/global instruction loading for provider context.
- `src/ava/tui/`: custom terminal UI rendering, input handling, runtime glue, and terminal abstraction.
- `tests/`: focused test sources linked into the `ava_tests` CTest target, plus support fakes under `tests/support/`.

## Local Workflow

```sh
cmake -S . -B build -DAVA_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Preset equivalent:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Sanitizers:

```sh
cmake -S . -B build-sanitize -DAVA_ENABLE_SANITIZERS=ON -DAVA_BUILD_TESTS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

Before handing work off, also run:

```sh
git --no-pager diff --check
```

## Engineering Rules

- Follow `docs/engineering/cpp-safety-rules.md` for C++ work.
- Use C++23 and CMake.
- Prefer small modules, narrow interfaces, and explicit ownership.
- No raw owning pointers, manual `new`, or manual `delete` in application code.
- Use RAII and explicit `Result<T>`/`VoidResult` errors for fallible core APIs.
- Keep filesystem writes behind the approved file/session/config layers.
- Keep command execution behind the permissioned process/tool layer.
- Keep destructive operations behind explicit policy checks.
- Treat model output, terminal input, paths, JSON, session files, auth files, and shell text as untrusted.
- Preserve actionable error context: operation, path/provider/tool name, and underlying cause.

## Change Guidelines

- Prefer the smallest correct change over broad rewrites.
- Keep `main.cpp` from growing further when a change has a clear subsystem home.
- Keep TUI code as presentation/runtime glue; backend modules own permissions, sessions, provider messages, and tool semantics.
- Keep public headers focused on APIs needed across modules. Move test-only or implementation-only helpers out of production interfaces when practical.
- Add regression tests for safety-sensitive fixes, permission behavior, session persistence, provider parsing, and tool execution.
- Format changed C++ with the repo `.clang-format` and keep `.clang-tidy` warnings actionable.

## Reference Code

- Reference repositories are expected under `docs/reference-code/` and can contain their own `.git` directories.
- For Pi parity or comparison work, check `docs/reference-code/` first for the Pi reference repository; do not assume Pi is the only reference repo there.
- Use reference code only for product and behavior comparison. Do not copy architecture or source code into AVA.
- Do not include reference repositories in builds, tests, formatting, or source searches unless the task explicitly asks for reference analysis.
