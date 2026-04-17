/**
 * Command Palette Component
 *
 * A searchable command palette (Cmd+K) for quick actions and navigation.
 * Supports fuzzy matching and keyboard navigation.
 */

import { Dialog } from '@kobalte/core/dialog'
import { Command, Search } from 'lucide-solid'
import { type Component, createEffect, createSignal, For, onCleanup, onMount, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import type { CommandItem, CommandPaletteProps } from './command-palette/types'
import { fuzzyMatch } from './command-palette/types'

export { createDefaultCommands } from './command-palette/default-commands'
// Re-export types and factory for backward compat
export type { CommandItem, CommandPaletteProps } from './command-palette/types'

// ============================================================================
// Command Palette Component
// ============================================================================

export const CommandPalette: Component<CommandPaletteProps> = (props) => {
  const [open, setOpen] = createSignal(false)
  const [query, setQuery] = createSignal('')
  const [selectedIndex, setSelectedIndex] = createSignal(0)

  let inputRef: HTMLInputElement | undefined

  // Filter commands based on query
  const filteredCommands = () => {
    const q = query().trim()
    if (!q) {
      // Show recent first, then all
      const recentSet = new Set(props.recentIds || [])
      const recent = props.commands.filter((c) => recentSet.has(c.id))
      const others = props.commands.filter((c) => !recentSet.has(c.id))
      return [...recent, ...others]
    }

    return props.commands.filter(
      (cmd) =>
        fuzzyMatch(q, cmd.label) ||
        (cmd.description && fuzzyMatch(q, cmd.description)) ||
        (cmd.category && fuzzyMatch(q, cmd.category))
    )
  }

  // Group commands by category
  const groupedCommands = () => {
    const groups: Record<string, CommandItem[]> = {}
    for (const cmd of filteredCommands()) {
      const category = cmd.category || 'Commands'
      if (!groups[category]) {
        groups[category] = []
      }
      groups[category].push(cmd)
    }
    return groups
  }

  // Flatten for keyboard navigation
  const flatCommands = () => filteredCommands()

  // Reset selection when query changes
  createEffect(() => {
    query() // Track
    setSelectedIndex(0)
  })

  // Keyboard shortcuts
  const handleGlobalKeyDown = (e: KeyboardEvent) => {
    // Open with Cmd+K or Ctrl+K or Ctrl+/
    if ((e.metaKey || e.ctrlKey) && (e.key === 'k' || e.key === '/')) {
      e.preventDefault()
      setOpen(true)
    }
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    const commands = flatCommands()

    switch (e.key) {
      case 'ArrowDown':
        e.preventDefault()
        setSelectedIndex((i) => Math.min(i + 1, commands.length - 1))
        break
      case 'ArrowUp':
        e.preventDefault()
        setSelectedIndex((i) => Math.max(i - 1, 0))
        break
      case 'Enter':
        e.preventDefault()
        if (commands[selectedIndex()]) {
          executeCommand(commands[selectedIndex()])
        }
        break
      case 'Escape':
        e.preventDefault()
        setOpen(false)
        break
    }
  }

  const executeCommand = (cmd: CommandItem) => {
    setOpen(false)
    setQuery('')
    cmd.action()
  }

  // Global keyboard listener
  onMount(() => {
    document.addEventListener('keydown', handleGlobalKeyDown)
  })

  onCleanup(() => {
    document.removeEventListener('keydown', handleGlobalKeyDown)
  })

  // Focus input when opened
  createEffect(() => {
    if (open() && inputRef) {
      setTimeout(() => inputRef?.focus(), 50)
    }
  })

  const handleOpenChange = (isOpen: boolean) => {
    setOpen(isOpen)
    if (!isOpen) {
      setQuery('')
      props.onClose?.()
    }
  }

  // Parse shortcut string into individual key badges
  const shortcutKeys = (shortcut: string): string[] => {
    return shortcut.split('+').map((k) => k.trim())
  }

  return (
    <Show when={open()}>
      <Dialog open={open()} onOpenChange={handleOpenChange}>
        <Dialog.Portal>
          {/* Overlay */}
          <Dialog.Overlay
            class="
            fixed inset-0 z-50
            data-[expanded]:animate-in data-[expanded]:fade-in-0
            data-[closed]:animate-out data-[closed]:fade-out-0
          "
            style={{ background: 'var(--modal-overlay)' }}
          />

          {/* Content */}
          <Dialog.Content
            class="
            fixed left-1/2 top-[20%] z-50
            -translate-x-1/2
            overflow-hidden
            data-[expanded]:animate-in data-[expanded]:fade-in-0 data-[expanded]:zoom-in-95
            data-[closed]:animate-out data-[closed]:fade-out-0 data-[closed]:zoom-out-95
            duration-200
          "
            style={{
              width: '640px',
              background: 'var(--modal-surface)',
              border: '1px solid var(--border-subtle)',
              'border-radius': 'var(--modal-radius-sm)',
              'box-shadow': 'var(--modal-shadow)',
            }}
            onKeyDown={handleKeyDown}
          >
            {/* Search Row — 48px */}
            <div
              class="flex items-center gap-2.5"
              style={{
                height: '48px',
                padding: '0 16px',
                background: 'var(--surface)',
              }}
            >
              <Search class="w-4 h-4 shrink-0 text-[var(--text-muted)]" />
              <input
                // biome-ignore lint/suspicious/noAssignInExpressions: SolidJS ref callback pattern
                ref={(el) => (inputRef = el)}
                type="text"
                placeholder="Type a command or search..."
                value={query()}
                onInput={(e) => setQuery(e.currentTarget.value)}
                class="
                flex-1
                bg-transparent
                text-[var(--text-primary)]
                placeholder:text-[var(--text-muted)]
                outline-none
              "
                style={{
                  'font-family': 'var(--font-sans)',
                  'font-size': '14px',
                }}
              />
              <kbd
                class="flex items-center shrink-0"
                style={{
                  padding: '3px 8px',
                  background: 'var(--alpha-white-8)',
                  border: '1px solid var(--border-default)',
                  'border-radius': '4px',
                  'font-family': 'var(--font-mono)',
                  'font-size': '10px',
                  color: 'var(--text-muted)',
                }}
              >
                Esc
              </kbd>
            </div>

            {/* Divider */}
            <div style={{ height: '1px', background: 'var(--border-default)' }} />

            {/* Results */}
            <div class="overflow-y-auto" style={{ 'max-height': '320px' }}>
              <Show
                when={flatCommands().length > 0}
                fallback={
                  <div
                    class="flex items-center justify-center text-[var(--text-muted)]"
                    style={{ padding: '32px 0', 'font-size': '13px' }}
                  >
                    No commands found
                  </div>
                }
              >
                <For each={Object.entries(groupedCommands())}>
                  {([category, commands], groupIdx) => (
                    <>
                      {/* Divider between sections (not before first) */}
                      <Show when={groupIdx() > 0}>
                        <div style={{ height: '1px', background: 'var(--border-default)' }} />
                      </Show>

                      {/* Section */}
                      <div style={{ padding: '8px 8px 4px 8px' }}>
                        {/* Section label — 9px uppercase */}
                        <div
                          style={{
                            'font-family': 'var(--font-sans)',
                            'font-size': '9px',
                            'font-weight': '600',
                            'letter-spacing': '1px',
                            'text-transform': 'uppercase',
                            color: 'var(--text-muted)',
                            padding: '0 10px 4px',
                          }}
                        >
                          {category}
                        </div>

                        {/* Rows */}
                        <div class="flex flex-col" style={{ gap: '2px' }}>
                          <For each={commands}>
                            {(cmd) => {
                              const globalIndex = () =>
                                flatCommands().findIndex((c) => c.id === cmd.id)
                              const isSelected = () => selectedIndex() === globalIndex()

                              return (
                                <button
                                  type="button"
                                  onClick={() => executeCommand(cmd)}
                                  onMouseEnter={() => setSelectedIndex(globalIndex())}
                                  class="w-full text-left flex items-center transition-colors"
                                  style={{
                                    height: '36px',
                                    padding: '0 10px',
                                    gap: '10px',
                                    'border-radius': 'var(--radius-sm)',
                                    background: isSelected() ? 'var(--accent)' : 'transparent',
                                    color: isSelected() ? '#ffffff' : 'inherit',
                                    'justify-content': cmd.shortcut
                                      ? 'space-between'
                                      : 'flex-start',
                                  }}
                                >
                                  {/* Left: Icon + Label */}
                                  <div class="flex items-center" style={{ gap: '10px' }}>
                                    {/* Icon — no bg wrapper, 14px */}
                                    <span
                                      class="shrink-0 flex items-center"
                                      style={{
                                        color: isSelected() ? '#ffffff' : 'var(--text-muted)',
                                      }}
                                    >
                                      <Show
                                        when={cmd.icon}
                                        fallback={<Command class="w-3.5 h-3.5" />}
                                      >
                                        <Dynamic component={cmd.icon!} class="w-3.5 h-3.5" />
                                      </Show>
                                    </span>

                                    {/* Label — 13px */}
                                    <span
                                      style={{
                                        'font-family': 'var(--font-sans)',
                                        'font-size': '13px',
                                        color: isSelected() ? '#ffffff' : 'var(--text-secondary)',
                                      }}
                                    >
                                      {cmd.label}
                                    </span>
                                  </div>

                                  {/* Right: Keyboard shortcut badges */}
                                  <Show when={cmd.shortcut}>
                                    <div class="flex items-center" style={{ gap: '4px' }}>
                                      <For each={shortcutKeys(cmd.shortcut!)}>
                                        {(key) => (
                                          <kbd
                                            style={{
                                              padding: '2px 6px',
                                              background: 'var(--alpha-white-8)',
                                              'border-radius': '4px',
                                              'font-family': 'var(--font-mono)',
                                              'font-size': '10px',
                                              color: 'var(--text-muted)',
                                            }}
                                          >
                                            {key}
                                          </kbd>
                                        )}
                                      </For>
                                    </div>
                                  </Show>
                                </button>
                              )
                            }}
                          </For>
                        </div>
                      </div>
                    </>
                  )}
                </For>
              </Show>
            </div>
          </Dialog.Content>
        </Dialog.Portal>
      </Dialog>
    </Show>
  )
}
