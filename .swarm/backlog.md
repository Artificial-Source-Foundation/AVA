# Swarm Backlog — 2026-03-20

## Recently Completed
1. **Backend/runtime hardening** — fixed streamed tool-call ordering, bounded read-only tool concurrency, tightened retry stop conditions, corrected context summary tracking, hardened session persistence, and made in-memory SQLite tests/runtime deterministic.
2. **Tooling SOTA pass** — parallelized `grep`, removed `glob` metadata sort tax, added file-not-found suggestions for `read`/`edit`, improved edit candidate selection and diagnostics, and hardened custom/web tool runtime safety.

## Critical
1. **Tool calls failing — parameter name mismatch** — `read` rejects "missing required parameter 'path'", `glob` rejects "missing required parameter 'pattern'". Model sends args but validation fails.

## High
2. **Cancel deletes all messages** — Cancelling an agent run wipes the entire conversation. Should preserve everything up to the cancel point. OpenCode stops in place.

## Medium
3. **Thinking + tools should interleave in UI** — When thinking model calls tools, show: Thinking → tool calls inline → Thinking resumes. OpenCode pattern.
4. **Compare chat UI against Goose + OpenCode** — Enhance tool display, message layout, streaming UX based on competitor best practices.
