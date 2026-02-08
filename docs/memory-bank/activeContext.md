# Active Context

> What we're working on RIGHT NOW

---

## Current Focus

**Desktop App — Core Wiring Complete, 1072 tests passing**

Phase 1 is done. Sessions 39-40 added comprehensive backend tests (536 tests across 24 files covering Config, Context, Memory, Session, Commander) and wired the 5 core modules to the frontend via a thin integration layer.

### What Just Happened (Sessions 39-40, 2026-02-08)

**Session 39 — Backend Testing:**
- 536 tests across 24 test files for 5 core modules (Config, Context, Memory, Session, Commander)
- All modules have full coverage: manager, integration, consolidation, parallel execution, batch, tool-wrapper
- Total test count: 1072 (was 536 pre-existing + 536 new)

**Session 40 — Core Frontend Wiring:**
- `src/services/core-bridge.ts` (NEW) — Central init for SettingsManager, ContextTracker, WorkerRegistry, MemoryManager
- `src/stores/settings.ts` — `pushSettingsToCore()` syncs frontend AppSettings → core SettingsManager
- `src/App.tsx` — Core bridge init in startup sequence ("Initializing core engine..." splash step)
- `src/hooks/useChat.ts` — Real token counting via ContextTracker (addMessage on send + complete)
- `src/stores/session.ts` — Tracker-backed `contextUsage` memo, session checkpoints (create/rollback)
- `src/components/chat/ContextBar.tsx` (NEW) — Token usage bar below chat input
- `src/hooks/useAgent.ts` — Episodic memory recording on agent completion

**Session 39 — Appearance Tab:**
- Dedicated settings tab with dark/light mode, 6 accent colors, UI scale slider, mono font selector
- `applyAppearance()` applies all settings to DOM immediately

---

## Next Up

### Immediate (Phase 1.5 Polish)
- [ ] Test full app flow in Tauri dev (chat, tools, settings, sessions)
- [ ] Memory recall injected into system prompts
- [ ] Settings UI tabs for core categories (agent, context, memory, permissions)
- [ ] Auto-compaction when context > 80%
- [ ] Checkpoint UI in sidebar (list/rollback buttons)
- [ ] Per-message token display in bubbles

### Phase 2: Plugin Ecosystem (THE DIFFERENTIATOR)
See `docs/ROADMAP.md` for sprint breakdown.

---

## Platform Priority

```
1. Desktop App (Tauri)     <- CURRENT (polish/testing)
2. Plugin Ecosystem        <- NEXT
3. CLI                     <- Secondary
4. Editor Integration      <- Future
5. Agent Network           <- Future
```

---

## Key File Map (Quick Reference)

### Layout System
| File | Purpose |
|------|---------|
| `src/stores/layout.ts` | All layout state (sidebar, panels, settings modal, shortcuts) |
| `src/components/layout/AppShell.tsx` | 3-column layout with @corvu/resizable |
| `src/components/layout/ActivityBar.tsx` | Left icon strip (sessions, explorer, settings) |
| `src/components/layout/SidebarPanel.tsx` | Contextual sidebar (sessions or explorer) |
| `src/components/layout/MainArea.tsx` | Chat view or welcome state |

### Splash / Init
| File | Purpose |
|------|---------|
| `src/components/SplashScreen.tsx` | Splash screen component |
| `src/App.tsx` | Init sequence, splash orchestration, window show |
| `src/index.tsx` | Suspense fallback (matches splash look) |
| `index.html` | Dark background on `#root` to prevent flash |

### Settings
| File | Purpose |
|------|---------|
| `src/components/settings/SettingsPage.tsx` | Full settings UI (providers, agents, MCP, keybindings, about) |
| `src/stores/settings.ts` | Settings persistence + credential sync + `pushSettingsToCore()` |

### Core Bridge
| File | Purpose |
|------|---------|
| `src/services/core-bridge.ts` | Init all 5 core singletons (settings, tracker, registry, memory) |
| `src/components/chat/ContextBar.tsx` | Token usage bar below chat input |

### Core Stores
| File | Purpose |
|------|---------|
| `src/stores/session.ts` | Session CRUD, fork, duplicate, checkpoints, tracker-backed contextUsage |
| `src/stores/project.ts` | Project management |
| `src/stores/team.ts` | Dev team hierarchy state |

---

## Naming Convention

| Old Name | New Name | Role |
|----------|----------|------|
| Commander | Team Lead | Plans, delegates, coordinates |
| Worker | Senior Lead | Domain specialist, leads a group |
| Operator | Junior Dev | Executes specific tasks |
