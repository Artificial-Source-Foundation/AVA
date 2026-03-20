# Swarm Backlog — 2026-03-20

## Critical
1. **Tool calls failing — parameter name mismatch** — `read` rejects "missing required parameter 'path'", `glob` rejects "missing required parameter 'pattern'". Model sends args but validation fails.

## High
2. **Cancel deletes all messages** — Cancelling an agent run wipes the entire conversation. Should preserve everything up to the cancel point. OpenCode stops in place.

## Medium
3. **Thinking + tools should interleave in UI** — When thinking model calls tools, show: Thinking → tool calls inline → Thinking resumes. OpenCode pattern.
4. **Compare chat UI against Goose + OpenCode** — Enhance tool display, message layout, streaming UX based on competitor best practices.
