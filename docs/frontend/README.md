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

## Key Files

```
src/
├── App.tsx                     # Root component
├── index.css                   # Global styles + Tailwind
├── styles/tokens.css           # Design tokens
├── stores/
│   ├── session.ts              # Session state
│   ├── layout.ts               # Panel visibility
│   └── navigation.ts           # Activity bar state
├── components/
│   ├── layout/
│   │   ├── AppShell.tsx        # 3-column layout
│   │   ├── ActivityBar.tsx     # Left icon bar
│   │   ├── MainArea.tsx        # Center content
│   │   ├── SidebarPanel.tsx    # Right sidebar
│   │   ├── BottomPanel.tsx     # Bottom tabs
│   │   └── StatusBar.tsx       # Status line
│   ├── chat/
│   │   ├── MessageBubble.tsx   # Chat messages
│   │   └── MessageInput.tsx    # Input with mode toggles
│   ├── panels/
│   │   ├── AgentActivityPanel.tsx
│   │   ├── FileOperationsPanel.tsx
│   │   ├── TerminalPanel.tsx
│   │   └── CodeEditorPanel.tsx
│   ├── sidebar/
│   │   ├── SidebarSessions.tsx
│   │   ├── SidebarExplorer.tsx
│   │   ├── SidebarAgents.tsx
│   │   └── SidebarMemory.tsx
│   ├── settings/
│   │   └── SettingsPage.tsx
│   └── ui/                     # Design system components
│       ├── Button.tsx, Card.tsx, Dialog.tsx
│       ├── Input.tsx, Select.tsx, Toggle.tsx
│       ├── Badge.tsx, Toast.tsx, ChatBubble.tsx
│       └── ...
└── lib/
    └── motion.ts               # Spring presets
```
