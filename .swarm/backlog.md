# Swarm Backlog — 2026-03-20

## Recently Completed
1. **Backend/runtime hardening** — fixed streamed tool-call ordering, bounded read-only tool concurrency, tightened retry stop conditions, corrected context summary tracking, hardened session persistence, and made in-memory SQLite tests/runtime deterministic.
2. **Tooling SOTA pass** — parallelized `grep`, removed `glob` metadata sort tax, added file-not-found suggestions for `read`/`edit`, improved edit candidate selection and diagnostics, and hardened custom/web tool runtime safety.
3. **Tool-adjacent infrastructure hardening** — `web_fetch` now fails closed on blocked redirects and caps streamed response bytes, backup version discovery now survives sparse histories, and snapshot git operations ignore inherited `GIT_*` environment leakage.
4. **Docs tree cleanup** — collapsed the docs entrypoint down to the current live references, updated `CLAUDE.md` links, and removed large stale duplicate doc trees that no longer match the repo layout.
5. **TUI hardening pass** — fixed multi-line side-by-side diff pairing, supported diff rendering without summary preambles, removed 16-bit overflow from chat scroll state, and trimmed an extra full-vector drain from per-frame message rendering.
6. **Desktop streaming hardening** — tool events now carry stable IDs through the Tauri bridge, approval state can target the exact tool call, and the browser API client now unwraps GET args cleanly without leaking path params back into the query string.
7. **Desktop reconnect hardening** — browser-mode WebSocket reconnects now ignore stale socket callbacks, trajectory events keep stable timestamps once ingested, and targeted frontend tests cover reconnect/event ordering regressions.
8. **Desktop state lifecycle hardening** — streaming event history is now bounded to prevent long-session growth, and session store window listeners are rebound safely so reloads do not accumulate duplicate handlers.
9. **Desktop session race hardening** — stale async session/message loads are now ignored during rapid switching, and remaining global desktop listeners use replaceable bindings so HMR does not stack duplicate handlers.
10. **Desktop listener/timer hardening** — prompt/diagnostic listeners now clean up more predictably, delayed deep-link plugin registration no longer leaks cleanup handlers, and chat/tool cards now share a single elapsed-time ticker instead of many per-card intervals.
7. **Instruction/delegation DX pass** — `.ava/rules` and contextual `AGENTS.md` now activate on direct file work without repeated spam, hidden subagents only unlock on broader tasks with explicit budgets and read-only specialist profiles, benchmark support is split into workspace/validation modules, benchmark mode now has `--task-filter` plus deterministic fallback answers for question prompts, and benchmark output surfaces delegation-heavy scenarios and helper-cost details more clearly.

## Critical
1. **Tool calls failing — parameter name mismatch** — `read` rejects "missing required parameter 'path'", `glob` rejects "missing required parameter 'pattern'". Model sends args but validation fails.

## High
2. **Cancel deletes all messages** — Cancelling an agent run wipes the entire conversation. Should preserve everything up to the cancel point. OpenCode stops in place.

## Medium
3. **Thinking + tools should interleave in UI** — When thinking model calls tools, show: Thinking → tool calls inline → Thinking resumes. OpenCode pattern.
4. **Compare chat UI against Goose + OpenCode** — Enhance tool display, message layout, streaming UX based on competitor best practices.
