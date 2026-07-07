# AVA Agent Guide

AVA is a native C++23 terminal coding agent. Treat the codebase as a small systems program: keep interfaces narrow, preserve backend safety boundaries, and make local behavior easy to verify with CMake tests.

## Current Scope

- AVA agent work in this repo includes backend, terminal frontend, and TUI runtime work unless the user explicitly narrows the task.
- TUI work is in scope now. Implement real user-facing terminal behavior when the goal calls for it, while preserving backend safety boundaries for permissions, sessions, providers, tools, and process execution.
- TUI changes must stay testable. Prefer renderer/editor/event-state seams that can be exercised with normal CTest tests, then add terminal-backed smoke coverage for behavior that only exists in a real TTY.

## Source Map

- `src/main.cpp`: application entry point, CLI argument handling, OpenAI connect flow, TUI startup, and non-TTY line shell wiring.
- `src/ava/core/`: shared primitives such as `Result<T>`, errors, JSON helpers, and IDs.
- `src/ava/config/`: XDG paths, auth storage, model configuration, prompt configuration, and OpenAI OAuth support.
- `src/ava/provider/`: provider contracts plus the OpenAI provider and `curl` transport.
- `src/ava/agent/`: agent loop, mode handling, tool dispatch, user-question plumbing, configurable task subagents, and background job registry.
- `src/ava/app/`: runtime orchestration, CLI/TUI/print/RPC glue, command dispatch, project trust, headless policy, and event serialization.
- `src/ava/permissions/`: backend permission policy and prompt/decision types.
- `src/ava/tools/`: built-in file, search, and shell tools. Keep filesystem and process safety checks here or in clearly permissioned call paths.
- `src/ava/session/`: append-only JSONL session storage and session-level formatting/lifecycle helpers.
- `src/ava/context/`: project/global instruction loading for provider context.
- `src/ava/mcp/`: stdio MCP config, protocol, client lifecycle, tool/resource/prompt broker, and containment helpers.
- `src/ava/plugin/`: local out-of-process plugin manifest, discovery, enablement, runner, diagnostics, tool broker, and event hooks.
- `src/ava/lsp/`: LSP client and configured provider integration for diagnostics, symbols, definitions, and references.
- `src/ava/tui/`: custom terminal UI rendering, input handling, runtime glue, and terminal abstraction.
- `src/ava/desktop/`: optional Qt/QML desktop prototype.
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
- TUI/frontend plans are allowed when they are tied to implementation and verification. Do not stop at UI plans when the user asked for working behavior.
- Keep public headers focused on APIs needed across modules. Move test-only or implementation-only helpers out of production interfaces when practical.
- Add regression tests for safety-sensitive fixes, permission behavior, session persistence, provider parsing, and tool execution.
- Add regression tests for TUI/editor/rendering changes. Terminal-visible behavior should have either deterministic renderer tests, scripted terminal smoke tests, or a documented manual smoke with captured evidence when automation is not yet reliable.
- Format changed C++ with the repo `.clang-format` and keep `.clang-tidy` warnings actionable.

## TUI And Terminal Testing

- Start TUI verification at the smallest deterministic layer: text wrapping, width calculation, editor state, keybinding dispatch, palette/filter state, event reducers, permission/tool-card formatting, and transcript rendering should be covered by CTest unit tests where possible.
- For full terminal behavior, use a pseudo-terminal harness rather than plain pipes. A PTY smoke can set `TERM`, rows, columns, and environment variables, start `ava`, send keystrokes or escape sequences, resize the terminal, and assert on captured screen state and process exit.
- For ncurses-backed behavior, keep `newterm`/RAII lifecycle tests and add real terminal smokes only for behavior that requires a controlling terminal: alternate-screen cleanup, bracketed paste, resize redraw, mouse events, Escape latency, cursor visibility, Unicode cell placement, and terminal-state restoration after cancellation or crash.
- Prefer stable screen assertions over raw escape-sequence snapshots. Capture the terminal through a parser, tmux pane, or equivalent screen model, normalize timing-sensitive output, and assert visible text, cursor position, dimensions, and absence of leaked control sequences.
- Use visual artifacts when they add evidence: tmux captures, asciinema casts, or VHS-style scripted recordings are useful for reviewing complex flows, but they should supplement focused automated checks rather than replace them.
- Every TUI/frontend task should state what was tested for real before handoff. If a terminal smoke cannot run in the current environment, document the blocker and provide the exact command or script that should be run next.

## Reference Code

- Reference repositories are expected under `docs/reference-code/` and can contain their own `.git` directories.
- For Pi parity or comparison work, check `docs/reference-code/` first for the Pi reference repository; do not assume Pi is the only reference repo there.
- Use reference code only for product and behavior comparison. Do not copy architecture or source code into AVA.
- Do not include reference repositories in builds, tests, formatting, or source searches unless the task explicitly asks for reference analysis.
