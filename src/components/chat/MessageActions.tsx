/**
 * MessageActions Component
 * Hover actions for edit (user) and regenerate (assistant)
 */

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
    <div class="absolute -top-2 right-0 opacity-0 group-hover:opacity-100 transition-opacity flex gap-1 bg-gray-800 rounded-lg px-1 py-0.5 shadow-lg">
      {/* Edit button for user messages */}
      <Show when={props.message.role === 'user'}>
        <button
          type="button"
          onClick={props.onEdit}
          disabled={props.isLoading}
          class="p-1 rounded hover:bg-gray-600 text-gray-400 hover:text-white disabled:opacity-50"
          title="Edit message"
        >
          <svg
            class="w-4 h-4"
            fill="none"
            stroke="currentColor"
            viewBox="0 0 24 24"
            role="img"
            aria-label="Edit"
          >
            <path
              stroke-linecap="round"
              stroke-linejoin="round"
              stroke-width="2"
              d="M15.232 5.232l3.536 3.536m-2.036-5.036a2.5 2.5 0 113.536 3.536L6.5 21.036H3v-3.572L16.732 3.732z"
            />
          </svg>
        </button>
      </Show>

      {/* Regenerate button for assistant messages (without errors) */}
      <Show when={props.message.role === 'assistant' && !props.message.error}>
        <button
          type="button"
          onClick={props.onRegenerate}
          disabled={props.isLoading}
          class="p-1 rounded hover:bg-gray-600 text-gray-400 hover:text-white disabled:opacity-50"
          title="Regenerate response"
        >
          <svg
            class="w-4 h-4"
            fill="none"
            stroke="currentColor"
            viewBox="0 0 24 24"
            role="img"
            aria-label="Regenerate"
          >
            <path
              stroke-linecap="round"
              stroke-linejoin="round"
              stroke-width="2"
              d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15"
            />
          </svg>
        </button>
      </Show>
    </div>
  )
}
