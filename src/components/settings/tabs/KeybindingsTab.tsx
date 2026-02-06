/**
 * Keybindings Settings Tab
 *
 * Modern 2026 aesthetic using semantic CSS tokens.
 * View and customize keyboard shortcuts.
 */

import { Command, Keyboard, RotateCcw, Search } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'

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
    <div class="flex items-center gap-[var(--space-1)]">
      <For each={props.keys}>
        {(key, index) => (
          <>
            <kbd
              class="
                px-[var(--space-2)] py-[var(--space-1)]
                bg-[var(--surface-raised)]
                border border-[var(--border-default)]
                rounded-[var(--radius-md)]
                text-[var(--text-xs)] font-[var(--font-mono)] font-medium
                text-[var(--text-primary)]
                shadow-[0_1px_0_var(--alpha-black-20)]
              "
            >
              {formatKey(key)}
            </kbd>
            <Show when={index() < props.keys.length - 1}>
              <span class="text-[var(--text-muted)] text-[var(--text-xs)]">+</span>
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
          <h3 class="text-[var(--text-lg)] font-semibold text-[var(--text-primary)]">
            Keyboard Shortcuts
          </h3>
          <p class="text-[var(--text-xs)] text-[var(--text-tertiary)] mt-[var(--space-0_5)]">
            {customCount() > 0 ? `${customCount()} custom bindings` : 'Using default bindings'}
          </p>
        </div>
        <Show when={customCount() > 0 && props.onResetAll}>
          <button
            type="button"
            onClick={() => props.onResetAll?.()}
            class="
              flex items-center gap-[var(--space-1_5)] px-[var(--space-3)] py-[var(--space-1_5)]
              text-[var(--text-secondary)]
              hover:text-[var(--text-primary)]
              hover:bg-[var(--button-ghost-hover)]
              text-[var(--text-sm)] font-medium
              rounded-[var(--radius-lg)]
              transition-colors duration-[var(--duration-fast)]
            "
          >
            <RotateCcw class="w-4 h-4" />
            Reset All
          </button>
        </Show>
      </div>

      {/* Search and Filter */}
      <div class="flex items-center gap-[var(--space-3)]">
        <div class="flex-1 relative">
          <Search class="absolute left-[var(--space-3)] top-1/2 -translate-y-1/2 w-4 h-4 text-[var(--text-muted)]" />
          <input
            type="text"
            placeholder="Search shortcuts..."
            value={searchQuery()}
            onInput={(e) => setSearchQuery(e.currentTarget.value)}
            class="
              w-full pl-[var(--space-10)] pr-[var(--space-4)] py-[var(--space-2_5)]
              bg-[var(--input-background)]
              border border-[var(--input-border)]
              rounded-[var(--radius-lg)]
              text-[var(--text-sm)] text-[var(--text-primary)]
              placeholder:text-[var(--input-placeholder)]
              focus:outline-none focus:border-[var(--input-border-focus)]
              focus:shadow-[0_0_0_3px_var(--input-focus-ring)]
              transition-colors duration-[var(--duration-fast)]
            "
          />
        </div>

        {/* Category Filter */}
        <select
          value={selectedCategory() ?? ''}
          onChange={(e) => setSelectedCategory(e.currentTarget.value || null)}
          class="
            px-[var(--space-3)] py-[var(--space-2_5)]
            bg-[var(--input-background)]
            border border-[var(--input-border)]
            rounded-[var(--radius-lg)]
            text-[var(--text-sm)] text-[var(--text-primary)]
            focus:outline-none focus:border-[var(--input-border-focus)]
            transition-colors duration-[var(--duration-fast)]
          "
        >
          <option value="">All Categories</option>
          <For each={categories()}>{(cat) => <option value={cat}>{cat}</option>}</For>
        </select>
      </div>

      {/* Keybindings List */}
      <div class="space-y-[var(--space-4)]">
        <Show
          when={Object.keys(groupedKeybindings()).length > 0}
          fallback={
            <div class="py-[var(--space-12)] text-center">
              <div class="w-12 h-12 mx-auto mb-[var(--space-3)] rounded-full bg-[var(--alpha-white-5)] flex items-center justify-center">
                <Keyboard class="w-6 h-6 text-[var(--text-muted)]" />
              </div>
              <p class="text-[var(--text-sm)] text-[var(--text-secondary)]">No shortcuts found</p>
            </div>
          }
        >
          <For each={Object.entries(groupedKeybindings())}>
            {([category, bindings]) => (
              <div>
                <h4 class="text-[var(--text-xs)] font-medium text-[var(--text-muted)] mb-[var(--space-2)] flex items-center gap-[var(--space-2)] uppercase tracking-wider">
                  <Command class="w-3 h-3" />
                  {category}
                </h4>
                <div class="space-y-[var(--space-1)]">
                  <For each={bindings}>
                    {(kb) => (
                      <div
                        class={`
                          flex items-center justify-between
                          p-[var(--space-2_5)] rounded-[var(--radius-lg)]
                          hover:bg-[var(--alpha-white-5)]
                          transition-colors duration-[var(--duration-fast)]
                          ${kb.isCustom ? 'border-l-2 border-[var(--accent)]' : ''}
                        `}
                      >
                        <div class="flex-1 min-w-0">
                          <div class="text-[var(--text-sm)] text-[var(--text-primary)]">
                            {kb.action}
                          </div>
                          <div class="text-[var(--text-xs)] text-[var(--text-muted)]">
                            {kb.description}
                          </div>
                        </div>
                        <div class="flex items-center gap-[var(--space-2)]">
                          <KeyDisplay keys={kb.keys} />
                          <Show when={props.onEdit}>
                            <button
                              type="button"
                              onClick={() => props.onEdit?.(kb.id)}
                              class="
                                px-[var(--space-2)] py-[var(--space-1)] text-[var(--text-xs)]
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
                                px-[var(--space-2)] py-[var(--space-1)] text-[var(--text-xs)]
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

      {/* Info Banner */}
      <div
        class="
          flex items-start gap-[var(--space-3)] p-[var(--space-4)]
          bg-[var(--info-subtle)]
          border border-[var(--info-border)]
          rounded-[var(--radius-xl)]
        "
      >
        <div class="w-8 h-8 rounded-full bg-[var(--info-subtle)] flex items-center justify-center flex-shrink-0">
          <Keyboard class="w-4 h-4 text-[var(--info)]" />
        </div>
        <p class="text-[var(--text-sm)] text-[var(--text-secondary)] leading-relaxed">
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
