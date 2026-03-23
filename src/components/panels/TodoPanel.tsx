/**
 * Todo Panel Component
 *
 * Displays the agent's current todo/checklist as updated by todo_write tool calls.
 * Shows each item with status indicators:
 *   - pending: empty circle
 *   - in_progress: yellow filled circle
 *   - completed: green check with strikethrough
 *   - cancelled: grey X with dimmed text
 */

import { CheckCircle2, Circle, CircleDashed, XCircle } from 'lucide-solid'
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
  const isHigh = (): boolean => props.item.priority === 'high'

  const iconEl = (): TodoStatus => props.item.status

  return (
    <div
      class="flex items-start gap-2.5 py-2 px-3 rounded-[var(--radius-md)] hover:bg-[var(--alpha-white-3)] transition-colors"
      classList={{
        'opacity-50': isCancelled(),
      }}
    >
      {/* Status icon */}
      <div class="mt-0.5 flex-shrink-0">
        <Show when={iconEl() === 'completed'}>
          <CheckCircle2 class="w-4 h-4 text-[var(--success)]" />
        </Show>
        <Show when={iconEl() === 'in_progress'}>
          <Circle class="w-4 h-4 text-[var(--warning)] fill-[var(--warning)]" />
        </Show>
        <Show when={iconEl() === 'pending'}>
          <CircleDashed class="w-4 h-4 text-[var(--text-muted)]" />
        </Show>
        <Show when={iconEl() === 'cancelled'}>
          <XCircle class="w-4 h-4 text-[var(--text-tertiary)]" />
        </Show>
      </div>

      {/* Content */}
      <span
        class="text-xs leading-relaxed min-w-0 flex-1"
        classList={{
          'text-[var(--text-muted)] line-through': isCompleted(),
          'text-[var(--text-tertiary)] line-through': isCancelled(),
          'text-[var(--text-primary)] font-medium': isInProgress(),
          'text-[var(--text-secondary)]': !isCompleted() && !isCancelled() && !isInProgress(),
        }}
      >
        <Show when={isHigh() && !isCompleted() && !isCancelled()}>
          <span class="text-[var(--error)] font-bold mr-1">!</span>
        </Show>
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
  // Use passed todos if available, otherwise fall back to own useRustAgent
  const rustAgent = props.todos ? null : useRustAgent()
  const todos = (): TodoItem[] => props.todos ?? rustAgent?.todos() ?? []

  const incompleteCount = createMemo(
    () => todos().filter((t) => t.status === 'pending' || t.status === 'in_progress').length
  )

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
    <div class="flex flex-col h-full">
      {/* Header */}
      <div class="flex items-center justify-between density-section-px density-section-py border-b border-[var(--border-subtle)]">
        <div class="flex items-center gap-2">
          <div class="p-2 bg-[var(--success-subtle)] rounded-[var(--radius-lg)]">
            <CheckCircle2 class="w-4 h-4 text-[var(--success)]" />
          </div>
          <div>
            <h2 class="text-sm font-semibold text-[var(--text-primary)]">Todos</h2>
            <Show
              when={todos().length > 0}
              fallback={<p class="text-xs text-[var(--text-muted)]">No active tasks</p>}
            >
              <p class="text-xs text-[var(--text-muted)]">
                {incompleteCount()} remaining
                <Show when={completedCount() > 0}> &middot; {completedCount()} done</Show>
              </p>
            </Show>
          </div>
        </div>
      </div>

      {/* Progress bar — only shown when there are todos */}
      <Show when={todos().length > 0}>
        <div class="px-3 py-2 border-b border-[var(--border-subtle)]">
          <div class="h-1.5 bg-[var(--surface-sunken)] rounded-full overflow-hidden">
            <div
              class="h-full bg-[var(--success)] rounded-full transition-[width] duration-300"
              style={{
                width: `${todos().length > 0 ? Math.round((completedCount() / todos().length) * 100) : 0}%`,
              }}
            />
          </div>
        </div>
      </Show>

      {/* Todo list */}
      <div class="flex-1 overflow-y-auto">
        <Show
          when={todos().length > 0}
          fallback={
            <div class="flex flex-col items-center justify-center h-full text-center p-6">
              <div class="p-4 bg-[var(--surface-raised)] rounded-full mb-4">
                <CheckCircle2 class="w-8 h-8 text-[var(--text-muted)]" />
              </div>
              <h3 class="text-sm font-medium text-[var(--text-secondary)] mb-1">No todos yet</h3>
              <p class="text-xs text-[var(--text-muted)]">
                The agent will track tasks here when it uses the todo_write tool
              </p>
            </div>
          }
        >
          <div class="p-2 space-y-0.5">
            <For each={orderedTodos()}>{(item) => <TodoRow item={item} />}</For>
          </div>
        </Show>
      </div>

      {/* Footer — only when there are todos */}
      <Show when={todos().length > 0}>
        <div class="density-section-px density-section-py border-t border-[var(--border-subtle)] bg-[var(--surface-sunken)]">
          <p class="text-xs text-[var(--text-muted)]">
            {todos().length} task{todos().length !== 1 ? 's' : ''} total
          </p>
        </div>
      </Show>
    </div>
  )
}
