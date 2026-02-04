/**
 * Keybindings Settings Tab
 *
 * View and customize keyboard shortcuts.
 */

import { Command, Keyboard, RotateCcw, Search } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { Button } from '../../ui/Button'

// ============================================================================
// Types
// ============================================================================

export interface Keybinding {
  id: string
  action: string
  description: string
  keys: string[]
  category: string
  isCustom?: boolean
}

export interface KeybindingsTabProps {
  keybindings: Keybinding[]
  onEdit?: (id: string) => void
  onReset?: (id: string) => void
  onResetAll?: () => void
}

// ============================================================================
// Key Display Component
// ============================================================================

const KeyDisplay: Component<{ keys: string[] }> = (props) => {
  return (
    <div class="flex items-center gap-1">
      <For each={props.keys}>
        {(key, index) => (
          <>
            <kbd
              class="
              px-2 py-1
              bg-[var(--surface-sunken)]
              border border-[var(--border-default)]
              rounded-[var(--radius-md)]
              text-xs font-mono font-medium
              text-[var(--text-primary)]
              shadow-sm
            "
            >
              {formatKey(key)}
            </kbd>
            <Show when={index() < props.keys.length - 1}>
              <span class="text-[var(--text-muted)] text-xs">+</span>
            </Show>
          </>
        )}
      </For>
    </div>
  )
}

const formatKey = (key: string): string => {
  const keyMap: Record<string, string> = {
    meta: '⌘',
    ctrl: 'Ctrl',
    alt: 'Alt',
    shift: '⇧',
    enter: '↵',
    escape: 'Esc',
    backspace: '⌫',
    delete: 'Del',
    tab: 'Tab',
    space: 'Space',
    arrowup: '↑',
    arrowdown: '↓',
    arrowleft: '←',
    arrowright: '→',
  }
  return keyMap[key.toLowerCase()] || key.toUpperCase()
}

// ============================================================================
// Keybindings Tab Component
// ============================================================================

export const KeybindingsTab: Component<KeybindingsTabProps> = (props) => {
  const [searchQuery, setSearchQuery] = createSignal('')
  const [selectedCategory, setSelectedCategory] = createSignal<string | null>(null)

  // Get unique categories
  const categories = () => {
    const cats = new Set(props.keybindings.map((k) => k.category))
    return Array.from(cats).sort()
  }

  // Filter keybindings
  const filteredKeybindings = () => {
    let filtered = props.keybindings

    if (selectedCategory()) {
      filtered = filtered.filter((k) => k.category === selectedCategory())
    }

    const query = searchQuery().toLowerCase()
    if (query) {
      filtered = filtered.filter(
        (k) =>
          k.action.toLowerCase().includes(query) ||
          k.description.toLowerCase().includes(query) ||
          k.keys.some((key) => key.toLowerCase().includes(query))
      )
    }

    return filtered
  }

  // Group by category
  const groupedKeybindings = () => {
    const groups: Record<string, Keybinding[]> = {}
    for (const kb of filteredKeybindings()) {
      if (!groups[kb.category]) {
        groups[kb.category] = []
      }
      groups[kb.category].push(kb)
    }
    return groups
  }

  const customCount = () => props.keybindings.filter((k) => k.isCustom).length

  return (
    <div class="space-y-6">
      {/* Header */}
      <div class="flex items-center justify-between">
        <div>
          <h3 class="text-sm font-medium text-[var(--text-primary)]">Keyboard Shortcuts</h3>
          <p class="text-xs text-[var(--text-muted)] mt-0.5">
            {customCount() > 0 ? `${customCount()} custom bindings` : 'Using default bindings'}
          </p>
        </div>
        <Show when={customCount() > 0 && props.onResetAll}>
          <Button variant="ghost" size="sm" onClick={props.onResetAll}>
            <RotateCcw class="w-4 h-4 mr-1" />
            Reset All
          </Button>
        </Show>
      </div>

      {/* Search and Filter */}
      <div class="flex items-center gap-3">
        <div class="flex-1 relative">
          <Search class="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-[var(--text-muted)]" />
          <input
            type="text"
            placeholder="Search shortcuts..."
            value={searchQuery()}
            onInput={(e) => setSearchQuery(e.currentTarget.value)}
            class="
              w-full pl-10 pr-4 py-2
              bg-[var(--input-background)]
              border border-[var(--input-border)]
              rounded-[var(--radius-lg)]
              text-sm text-[var(--text-primary)]
              placeholder:text-[var(--text-muted)]
              focus:outline-none focus:border-[var(--accent)]
              transition-colors duration-[var(--duration-fast)]
            "
          />
        </div>

        {/* Category Filter */}
        <select
          value={selectedCategory() ?? ''}
          onChange={(e) => setSelectedCategory(e.currentTarget.value || null)}
          class="
            px-3 py-2
            bg-[var(--input-background)]
            border border-[var(--input-border)]
            rounded-[var(--radius-lg)]
            text-sm text-[var(--text-primary)]
            focus:outline-none focus:border-[var(--accent)]
            transition-colors duration-[var(--duration-fast)]
          "
        >
          <option value="">All Categories</option>
          <For each={categories()}>{(cat) => <option value={cat}>{cat}</option>}</For>
        </select>
      </div>

      {/* Keybindings List */}
      <div class="max-h-80 overflow-y-auto space-y-4 -mx-4 px-4">
        <Show
          when={Object.keys(groupedKeybindings()).length > 0}
          fallback={
            <div class="py-8 text-center">
              <Keyboard class="w-10 h-10 mx-auto mb-3 text-[var(--text-muted)]" />
              <p class="text-sm text-[var(--text-secondary)]">No shortcuts found</p>
            </div>
          }
        >
          <For each={Object.entries(groupedKeybindings())}>
            {([category, bindings]) => (
              <div>
                <h4 class="text-xs font-medium text-[var(--text-muted)] mb-2 flex items-center gap-2">
                  <Command class="w-3 h-3" />
                  {category}
                </h4>
                <div class="space-y-1">
                  <For each={bindings}>
                    {(kb) => (
                      <div
                        class={`
                          flex items-center justify-between
                          p-2.5 rounded-[var(--radius-lg)]
                          hover:bg-[var(--surface-raised)]
                          transition-colors duration-[var(--duration-fast)]
                          ${kb.isCustom ? 'border-l-2 border-[var(--accent)]' : ''}
                        `}
                      >
                        <div class="flex-1 min-w-0">
                          <div class="text-sm text-[var(--text-primary)]">{kb.action}</div>
                          <div class="text-xs text-[var(--text-muted)]">{kb.description}</div>
                        </div>
                        <div class="flex items-center gap-2">
                          <KeyDisplay keys={kb.keys} />
                          <Show when={props.onEdit}>
                            <button
                              type="button"
                              onClick={() => props.onEdit?.(kb.id)}
                              class="
                                px-2 py-1 text-xs
                                text-[var(--text-muted)]
                                hover:text-[var(--accent)]
                                hover:bg-[var(--accent-subtle)]
                                rounded-[var(--radius-md)]
                                transition-colors duration-[var(--duration-fast)]
                              "
                            >
                              Edit
                            </button>
                          </Show>
                          <Show when={kb.isCustom && props.onReset}>
                            <button
                              type="button"
                              onClick={() => props.onReset?.(kb.id)}
                              class="
                                px-2 py-1 text-xs
                                text-[var(--text-muted)]
                                hover:text-[var(--warning)]
                                hover:bg-[var(--warning-subtle)]
                                rounded-[var(--radius-md)]
                                transition-colors duration-[var(--duration-fast)]
                              "
                            >
                              Reset
                            </button>
                          </Show>
                        </div>
                      </div>
                    )}
                  </For>
                </div>
              </div>
            )}
          </For>
        </Show>
      </div>

      {/* Info */}
      <div class="flex items-start gap-3 p-3 bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-lg)]">
        <Keyboard class="w-5 h-5 text-[var(--info)] flex-shrink-0 mt-0.5" />
        <p class="text-sm text-[var(--text-secondary)]">
          Click "Edit" to customize a shortcut. Custom shortcuts are highlighted with an accent
          border.
        </p>
      </div>
    </div>
  )
}

