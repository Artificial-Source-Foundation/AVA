/**
 * MessageBubble Component
 * Individual message display with tokens, error state, and actions
 */

import { type Component, Show } from 'solid-js'
import type { Message } from '../../types'
import { EditForm } from './EditForm'
import { MessageActions } from './MessageActions'
import { TypingIndicator } from './TypingIndicator'

interface MessageBubbleProps {
  message: Message
  isEditing: boolean
  isRetrying: boolean
  isStreaming: boolean
  onStartEdit: () => void
  onCancelEdit: () => void
  onSaveEdit: (content: string) => Promise<void>
  onRetry: () => void
  onRegenerate: () => void
}

export const MessageBubble: Component<MessageBubbleProps> = (props) => {
  return (
    <div class={`flex ${props.message.role === 'user' ? 'justify-end' : 'justify-start'}`}>
      {/* Edit mode for user messages */}
      <Show
        when={!props.isEditing}
        fallback={
          <EditForm
            initialContent={props.message.content}
            onSave={props.onSaveEdit}
            onCancel={props.onCancelEdit}
          />
        }
      >
        {/* Normal message display */}
        <div class="relative group max-w-[80%]">
          <div
            class={`rounded-lg px-4 py-2 ${
              props.message.role === 'user' ? 'bg-blue-600 text-white' : 'bg-gray-700 text-gray-100'
            }`}
          >
            {/* Show typing indicator for empty assistant messages */}
            <Show
              when={props.message.content || props.message.role === 'user'}
              fallback={<TypingIndicator />}
            >
              <p class="whitespace-pre-wrap break-words">{props.message.content}</p>
            </Show>

            {/* Token badge for assistant messages */}
            <Show when={props.message.role === 'assistant' && props.message.tokensUsed}>
              <div class="mt-1 text-xs text-gray-400 text-right">
                {props.message.tokensUsed?.toLocaleString()} tokens
              </div>
            </Show>

            {/* Edited indicator */}
            <Show when={props.message.editedAt}>
              <div class="mt-1 text-xs text-gray-500 italic">(edited)</div>
            </Show>
          </div>

          {/* Error display with retry button */}
          <Show when={props.message.error}>
            <div class="mt-2 p-2 bg-red-900/30 border border-red-700 rounded text-sm">
              <div class="flex items-center justify-between gap-2">
                <span class="text-red-300 flex-1">{props.message.error!.message}</span>
                <button
                  type="button"
                  onClick={props.onRetry}
                  disabled={props.isStreaming || props.isRetrying}
                  class="px-3 py-1 bg-red-600 hover:bg-red-500 text-white rounded text-xs disabled:opacity-50 flex-shrink-0"
                >
                  <Show when={props.isRetrying} fallback="Retry">
                    <span class="inline-block animate-spin">...</span>
                  </Show>
                </button>
              </div>
              <Show when={props.message.error!.retryAfter}>
                <p class="text-xs text-red-400 mt-1">
                  Retry available in {props.message.error!.retryAfter}s
                </p>
              </Show>
            </div>
          </Show>

          {/* Action buttons on hover */}
          <Show when={props.message.content && !props.message.error}>
            <MessageActions
              message={props.message}
              onEdit={props.onStartEdit}
              onRegenerate={props.onRegenerate}
              isLoading={props.isStreaming}
            />
          </Show>
        </div>
      </Show>
    </div>
  )
}
