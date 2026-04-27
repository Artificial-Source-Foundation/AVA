# AVA Agent Guide

AVA is being rebuilt from zero as a lean native C++ agentic coding tool.

## Current Direction

- Build one C++23 CLI binary first.
- Keep the backend correct before polishing the TUI.
- Start with OpenAI as the only provider.
- Implement `build` and `plan` modes with backend-enforced permissions.
- Use pi-mono and OpenCode only as product/behavior references, not as code to copy.

## 0.1 Target

- Simple terminal interface.
- Append-only JSONL sessions.
- Permissioned tools for file reads, file writes, exact edits, glob, grep, patching, questions, and shell commands.
- A provider abstraction with OpenAI wired first.
- Tests around sessions, permissions, tools, filesystem writes, and process execution.

## Current 0.1 Status

- Complete: provider/model/auth foundation, agent tool loop, session resume, simple TUI composer, and XDG config/state paths.
- Current hardening focus: keep docs accurate, keep tests/sanitizers passing, and avoid adding post-0.1 subsystems.
- Known deferrals: multiple providers, plugins, MCP, compaction, full permission modals, LSP, web fetch, and session tree UI.

## Engineering Rules

- Follow `docs/engineering/cpp-safety-rules.md` for all C++ work.
- Use C++23 and CMake.
- Prefer small modules and narrow interfaces.
- No raw owning pointers, manual `new`, or manual `delete` in application code.
- Use RAII and explicit `Result<T>`/`VoidResult` errors for fallible core APIs.
- All filesystem writes must go through the approved file tool layer.
- All command execution must go through the approved permissioned process layer.
- Keep destructive operations behind explicit policy checks.
- Treat model output, terminal input, paths, JSON, and shell text as untrusted.

## Collaboration Notes

- The `zero` branch intentionally deleted the old codebase. Do not restore old tracked files unless explicitly asked.
- Reference repositories live in `docs/reference-code/` and may contain their own `.git` directories.
- Planning documents live in `docs/product/`, `docs/engineering/`, and `docs/versions/`.
- Before substantial C++ changes, check the safety rules and keep the implementation minimal.
- After implementation milestones, run a reviewer agent against the C++ safety rules before considering the work complete.
