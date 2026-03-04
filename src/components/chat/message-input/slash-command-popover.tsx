/**
 * Slash Command Popover
 *
 * Autocomplete dropdown triggered by / in the message input.
 * Groups commands by type (built-in vs custom) with visual distinction.
 */

import { Puzzle, Terminal } from 'lucide-solid'
import { type Accessor, type Component, createMemo, For, Show } from 'solid-js'
import type { CommandEntry } from '../../../services/command-resolver'

interface SlashCommandPopoverProps {
  /** Whether the popover is visible */
  open: Accessor<boolean>
  /** Pre-filtered command results */
  commands: Accessor<CommandEntry[]>
  /** Called when user selects a command */
  onSelect: (cmd: CommandEntry) => void
  /** Current keyboard-selected index */
  selectedIndex: Accessor<number>
}

export const SlashCommandPopover: Component<SlashCommandPopoverProps> = (props) => {
  // Group commands: built-in first, then custom
  const grouped = createMemo(() => {
    const cmds = props.commands()
    const builtIn = cmds.filter((c) => c.isBuiltIn)
    const custom = cmds.filter((c) => !c.isBuiltIn)
    return { builtIn, custom }
  })

  // Flat list for index tracking (built-in first, then custom)
  const flatList = createMemo(() => [...grouped().builtIn, ...grouped().custom])

  return (
    <Show when={props.open() && props.commands().length > 0}>
      <div
        class="
          absolute bottom-full left-0 right-0 mb-1 z-[var(--z-popover)]
          max-h-[320px] overflow-y-auto
          bg-[var(--surface-overlay)] border border-[var(--border-default)]
          rounded-[var(--radius-lg)] shadow-[var(--shadow-lg)]
          backdrop-blur-sm
          animate-dropdown-in
        "
        role="listbox"
      >
        {/* Built-in commands */}
        <Show when={grouped().builtIn.length > 0}>
          <div class="px-3 pt-2 pb-1">
            <span class="text-[9px] font-medium uppercase tracking-wider text-[var(--text-muted)]">
              Commands
            </span>
          </div>
          <div class="px-1 pb-1">
            <For each={grouped().builtIn}>
              {(cmd) => {
                const flatIdx = () => flatList().indexOf(cmd)
                return (
                  <CommandRow
                    cmd={cmd}
                    isSelected={() => flatIdx() === props.selectedIndex()}
                    onSelect={props.onSelect}
                    icon="builtin"
                  />
                )
              }}
            </For>
          </div>
        </Show>

        {/* Custom commands */}
        <Show when={grouped().custom.length > 0}>
          <Show when={grouped().builtIn.length > 0}>
            <div class="mx-3 border-t border-[var(--border-subtle)]" />
          </Show>
          <div class="px-3 pt-2 pb-1">
            <span class="text-[9px] font-medium uppercase tracking-wider text-[var(--text-muted)]">
              Custom
            </span>
          </div>
          <div class="px-1 pb-1">
            <For each={grouped().custom}>
              {(cmd) => {
                const flatIdx = () => flatList().indexOf(cmd)
                return (
                  <CommandRow
                    cmd={cmd}
                    isSelected={() => flatIdx() === props.selectedIndex()}
                    onSelect={props.onSelect}
                    icon="custom"
                  />
                )
              }}
            </For>
          </div>
        </Show>

        {/* Footer */}
        <div class="px-3 py-1.5 border-t border-[var(--border-subtle)] text-[9px] text-[var(--text-muted)] flex items-center gap-3">
          <span>
            <kbd class="px-1 py-0.5 bg-[var(--surface-raised)] rounded text-[8px] font-[var(--font-mono)]">
              ↑↓
            </kbd>{' '}
            navigate
          </span>
          <span>
            <kbd class="px-1 py-0.5 bg-[var(--surface-raised)] rounded text-[8px] font-[var(--font-mono)]">
              Tab
            </kbd>{' '}
            select
          </span>
          <span>
            <kbd class="px-1 py-0.5 bg-[var(--surface-raised)] rounded text-[8px] font-[var(--font-mono)]">
              Esc
            </kbd>{' '}
            dismiss
          </span>
        </div>
      </div>
    </Show>
  )
}

/** Single command row */
const CommandRow: Component<{
  cmd: CommandEntry
  isSelected: Accessor<boolean>
  onSelect: (cmd: CommandEntry) => void
  icon: 'builtin' | 'custom'
}> = (props) => (
  <button
    type="button"
    role="option"
    aria-selected={props.isSelected()}
    class="flex items-center gap-2.5 w-full px-2 py-1.5 text-left text-xs rounded-[var(--radius-md)] transition-colors"
    classList={{
      'bg-[var(--accent-subtle)] text-[var(--text-primary)]': props.isSelected(),
      'text-[var(--text-secondary)] hover:bg-[var(--alpha-white-5)]': !props.isSelected(),
    }}
    onClick={() => props.onSelect(props.cmd)}
  >
    {props.icon === 'builtin' ? (
      <Terminal class="w-3.5 h-3.5 text-[var(--accent)] flex-shrink-0 opacity-70" />
    ) : (
      <Puzzle class="w-3.5 h-3.5 text-[var(--success)] flex-shrink-0 opacity-70" />
    )}
    <span class="font-semibold font-[var(--font-mono)] text-[11px]">/{props.cmd.name}</span>
    <span class="text-[var(--text-muted)] text-[10px] truncate">{props.cmd.description}</span>
  </button>
)
