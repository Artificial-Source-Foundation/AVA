# Execution Backend Boundary (B65)

## Purpose

Define the execution boundary used by AVA tools so command and file operations can move from a local-only model to pluggable backends (local, worktree-isolated, remote, containerized) without rewriting tool logic.

## Current Boundary

Primary trait: `ava_platform::Platform`.

Key methods:

- `read_file(path)`
- `write_file(path, content)`
- `create_dir_all(path)`
- `exists(path)`
- `is_directory(path)`
- `execute_with_options(command, ExecuteOptions)`
- `execute_streaming_with_options(command, ExecuteOptions)`

Convenience defaults:

- `execute(command)` delegates to `execute_with_options(..., ExecuteOptions::default())`
- `execute_streaming(command)` delegates to `execute_streaming_with_options(..., ExecuteOptions::default())`

`ExecuteOptions` currently carries:

- timeout
- working directory
- environment overrides

## Why This Matters

This boundary removes implicit shell behavior from tools (for example, composing `cd ... && ...` strings) and makes execution intent explicit via options.

Benefits:

- safer command construction
- clearer timeout and cwd semantics
- easier per-run execution isolation
- cleaner path to non-local backends

## Adopted in Sprint 63 (current slice)

Tools now using option-aware execution paths:

- `bash`
- `lint`
- `test_runner`
- `diagnostics`
- `git` (read-only)

File write path now uses platform directory creation (`create_dir_all`) instead of direct ad-hoc fs calls in tool code.

## Next Steps

1. Add a backend handle at run/session scope so each run can carry its own cwd/backend identity.
2. Route background-agent execution through run-scoped backend context (B39 prerequisite).
3. Add non-local backend implementations behind the same trait contract.
