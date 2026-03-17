/**
 * Shortcut Defaults
 * Default keyboard shortcut definitions. Pure data — no runtime dependencies.
 */

// ============================================================================
// Types
// ============================================================================

export interface ShortcutDef {
  id: string
  keys: string[] // e.g. ['ctrl', 'b']
  label: string
  description: string
  category: string
  isCustom?: boolean
}

// ============================================================================
// Default Shortcut Definitions
// ============================================================================

export const DEFAULT_SHORTCUTS: ShortcutDef[] = [
  {
    id: 'toggle-sidebar',
    keys: ['ctrl', 'b'],
    label: 'Toggle Sidebar',
    description: 'Show or hide the sidebar',
    category: 'General',
  },
  {
    id: 'toggle-settings',
    keys: ['ctrl', ','],
    label: 'Open Settings',
    description: 'Open the settings modal',
    category: 'General',
  },
  {
    id: 'toggle-bottom-panel',
    keys: ['ctrl', 'm'],
    label: 'Toggle Memory Panel',
    description: 'Show or hide the bottom panel',
    category: 'General',
  },
  {
    id: 'new-chat',
    keys: ['ctrl', 'n'],
    label: 'New Chat',
    description: 'Start a new conversation',
    category: 'General',
  },
  {
    id: 'command-palette',
    keys: ['ctrl', 'k'],
    label: 'Command Palette',
    description: 'Quick access to all commands',
    category: 'General',
  },
  {
    id: 'model-browser',
    keys: ['ctrl', 'shift', 'm'],
    label: 'Model Browser',
    description: 'Open the model browser dialog',
    category: 'General',
  },
  {
    id: 'quick-model-picker',
    keys: ['ctrl', 'o'],
    label: 'Quick Model Picker',
    description: 'Quick model selection with search',
    category: 'Chat',
  },
  {
    id: 'session-switcher',
    keys: ['ctrl', 'j'],
    label: 'Switch Session',
    description: 'Quick session switcher with fuzzy search',
    category: 'Chat',
  },
  {
    id: 'export-chat',
    keys: ['ctrl', 'shift', 'e'],
    label: 'Export Chat',
    description: 'Download conversation as Markdown',
    category: 'Chat',
  },
  {
    id: 'search-chat',
    keys: ['ctrl', 'f'],
    label: 'Search Chat',
    description: 'Search messages in current conversation',
    category: 'Chat',
  },
  {
    id: 'expanded-editor',
    keys: ['ctrl', 'e'],
    label: 'Expanded Editor',
    description: 'Open expanded editor for long prompts',
    category: 'Chat',
  },
  {
    id: 'toggle-terminal',
    keys: ['ctrl', '`'],
    label: 'Toggle Terminal',
    description: 'Show or hide the integrated terminal',
    category: 'General',
  },
  {
    id: 'undo-file-change',
    keys: ['ctrl', 'shift', 'z'],
    label: 'Undo File Change',
    description: 'Revert the last file modification made by an agent',
    category: 'Chat',
  },
  {
    id: 'redo-file-change',
    keys: ['ctrl', 'shift', 'y'],
    label: 'Redo File Change',
    description: 'Re-apply the last undone file change',
    category: 'Chat',
  },
  {
    id: 'stash-prompt',
    keys: ['ctrl', 'shift', 's'],
    label: 'Stash Prompt',
    description: 'Save current input to stash and clear',
    category: 'Chat',
  },
  {
    id: 'restore-prompt',
    keys: ['ctrl', 'shift', 'r'],
    label: 'Restore Prompt',
    description: 'Restore last stashed prompt to input',
    category: 'Chat',
  },
  {
    id: 'save-checkpoint',
    keys: ['ctrl', 'shift', 'c'],
    label: 'Save Checkpoint',
    description: 'Create a named checkpoint of the current conversation',
    category: 'Session',
  },
  {
    id: 'cycle-thinking',
    keys: ['ctrl', 't'],
    label: 'Cycle Thinking Level',
    description: 'Cycle reasoning effort: Off → Low → Medium → High → Max → Off',
    category: 'Chat',
  },
  {
    id: 'copy-last-response',
    keys: ['ctrl', 'y'],
    label: 'Copy Last Response',
    description: 'Copy the last assistant response to clipboard',
    category: 'Chat',
  },
]
