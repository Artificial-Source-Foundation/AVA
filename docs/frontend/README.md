# Frontend Architecture

> Estela's SolidJS-based user interface

---

## Overview

Estela uses **SolidJS** for fine-grained reactivity, **Kobalte** for accessible primitives, and a custom **design token system** supporting 4 themes.

```
src/
├── components/          # UI components
│   ├── chat/           # Chat interface
│   ├── dialogs/        # Modal dialogs
│   ├── layout/         # App shell, sidebar, tabs
│   ├── panels/         # Tab panels (agents, files, memory, terminal)
│   ├── projects/       # Project/workspace selector
│   ├── sessions/       # Session list & items
│   ├── settings/       # Settings modal & tabs
│   └── ui/             # Reusable primitives
├── contexts/           # React-style contexts
├── stores/             # SolidJS reactive stores
├── services/           # Database & platform services
├── types/              # TypeScript definitions
└── pages/              # Route pages
```

---

## Screens & Components

### Main Application Shell

```
┌─────────────────────────────────────────────────────────────────┐
│ Sidebar (w-72)              │ Main Content Area                 │
│ ┌─────────────────────────┐ │ ┌─────────────────────────────────┐
│ │ Brand/Logo              │ │ │ TabBar                          │
│ ├─────────────────────────┤ │ │  [Chat] [Agents] [Files] ...    │
│ │ ProjectSelector         │ │ ├─────────────────────────────────┤
│ │ [📁 Project ▼]         │ │ │                                 │
│ ├─────────────────────────┤ │ │ MainContent                     │
│ │ SessionList             │ │ │  (Renders active tab panel)     │
│ │   💬 Session 1          │ │ │                                 │
│ │   💬 Session 2          │ │ │                                 │
│ ├─────────────────────────┤ │ ├─────────────────────────────────┤
│ │ [⚙ Settings]           │ │ │ StatusBar                       │
│ └─────────────────────────┘ │ └─────────────────────────────────┘
└─────────────────────────────────────────────────────────────────┘
```

### Components

| Component | Location | Purpose |
|-----------|----------|---------|
| `AppShell` | `layout/AppShell.tsx` | Root layout with sidebar + main |
| `Sidebar` | `layout/Sidebar.tsx` | Navigation, projects, sessions |
| `TabBar` | `layout/TabBar.tsx` | Tab navigation |
| `MainContent` | `layout/MainContent.tsx` | Tab content router |
| `StatusBar` | `layout/StatusBar.tsx` | Status info, model indicator |
| `ProjectSelector` | `projects/ProjectSelector.tsx` | Workspace picker dropdown |
| `SessionList` | `sessions/SessionList.tsx` | Session history |
| `ChatView` | `chat/ChatView.tsx` | Main chat interface |

---

## Dialogs

| Dialog | File | Purpose |
|--------|------|---------|
| `SettingsModal` | `settings/SettingsModal.tsx` | App settings (6 tabs) |
| `OnboardingDialog` | `dialogs/OnboardingDialog.tsx` | First-run setup wizard |
| `PermissionDialog` | `dialogs/PermissionDialog.tsx` | Tool permission prompts |
| `WorkspaceSelectorDialog` | `dialogs/WorkspaceSelectorDialog.tsx` | Full workspace picker |
| `ModelSelectorDialog` | `dialogs/ModelSelectorDialog.tsx` | AI model selection |
| `AlertDialog` | `ui/AlertDialog.tsx` | Simple alerts |
| `ConfirmDialog` | `ui/ConfirmDialog.tsx` | Confirm/cancel actions |
| `InputDialog` | `ui/InputDialog.tsx` | Text input prompts |

---

## Settings Tabs

The settings modal has 6 tabs:

| Tab | File | Purpose |
|-----|------|---------|
| Appearance | `tabs/AppearanceTab.tsx` | Theme, colors, font |
| Providers | `tabs/ProvidersTab.tsx` | LLM API keys & models |
| Agents | `tabs/AgentsTab.tsx` | Agent presets & behavior |
| MCP | `tabs/MCPServersTab.tsx` | MCP server management |
| Keybindings | `tabs/KeybindingsTab.tsx` | Keyboard shortcuts |
| About | (inline) | Version, credits |

---

## Tab Panels

| Panel | File | Tab ID | Purpose |
|-------|------|--------|---------|
| ChatView | `chat/ChatView.tsx` | `chat` | Main conversation |
| AgentActivityPanel | `panels/AgentActivityPanel.tsx` | `agents` | Agent status & history |
| FileOperationsPanel | `panels/FileOperationsPanel.tsx` | `files` | File changes & diffs |
| MemoryPanel | `panels/MemoryPanel.tsx` | `memory` | Knowledge base |
| TerminalPanel | `panels/TerminalPanel.tsx` | `terminal` | Command output |

---

## UI Primitives

### Core Components

| Component | File | Purpose |
|-----------|------|---------|
| `Button` | `ui/Button.tsx` | Primary interactive element |
| `Badge` | `ui/Badge.tsx` | Status labels |
| `Card` | `ui/Card.tsx` | Content containers |
| `Toggle` | `ui/Toggle.tsx` | On/off switches |
| `Select` | `ui/Select.tsx` | Dropdown selection |
| `Avatar` | `ui/Avatar.tsx` | User/model avatars |
| `Dialog` | `ui/Dialog.tsx` | Modal base |
| `Toast` | `ui/Toast.tsx` | Notifications |

