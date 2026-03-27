# Swarm Backlog — 2026-03-20

## Recently Completed
1. **Backend/runtime hardening** — fixed streamed tool-call ordering, bounded read-only tool concurrency, tightened retry stop conditions, corrected context summary tracking, hardened session persistence, and made in-memory SQLite tests/runtime deterministic.
2. **Tooling SOTA pass** — parallelized `grep`, removed `glob` metadata sort tax, added file-not-found suggestions for `read`/`edit`, improved edit candidate selection and diagnostics, and hardened custom/web tool runtime safety.
3. **Tool-adjacent infrastructure hardening** — `web_fetch` now fails closed on blocked redirects and caps streamed response bytes, backup version discovery now survives sparse histories, and snapshot git operations ignore inherited `GIT_*` environment leakage.
4. **Docs tree cleanup** — collapsed the docs entrypoint down to the current live references, updated `CLAUDE.md` links, and removed large stale duplicate doc trees that no longer match the repo layout.
5. **TUI hardening pass** — fixed multi-line side-by-side diff pairing, supported diff rendering without summary preambles, removed 16-bit overflow from chat scroll state, and trimmed an extra full-vector drain from per-frame message rendering.

## Critical
1. **Tool calls failing — parameter name mismatch** — `read` rejects "missing required parameter 'path'", `glob` rejects "missing required parameter 'pattern'". Model sends args but validation fails.

## High
2. **Cancel deletes all messages** — Cancelling an agent run wipes the entire conversation. Should preserve everything up to the cancel point. OpenCode stops in place.

## Medium
3. **Thinking + tools should interleave in UI** — When thinking model calls tools, show: Thinking → tool calls inline → Thinking resumes. OpenCode pattern.
4. **Compare chat UI against Goose + OpenCode** — Enhance tool display, message layout, streaming UX based on competitor best practices.
