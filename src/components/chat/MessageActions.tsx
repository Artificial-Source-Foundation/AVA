/**
 * Message Actions Component
 *
 * Hover actions for edit (user) and regenerate (assistant).
 * Premium floating toolbar with smooth transitions.
 */

import { Pencil, RefreshCw } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import type { Message } from '../../types'

interface MessageActionsProps {
  message: Message
  onEdit: () => void
  onRegenerate: () => void
  isLoading: boolean
}

export const MessageActions: Component<MessageActionsProps> = (props) => {
  return (
    <div
      class="
        absolute -top-3 right-0
        opacity-0 group-hover:opacity-100
        transition-opacity duration-[var(--duration-fast)]
        flex gap-0.5
        bg-[var(--surface-overlay)]
        border border-[var(--border-subtle)]
        rounded-[var(--radius-md)]
        p-0.5
        shadow-md
      "
    >
      {/* Edit button for user messages */}
      <Show when={props.message.role === 'user'}>
        <button
          type="button"
          onClick={() => props.onEdit()}
          disabled={props.isLoading}
          class="
            p-1.5
            rounded-[var(--radius-sm)]
            text-[var(--text-tertiary)]
            hover:text-[var(--text-primary)]
            hover:bg-[var(--surface-raised)]
            transition-colors duration-[var(--duration-fast)]
            disabled:opacity-50 disabled:cursor-not-allowed
          "
          title="Edit message"
        >
          <Pencil class="w-3.5 h-3.5" />
        </button>
      </Show>

      {/* Regenerate button for assistant messages (without errors) */}
      <Show when={props.message.role === 'assistant' && !props.message.error}>
        <button
          type="button"
          onClick={() => props.onRegenerate()}
          disabled={props.isLoading}
          class="
            p-1.5
            rounded-[var(--radius-sm)]
            text-[var(--text-tertiary)]
            hover:text-[var(--text-primary)]
            hover:bg-[var(--surface-raised)]
            transition-colors duration-[var(--duration-fast)]
            disabled:opacity-50 disabled:cursor-not-allowed
          "
          title="Regenerate response"
        >
          <RefreshCw class="w-3.5 h-3.5" />
        </button>
      </Show>
    </div>
  )
}
