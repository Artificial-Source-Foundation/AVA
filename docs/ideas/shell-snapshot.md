# Shell Environment Snapshot

> Status: Idea (not implemented)
> Source: Codex CLI
> Effort: Low

## Summary
Captures the user's shell environment (env vars, cwd, shell) at session start. Can be persisted to disk and restored on session resume for consistent tool execution context. Includes diffing between snapshots to detect environment changes.

## Key Design Points
- `ShellSnapshot` with BTreeMap of env vars, cwd, shell path, and capture timestamp
- 12 excluded sensitive variables: SSH keys, AWS secrets, API keys, database URLs
- Variables with values >4096 chars skipped
- `diff()` computes added, removed, changed variables and cwd changes between snapshots
- JSON serialization for persistence via `save()`/`load()`
- `context_summary()` generates compact text with CWD, shell, and 10 useful env vars (LANG, TERM, EDITOR, GOPATH, etc.)

## Integration Notes
- Would capture at session start and inject summary into agent context
- Could detect environment drift between sessions and warn the user
- The startup context module (also archived) would consume this data
