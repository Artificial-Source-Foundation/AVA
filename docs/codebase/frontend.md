# Frontend

> SolidJS + TypeScript frontend for the AVA desktop app

## Directory Structure

| Directory | Contents |
|-----------|----------|
| `src/` | Root source with App.tsx, main entry points |
| `src/components/` | SolidJS components organized by feature |
| `src/components/ui/` | Reusable UI primitives (Button, Input, Dialog, etc.) |
| `src/components/layout/` | Shell components (AppShell, MainArea, SidebarPanel) |
| `src/components/chat/` | Chat components (MessageList, MessageInput, ToolCallCard) |
| `src/components/sidebar/` | Sidebar panels (Sessions, Explorer, Memory) |
| `src/components/dialogs/` | Modal dialogs (Settings, Onboarding, Permissions) |
| `src/components/settings/` | Settings UI components |
| `src/components/projects/` | Project management UI |
| `src/hooks/` | Custom SolidJS hooks |
| `src/hooks/agent/` | Agent-specific hooks |
| `src/hooks/chat/` | Chat-related hooks |
| `src/stores/` | State management |
| `src/services/` | Service layer (LLM, logging, etc.) |
| `src/lib/` | Utility libraries (API client, logger) |
| `src/types/` | TypeScript type definitions |
| `src/contexts/` | Context providers |
| `src/config/` | Configuration files |

## Key Components

| Component | File | Purpose |
|-----------|------|---------|
| App | `App.tsx` | Root component with initialization logic |
| AppShell | `components/layout/AppShell.tsx` | Main application layout shell |
| MainArea | `components/layout/MainArea.tsx` | Primary content area |
| SidebarPanel | `components/layout/SidebarPanel.tsx` | Collapsible sidebar container |
| RightPanel | `components/layout/RightPanel.tsx` | Right-side auxiliary panel |
| ActivityBar | `components/layout/ActivityBar.tsx` | Left activity/navigation bar |
| StatusBar | `components/layout/StatusBar.tsx` | Bottom status bar |
| MessageList | `components/chat/MessageList.tsx` | Chat message list display |
| MessageInput | `components/chat/MessageInput.tsx` | Chat input with submit |
| ToolCallCard | `components/chat/ToolCallCard.tsx` | Tool execution display card |
| ApprovalDock | `components/chat/ApprovalDock.tsx` | Tool approval UI overlay |
| SidebarSessions | `components/sidebar/SidebarSessions.tsx` | Session list sidebar |
| SidebarExplorer | `components/sidebar/SidebarExplorer.tsx` | File explorer sidebar |
| SettingsModal | `components/settings/SettingsModal.tsx` | Settings dialog |
| CommandPalette | `components/CommandPalette.tsx` | Quick command picker |
| ProjectHub | `components/projects/ProjectHub.tsx` | Project management |

### UI Primitives

Button, Input, Dialog, Card, Select, Checkbox, Toggle, Badge, Avatar, Toast, ChatBubble, FileTree, DiffViewer, ContextMenu in `components/ui/`.

## Hooks

| Hook | File | Purpose |
|------|------|---------|
| useAgent | `hooks/useAgent.ts` | Main agent orchestrator |
| useRustAgent | `hooks/use-rust-agent.ts` | Rust agent communication |
| useRustMemory | `hooks/use-rust-memory.ts` | Memory operations |
| useRustValidation | `hooks/use-rust-validation.ts` | Content validation |
| useRustTools | `hooks/use-rust-tools.ts` | Tool execution |
| useModelStatus | `hooks/useModelStatus.ts` | Model status checking |
| useAppInit | `hooks/useAppInit.ts` | App initialization |
| useAppShortcuts | `hooks/useAppShortcuts.ts` | Global shortcuts |
| useBackend | `hooks/use-backend.ts` | Backend connection |
| useChat | `hooks/useChat.ts` | Chat operations |

### Agent/Chat Hooks

