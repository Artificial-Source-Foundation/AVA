# Current Focus

**Last Updated:** 2026-02-28
**Active Sprint:** Gap Analysis complete. Next: Manual QA + Plugin Registry API + Phase 3 prep.

## Completed Recently

- **Gap Analysis Sprint** — 20 frontend items + backend extensions across 10 batches. All done.
  - Frontend: pause removal, delegation UI, doom loop banner, bento cards, skill CRUD, session tree, memory browser, trusted folders, marketplace UX, OAuth, plugin wizard
  - Backend: session storage, LSP client, MCP OAuth/reconnect, symbol extractor, memory extension, 10 provider tests, validator tests
- **Sprint 16 (Praxis)** — 3-tier agent hierarchy (13 agents, tier-aware delegation, import/export)
- **Sprint 2.3 (Plugin UX)** — Marketplace sort/ratings/downloads, publish stub, creation wizard, hot reload
- **All P0-P3-C competitive gaps** — Delivered (31 items from 7-tool audit)
- **PI Parity** — Provider switching, session branching tree, minimal tool mode, runtime skill creation all done

## Top Priorities

1. Manual QA pass — Linux DEs (GNOME, KDE, Cosmic), light mode, Tauri desktop runtime
2. Manual OAuth runtime matrix — OpenAI, Anthropic, Copilot connect/disconnect/send flows
3. Plugin registry API backend — Frontend UI is ready, backend needed
4. Phase 3 prep — CLI polish, docs site planning

Active execution docs:

- `docs/frontend/backlog.md`
- `docs/backend/backlog.md`
- `docs/ROADMAP.md`

## Blockers

- Provider credentials and callback reliability for manual OAuth runtime validation.
