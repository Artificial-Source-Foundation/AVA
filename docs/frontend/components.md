# Component Catalog

> Quick reference for all UI components

---

## Layout Components

### AppShell
Main application container with sidebar and content area.

```tsx
import { AppShell } from './components/layout'

<AppShell />
```

### Sidebar
Navigation sidebar with project selector, sessions, and settings.

```tsx
// Automatically rendered by AppShell
// Contains: Brand, ProjectSelector, SessionList, Settings button
```

### TabBar
Horizontal tab navigation for content panels.

```tsx
// Tab IDs: 'chat' | 'agents' | 'files' | 'memory' | 'terminal'
```

### MainContent
Routes to the active tab panel component.

---

## Project Components

### ProjectSelector
Dropdown workspace picker in sidebar.

```tsx
import { ProjectSelector } from './components/projects'

<ProjectSelector />
```

**Features:**
- Current project display with icon
- Git branch indicator
- Favorites section
- Recent projects
- "Open Folder" action

---

## Dialog Components

### SettingsModal
Full settings dialog with 6 tabs.

```tsx
import { SettingsModal } from './components/settings'

<SettingsModal
  isOpen={showSettings()}
  onClose={() => setShowSettings(false)}
/>
```

### OnboardingDialog
First-run setup wizard.

```tsx
import { OnboardingDialog } from './components/dialogs'

<OnboardingDialog
  isOpen={showOnboarding()}
  onComplete={(config) => handleSetup(config)}
  onSkip={() => setShowOnboarding(false)}
/>
```

**Steps:**
1. Welcome
2. Theme selection
3. API key setup
4. Feature preferences
5. Complete

### PermissionDialog
Tool permission request prompt.

```tsx
import { PermissionDialog, type PermissionRequest } from './components/dialogs'

<PermissionDialog
  isOpen={showPermission()}
  request={permissionRequest}
  onAllow={(scope) => handleAllow(scope)}
  onDeny={() => handleDeny()}
/>
```

**Scopes:** `once`, `session`, `always`

### WorkspaceSelectorDialog
Full-featured workspace picker dialog.

```tsx
import { WorkspaceSelectorDialog } from './components/dialogs'

<WorkspaceSelectorDialog
  isOpen={showWorkspaces()}
  workspaces={workspaces}
  currentId={currentWorkspace?.id}
  onSelect={(ws) => handleSelect(ws)}
  onClose={() => setShowWorkspaces(false)}
/>
```

### ModelSelectorDialog
AI model selection dialog.

```tsx
import { ModelSelectorDialog } from './components/dialogs'

<ModelSelectorDialog
  isOpen={showModelPicker()}
  selectedModel={currentModel}
  onSelect={(model) => handleModelChange(model)}
  onClose={() => setShowModelPicker(false)}
/>
```

---

## UI Primitives

### Button
Primary interactive element.

```tsx
import { Button } from './components/ui'

<Button
  variant="primary" // primary | secondary | ghost | danger | success
  size="md"         // sm | md | lg | icon
  icon={<Icon />}
  loading={false}
  disabled={false}
>
  Click me
</Button>
```

### Badge
Status indicator labels.

```tsx
import { Badge } from './components/ui'

<Badge
  variant="success" // default | secondary | success | warning | error | info | outline
  size="md"         // sm | md | lg
  dot={false}       // Show as dot instead of text
>
  Active
</Badge>
```

### Card
Content container.

```tsx
import { Card } from './components/ui'

<Card
  variant="elevated" // flat | elevated | outlined
  padding="md"       // none | sm | md | lg
  interactive={false}
  onClick={handler}
>
  Content here
</Card>
```

### Toggle
On/off switch.

```tsx
import { Toggle } from './components/ui'

<Toggle
  checked={isEnabled()}
  onChange={(checked) => setEnabled(checked)}
  size="md"    // sm | md | lg
  disabled={false}
/>
```

### Select
Dropdown selection.

