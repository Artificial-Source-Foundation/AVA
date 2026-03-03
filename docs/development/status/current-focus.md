# Current Focus

**Last Updated:** 2026-03-02
**Active Sprint:** Sprint 19 complete. Backend at 95%+ feature parity. Next: Praxis quality fixes + Desktop integration.

## Completed Recently

- **Sprint 19 — Backend Completion Sprint** — 28 items across 4 phases, 360+ new tests
  - Phase 1 (Core Loop): parallel tools, vision, truncation, background shell (3 tools), tool notifications, MCP health, compaction, steering
  - Phase 2 (Praxis E2E): orchestrator, task routing, domain tool filtering, result aggregation, error recovery, parallel agents
  - Phase 3 (Competitive): git tools (4), checkpoints, permission modes (5), MCP HTTP streaming, auto-learn memory, model fallback, global doom loop, toolshim
  - Phase 4 (Plugins): install/uninstall backend, catalog API
  - Smoke tested end-to-end with Sonnet 4.6 via CLI — Praxis hierarchy active, 39 tests pass in generated project
- **Sprint 18 — CLI Reliability** — compaction threshold, message normalization, session resume, instructions wiring
- **Sprint 17 — Backend Completion** — LSP, memory, MCP advanced, SQLite sessions, symbol extraction
- **Sprint 16 (Praxis)** — 3-tier agent hierarchy (13 agents, tier-aware delegation, import/export)
- **All competitive gaps closed** — Current snapshot: 55+ tools, 30+ extensions, 16 providers, ~4,280 tests

## Current Stats

| Metric | Count |
|--------|-------|
| Tools | 55+ (core-v2 + extensions + desktop/CLI integrations) |
| Extensions | 30+ active |
| LLM Providers | 16 (all tested) |
| Tests | ~4,280 passing across ~270 files |
| Agents | 13 built-in (1 commander, 4 leads, 8 workers) |

## Top Priorities

1. **Praxis quality fixes** (Tier 6.5) — child agent cwd scoping, flat mode fallback for simple tasks, shared file cache across tiers
2. **Manual QA pass** — Linux DEs (GNOME, KDE, Cosmic), light mode, Tauri desktop runtime
3. **Desktop integration** (Tier 7) — Wire core-v2 into Tauri bridge, stream events to SolidJS UI
4. **Remaining Tier 3 gaps** — file @mentions, session export
5. **Phase 3 prep** — CLI polish, docs site planning

Active execution docs:

- `docs/frontend/backlog.md`
- `docs/backend/BACKLOG.md`
- `docs/ROADMAP.md`

## Blockers

- Provider credentials and callback reliability for manual OAuth runtime validation.
- Praxis cwd scoping issue (P-01/P-02) needs fixing before desktop integration.
