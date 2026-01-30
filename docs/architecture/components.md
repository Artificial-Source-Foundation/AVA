# Component Architecture

> UI component hierarchy and patterns

---

## Component Tree

```
App
└── AppShell
    ├── Sidebar
    │   ├── Logo
    │   ├── SessionList
    │   │   └── SessionListItem (for each)
    │   │       ├── Name + Preview
    │   │       └── Actions (rename, archive)
    │   └── Settings Button
    │
    ├── Main Area
    │   ├── TabBar
    │   │   ├── Session Tabs
    │   │   └── Model Selector
    │   │
    │   └── ChatView
    │       ├── MessageList
    │       │   └── MessageBubble (for each)
    │       │       ├── Avatar
    │       │       ├── Content
    │       │       ├── MessageActions
    │       │       └── EditForm (when editing)
    │       │
    │       ├── TypingIndicator
    │       │
    │       └── MessageInput
    │           ├── Textarea
    │           └── Send Button
    │
    └── StatusBar
        ├── Provider Status
        └── Token Count

Modals
├── SettingsModal
│   └── API Key Inputs
└── ErrorBoundary (wraps App)
```

---

## Component Categories

### Layout Components

| Component | Location | Purpose |
|-----------|----------|---------|
| `AppShell` | `layout/` | Main layout container |
| `Sidebar` | `layout/` | Left navigation panel |
| `TabBar` | `layout/` | Top tab bar with model selector |
| `StatusBar` | `layout/` | Bottom status bar |

### Chat Components

| Component | Location | Purpose |
|-----------|----------|---------|
| `ChatView` | `chat/` | Main chat container |
| `MessageList` | `chat/` | Renders message list |
| `MessageBubble` | `chat/` | Single message display |
| `MessageInput` | `chat/` | User input area |
| `MessageActions` | `chat/` | Copy, edit, retry buttons |
| `EditForm` | `chat/` | Inline message editing |
| `TypingIndicator` | `chat/` | Streaming indicator |

### Session Components

| Component | Location | Purpose |
|-----------|----------|---------|
| `SessionList` | `sessions/` | Session sidebar list |
| `SessionListItem` | `sessions/` | Single session item |

### Settings Components

| Component | Location | Purpose |
|-----------|----------|---------|
| `SettingsModal` | `settings/` | API key configuration |

### Common Components

| Component | Location | Purpose |
|-----------|----------|---------|
| `ErrorBoundary` | `common/` | Error handling wrapper |

---

## Component Patterns

### Props Interface

Every component defines a `Props` interface:

```typescript
interface MessageBubbleProps {
  message: Message;
  isEditing: boolean;
  isRetrying: boolean;
  isStreaming: boolean;
  onStartEdit: () => void;
  onSaveEdit: (content: string) => Promise<void>;
  onCancelEdit: () => void;
  onCopy: () => void;
  onRetry: () => void;
  onRegenerate: () => void;
}

export const MessageBubble: Component<MessageBubbleProps> = (props) => {
  // ...
};
```

### Conditional Rendering

Use SolidJS `Show` for conditional content:

```typescript
<Show when={props.isEditing} fallback={<MessageContent />}>
  <EditForm
    content={props.message.content}
    onSave={props.onSaveEdit}
    onCancel={props.onCancelEdit}
  />
</Show>
```

### List Rendering

Use SolidJS `For` for lists:

```typescript
<For each={session.messages()}>
  {(message) => (
    <MessageBubble
      message={message}
      isEditing={session.editingMessageId() === message.id}
      // ...
    />
  )}
</For>
```

### Local State

Use `createSignal` for component-local state:

```typescript
const [isEditing, setIsEditing] = createSignal(false);
const [editName, setEditName] = createSignal(props.session.name);
```

### Store Access

Components access global state via hooks:

```typescript
import { useSession } from '../stores/session';

export const SessionList: Component = () => {
  const session = useSession();

  return (
    <For each={session.sessions()}>
      {/* ... */}
    </For>
  );
};
```

---

## Component Communication

### Parent → Child (Props)

```typescript
// Parent
<MessageBubble
  message={msg}
  onRetry={() => handleRetry(msg.id)}
/>

// Child
export const MessageBubble: Component<Props> = (props) => {
  return <button onClick={props.onRetry}>Retry</button>;
};
```

### Child → Parent (Callbacks)

```typescript
// Parent provides callback
<SessionListItem
  onRename={(name) => session.renameSession(id, name)}
/>

// Child calls it
<button onClick={() => props.onRename(editName())}>Save</button>
```

### Shared State (Stores)

```typescript
// Any component can access and modify
const session = useSession();

// Read
const messages = session.messages();

// Write
session.addMessage(newMsg);
```

---

## Styling Patterns

### Tailwind Classes

All components use Tailwind for styling:

```typescript
<div class="flex flex-col h-full bg-gray-900">
  <div class="flex-1 overflow-y-auto p-4">
    {/* content */}
  </div>
</div>
```

### Conditional Classes

```typescript
<div class={`
  p-3 rounded-lg cursor-pointer transition
  ${props.isActive ? 'bg-blue-600' : 'hover:bg-gray-700'}
`}>
```

### Group Hover

For hover-revealed actions:

```typescript
<div class="group relative">
  <span>{props.session.name}</span>
  <div class="opacity-0 group-hover:opacity-100 flex gap-1">
    <button>Edit</button>
    <button>Delete</button>
  </div>
</div>
```

---

## Error Handling

### ErrorBoundary

Wraps the app to catch rendering errors:

```typescript
<ErrorBoundary>
  <App />
</ErrorBoundary>
```

### Message-level Errors

Each message can have an error state:

```typescript
<Show when={props.message.error}>
  <div class="text-red-400">
    {props.message.error.message}
    <button onClick={props.onRetry}>Retry</button>
  </div>
</Show>
```

---

## File Structure

```
src/components/
├── index.ts           # Re-exports all components
│
├── chat/
│   ├── index.ts       # Barrel export
│   ├── ChatView.tsx
│   ├── MessageList.tsx
│   ├── MessageBubble.tsx
│   ├── MessageInput.tsx
│   ├── MessageActions.tsx
│   ├── EditForm.tsx
│   └── TypingIndicator.tsx
│
├── sessions/
│   ├── index.ts
│   ├── SessionList.tsx
│   └── SessionListItem.tsx
│
├── settings/
│   ├── index.ts
│   └── SettingsModal.tsx
│
├── layout/
│   ├── index.ts
│   ├── AppShell.tsx
│   ├── Sidebar.tsx
│   ├── TabBar.tsx
│   └── StatusBar.tsx
│
└── common/
    ├── index.ts
    └── ErrorBoundary.tsx
```
