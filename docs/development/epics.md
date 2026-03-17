# AVA Epics

> Last updated: 2026-03-16
> Related: `docs/development/roadmap.md`, `docs/development/backlog.md`, `docs/development/v3-plan.md`

## Completed Epics

### E1: Dynamic Model Intelligence (Sprints 53-55, 59)

| Sprint | Focus | Status |
|--------|-------|--------|
| 53 | Dynamic model catalog (models.dev fetch, whitelist, cache) | Complete |
| 54 | Thinking/reasoning mode (per-provider variants, `/think`, `Ctrl+T`) | Complete |
| 55 | Coding plan providers (Alibaba, ZAI, ZhipuAI, Kimi, MiniMax) | Complete |
| 59 | Provider mega: Copilot, verification, retry jitter, circuit breaker wiring, compiled-in model registry, rich `StreamChunk`, Alibaba fixes | Complete |

### E2: Codebase Quality (Sprints 56-58)

| Sprint | Focus | Status |
|--------|-------|--------|
| 56 | Quality audit (unwrap, tests, docs, modularity, perf, hygiene) | Complete |
| 57 | Quality fixes: P0 panics/tests plus P1 docs/modularity | Complete |
| 58 | Modal system revamp (`SelectList`, scroll fixes, visual polish) | Complete |

### E3: Streaming and Session UX (Sprint 60)

| Sprint | Focus | Status |
|--------|-------|--------|
| 60 | Streaming tool calls, session/context UX, project instructions, workflow polish | Complete |

### E4: Reliable Edit Loop (Sprint 61)

| Sprint | Focus | Status |
|--------|-------|--------|
| 61 | `B67`, `B54`, `B37`, `B66` safer edit -> validate -> commit flow | Implemented and archived |

### E5: Cost and Runtime Controls (Sprint 62)

| Sprint | Focus | Status |
|--------|-------|--------|
| 62 | `B64`, `B63`, `B47`, `B40` stronger budgeting, credential refresh, routing, and cost visibility | Implemented, validated, archived via Sprint 62V |

## Completed v3 Backend Epics

### E6: Execution and Ecosystem Foundations (Sprint 63) -- COMPLETE

| Item | Description |
|------|-------------|
| `B65` | Pluggable backend operations |
| `B39` | Background agents on branches |
| `B61` | Dev tooling setup |
| `B71` | Skill discovery |
| `B45` | File watcher mode |

### E7: Knowledge and Context Intelligence (Sprint 64) -- COMPLETE

| Item | Description |
|------|-------------|
| `B38` | Auto-learned project memories |
| `B57` | Multi-repo context |
| `B58` | Semantic codebase indexing |
| `B48` | Change impact analysis |

### E8: Agent Coordination Backend (Sprint 65) -- COMPLETE

| Item | Description |
|------|-------------|
| `B49` | Spec-driven development |
| `B59` | Agent artifacts system |
| `B50` | Agent team peer communication |
| `B76` | Agent Client Protocol (ACP) |

### E9: Optional Capability Backends (Sprint 66) -- COMPLETE

| Item | Description |
|------|-------------|
| `B44` | Web search capability (Extended) |
| `B52` | AST-aware operations (Extended) |
| `B53` | Full LSP exposure (Extended) |
| `B69` | Code search tool (plugin/MCP) |

## Completed v3 Frontend and UX Epics

All delivered. See `docs/development/v3-plan.md` for details.

| Epic | Focus | Paired Sprint |
|------|-------|---------------|
| FE-D | Praxis chat UX and worker visibility, including `B26` | 65 |
| FE-A | Ambient awareness (`context %`, modular footer, duration/cost visibility) — largely delivered, follow-through only | 62 |
| FE-B | Conversation clarity (tool grouping, inline diffs, quieter streaming) — partially delivered, polish remains | 62 |
| FE-C | Session and history UX (search, rewind preview, stats) | 63 |
| FE-E | Input and discoverability (shortcuts, richer command discovery, long-input polish) | 64 |
| FE-F | Desktop parity follow-through for proven TUI patterns | 66 |