```tsx
import { Select } from './components/ui'

<Select
  options={[
    { value: 'opt1', label: 'Option 1', description: 'Description' }
  ]}
  value={selected()}
  onChange={(val) => setSelected(val)}
  placeholder="Select..."
  size="md"
/>
```

### Avatar
User/entity avatar.

```tsx
import { Avatar } from './components/ui'

<Avatar
  src="image-url"
  initials="AB"
  size="md"     // xs | sm | md | lg | xl
  shape="circle" // circle | square
  status="online" // online | offline | away | busy
/>
```

### Dialog
Base modal dialog.

```tsx
import { Dialog } from './components/ui'

<Dialog
  open={isOpen()}
  onOpenChange={setIsOpen}
  title="Dialog Title"
  description="Optional description"
  size="md"     // sm | md | lg | xl | full
>
  {/* Content */}
</Dialog>
```

### Toast
Notification toasts (use via context).

```tsx
import { useNotification } from './contexts/notification'

const { showToast, dismissToast } = useNotification()

showToast({
  title: 'Success!',
  description: 'Operation completed',
  variant: 'success', // default | success | warning | error | info
  duration: 5000,
  action: { label: 'Undo', onClick: () => {} }
})
```

### AlertDialog
Simple alert message.

```tsx
import { AlertDialog } from './components/ui'

<AlertDialog
  isOpen={showAlert()}
  title="Alert"
  message="Something happened"
  onClose={() => setShowAlert(false)}
/>
```

### ConfirmDialog
Confirm/cancel prompt.

```tsx
import { ConfirmDialog } from './components/ui'

<ConfirmDialog
  isOpen={showConfirm()}
  title="Confirm Delete"
  message="Are you sure?"
  confirmLabel="Delete"
  confirmVariant="danger"
  onConfirm={() => handleDelete()}
  onCancel={() => setShowConfirm(false)}
/>
```

### InputDialog
Text input prompt.

```tsx
import { InputDialog } from './components/ui'

<InputDialog
  isOpen={showInput()}
  title="Rename"
  placeholder="Enter name"
  initialValue="Current name"
  onSubmit={(value) => handleRename(value)}
  onCancel={() => setShowInput(false)}
/>
```

---

## Specialized Components

### DiffViewer
Side-by-side code diff display.

```tsx
import { DiffViewer } from './components/ui'

<DiffViewer
  oldCode={originalCode}
  newCode={modifiedCode}
  language="typescript"
  fileName="example.ts"
/>
```

### FileTree
Hierarchical file browser.

```tsx
import { FileTree, type FileNode } from './components/ui'

<FileTree
  nodes={fileNodes}
  selectedId={selectedFile()}
  expandedIds={expandedFolders()}
  onSelect={(node) => handleSelect(node)}
  onToggle={(id) => toggleFolder(id)}
/>
```

### CommandPalette
Cmd+K quick action palette.

```tsx
import { CommandPalette, createDefaultCommands } from './components/CommandPalette'

const commands = createDefaultCommands({
  newChat: () => createSession(),
  openSettings: () => setShowSettings(true),
  // ...
})

<CommandPalette
  commands={commands}
  onClose={() => {}}
  recentIds={recentCommands}
/>
```

### ErrorBoundary
Graceful error handling wrapper.

```tsx
import { ErrorBoundary } from './components/ErrorBoundary'

<ErrorBoundary
  fallback={(error, reset) => (
    <div>Error: {error.message} <button onClick={reset}>Retry</button></div>
  )}
>
  <ComponentThatMightError />
</ErrorBoundary>
```

---

## Panel Components

### ChatView
Main chat interface with messages.

### AgentActivityPanel
Agent status and task history.

### FileOperationsPanel
File changes and diffs.

### MemoryPanel
Knowledge base and context.

### TerminalPanel
Command execution output with ANSI color support.

---

## Settings Tab Components

### AppearanceTab
Theme, accent color, font settings.

### ProvidersTab
LLM provider configuration with API keys.

### AgentsTab
Agent presets and custom configurations.

### MCPServersTab
MCP server management and status.

### KeybindingsTab
Keyboard shortcut viewer and editor.
