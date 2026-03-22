/**
 * Keybindings Settings Tab
 *
 * Bento-grid layout using SettingsCard, matching AppearanceTab/BehaviorTab.
 * View and customize keyboard shortcuts.
 */

import { Code2, Compass, Keyboard, MessageCircle, Settings2 } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { SettingsCard } from '../SettingsCard'
import { SETTINGS_CARD_GAP } from '../settings-constants'

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

const KeyCombo: Component<{ keys: string[] }> = (props) => (
  <span class="text-[var(--settings-text-input)] font-mono text-[var(--text-muted)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded px-1.5 py-0.5">
    {props.keys.map((k) => formatKey(k)).join(' + ')}
  </span>
)

// ============================================================================
// Keybindings Tab Component
// ============================================================================

export const KeybindingsTab: Component<KeybindingsTabProps> = (props) => {
  const [searchQuery, setSearchQuery] = createSignal('')

  const customCount = () => props.keybindings.filter((k) => k.isCustom).length

  const filteredKeybindings = () => {
    const query = searchQuery().toLowerCase()
    if (!query) return props.keybindings
    return props.keybindings.filter(
      (k) =>
        k.action.toLowerCase().includes(query) ||
        k.description.toLowerCase().includes(query) ||
        k.keys.some((key) => key.toLowerCase().includes(query))
    )
  }

  // Group by category
  const grouped = () => {
    const groups: Record<string, Keybinding[]> = {}
    for (const kb of filteredKeybindings()) {
      if (!groups[kb.category]) groups[kb.category] = []
      groups[kb.category].push(kb)
    }
    return groups
  }

  const categoryMeta: Record<string, { icon: Component<{ class?: string }>; description: string }> =
    {
      General: { icon: Settings2, description: 'Global shortcuts' },
      Navigation: { icon: Compass, description: 'Tab and panel shortcuts' },
      Chat: { icon: MessageCircle, description: 'Message and conversation shortcuts' },
      Editor: { icon: Code2, description: 'Text editing shortcuts' },
    }

  const defaultMeta = { icon: Keyboard, description: 'Keyboard shortcuts' }

  return (
    <div class="space-y-5">
      {/* Search + Reset All */}
      <div class="space-y-2">
        <input
          type="text"
          placeholder="Search shortcuts..."
          value={searchQuery()}
          onInput={(e) => setSearchQuery(e.currentTarget.value)}
          class="
            w-full px-3 py-2
            bg-[var(--input-background)]
            border border-[var(--input-border)]
            rounded-[var(--radius-md)]
            text-[var(--settings-text-input)] text-[var(--text-primary)]
            placeholder:text-[var(--input-placeholder)]
            focus:outline-none focus:border-[var(--input-border-focus)]
            transition-colors
          "
        />

        <Show when={customCount() > 0 && props.onResetAll}>
          <div class="flex items-center justify-between py-1">
            <span class="text-[var(--settings-text-badge)] text-[var(--text-muted)]">
              {customCount()} customized
            </span>
            <button
              type="button"
              onClick={() => props.onResetAll?.()}
              class="text-[var(--settings-text-badge)] text-[var(--text-muted)] hover:text-[var(--warning)] transition-colors"
            >
              Reset all
            </button>
          </div>
        </Show>
      </div>

      {/* Category cards in bento grid */}
      <Show
        when={filteredKeybindings().length > 0}
        fallback={
          <p class="text-[var(--settings-text-description)] text-[var(--text-muted)] text-center py-6">
            No shortcuts found
          </p>
        }
      >
        <div class="grid grid-cols-1" style={{ gap: SETTINGS_CARD_GAP }}>
          <For each={Object.entries(grouped())}>
            {([category, bindings]) => {
              const meta = categoryMeta[category] ?? defaultMeta
              return (
                <SettingsCard icon={meta.icon} title={category} description={meta.description}>
                  <div class="space-y-0.5">
                    <For each={bindings}>
                      {(kb) => (
                        <div class="flex items-center justify-between py-1.5 group">
                          <div class="flex-1 min-w-0">
                            <span class="text-[var(--settings-text-label)] text-[var(--text-secondary)]">
                              {kb.action}
                            </span>
                            <Show when={kb.isCustom}>
                              <span class="ml-1.5 text-[var(--settings-text-caption)] text-[var(--accent)]">
                                modified
                              </span>
                            </Show>
                          </div>
                          <div class="flex items-center gap-2">
                            <KeyCombo keys={kb.keys} />
                            <Show when={props.onEdit}>
                              <button
                                type="button"
                                onClick={() => props.onEdit?.(kb.id)}
                                class="text-[var(--settings-text-button)] text-[var(--text-muted)] hover:text-[var(--accent)] opacity-0 group-hover:opacity-100 transition-[color,opacity]"
                              >
                                Edit
                              </button>
                            </Show>
                            <Show when={kb.isCustom && props.onReset}>
                              <button
                                type="button"
                                onClick={() => props.onReset?.(kb.id)}
                                class="text-[var(--settings-text-button)] text-[var(--text-muted)] hover:text-[var(--warning)] opacity-0 group-hover:opacity-100 transition-[color,opacity]"
                              >
                                Reset
                              </button>
                            </Show>
                          </div>
                        </div>
                      )}
                    </For>
                  </div>
                </SettingsCard>
              )
            }}
          </For>
        </div>
      </Show>
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
