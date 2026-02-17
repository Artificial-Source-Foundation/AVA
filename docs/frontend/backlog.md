# Frontend Backlog

> What's missing, prioritized. Updated 2026-02-15.

---

## Status Summary

| Phase | Status | Remaining |
|-------|--------|-----------|
| **1: Desktop App** | **Complete** | - |
| **1.5: Desktop Polish** | **Complete** | Manual testing only |
| **2: Plugin Ecosystem** | In progress | UX baseline shipped; runtime validation + parity gaps pending |
| **2+: Competitive Gaps** | Mostly complete | Focus moved to verification + plugin UX |

## Ownership Rules

- Source of truth here: frontend-only scope and UI deliverables.
- Cross-cutting frontend-backend work is tracked in `docs/development/backlogs/integration-backlog.md`.
- Roadmap and sprint docs should reference this backlog instead of duplicating frontend status details.

---

## Phase 1.5 — Manual Testing (Ready)

These require running `npm run tauri dev` and manually verifying:

- [ ] Test full app flow (chat, tools, settings, sessions)
- [ ] Verify keyboard shortcuts (Ctrl+B, Ctrl+,, Ctrl+M, Ctrl+N)
- [ ] Test on multiple Linux DEs (GNOME, KDE, Cosmic)
- [ ] Test light mode across all components
- [ ] Test density settings (compact, default, comfortable)
- [ ] Test file explorer with real project directories
- [ ] Test agent persistence (create agent, switch session, verify loaded)

---

## Phase 2+ — Competitive Gaps (Status Normalized)

These gaps were prioritized previously and are now mostly delivered based on changelog + code audit.

### Delivered
- [x] Git auto-commit on AI edits
- [x] Weak/secondary model support
- [x] Streaming tool preview
- [x] File watcher + AI comment patterns
- [x] Architect/editor model split
- [x] Message queue + steering
- [x] Session step-level undo

### Remaining hardening
- [x] Add automated tests for queue/steer/cancel, watcher-triggered flow, and OAuth edge cases (Sprint 1.6)
- [x] Stabilize streaming jitter/flicker and chat overflow behavior in desktop runtime
- [ ] Expand manual Tauri validation on Linux DE variants and light mode polish

### Next execution queue
- [x] Implement session auto-title UX validation and polish
- [x] Execute benchmark-derived frontend gaps FG-001/FG-002/FG-003 (chat git strip, usage details dialog, plugin metadata/trust pass)
- [x] Land inline approval-state UX baseline (FG-005)
- [ ] Final manual QA pass for chat stream UX across long sessions
- [ ] Complete Sprint 2.3 plugin UX runtime wiring/validation from `docs/development/sprints/2026-S2.3-plugin-ux-wiring.md`
- [ ] Complete benchmark-derived frontend gaps FG-004 remainder, FG-006, and FG-007 from `docs/development/status/frontend-gap-matrix-2026-02-15.md`
- [ ] Wire plugin install/uninstall to real backend lifecycle APIs (replace local mock adapter)

---

## Phase 2 — Plugin Ecosystem (THE DIFFERENTIATOR)

This is what makes AVA "The Obsidian of AI Coding".

