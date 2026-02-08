# Active Context

> What we're working on RIGHT NOW

---

## Current Focus

**Desktop App — Phase 1 Complete, entering polish/testing**

Phase 1 is done. Session 38 focused on WebKitGTK fixes and UX polish (splash screen, layout cleanup). Now in testing mode with `npm run tauri dev`.

### What Just Happened (Session 38, 2026-02-08)

**WebKitGTK Fixes:**
- DMABUF ghost rendering fix (`WEBKIT_DISABLE_DMABUF_RENDERER=1` in `src-tauri/src/main.rs`)
- Nested `<button>` crash fix — outer buttons → `<div role="button">` in Settings, SessionListItem, TerminalPanel
- Cargo linker fix for Pop OS (`gcc-14` in `.cargo/config.toml`)

**Splash Screen:**
- `src/components/SplashScreen.tsx` — Diamond logo placeholder, "ESTELA" title, "AI Coding Companion" tagline, animated loading dots, real-time init status, version number
- Window shows early so splash is visible during init
- 800ms minimum display time, mesh gradient background, fade-out transition
- `index.tsx` LoadingFallback matches splash look

**Layout Refactoring:**
- Deleted `src/stores/navigation.ts` — replaced by `settingsOpen` signal in layout store
- Sidebar slimmed: `ActivityId` reduced to `'sessions' | 'explorer'` (was 7 options)
- Settings moved from page-based navigation to modal pattern (`openSettings`/`closeSettings`)
- Added right panel, bottom panel, and bottom panel height state to layout store
- New keyboard shortcuts: `Ctrl+,` (settings), `Ctrl+M` (bottom panel)
- `SettingsPage.tsx` uses `closeSettings()` instead of `goToChat()`

---

## Next Up

### Immediate (Phase 1 Polish)
- [ ] Test splash screen in Tauri dev
- [ ] Wire settings as modal overlay (currently `settingsOpen` signal exists but SettingsPage isn't rendered as overlay yet)
- [ ] Wire right panel (agent activity) to layout
- [ ] Wire bottom panel (memory/terminal) to layout
- [ ] Fix remaining TS errors (`SidebarPanel.tsx` references removed activity IDs)

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
| `src/stores/settings.ts` | Settings persistence + credential sync bridge |

### Core Stores
| File | Purpose |
|------|---------|
| `src/stores/session.ts` | Session CRUD, fork, duplicate |
| `src/stores/project.ts` | Project management |
| `src/stores/team.ts` | Dev team hierarchy state |

---

## Naming Convention

| Old Name | New Name | Role |
|----------|----------|------|
| Commander | Team Lead | Plans, delegates, coordinates |
| Worker | Senior Lead | Domain specialist, leads a group |
| Operator | Junior Dev | Executes specific tasks |
