/**
 * Command Palette Component
 *
 * A searchable command palette (Cmd+K) for quick actions and navigation.
 * Supports fuzzy matching and keyboard navigation.
 */

import { Dialog } from '@kobalte/core/dialog'
import {
  ArrowDownCircle,
  BarChart3,
  BookmarkPlus,
  Command,
  Download,
  FileText,
  Flag,
  Library,
  Megaphone,
  MessageSquare,
  Search,
  Settings,
  Sparkles,
  Terminal,
  Upload,
} from 'lucide-solid'
import {
  type Component,
  createEffect,
  createSignal,
  For,
  type JSX,
  onCleanup,
  onMount,
  Show,
} from 'solid-js'
import { Dynamic } from 'solid-js/web'

// ============================================================================
// Types
// ============================================================================

type IconComponent = (props: { class?: string }) => JSX.Element

export interface CommandItem {
  id: string
  label: string
  description?: string
  icon?: IconComponent
  category?: string
  shortcut?: string
  action: () => void
}

export interface CommandPaletteProps {
  /** Available commands */
  commands: CommandItem[]
  /** Called when palette is closed */
  onClose?: () => void
  /** Recent command IDs */
  recentIds?: string[]
}

// ============================================================================
// Fuzzy Matching
// ============================================================================

const fuzzyMatch = (query: string, text: string): boolean => {
  const queryLower = query.toLowerCase()
  const textLower = text.toLowerCase()

  let queryIdx = 0
  for (let i = 0; i < textLower.length && queryIdx < queryLower.length; i++) {
    if (textLower[i] === queryLower[queryIdx]) {
      queryIdx++
    }
  }

  return queryIdx === queryLower.length
}

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
    // Open with Cmd+K or Ctrl+K
    if ((e.metaKey || e.ctrlKey) && e.key === 'k') {
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

  return (
    <Dialog open={open()} onOpenChange={handleOpenChange}>
      <Dialog.Portal>
        {/* Overlay */}
        <Dialog.Overlay
          class="
            fixed inset-0 z-50
            bg-black/60
            data-[expanded]:animate-in data-[expanded]:fade-in-0
            data-[closed]:animate-out data-[closed]:fade-out-0
          "
        />

        {/* Content */}
        <Dialog.Content
          class="
            fixed left-1/2 top-[20%] z-50
            -translate-x-1/2
            w-full max-w-lg
            bg-[var(--surface-overlay)]
            border border-[var(--border-default)]
            rounded-[var(--radius-xl)]
            shadow-2xl
            overflow-hidden
            data-[expanded]:animate-in data-[expanded]:fade-in-0 data-[expanded]:zoom-in-95
            data-[closed]:animate-out data-[closed]:fade-out-0 data-[closed]:zoom-out-95
            duration-200
          "
          onKeyDown={handleKeyDown}
        >
          {/* Search Input */}
          <div class="flex items-center gap-3 px-4 py-3 border-b border-[var(--border-subtle)]">
            <Search class="w-5 h-5 text-[var(--text-muted)]" />
            <input
              // biome-ignore lint/suspicious/noAssignInExpressions: SolidJS ref callback pattern
              ref={(el) => (inputRef = el)}
              type="text"
              placeholder="Search commands..."
              value={query()}
              onInput={(e) => setQuery(e.currentTarget.value)}
              class="
                flex-1
                bg-transparent
                text-[var(--text-primary)]
                placeholder:text-[var(--text-muted)]
                outline-none
                text-sm
              "
            />
            <kbd class="px-2 py-0.5 text-xs bg-[var(--surface-sunken)] rounded-[var(--radius-md)] text-[var(--text-muted)]">
              ESC
            </kbd>
          </div>

          {/* Results */}
          <div class="max-h-80 overflow-y-auto">
            <Show
              when={flatCommands().length > 0}
              fallback={
                <div class="py-8 text-center text-sm text-[var(--text-muted)]">
                  No commands found
                </div>
              }
            >
              <For each={Object.entries(groupedCommands())}>
                {([category, commands]) => (
                  <div>
                    {/* Category Header */}
                    <div class="px-4 py-2 text-xs font-medium text-[var(--text-muted)] bg-[var(--surface-sunken)]">
                      {category}
                    </div>

                    {/* Commands */}
                    <For each={commands}>
                      {(cmd) => {
                        const globalIndex = () => flatCommands().findIndex((c) => c.id === cmd.id)
                        const isSelected = () => selectedIndex() === globalIndex()

                        return (
                          <button
                            type="button"
                            onClick={() => executeCommand(cmd)}
                            onMouseEnter={() => setSelectedIndex(globalIndex())}
                            class={`
                              w-full text-left
                              flex items-center gap-3
                              px-4 py-2.5
                              transition-colors duration-[var(--duration-fast)]
                              ${
                                isSelected()
                                  ? 'bg-[var(--accent-subtle)] text-[var(--accent)]'
                                  : 'hover:bg-[var(--surface-raised)]'
                              }
                            `}
                          >
                            {/* Icon */}
                            <div
                              class={`
                                p-1.5 rounded-[var(--radius-md)]
                                ${isSelected() ? 'bg-[var(--accent)]/10' : 'bg-[var(--surface-sunken)]'}
                              `}
                            >
                              <Show when={cmd.icon} fallback={<Command class="w-4 h-4" />}>
                                <Dynamic component={cmd.icon!} class="w-4 h-4" />
                              </Show>
                            </div>

                            {/* Label & Description */}
                            <div class="flex-1 min-w-0">
                              <div class="text-sm font-medium text-[var(--text-primary)]">
                                {cmd.label}
                              </div>
                              <Show when={cmd.description}>
                                <div class="text-xs text-[var(--text-muted)] truncate">
                                  {cmd.description}
                                </div>
                              </Show>
                            </div>

                            {/* Shortcut */}
                            <Show when={cmd.shortcut}>
                              <kbd class="px-2 py-0.5 text-xs bg-[var(--surface-sunken)] rounded-[var(--radius-md)] text-[var(--text-muted)]">
                                {cmd.shortcut}
                              </kbd>
                            </Show>
                          </button>
                        )
                      }}
                    </For>
                  </div>
                )}
              </For>
            </Show>
          </div>

          {/* Footer */}
          <div class="flex items-center justify-between px-4 py-2 border-t border-[var(--border-subtle)] bg-[var(--surface-sunken)]">
            <div class="flex items-center gap-4 text-xs text-[var(--text-muted)]">
              <span class="flex items-center gap-1">
                <kbd class="px-1.5 py-0.5 bg-[var(--surface-raised)] rounded">↑↓</kbd>
                navigate
              </span>
              <span class="flex items-center gap-1">
                <kbd class="px-1.5 py-0.5 bg-[var(--surface-raised)] rounded">↵</kbd>
                select
              </span>
            </div>
            <span class="text-xs text-[var(--text-muted)]">{flatCommands().length} commands</span>
          </div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog>
  )
}