Agent hooks in `hooks/agent/`: agent-events, agent-team-bridge, tool-execution, message-actions, streaming, turn-manager, config-builder.

Chat hooks in `hooks/chat/`: prompt-builder, history-builder, tool-execution.

## Services/Stores

### Session Store

| Service | File | Purpose |
|---------|------|---------|
| useSession | `stores/session/index.ts` | Main session hook |
| session-state | `session-state.ts` | State signals |
| session-lifecycle | `session-lifecycle.ts` | Session CRUD |
| session-messages | `session-messages.ts` | Message management |
| session-branching | `session-branching.ts` | Session branching |

### Settings Store

| Service | File | Purpose |
|---------|------|---------|
| useSettings | `stores/settings/index.ts` | Main settings hook |
| settings-signal | `settings-signal.ts` | State signals |
| settings-mutators | `settings-mutators.ts` | Update functions |
| settings-persistence | `settings-persistence.ts` | Save/load |
| settings-appearance | `settings-appearance.ts` | Theme/dark mode |

### Other Stores

useLayout, useProject, useTeam, usePlugins, useTerminal, useShortcuts, useWorkflows, useSandbox in `stores/`.

### Service Layer

| Service | File | Purpose |
|---------|------|---------|
| rust-bridge | `services/rust-bridge.ts` | Rust backend communication |
| core-bridge | `services/core-bridge.ts` | Core integration |
| logger | `services/logger.ts` | Application logging |
| auto-updater | `services/auto-updater.ts` | Update checking |

## Tauri IPC

Frontend communicates with Rust via Tauri commands (IPC) or HTTP API (browser mode).

### Command Pattern

```typescript
// Rust: src-tauri/src/commands/mod.rs
#[tauri::command]
async fn submit_goal(args: SubmitGoalArgs) -> Result<SubmitGoalResult, String>

// Frontend: src/services/rust-bridge.ts
export const rustBackend = {
  submitGoal: (args) => invokeCommand('submit_goal', { args })
}
```

### Adding Commands

1. Add Rust command in `src-tauri/src/commands/my_command.rs`
2. Export in `commands/mod.rs`
3. Register in `lib.rs` invoke handler
4. Add types in `src/types/rust-ipc.ts`
5. Add to `rust-bridge.ts`

### Key Commands

| Command | Module | Purpose |
|---------|--------|---------|
| submit_goal | agent_commands | Start agent |
| cancel_agent | agent_commands | Stop agent |
| steer_agent | agent_commands | Mid-stream message |
| list_sessions | session_commands | List sessions |
| create_session | session_commands | Create session |
| list_models | model_commands | List models |
| switch_model | model_commands | Change model |
| list_providers | provider_commands | List providers |
| list_tools | tool_commands | List tools |
| list_mcp_servers | mcp_commands | List MCP servers |
| compact_context | context_commands | Compress context |

### Browser Mode

Commands route through HTTP API when not in Tauri:

```typescript
// src/lib/api-client.ts
const COMMAND_TO_ENDPOINT = {
  submit_goal: { path: '/api/agent/submit', method: 'POST' },
}
```

## State Management

### Reactive Patterns

SolidJS signals provide fine-grained reactivity:

```typescript
const [count, setCount] = createSignal(0)
export function useCounter() {
  return { count, increment: () => setCount(c => c + 1) }
}
```

### Store Usage

```typescript
import { useSession } from './stores/session'
function MyComponent() {
  const session = useSession()
  const currentSession = session.currentSession()
  session.addMessage({ role: 'user', content: 'Hello' })
}
```

### Cross-Component Communication

- **Custom Events**: `window.dispatchEvent(new CustomEvent('ava:event-name'))`
- **Signals**: Shared signals auto-update subscribers
- **Context**: SolidJS context for theme, notifications

### Key Events

| Event | Purpose |
|-------|---------|
| `ava:core-settings-changed` | Settings update |
| `ava:compacted` | Context compacted |
| `ava:check-update` | Trigger update check |
