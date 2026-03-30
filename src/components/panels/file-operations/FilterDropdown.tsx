/**
 * Filter Dropdown for File Operations Panel
 *
 * Dropdown menu to filter file operations by type (read/write/edit/delete).
 */

import { ChevronDown, Filter } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import type { FileOperationType } from '../../../types'
import { operationConfig } from './file-operations-helpers'

export interface FilterDropdownProps {
  filterType: FileOperationType | 'all'
  showMenu: boolean
  counts: Record<FileOperationType | 'all', number>
  onFilterChange: (type: FileOperationType | 'all') => void
  onToggleMenu: () => void
}

export const FilterDropdown: Component<FilterDropdownProps> = (props) => {
  return (
    <div class="relative">
      <button
        type="button"
        onClick={() => props.onToggleMenu()}
        class={`
          flex items-center gap-1.5 px-2.5 py-1.5
          rounded-[var(--radius-md)]
          text-xs font-medium
          transition-colors duration-[var(--duration-fast)]
          ${
            props.filterType !== 'all'
              ? 'bg-[var(--accent-subtle)] text-[var(--accent)]'
              : 'text-[var(--text-tertiary)] hover:text-[var(--text-primary)] hover:bg-[var(--surface-raised)]'
          }
        `}
      >
        <Filter class="w-3.5 h-3.5" />
        {props.filterType === 'all'
          ? 'All'
          : operationConfig[props.filterType as FileOperationType].label}
        <ChevronDown class={`w-3 h-3 transition-transform ${props.showMenu ? 'rotate-180' : ''}`} />
      </button>

      <Show when={props.showMenu}>
        <div
          class="
            absolute right-0 top-full mt-1
            py-1 min-w-[140px]
            z-10
            animate-dropdown-in
          "
          style={{
            background: 'var(--dropdown-surface)',
            border: '1px solid var(--dropdown-border)',
            'border-radius': 'var(--dropdown-radius)',
            'box-shadow': 'var(--modal-shadow)',
          }}
        >
          <button
            type="button"
            onClick={() => props.onFilterChange('all')}
            class={`
              w-full flex items-center justify-between gap-2 px-3 text-left
              ${props.filterType === 'all' ? '' : 'hover:bg-[var(--dropdown-item-hover)]'}
            `}
            style={{
              height: '32px',
              'font-size': '13px',
              background:
                props.filterType === 'all' ? 'var(--dropdown-item-active-bg)' : 'transparent',
              color:
                props.filterType === 'all' ? 'var(--dropdown-item-active-text)' : 'var(--gray-9)',
            }}
          >
            <span>All Operations</span>
            <span class="text-[var(--text-muted)]">{props.counts.all}</span>
          </button>
          <For each={Object.entries(operationConfig)}>
            {([type, config]) => (
              <button
                type="button"
                onClick={() => props.onFilterChange(type as FileOperationType)}
                class={`
                  w-full flex items-center justify-between gap-2 px-3 py-2
                  text-xs text-left
                  ${props.filterType === type ? '' : 'hover:bg-[var(--dropdown-item-hover)]'}
                `}
              >
                <span class="flex items-center gap-2">
                  <config.icon class="w-3.5 h-3.5" style={{ color: config.color }} />
                  {config.label}
                </span>
                <span class="text-[var(--text-muted)]">
                  {props.counts[type as FileOperationType]}
                </span>
              </button>
            )}
          </For>
        </div>
      </Show>
    </div>
  )
}
