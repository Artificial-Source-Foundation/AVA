# Frontend

> Desktop app built with SolidJS + Tauri

---

## Stack

| Layer | Technology |
|-------|-----------|
| Framework | SolidJS (fine-grained reactivity) |
| Language | TypeScript (strict) |
| Styling | Tailwind CSS v4 |
| Animations | solid-motionone (spring physics) |
| Panels | @corvu/resizable |
| Code viewer | CodeMirror 6 (One Dark theme) |
| Virtual scroll | @tanstack/solid-virtual |

## Layout (IDE-Inspired)

```
┌──────┬────────────────────────────┬──────────────┐
│      │                            │              │
│  A   │      Main Area             │   Sidebar    │
│  c   │  ┌────────────────────┐    │   (context)  │
│  t   │  │  Team Lead Chat    │    │              │
│  i   │  │  Agent Cards       │    │  Sessions    │
│  v   │  │  Code Viewer       │    │  Explorer    │
│  i   │  └────────────────────┘    │  Agents      │
│  t   │                            │  Memory      │
│  y   ├────────────────────────────┤              │
│      │  Bottom Panel              │              │
│  B   │  Terminal | Activity | Git │              │
│  a   │                            │              │
│  r   │                            │              │
└──────┴────────────────────────────┴──────────────┘
```

- **Activity Bar** (48px, left) — Icon buttons to switch sidebar context
- **Main Area** — Chat with Team Lead, agent cards, code viewer
- **Sidebar** — Contextual: Sessions, Explorer, Agents, Memory
- **Bottom Panel** — Resizable, collapsible: Terminal, Agent Activity, File Changes
- **Keyboard shortcuts:** Ctrl+B (sidebar), Ctrl+` (bottom panel)

## Design System

- Glassmorphism with blur/transparency
- Spring physics animations (not CSS transitions)
- Dark theme with ambient gradient mesh background
- Premium, minimalistic feel (Arc/Vercel/Warp inspired)

## Sidebar Toggle

The sidebar uses **width-based toggle** with `overflow: hidden` (not `margin-left`, which causes content bleed in WebKitGTK). Animation: `transition: width 120ms ease`.

## Tauri Hardening

- **CSP** enabled in `tauri.conf.json`
- **Scoped FS** — limited to `$APPDATA/**` and `$HOME/.estela/**`
- **Deferred window show** — `visible: false` + `getCurrentWindow().show()` after mount
- **Release profile** — `lto=true, codegen-units=1, strip=true, opt-level="s"`
- **Window state persistence** via `tauri-plugin-window-state`
- **Native CSS** — `user-select: none` on UI chrome, drag regions

## Performance Notes

- **No noise texture overlay** — Removed `#root::after` pseudo-element (WebKitGTK doesn't respect `pointer-events: none` on fixed pseudo-elements, blocking all clicks)
- **GPU layer promotion** — Settings scroll container uses `transform: translateZ(0)` for smooth scrolling in WebKitGTK
- **`transition-colors` over `transition-all`** — All settings tabs use `transition-colors` to avoid transitioning every CSS property during scroll
- **No `hover:-translate-y`** — Removed hover transforms that cause layout reflow

## Key Files

```
src/
├── App.tsx                     # Root component + onboarding gate
├── index.css                   # Global styles + Tailwind
├── styles/tokens.css           # Design tokens (406 lines)
├── stores/
│   ├── session.ts              # Session state
│   ├── settings.ts             # Settings persistence (localStorage)
│   ├── team.ts                 # Dev team hierarchy store
│   ├── layout.ts               # Panel visibility
│   └── navigation.ts           # Activity bar state
├── types/
│   └── team.ts                 # TeamMember, TeamDomain, TeamHierarchy
├── components/
│   ├── layout/
│   │   ├── AppShell.tsx        # 3-column layout, width-based sidebar
│   │   ├── ActivityBar.tsx     # Left icon bar
│   │   ├── MainArea.tsx        # Center content
│   │   ├── SidebarPanel.tsx    # Right sidebar
│   │   └── StatusBar.tsx       # Monospace status line
│   ├── chat/
│   │   ├── MessageBubble.tsx   # Chat messages
│   │   └── MessageInput.tsx    # Input with mode toggles
│   ├── panels/
│   │   ├── AgentActivityPanel.tsx
│   │   ├── FileOperationsPanel.tsx
│   │   ├── TerminalPanel.tsx
│   │   ├── CodeEditorPanel.tsx
│   │   ├── TeamPanel.tsx       # Dev team hierarchy tree
│   │   └── TeamMemberChat.tsx  # Scoped chat per team member
│   ├── sidebar/
│   │   ├── SidebarSessions.tsx
│   │   ├── SidebarExplorer.tsx
│   │   ├── SidebarAgents.tsx
│   │   └── SidebarMemory.tsx
│   ├── settings/
│   │   └── SettingsPage.tsx    # Full-page with sidebar tabs
│   └── ui/                     # Design system components
│       ├── Button.tsx, Card.tsx, Dialog.tsx
│       ├── Input.tsx, Select.tsx, Toggle.tsx
│       ├── Badge.tsx, Toast.tsx, ChatBubble.tsx
│       └── ...
└── lib/
    └── motion.ts               # Spring presets
```
