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
          animate-dropdown-in
        "
        style={{
          background: 'var(--dropdown-surface)',
          border: '1px solid var(--dropdown-border)',
          'border-radius': 'var(--dropdown-radius)',
          'box-shadow': 'var(--modal-shadow)',
        }}
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
            <div
              class="mx-3"
              style={{ 'border-top': '1px solid var(--dropdown-separator)', margin: '4px 12px' }}
            />
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
        <div
          class="px-3 py-1.5 text-[9px] flex items-center gap-3"
          style={{ 'border-top': '1px solid var(--dropdown-separator)', color: 'var(--gray-6)' }}
        >
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
    class="flex items-center gap-2.5 w-full px-3 text-left rounded-[var(--radius-md)] transition-colors"
    style={{
      height: '34px',
      'font-size': '13px',
      'font-family': 'var(--font-sans)',
      background: props.isSelected() ? 'var(--dropdown-item-active-bg)' : 'transparent',
      color: props.isSelected() ? 'var(--dropdown-item-active-text)' : 'var(--gray-9)',
    }}
    onClick={() => props.onSelect(props.cmd)}
  >
    {props.icon === 'builtin' ? (
      <Terminal
        class="w-3.5 h-3.5 flex-shrink-0"
        style={{ color: props.isSelected() ? 'var(--dropdown-item-active-text)' : 'var(--gray-7)' }}
      />
    ) : (
      <Puzzle
        class="w-3.5 h-3.5 flex-shrink-0"
        style={{
          color: props.isSelected() ? 'var(--dropdown-item-active-text)' : 'var(--success)',
        }}
      />
    )}
    <span class="font-semibold font-[var(--font-mono)]" style={{ 'font-size': '13px' }}>
      /{props.cmd.name}
    </span>
    <span class="truncate" style={{ 'font-size': '11px', color: 'var(--gray-6)' }}>
      {props.cmd.description}
    </span>
  </button>
)
