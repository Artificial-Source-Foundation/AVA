/**
 * Memory Item Card
 *
 * Expandable card showing a single memory item with type badge, preview, and actions.
 */

import { ChevronRight, Trash2 } from 'lucide-solid'
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
      class={`
        w-full text-left
        density-section-px density-section-py
        rounded-[var(--radius-lg)]
        border
        transition-all duration-[var(--duration-fast)]
        ${
          expanded()
            ? 'border-[var(--accent)] bg-[var(--accent-subtle)]'
            : 'border-[var(--border-subtle)] hover:border-[var(--border-default)] hover:bg-[var(--surface-raised)]'
        }
      `}
    >
      <div class="flex items-start gap-3">
        {/* Item Icon */}
        <div
          class="p-2 rounded-[var(--radius-md)] flex-shrink-0"
          style={{ background: config().bg }}
        >
          {(() => {
            const ItemIcon = config().icon
            return <ItemIcon class="w-4 h-4" style={{ color: config().color }} />
          })()}
        </div>

        {/* Item Info */}
        <div class="flex-1 min-w-0">
          <div class="flex items-center justify-between gap-2">
            <span class="text-sm font-medium text-[var(--text-primary)] truncate">
              {props.item.title}
            </span>
            <span
              class="flex items-center gap-1 px-1.5 py-0.5 text-[10px] font-medium rounded-full flex-shrink-0"
              style={{ background: config().bg, color: config().color }}
            >
              {config().label}
            </span>
          </div>

          <p class="text-xs text-[var(--text-muted)] mt-1 line-clamp-2">{props.item.preview}</p>

          {/* Token count */}
          <div class="flex items-center gap-2 mt-2">
            <span class="text-xs text-[var(--text-muted)]">
              {formatTokens(props.item.tokens)} tokens
            </span>
            <Show when={props.item.source}>
              <span class="text-xs text-[var(--text-muted)]">&middot;</span>
              <span class="text-xs text-[var(--text-muted)] truncate">{props.item.source}</span>
            </Show>
          </div>

          {/* Expanded details */}
          <Show when={expanded()}>
            <div class="mt-3 pt-3 border-t border-[var(--border-subtle)] space-y-2">
              <div class="text-xs text-[var(--text-secondary)] p-2 bg-[var(--surface-sunken)] rounded-[var(--radius-md)] max-h-32 overflow-y-auto">
                {props.item.preview}
              </div>

              <Show when={props.item.id !== 'conversation-current'}>
                <div class="flex items-center justify-end">
                  <button
                    type="button"
                    onClick={(e) => {
                      e.stopPropagation()
                      props.onRemove(props.item.id)
                    }}
                    class="
                      flex items-center gap-1 px-2 py-1
                      text-xs text-[var(--error)]
                      hover:bg-[var(--error-subtle)]
                      rounded-[var(--radius-md)]
                      transition-colors
                    "
                  >
                    <Trash2 class="w-3 h-3" />
                    Remove
                  </button>
                </div>
              </Show>
            </div>
          </Show>
        </div>

        {/* Expand indicator */}
        <ChevronRight
          class={`
            w-4 h-4 flex-shrink-0
            text-[var(--text-muted)]
            transition-transform duration-[var(--duration-fast)]
            ${expanded() ? 'rotate-90' : ''}
          `}
        />
      </div>
    </button>
  )
}