// ============================================================================
// Default Commands
// ============================================================================

export const createDefaultCommands = (handlers: {
  newChat?: () => void
  openSettings?: () => void
  clearChat?: () => void
  exportChat?: () => void
  initProject?: () => void
  switchTab?: (tab: string) => void
  saveWorkflow?: () => void
  browseWorkflows?: () => void
  importWorkflows?: () => void
  exportWorkflows?: () => void
  openProjectStats?: () => void
  saveCheckpoint?: () => void
}): CommandItem[] => [
  {
    id: 'new-chat',
    label: 'New Chat',
    description: 'Start a new conversation',
    icon: MessageSquare,
    category: 'Chat',
    shortcut: '⌘N',
    action: handlers.newChat || (() => {}),
  },
  {
    id: 'clear-chat',
    label: 'Clear Chat',
    description: 'Clear current conversation',
    icon: MessageSquare,
    category: 'Chat',
    action: handlers.clearChat || (() => {}),
  },
  {
    id: 'export-chat',
    label: 'Export Chat',
    description: 'Download conversation as Markdown',
    icon: Download,
    category: 'Chat',
    shortcut: '⌘⇧E',
    action: handlers.exportChat || (() => {}),
  },
  {
    id: 'init-project',
    label: 'Initialize Project',
    description: 'Analyze codebase and generate project rules',
    icon: Command,
    category: 'General',
    action: handlers.initProject || (() => {}),
  },
  {
    id: 'open-settings',
    label: 'Open Settings',
    description: 'Configure preferences',
    icon: Settings,
    category: 'General',
    shortcut: '⌘,',
    action: handlers.openSettings || (() => {}),
  },
  {
    id: 'tab-chat',
    label: 'Go to Chat',
    description: 'Switch to Chat tab',
    icon: MessageSquare,
    category: 'Navigation',
    action: () => handlers.switchTab?.('chat'),
  },
  {
    id: 'tab-agents',
    label: 'Go to Agents',
    description: 'Switch to Agents tab',
    icon: Sparkles,
    category: 'Navigation',
    action: () => handlers.switchTab?.('agents'),
  },
  {
    id: 'tab-files',
    label: 'Go to Files',
    description: 'Switch to Files tab',
    icon: FileText,
    category: 'Navigation',
    action: () => handlers.switchTab?.('files'),
  },
  {
    id: 'tab-terminal',
    label: 'Go to Terminal',
    description: 'Switch to Terminal tab',
    icon: Terminal,
    category: 'Navigation',
    action: () => handlers.switchTab?.('terminal'),
  },
  {
    id: 'save-workflow',
    label: 'Save Session as Workflow',
    description: 'Create a reusable workflow from this session',
    icon: BookmarkPlus,
    category: 'Workflows',
    action: handlers.saveWorkflow || (() => {}),
  },
  {
    id: 'browse-workflows',
    label: 'Browse Workflows',
    description: 'View and apply saved workflows',
    icon: Library,
    category: 'Workflows',
    action: handlers.browseWorkflows || (() => {}),
  },
  {
    id: 'import-workflows',
    label: 'Import Workflows',
    description: 'Import workflows from a JSON file',
    icon: Upload,
    category: 'Workflows',
    action: handlers.importWorkflows || (() => {}),
  },
  {
    id: 'export-workflows',
    label: 'Export Workflows',
    description: 'Export all workflows as JSON',
    icon: Download,
    category: 'Workflows',
    action: handlers.exportWorkflows || (() => {}),
  },
  {
    id: 'project-stats',
    label: 'Project Stats',
    description: 'View project-level usage and cost statistics',
    icon: BarChart3,
    category: 'Session',
    action: handlers.openProjectStats || (() => {}),
  },
  {
    id: 'save-checkpoint',
    label: 'Save Checkpoint',
    description: 'Create a named snapshot of the conversation',
    icon: Flag,
    category: 'Session',
    shortcut: '⌘⇧C',
    action: handlers.saveCheckpoint || (() => {}),
  },
  {
    id: 'whats-new',
    label: "What's New",
    description: 'View recent changes and release notes',
    icon: Megaphone,
    category: 'General',
    action: () => window.dispatchEvent(new CustomEvent('ava:open-changelog')),
  },
  {
    id: 'check-updates',
    label: 'Check for Updates',
    description: 'Check if a new version of AVA is available',
    icon: ArrowDownCircle,
    category: 'General',
    action: () => window.dispatchEvent(new CustomEvent('ava:check-update')),
  },
]