### Sprint 2.1: Plugin Format & SDK
- [ ] Define unified plugin manifest (skills + commands + hooks + MCP in one package)
- [ ] Plugin SDK with TypeScript types and helpers
- [ ] Plugin lifecycle (install, enable, disable, uninstall, reload)
- [ ] Plugin sandboxing (what plugins can/can't access)
**Frontend**: None yet (backend-first)

### Sprint 2.2: Plugin Development Experience
- [ ] `ava plugin init` scaffold command
- [ ] Hot reload during plugin development
- [ ] Plugin testing utilities
- [ ] Plugin documentation template
**Frontend**: Plugin dev panel showing reload status, logs

### Sprint 2.3: Built-in Marketplace UI
- [x] Plugin browser in sidebar/settings surfaces
- [x] Settings-only plugin manager surface (replace Plugins placeholder)
- [x] Search + category-aware filtering in settings manager
- [x] Install/uninstall + enable/disable controls in settings manager
- [x] Plugin detail/settings panel in settings manager
- [x] Metadata/trust/version/changelog fields surfaced in plugin cards/details
- [ ] Featured plugin catalog curation + remote source integration
- [ ] Wire settings manager actions to real backend extension lifecycle APIs (tracked as `INT-001`/`INT-002`/`INT-003` in `docs/development/backlogs/integration-backlog.md`)
**Frontend**: Settings tab plugin manager, search, install flow, detail/settings view. Shipped with shared `plugins` store and mock lifecycle adapter.

---

## Frontend Gaps Currently Open

- **FG-004 (partial):** long-session performance still needs render-window/backfill hardening validation for very large histories.
- **FG-006:** session share/export UX is still not implemented.
- **FG-007:** panel adaptability (draggable/persisted panel ratios) is still limited.
- **INT-001/INT-002/INT-003 closeout:** plugin lifecycle wiring exists, but runtime validation and failure-path evidence are still open in sprint docs.
- **Manual QA closeout:** Linux DE matrix and light-mode regression pass still required.

### Goose parity checklist (prioritized)

Reference baseline: `docs/reference-code/goose/ui/desktop`.

#### P0 - high impact
- [ ] Implement conversation/session share UX (create share link, open shared session) and lightweight export path (`FG-006`).
- [ ] Add explicit user-triggered compaction UX in chat ("compact now" action + clear compaction state feedback).
- [ ] Add in-chat search UX (find in conversation, next/previous navigation, keyboard path).

#### P1 - maturity and ergonomics
- [ ] Expand plugin UX beyond settings manager: per-chat plugin selection + stronger install/load warning/error affordances.
- [ ] Improve long-session context ergonomics with clearer context-pressure alerts and validation against very large histories (`FG-004` remainder).
- [ ] Increase panel adaptability (split ratio persistence + richer panel layout controls) (`FG-007`).

#### P2 - differentiation opportunities
- [ ] Evaluate voice dictation input workflow (microphone input in `MessageInput`) as optional UX parity item.
- [ ] Evaluate recipe/schedule/app-launcher style automation surfaces for user workflows.

### Sprint 2.4: Plugin Distribution
- [ ] Publish plugins from GitHub repos
- [ ] Plugin registry API
- [ ] Version management and updates
- [ ] Community ratings and reviews
**Frontend**: Publish flow in settings, update notifications

### Sprint 2.5: Starter Plugins
- [ ] 5-10 built-in plugins demonstrating the system
- [ ] Example: "React Patterns" skill plugin
- [ ] Example: "/deploy" command plugin
- [ ] Example: "Auto-commit" hook plugin
**Frontend**: Plugin showcase page

---

## Phase 3+ — Longer Term

| Feature | Effort | Frontend Impact |
|---------|--------|----------------|
| Sandbox / container execution | 2-3 weeks | Toggle in settings, status indicator |
| Tree-sitter for 100+ languages | 2 weeks | Better code highlighting, symbol extraction |
| Voice input | 2 weeks | Microphone button in MessageInput |
| CLI polish | 1-2 weeks | None (CLI-only) |
| ACP editor integration | 2 weeks | Minimal (backend protocol) |
| A2A agent network | 2 weeks | Agent discovery UI, remote agent cards |

---

## What's Complete (No Work Needed)

These were identified as gaps but are now fully implemented:

| Feature | Session | Status |
|---------|---------|--------|
| Checkpointing / time-travel undo | 40 | createCheckpoint, rollbackToCheckpoint, UI |
| Cost & token tracking | 44 | Per-message tokens+cost in bubbles, session total in ContextBar |
| Vision / image support | Multiple | Paste, drop, base64, multimodal API, inline display |
| Iterative lint-fix loop | 44 | autoFixLint setting, biome/eslint after edits, errors fed back |
| Memory recall | 45 | recallSimilar + procedural recall injected into system prompts |
| Auto-compaction | 45 | Sliding window when context > 80%, syncs state + DB + tracker |
| File explorer | 45 | Recursive tree, lazy-load, Tauri FS |
| Code editor file reading | 45 | readFileContent via Tauri FS, auto-open from explorer |
| Agent persistence | 45 | DB CRUD (save, get, update), wired in session store |
| Google models API | 45 | Dynamic fetch with hardcoded fallback |
| DiffViewer split view | 45 | buildSplitPairs, two-column rendering |
| Dark/light/system theme | 41 | With midnight + charcoal dark variants |
| 6 accent colors + custom hex | 41 | hexToAccentVars computes all accent vars |
| 6 code themes | 41 | Via data-code-theme attribute |
| UI density (3 levels) | 42 | 8 components wired |
| Custom instructions | 44 | Injected as system message |
| Desktop notifications | 44 | Unfocused-only + AudioContext chime |
| Settings export/import | 44 | JSON download, file picker, deep merge |
| Project hub screen + resume/open flow | 54 | Full-screen hub, open-folder CTA, resume current project |
| Project-scoped session restore | 54 | Last-session persistence per project + startup restore |
| Sidebar quick project switching | 54 | Hub shortcut, open-project action, project switch dropdown |
| Plugin browser in settings + sidebar | 55 | Shared plugin store, search, categories, featured, quick actions |
| Plugin install/uninstall + settings entry | 55 | One-click actions with AVA/legacy install-state compatibility |
| Plugin scaffold CLI foundation | 56 | `ava plugin init` command + generated package template docs |

---

## Unique Advantages (AVA vs Everyone)

Features no other AI coding tool has:

| Feature | Status |
|---------|--------|
| Multi-agent hierarchy (Team Lead + Senior Leads + Junior Devs) | Built, visible in UI |
| Worker scope filtering (each agent sees only relevant files/tools) | Built |
| Parallel agent execution | Built |
| Auto-reporting (workers report up the chain) | Built |
| User intervention points (click into any agent's chat) | Built |
| Doom loop detection | Built |
| Validator/QA pipeline (syntax, types, lint, test, review) | Built |
| Codebase intelligence (PageRank, dependency graph, symbols) | Built |
| Memory system (episodic + semantic + procedural + RAG) | Built |
| Permission/policy engine (risk assessment, auto-approval) | Built |
| Hook system (PreToolUse, PostToolUse, lifecycle) | Built |
| Plugin marketplace | Phase 2 (planned) |
| Protocol support (ACP + A2A) | Built (backend) |