### Specialized Components

| Component | File | Purpose |
|-----------|------|---------|
| `DiffViewer` | `ui/DiffViewer.tsx` | Code diff display |
| `FileTree` | `ui/FileTree.tsx` | Hierarchical file browser |
| `CommandPalette` | `CommandPalette.tsx` | Cmd+K quick actions |
| `ErrorBoundary` | `ErrorBoundary.tsx` | Error handling |

---

## State Management

### Stores

All stores use SolidJS signals for fine-grained reactivity.

```typescript
// Example usage
import { useSession } from './stores/session'
import { useProject } from './stores/project'

const { currentSession, messages, createNewSession } = useSession()
const { currentProject, switchProject, openDirectory } = useProject()
```

| Store | File | Purpose |
|-------|------|---------|
| `useSession()` | `stores/session.ts` | Sessions, messages, agents |
| `useProject()` | `stores/project.ts` | Projects/workspaces |

### Contexts

| Context | File | Purpose |
|---------|------|---------|
| `NotificationContext` | `contexts/notification.tsx` | Toast notifications |

---

## Design System

### Themes

4 built-in themes with light/dark modes:

| Theme | Style |
|-------|-------|
| **Glass** | Apple-inspired, frosted glass, soft shadows |
| **Minimal** | Linear-inspired, sharp edges, clean |
| **Terminal** | Catppuccin colors, monospace, dark |
| **Soft** | Warm, rounded, friendly |

### Design Tokens

```css
/* Colors */
--accent, --accent-hover, --accent-subtle
--surface, --surface-raised, --surface-overlay
--text-primary, --text-secondary, --text-muted
--border-subtle, --border-default
--success, --warning, --error, --info

/* Spacing (4px grid) */
--space-1 through --space-12

/* Border Radius */
--radius-sm, --radius-md, --radius-lg, --radius-xl

/* Animation */
--duration-fast (150ms), --duration-normal (200ms)
--ease-out, --ease-spring
```

### Animations

```tsx
// Built-in animation classes
class="animate-fade-in"
class="animate-slide-down"
class="animate-scale-in"
class="animate-spin"
```

---

## Database Schema

### Tables

```sql
-- Projects (workspaces)
projects (id, name, directory, icon, git_branch, git_root_commit,
          created_at, updated_at, last_opened_at, is_favorite)

-- Sessions (conversations)
sessions (id, project_id, name, created_at, updated_at, status, metadata)

-- Messages
messages (id, session_id, role, content, agent_id, created_at, tokens_used, metadata)

-- Agents (AI workers)
agents (id, session_id, type, status, model, created_at, completed_at, ...)

-- File Changes
file_changes (id, session_id, agent_id, file_path, change_type, ...)
```

### Relationships

```
Project (1) ──┬── (N) Session (1) ──┬── (N) Message
              │                     └── (N) Agent ── (N) FileChange
              │
              └── Sessions are scoped to projects
```

---

## Key Features

### Project/Workspace System

- **Git-aware**: Detects git root automatically
- **Favorites**: Star projects for quick access
- **Recent**: Tracks last opened projects
- **Scoped sessions**: Conversations belong to projects

### Command Palette (Cmd+K)

- Fuzzy search across all commands
- Keyboard navigation
- Category grouping
- Recent commands

### Notifications

```typescript
import { useNotification } from './contexts/notification'

const { showToast } = useNotification()
showToast({ title: 'Saved!', variant: 'success' })
```

---

## File Organization

### Naming Conventions

- **Files**: `kebab-case.tsx`
- **Components**: `PascalCase`
- **Stores**: `camelCase` hooks (`useSession`)
- **Types**: `PascalCase` interfaces

### Component Structure

```typescript
/**
 * Component Name
 * Brief description
 */

import { ... } from 'solid-js'
import { ... } from 'lucide-solid'

// Types
interface ComponentProps { ... }

// Component
export const Component: Component<ComponentProps> = (props) => {
  // Signals
  const [state, setState] = createSignal(...)

  // Computed
  const computed = createMemo(() => ...)

  // Effects
  createEffect(() => { ... })

  // Render
  return (...)
}
```

---

## Development

### Commands

```bash
npm run dev          # Start Vite dev server
npm run tauri dev    # Start full Tauri app
npm run lint         # Check linting
npm run typecheck    # TypeScript check
```

### Adding a New Component

1. Create file in appropriate directory
2. Add to `index.ts` exports
3. Follow existing patterns for props/styling
4. Use design tokens, not hardcoded colors

### Adding a New Tab Panel

1. Create panel in `components/panels/`
2. Add to `TabId` type in `stores/session.ts`
3. Add to tab definitions in `TabBar.tsx`
4. Add case in `MainContent.tsx` switch

---

## Tech Stack

| Technology | Purpose |
|------------|---------|
| SolidJS | UI framework |
| Kobalte | Accessible primitives |
| Tauri | Desktop runtime |
| SQLite | Local database |
| Lucide | Icons |
| Vite | Build tool |
