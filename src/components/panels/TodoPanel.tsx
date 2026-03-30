/**
 * Todo Panel Component
 *
 * Displays the agent's current todo/checklist as updated by todo_write tool calls.
 * Shows each item with status indicators:
 *   - pending: empty circle
 *   - in_progress: yellow filled circle
 *   - completed: green check-circle (italic + muted text)
 *   - cancelled: grey X with dimmed text
 *
 * Design: rounded-12 card, #111114 bg, #0F0F12 header, blue square-check icon,
 * blue "done/total" badge, checklist rows with 32px height and 10px gap.
 */

import { CheckCircle2, CheckSquare, Circle, Plus, XCircle } from 'lucide-solid'
import { type Component, createMemo, For, Show } from 'solid-js'
import { useRustAgent } from '../../hooks/use-rust-agent'
import type { TodoItem, TodoStatus } from '../../types/rust-ipc'

// ── Status helpers ───────────────────────────────────────────────────────────

interface TodoRowProps {
  item: TodoItem
}

const TodoRow: Component<TodoRowProps> = (props) => {
  const isCompleted = (): boolean => props.item.status === 'completed'
  const isCancelled = (): boolean => props.item.status === 'cancelled'
  const isInProgress = (): boolean => props.item.status === 'in_progress'

  const iconEl = (): TodoStatus => props.item.status

  return (
    <div
      class="flex items-center gap-2.5 h-8 px-2.5 rounded-[6px] transition-colors"
      classList={{
        'opacity-40': isCancelled(),
      }}
    >
      {/* Status icon */}
      <div class="flex-shrink-0">
        <Show when={iconEl() === 'completed'}>
          <CheckCircle2 class="w-3.5 h-3.5 text-[var(--system-green)]" />
        </Show>
        <Show when={iconEl() === 'in_progress'}>
          <Circle class="w-3.5 h-3.5 text-[var(--warning)] fill-[var(--warning)]" />
        </Show>
        <Show when={iconEl() === 'pending'}>
          <Circle class="w-3.5 h-3.5 text-[var(--text-muted)]" />
        </Show>
        <Show when={iconEl() === 'cancelled'}>
          <XCircle class="w-3.5 h-3.5 text-[var(--text-muted)]" />
        </Show>
      </div>

      {/* Content */}
      <span
        class="text-[11px] leading-none min-w-0 flex-1 truncate"
        classList={{
          'text-[var(--text-muted)] italic': isCompleted(),
          'text-[var(--text-muted)] italic line-through': isCancelled(),
          'text-[var(--text-primary)] font-medium': isInProgress(),
          'text-[var(--text-primary)]': !isCompleted() && !isCancelled() && !isInProgress(),
        }}
      >
        {props.item.content}
      </span>
    </div>
  )
}

// ── Main panel ───────────────────────────────────────────────────────────────

interface TodoPanelProps {
  todos?: TodoItem[]
}

export const TodoPanel: Component<TodoPanelProps> = (props) => {
  const rustAgent = useRustAgent()
  const todos = (): TodoItem[] => props.todos ?? rustAgent.todos() ?? []

  const completedCount = createMemo(() => todos().filter((t) => t.status === 'completed').length)

  // Group: in_progress first, then pending, then completed/cancelled
  const orderedTodos = createMemo(() => {
    const all = todos()
    const inProgress = all.filter((t) => t.status === 'in_progress')
    const pending = all.filter((t) => t.status === 'pending')
    const done = all.filter((t) => t.status === 'completed' || t.status === 'cancelled')
    return [...inProgress, ...pending, ...done]
  })

  return (
    <div class="flex flex-col h-full overflow-hidden rounded-[10px] bg-[var(--surface)] border border-[var(--border-subtle)]">
      {/* Header */}
      <div class="flex items-center justify-between h-10 px-3 bg-[var(--background-subtle)] shrink-0">
        <div class="flex items-center gap-2">
          <CheckSquare class="w-3.5 h-3.5 text-[var(--accent)]" />
          <span class="text-xs font-medium text-[var(--text-secondary)]">Todos</span>
          <Show when={todos().length > 0}>
            <span class="inline-flex items-center px-1.5 py-0.5 text-[10px] font-medium rounded-md bg-[var(--accent)]/15 text-[var(--accent)]">
              {completedCount()}/{todos().length}
            </span>
          </Show>
        </div>
        <div class="flex items-center gap-1">
          <button
            type="button"
            class="p-1 rounded-[6px] text-[var(--text-muted)] hover:text-[var(--text-primary)] transition-colors"
            title="Add todo"
          >
            <Plus class="w-[13px] h-[13px]" />
          </button>
        </div>
      </div>

      {/* Todo list */}
      <div class="flex-1 overflow-y-auto p-2.5">
        <Show
          when={todos().length > 0}
          fallback={
            <div class="flex flex-col items-center justify-center h-full text-center">
              <CheckSquare class="w-6 h-6 text-[var(--text-muted)] mb-2" />
              <p class="text-[11px] text-[var(--text-muted)]">
                Tasks will appear here when the agent uses todo_write
              </p>
            </div>
          }
        >
          <div class="space-y-1">
            <For each={orderedTodos()}>{(item) => <TodoRow item={item} />}</For>
          </div>
        </Show>
      </div>
    </div>
  )
}