// ============================================================================
// Default Keybindings
// ============================================================================

export const defaultKeybindings: Keybinding[] = [
  // General
  {
    id: 'command-palette',
    action: 'Open Command Palette',
    description: 'Quick access to all commands',
    keys: ['meta', 'k'],
    category: 'General',
  },
  {
    id: 'settings',
    action: 'Open Settings',
    description: 'Open the settings modal',
    keys: ['meta', ','],
    category: 'General',
  },
  {
    id: 'new-chat',
    action: 'New Chat',
    description: 'Start a new conversation',
    keys: ['meta', 'n'],
    category: 'General',
  },

  // Navigation
  {
    id: 'tab-chat',
    action: 'Go to Chat',
    description: 'Switch to Chat tab',
    keys: ['meta', '1'],
    category: 'Navigation',
  },
  {
    id: 'tab-agents',
    action: 'Go to Agents',
    description: 'Switch to Agents tab',
    keys: ['meta', '2'],
    category: 'Navigation',
  },
  {
    id: 'tab-files',
    action: 'Go to Files',
    description: 'Switch to Files tab',
    keys: ['meta', '3'],
    category: 'Navigation',
  },
  {
    id: 'tab-memory',
    action: 'Go to Memory',
    description: 'Switch to Memory tab',
    keys: ['meta', '4'],
    category: 'Navigation',
  },
  {
    id: 'tab-terminal',
    action: 'Go to Terminal',
    description: 'Switch to Terminal tab',
    keys: ['meta', '5'],
    category: 'Navigation',
  },

  // Chat
  {
    id: 'send-message',
    action: 'Send Message',
    description: 'Send the current message',
    keys: ['enter'],
    category: 'Chat',
  },
  {
    id: 'newline',
    action: 'New Line',
    description: 'Insert a new line',
    keys: ['shift', 'enter'],
    category: 'Chat',
  },
  {
    id: 'clear-chat',
    action: 'Clear Chat',
    description: 'Clear current conversation',
    keys: ['meta', 'shift', 'k'],
    category: 'Chat',
  },

  // Editor
  {
    id: 'copy',
    action: 'Copy',
    description: 'Copy selected text',
    keys: ['meta', 'c'],
    category: 'Editor',
  },
  {
    id: 'paste',
    action: 'Paste',
    description: 'Paste from clipboard',
    keys: ['meta', 'v'],
    category: 'Editor',
  },
  {
    id: 'undo',
    action: 'Undo',
    description: 'Undo last action',
    keys: ['meta', 'z'],
    category: 'Editor',
  },
  {
    id: 'redo',
    action: 'Redo',
    description: 'Redo last undone action',
    keys: ['meta', 'shift', 'z'],
    category: 'Editor',
  },
]
