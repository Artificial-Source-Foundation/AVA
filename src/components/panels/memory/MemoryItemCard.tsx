/**
 * Memory Item Card
 *
 * Card showing a single memory item with type badge and preview text.
 * Design: rounded-6, bg #0F0F12, border #ffffff06, type badge (project=blue, feedback=green),
 * title + description, 10px padding, 6px gap.
 */

import { Trash2 } from 'lucide-solid'
import { type Component, createSignal, Show } from 'solid-js'
import type { MemoryItem } from '../../../types'
import { formatTokens, memoryTypeConfig } from './memory-config'

export interface MemoryItemCardProps {
  item: MemoryItem
  onRemove: (id: string) => void
}

export const MemoryItemCard: Component<MemoryItemCardProps> = (props) => {
  const [expanded, setExpanded] = createSignal(false)

  const config = () => memoryTypeConfig[props.item.type]

  return (
    <button
      type="button"
      onClick={() => setExpanded(!expanded())}
      class="w-full text-left rounded-[6px] p-2.5 border transition-colors"
      classList={{
        'border-[var(--accent)] bg-[var(--accent-subtle)]': expanded(),
        'border-[var(--border-subtle)] bg-[var(--background-subtle)] hover:border-[var(--border-default)]':
          !expanded(),
      }}
      style={
        {
          '--memory-accent': config().color,
          '--memory-accent-bg': config().bg,
        } as { '--memory-accent': string; '--memory-accent-bg': string }
      }
    >
      {/* Header row: title + type badge */}
      <div class="flex items-center justify-between gap-2">
        <span class="text-[11px] font-medium text-[var(--text-primary)] truncate flex-1">
          {props.item.title}
        </span>
        <span class="inline-flex items-center px-1.5 py-0.5 text-[9px] font-medium rounded-md flex-shrink-0 bg-[var(--memory-accent-bg)] text-[var(--memory-accent)]">
          {config().label}
        </span>
      </div>

      {/* Preview text */}
      <p class="text-[10px] text-[var(--text-muted)] mt-1.5 line-clamp-2 leading-relaxed">
        {props.item.preview}
      </p>

      {/* Expanded details */}
      <Show when={expanded()}>
        <div class="mt-2.5 pt-2.5 border-t border-[var(--border-subtle)] space-y-2">
          <div class="text-[10px] text-[var(--text-secondary)] p-2 bg-[var(--surface)] rounded-[4px] max-h-32 overflow-y-auto leading-relaxed">
            {props.item.preview}
          </div>

          <div class="flex items-center justify-between">
            <span class="text-[10px] text-[var(--text-muted)]">
              {formatTokens(props.item.tokens)} tokens
            </span>
            <Show when={props.item.id !== 'conversation-current'}>
              <button
                type="button"
                onClick={(e) => {
                  e.stopPropagation()
                  props.onRemove(props.item.id)
                }}
                class="flex items-center gap-1 px-1.5 py-0.5 text-[10px] text-[var(--system-red)] hover:bg-[var(--system-red)]/10 rounded-[4px] transition-colors"
              >
                <Trash2 class="w-2.5 h-2.5" />
                Remove
              </button>
            </Show>
          </div>
        </div>
      </Show>
    </button>
  )
}
