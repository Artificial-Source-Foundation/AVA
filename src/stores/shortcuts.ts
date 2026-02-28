/**
 * Centralized Keyboard Shortcuts Store
 *
 * Single global keydown listener. Shortcuts are reactive — editing a
 * binding in settings immediately takes effect. Custom overrides are
 * persisted to localStorage.
 */

import { createSignal } from 'solid-js'
import { STORAGE_KEYS } from '../config/constants'

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

export interface ShortcutAction extends ShortcutDef {
  action: () => void
}

// ============================================================================
// Default Shortcut Definitions (keys only — actions bound at setup)
// ============================================================================

const DEFAULT_SHORTCUTS: ShortcutDef[] = [
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
]

// ============================================================================
// Persistence
// ============================================================================

const STORAGE_KEY = STORAGE_KEYS.SHORTCUTS ?? 'ava_shortcuts'

function loadOverrides(): Record<string, string[]> {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    if (raw) return JSON.parse(raw) as Record<string, string[]>
  } catch {
    /* ignore */
  }
  return {}
}

function saveOverrides(overrides: Record<string, string[]>) {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(overrides))
  } catch {
    /* ignore */
  }
}

// ============================================================================
// State
// ============================================================================

// Custom key overrides (id → keys)
const [overrides, setOverrides] = createSignal<Record<string, string[]>>(loadOverrides())

// Registered actions (bound at setup time)
const actionMap = new Map<string, () => void>()

// ============================================================================
// Key Matching
// ============================================================================

function normalizeKey(key: string): string {
  const lower = key.toLowerCase()
  if (lower === 'control' || lower === 'meta') return 'ctrl'
  return lower
}

function matchesShortcut(e: KeyboardEvent, keys: string[]): boolean {
  const pressed = new Set<string>()
  if (e.ctrlKey || e.metaKey) pressed.add('ctrl')
  if (e.shiftKey) pressed.add('shift')
  if (e.altKey) pressed.add('alt')

  // Add the actual key (not modifiers)
  const actual = normalizeKey(e.key)
  if (actual !== 'ctrl' && actual !== 'shift' && actual !== 'alt') {
    pressed.add(actual)
  }

  if (pressed.size !== keys.length) return false
  return keys.every((k) => pressed.has(k.toLowerCase()))
}

// ============================================================================
// Resolved Shortcuts (defaults + overrides merged)
// ============================================================================

function getResolvedShortcuts(): ShortcutDef[] {
  const ovr = overrides()
  return DEFAULT_SHORTCUTS.map((s) => {
    if (ovr[s.id]) {
      return { ...s, keys: ovr[s.id], isCustom: true }
    }
    return { ...s, isCustom: false }
  })
}

// ============================================================================
// Public API
// ============================================================================

/**
 * Register an action for a shortcut ID. Called during app setup.
 */
function registerAction(id: string, action: () => void) {
  actionMap.set(id, action)
}

/**
 * Update the key binding for a shortcut. Persists to localStorage.
 */
function updateShortcut(id: string, keys: string[]) {
  const current = { ...overrides() }
  // Check if this matches the default — if so, remove override
  const def = DEFAULT_SHORTCUTS.find((s) => s.id === id)
  if (def && arraysEqual(def.keys, keys)) {
    delete current[id]
  } else {
    current[id] = keys
  }
  setOverrides(current)
  saveOverrides(current)
}

/**
 * Reset a single shortcut to default.
 */
function resetShortcut(id: string) {
  const current = { ...overrides() }
  delete current[id]
  setOverrides(current)
  saveOverrides(current)
}

/**
 * Reset all shortcuts to defaults.
 */
function resetAllShortcuts() {
  setOverrides({})
  saveOverrides({})
}

function arraysEqual(a: string[], b: string[]): boolean {
  if (a.length !== b.length) return false
  const sortedA = [...a].sort()
  const sortedB = [...b].sort()
  return sortedA.every((v, i) => v === sortedB[i])
}

/**
 * Install the global keydown listener. Returns cleanup function.
 */
function setupShortcutListener(): () => void {
  const handler = (e: KeyboardEvent) => {
    // Don't fire shortcuts when typing in inputs
    const tag = (e.target as HTMLElement)?.tagName
    const isInput = tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT'

    for (const shortcut of getResolvedShortcuts()) {
      if (matchesShortcut(e, shortcut.keys)) {
        // Allow Ctrl+K and Ctrl+F even in inputs
        if (
          isInput &&
          shortcut.id !== 'command-palette' &&
          shortcut.id !== 'search-chat' &&
          shortcut.id !== 'expanded-editor' &&
          shortcut.id !== 'stash-prompt' &&
          shortcut.id !== 'restore-prompt' &&
          shortcut.id !== 'save-checkpoint'
        )
          continue

        const action = actionMap.get(shortcut.id)
        if (action) {
          e.preventDefault()
          action()
          return
        }
      }
    }
  }

  document.addEventListener('keydown', handler)
  return () => document.removeEventListener('keydown', handler)
}

// ============================================================================
// Hook
// ============================================================================

export function useShortcuts() {
  return {
    /** Resolved shortcuts (defaults + overrides) */
    shortcuts: getResolvedShortcuts,
    /** Register an action callback for a shortcut ID */
    registerAction,
    /** Update key binding for a shortcut */
    updateShortcut,
    /** Reset one shortcut to default */
    resetShortcut,
    /** Reset all shortcuts to defaults */
    resetAll: resetAllShortcuts,
    /** Custom override count */
    customCount: () => Object.keys(overrides()).length,
    /** Install global keydown listener, returns cleanup */
    setupShortcutListener,
  }
}
