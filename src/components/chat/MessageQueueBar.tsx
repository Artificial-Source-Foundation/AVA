/**
 * Message Queue Bar
 *
 * Displays queued messages above the composer when the agent is busy.
 * Shows count, allows viewing individual messages, and removing from queue.
 */

import { ListOrdered, X } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'

export interface QueuedMessageDisplay {
  content: string
  tier?: 'steering' | 'follow-up' | 'post-complete'
  group?: number
  model?: string
}

function tierBadge(msg: QueuedMessageDisplay): string {
  switch (msg.tier) {
    case 'steering':
      return '[S]'
    case 'follow-up':
      return '[F]'
    case 'post-complete':
      return `[G${msg.group ?? 1}]`
    default:
      return ''
  }
}

function tierColor(msg: QueuedMessageDisplay): string {
  switch (msg.tier) {
    case 'steering':
      return 'var(--warning)'
    case 'follow-up':
      return 'var(--accent)'
    case 'post-complete':
      return 'var(--text-muted)'
    default:
      return 'var(--text-muted)'
  }
}

interface MessageQueueBarProps {
  messages: QueuedMessageDisplay[]
  onRemove: (index: number) => void
  onClear: () => void
}

export const MessageQueueBar: Component<MessageQueueBarProps> = (props) => {
  const [expanded, setExpanded] = createSignal(false)
  const count = () => props.messages.length

  return (
    <Show when={count() > 0}>
      <div class="border-t border-[var(--border-subtle)] bg-[var(--surface-raised)] animate-[approvalSlideUp_150ms_ease-out]">
        {/* Compact header row */}
        <div class="flex items-center gap-2 px-3 py-1.5">
          <button
            type="button"
            onClick={() => setExpanded((e) => !e)}
            class="flex items-center gap-2 flex-1 min-w-0 text-left cursor-pointer hover:opacity-80 transition-opacity"
          >
            <ListOrdered class="w-3.5 h-3.5 text-[var(--accent)] flex-shrink-0" />
            <span class="text-xs font-medium text-[var(--text-secondary)]">
              {count()} queued message{count() > 1 ? 's' : ''}
            </span>
            <span class="text-[10px] text-[var(--text-muted)]">
              {expanded() ? '(click to collapse)' : '(click to expand)'}
            </span>
          </button>

          <button
            type="button"
            onClick={props.onClear}
            class="text-[10px] text-[var(--text-muted)] hover:text-[var(--error)] transition-colors cursor-pointer px-1.5 py-0.5 rounded hover:bg-[var(--alpha-white-5)]"
          >
            Clear all
          </button>
        </div>

        {/* Expanded message list */}
        <Show when={expanded()}>
          <div class="px-3 pb-2 space-y-1 max-h-40 overflow-y-auto scrollbar-none">
            <For each={props.messages}>
              {(msg, index) => (
                <div class="flex items-start gap-2 py-1 px-2 rounded bg-[var(--alpha-white-5)] group">
                  <span
                    class="text-[10px] font-mono font-bold mt-0.5 flex-shrink-0"
                    style={{ color: tierColor(msg) }}
                  >
                    {tierBadge(msg) || `#${index() + 1}`}
                  </span>
                  <span class="text-xs text-[var(--text-secondary)] flex-1 min-w-0 truncate">
                    {msg.content.slice(0, 120)}
                    {msg.content.length > 120 ? '...' : ''}
                  </span>
                  <Show when={msg.model}>
                    <span class="text-[9px] text-[var(--text-muted)] bg-[var(--alpha-white-5)] px-1 rounded flex-shrink-0">
                      {msg.model}
                    </span>
                  </Show>
                  <button
                    type="button"
                    onClick={() => props.onRemove(index())}
                    class="opacity-0 group-hover:opacity-100 transition-opacity p-0.5 rounded hover:bg-[var(--alpha-white-10)] cursor-pointer flex-shrink-0"
                    title="Remove from queue"
                  >
                    <X class="w-3 h-3 text-[var(--text-muted)]" />
                  </button>
                </div>
              )}
            </For>
          </div>
        </Show>
      </div>
    </Show>
  )
}
