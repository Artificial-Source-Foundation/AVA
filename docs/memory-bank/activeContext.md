# Active Context

> What we're working on RIGHT NOW

---

## Current Focus

**Desktop App — Phase 1.5 feature-complete, 1778 tests passing**

Phase 1 is done. Phase 1.5 has closed all competitive gaps, added comprehensive settings, and wired memory recall + auto-compaction. Only manual testing/verification remains.

### What Just Happened (2026-02-09)

**Settings Hardening (Session 46):**
- **16 new settings** across 4 sub-interfaces: GenerationSettings, AgentLimitSettings, BehaviorSettings, NotificationSettings
- **2 new tabs** — LLM (maxTokens, temperature, topP, custom instructions, agent limits) + Behavior (sendKey, autoScroll, autoTitle, lineNumbers, wordWrap, notifications, sound)
- **3 new files** — `LLMTab.tsx`, `BehaviorTab.tsx`, `src/services/notifications.ts`
- **4 hardcoded values wired** — maxTokens/temperature to useChat, agentMaxTurns/maxTimeMinutes to useAgent
- Data management: export (JSON download), import (file picker + deep merge), clear all

**Gap Closure (Sessions 46+):**
- ~~Cost tracking UI~~ — Per-message cost + tokens in bubbles, session total in ContextBar
- ~~Vision/image support~~ — Paste, drop, base64 multimodal, inline display
- ~~Iterative lint→fix~~ — autoFixLint after file edits, errors fed back to LLM
- ~~Checkpoint UI~~ — Create button, inline display with restore, full DB rollback
- ~~Per-message token display~~ — Shown in message bubbles

**Previous sessions:** Backend docs, gap analysis (15 gaps across 8 codebases), appearance expansion, density wiring

---

## Next Up

### Phase 1.5 Remaining (testing only)
- [x] ~~Memory recall in system prompts~~ — Done (recallSimilar + procedural → system message)
- [x] ~~Auto-compaction at 80% context~~ — Done (sliding window, syncs state + DB + tracker)
- [ ] Test full app flow in Tauri dev (chat, tools, settings, sessions)
- [ ] Verify all keyboard shortcuts work (Ctrl+B, Ctrl+,, Ctrl+M)
- [ ] Test on multiple Linux DEs (GNOME, KDE, Cosmic)

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
| `src/components/settings/SettingsModal.tsx` | Settings modal (providers, agents, MCP, shortcuts, appearance, LLM, behavior) |
| `src/components/settings/tabs/LLMTab.tsx` | Generation params, agent limits, custom instructions |
| `src/components/settings/tabs/BehaviorTab.tsx` | Send key, auto-scroll, code blocks, notifications |
| `src/components/settings/tabs/AppearanceTab.tsx` | Theme, accents, fonts, density, code themes |
| `src/stores/settings.ts` | Settings persistence + credential sync + `pushSettingsToCore()` |
| `src/services/notifications.ts` | Desktop notifications + AudioContext chime |

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
