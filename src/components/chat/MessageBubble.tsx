/**
 * Message Bubble Component
 *
 * Individual message display with tokens, error state, and actions.
 * Premium styling with themed colors and smooth interactions.
 */

import { AlertCircle, Loader2, RotateCcw } from 'lucide-solid'
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
  const isUser = () => props.message.role === 'user'

  return (
    <div class={`flex ${isUser() ? 'justify-end' : 'justify-start'}`}>
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
            class={`
              rounded-[var(--radius-lg)] px-4 py-3
              transition-colors duration-[var(--duration-fast)]
              ${
                isUser()
                  ? 'bg-[var(--accent)] text-white'
                  : 'bg-[var(--surface-raised)] text-[var(--text-primary)] border border-[var(--border-subtle)]'
              }
            `}
          >
            {/* Show typing indicator for empty assistant messages */}
            <Show when={props.message.content || isUser()} fallback={<TypingIndicator />}>
              <p class="whitespace-pre-wrap break-words text-sm leading-relaxed">
                {props.message.content}
              </p>
            </Show>

            {/* Token badge for assistant messages */}
            <Show when={!isUser() && props.message.tokensUsed}>
              <div class="mt-2 text-xs text-[var(--text-muted)] text-right tabular-nums">
                {props.message.tokensUsed?.toLocaleString()} tokens
              </div>
            </Show>

            {/* Edited indicator */}
            <Show when={props.message.editedAt}>
              <div class="mt-1 text-xs text-[var(--text-muted)] italic opacity-75">(edited)</div>
            </Show>
          </div>

          {/* Error display with retry button */}
          <Show when={props.message.error}>
            <div
              class="
                mt-2 p-3
                bg-[var(--error-subtle)]
                border border-[var(--error-muted)]
                rounded-[var(--radius-md)]
              "
            >
              <div class="flex items-center justify-between gap-3">
                <div class="flex items-center gap-2 flex-1 min-w-0">
                  <AlertCircle class="w-4 h-4 text-[var(--error)] flex-shrink-0" />
                  <span class="text-sm text-[var(--error)] truncate">
                    {props.message.error!.message}
                  </span>
                </div>
                <button
                  type="button"
                  onClick={() => props.onRetry()}
                  disabled={props.isStreaming || props.isRetrying}
                  class="
                    px-3 py-1.5
                    bg-[var(--error)] hover:bg-[var(--error-hover)]
                    text-white text-xs font-medium
                    rounded-[var(--radius-md)]
                    transition-colors duration-[var(--duration-fast)]
                    disabled:opacity-50 disabled:cursor-not-allowed
                    flex items-center gap-1.5
                  "
                >
                  <Show
                    when={props.isRetrying}
                    fallback={
                      <>
                        <RotateCcw class="w-3 h-3" />
                        Retry
                      </>
                    }
                  >
                    <Loader2 class="w-3 h-3 animate-spin" />
                    Retrying
                  </Show>
                </button>
              </div>
              <Show when={props.message.error!.retryAfter}>
                <p class="text-xs text-[var(--error)] opacity-75 mt-2">
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
