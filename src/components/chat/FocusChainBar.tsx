/**
 * Focus Chain Bar
 * Compact progress indicator above the message list.
 * Shows task progress from the focus-chain extension.
 */

import { CheckCircle, Circle, Loader2 } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { type FocusItem, useFocusChain } from '../../stores/focus-chain'

const StatusIcon: Component<{ status: FocusItem['status'] }> = (props) => {
  switch (props.status) {
    case 'completed':
      return <CheckCircle class="w-3 h-3 text-[var(--success)]" />
    case 'in_progress':
      return <Loader2 class="w-3 h-3 text-[var(--accent)] animate-spin" />
    default:
      return <Circle class="w-3 h-3 text-[var(--text-muted)]" />
  }
}

export const FocusChainBar: Component = () => {
  const { items, completedCount, totalCount, progressPercent, currentDescription } = useFocusChain()
  const [expanded, setExpanded] = createSignal(false)

  return (
    <Show when={totalCount() > 0}>
      <div class="border-b border-[var(--border-subtle)] bg-[var(--surface-sunken)]">
        {/* Compact bar */}
        <button
          type="button"
          onClick={() => setExpanded((v) => !v)}
          class="w-full flex items-center gap-2 px-3 py-1.5 text-left hover:bg-[var(--alpha-white-3)] transition-colors"
        >
          {/* Progress bar */}
          <div class="w-16 h-1 bg-[var(--surface-raised)] rounded-full overflow-hidden shrink-0">
            <div
              class="h-full bg-[var(--accent)] rounded-full transition-all duration-300"
              style={{ width: `${progressPercent()}%` }}
            />
          </div>

          {/* Label */}
          <span class="text-[10px] text-[var(--text-secondary)] font-[var(--font-ui-mono)] tabular-nums shrink-0">
            Task {completedCount()}/{totalCount()}
          </span>

          {/* Current description */}
          <Show when={currentDescription()}>
            <span class="text-[10px] text-[var(--text-muted)] truncate">
              {currentDescription()}
            </span>
          </Show>
        </button>

        {/* Expanded checklist */}
        <Show when={expanded()}>
          <div class="px-3 pb-2 space-y-0.5 max-h-40 overflow-y-auto">
            <For each={items()}>
              {(item) => (
                <div class="flex items-center gap-2 py-0.5">
                  <StatusIcon status={item.status} />
                  <span
                    class="text-[11px] truncate"
                    classList={{
                      'text-[var(--text-muted)] line-through': item.status === 'completed',
                      'text-[var(--text-primary)]': item.status === 'in_progress',
                      'text-[var(--text-secondary)]': item.status === 'pending',
                    }}
                  >
                    {item.description}
                  </span>
                </div>
              )}
            </For>
          </div>
        </Show>
      </div>
    </Show>
  )
}
