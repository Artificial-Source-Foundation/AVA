/**
 * Keybindings Settings Tab
 *
 * Pencil design: search input (rounded-8, #ffffff08 bg, #ffffff0a border),
 * category cards (#111114, rounded-12, 12px gap between rows),
 * key combos in Geist Mono 11px #48484A, labels in 13px #C8C8CC.
 */

import { Compass, Keyboard, MessageCircle, Settings2 } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { SettingsCard } from '../SettingsCard'

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
    meta: 'Ctrl',
    ctrl: 'Ctrl',
    alt: 'Alt',
    shift: 'Shift',
    enter: 'Enter',
    escape: 'Esc',
    backspace: 'Backspace',
    delete: 'Del',
    tab: 'Tab',
    space: 'Space',
    arrowup: 'Up',
    arrowdown: 'Down',
    arrowleft: 'Left',
    arrowright: 'Right',
  }
  return keyMap[key.toLowerCase()] || key.toUpperCase()
}

const KeyCombo: Component<{ keys: string[] }> = (props) => (
  <span
    style={{
      'font-family': 'Geist Mono, monospace',
      'font-size': '11px',
      'font-weight': '400',
      color: '#48484A',
    }}
  >
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
    }

  const defaultMeta = { icon: Keyboard, description: 'Keyboard shortcuts' }

  return (
    <div class="flex flex-col" style={{ gap: '24px' }}>
      {/* Page title */}
      <h2
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '22px',
          'font-weight': '600',
          color: '#F5F5F7',
          margin: '0',
        }}
      >
        Shortcuts
      </h2>

      {/* Search input */}
      <input
        type="text"
        placeholder="Search shortcuts..."
        value={searchQuery()}
        onInput={(e) => setSearchQuery(e.currentTarget.value)}
        style={{
          width: '100%',
          padding: '8px 12px',
          background: '#ffffff08',
          border: '1px solid #ffffff0a',
          'border-radius': '8px',
          'font-family': 'Geist, sans-serif',
          'font-size': '12px',
          color: '#F5F5F7',
          outline: 'none',
        }}
      />

      <Show when={customCount() > 0 && props.onResetAll}>
        <div class="flex items-center justify-between">
          <span
            style={{ 'font-family': 'Geist, sans-serif', 'font-size': '11px', color: '#48484A' }}
          >
            {customCount()} customized
          </span>
          <button
            type="button"
            onClick={() => props.onResetAll?.()}
            style={{
              'font-family': 'Geist, sans-serif',
              'font-size': '11px',
              color: '#48484A',
              background: 'none',
              border: 'none',
              cursor: 'pointer',
            }}
          >
            Reset all
          </button>
        </div>
      </Show>

      {/* Category cards */}
      <Show
        when={filteredKeybindings().length > 0}
        fallback={
          <p
            class="text-center py-6"
            style={{ 'font-family': 'Geist, sans-serif', 'font-size': '12px', color: '#48484A' }}
          >
            No shortcuts found
          </p>
        }
      >
        <div class="flex flex-col" style={{ gap: '24px' }}>
          <For each={Object.entries(grouped())}>
            {([category, bindings]) => {
              const meta = categoryMeta[category] ?? defaultMeta
              return (
                <SettingsCard icon={meta.icon} title={category}>
                  <div class="flex flex-col" style={{ gap: '0px' }}>
                    <For each={bindings}>
                      {(kb) => (
                        <div
                          class="flex items-center justify-between group"
                          style={{ padding: '6px 0' }}
                        >
                          <div class="flex items-center gap-1.5 min-w-0">
                            <span
                              style={{
                                'font-family': 'Geist, sans-serif',
                                'font-size': '13px',
                                color: '#C8C8CC',
                              }}
                            >
                              {kb.action}
                            </span>
                            <Show when={kb.isCustom}>
                              <span
                                style={{
                                  'font-family': 'Geist, sans-serif',
                                  'font-size': '10px',
                                  color: '#0A84FF',
                                }}
                              >
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
                                class="opacity-0 group-hover:opacity-100 transition-opacity"
                                style={{
                                  'font-family': 'Geist, sans-serif',
                                  'font-size': '11px',
                                  color: '#48484A',
                                  background: 'none',
                                  border: 'none',
                                  cursor: 'pointer',
                                }}
                              >
                                Edit
                              </button>
                            </Show>
                            <Show when={kb.isCustom && props.onReset}>
                              <button
                                type="button"
                                onClick={() => props.onReset?.(kb.id)}
                                class="opacity-0 group-hover:opacity-100 transition-opacity"
                                style={{
                                  'font-family': 'Geist, sans-serif',
                                  'font-size': '11px',
                                  color: '#48484A',
                                  background: 'none',
                                  border: 'none',
                                  cursor: 'pointer',
                                }}
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
    id: 'toggle-sidebar',
    action: 'Toggle Sidebar',
    description: 'Show or hide the sidebar',
    keys: ['meta', 's'],
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
]
