# C++ Milestone 6 Boundaries

This note records what is implemented for the C++ `ava_tools` core-tool-system slice and what remains intentionally deferred.

## Implemented in Milestone 6

1. New `ava_tools` static library wired into `ava_runtime`.
2. Real tool-system core primitives:
   - `Tool` interface (name/description/JSON-schema parameters/execute)
   - `ToolRegistry` (registration, listing, tier/source tracking, middleware chain)
   - execution path with tool input backfill hook and call-id normalization to the incoming `ToolCall.id`
3. Retry behavior aligned to scoped Rust M6 behavior:
   - `MAX_RETRIES = 2` (three attempts total)
   - backoff timings `100ms`, `200ms`
   - retry-only for read-only tool names (`read`, `glob`, `grep`, `git`, `git_read`)
   - transient/permanent error heuristics matching the Rust helper intent
4. Simplified permission middleware seam:
   - `Allow / Deny / Ask` inspection outcomes
   - approval bridge with `Allowed / AllowedForSession / AllowAlways / Rejected`
   - fail-closed behavior when approval is required and no bridge is attached
5. Supporting helpers used by tools:
   - workspace path guard (`enforce_workspace_path`) with workspace-boundary checks
   - file-backup session (`.ava/file-history-m6/<session>/...` backups before write/edit)
   - output fallback truncation helper
6. Real core tool implementations:
   - `read`
   - `write`
   - `edit` (honest narrowed strategy: exact replace + `replace_all` only)
   - `bash`
   - `glob`
   - `grep`
   - `git` and `git_read` (same read-only behavior under both names)
7. Focused Catch2 coverage for registry/middleware/retry basics and core tool behavior.

## Explicitly Deferred

1. Plugin-manager hook integration and plugin-provided tool registration lifecycle.
2. MCP bridge/tool ingestion.
3. Browser/desktop automation tool surfaces.
4. `web_fetch` / `web_search` implementations (not registered as real defaults in this milestone).
5. Full Rust edit-engine parity (hashline, fuzzy cascade, advanced recovery).
6. Runtime-agent orchestration integration beyond exposing `ava_tools` via `ava_runtime` composition.
7. Full `ava_platform` unification for every tool-side filesystem/process operation; the current Milestone 6 slice still owns scoped local file/process execution behavior directly inside `ava_tools`.

Milestone 6 is intentionally partial and honest: it lands a real C++ core tool system and practical local core tools without over-claiming plugin/MCP/web/browser/runtime parity.
