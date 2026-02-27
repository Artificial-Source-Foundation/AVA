/**
 * Message Bubble Component
 *
 * Individual message display with markdown rendering, tokens, error state,
 * and actions (copy, delete, edit, regenerate, retry).
 */

import { formatCost } from '@ava/core'
import { AlertCircle, ChevronDown, ChevronUp, Loader2, RotateCcw } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type { Message } from '../../types'
import { ActiveToolIndicator } from './active-tool-indicator'
import { EditForm } from './EditForm'
import { MarkdownContent } from './MarkdownContent'
import { MessageActions } from './MessageActions'
import { ToolCallGroup } from './ToolCallGroup'
import { TypingIndicator } from './TypingIndicator'
import { ToolCallErrorBoundary } from './tool-call-error-boundary'

interface MessageBubbleProps {
  message: Message
  isEditing: boolean
  isRetrying: boolean
  isStreaming: boolean
  isLastMessage: boolean
  onStartEdit: () => void
  onCancelEdit: () => void
  onSaveEdit: (content: string) => Promise<void>
  onRetry: () => void
  onRegenerate: () => void
  onCopy: () => void
  onDelete: () => void
}

const ASSISTANT_COLLAPSE_CHARS = 1500
const USER_COLLAPSE_LINES = 8

export const MessageBubble: Component<MessageBubbleProps> = (props) => {
  const isUser = () => props.message.role === 'user'
  const shouldAnimateIn = () => isUser() && !props.isEditing
  const lineCount = () => props.message.content.split('\n').length
  const isLong = () => {
    if (isUser()) return lineCount() > USER_COLLAPSE_LINES
    return props.message.content.length > ASSISTANT_COLLAPSE_CHARS
  }
  const [expanded, setExpanded] = createSignal(false)
  const shouldCollapse = () => isLong() && !expanded() && !props.isStreaming

  return (
    <div
      class={`flex ${isUser() ? 'justify-end' : 'justify-start'} ${shouldAnimateIn() ? 'animate-message-in' : ''}`}
    >
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
        <div class="relative group max-w-[85%]">
          <div
            class={`
              rounded-[var(--radius-lg)] density-section-px density-section-py
              transition-colors duration-[var(--duration-fast)]
              ${
                isUser()
                  ? 'bg-[var(--chat-user-bg)] text-[var(--chat-user-text)]'
                  : 'bg-[var(--chat-assistant-bg)] text-[var(--chat-assistant-text)] border border-[var(--chat-assistant-border)]'
              }
            `}
          >
            {/* Attached images */}
            <Show
              when={
                (props.message.metadata?.images as Array<{ data: string; mimeType: string }>) ?? []
              }
            >
              {(images) => (
                <Show when={images().length > 0}>
                  <div class="flex gap-2 mb-2 flex-wrap">
                    <For each={images()}>
                      {(img) => (
                        <img
                          src={`data:${img.mimeType};base64,${img.data}`}
                          alt="Attached"
                          class="max-w-[200px] max-h-[200px] rounded object-contain"
                        />
                      )}
                    </For>
                  </div>
                </Show>
              )}
            </Show>

            {/* Show typing indicator while streaming, placeholder otherwise */}
            <Show
              when={props.message.content || isUser()}
              fallback={
                <Show
                  when={props.isStreaming}
                  fallback={
                    <div class={props.isLastMessage && !props.message.error ? 'h-5' : 'h-3'} />
                  }
                >
                  <TypingIndicator />
                </Show>
              }
            >
              <div
                class={shouldCollapse() ? 'relative overflow-hidden' : ''}
                style={shouldCollapse() ? { 'max-height': '300px' } : {}}
              >
                <MarkdownContent
                  content={props.message.content}
                  role={props.message.role}
                  isStreaming={props.isStreaming}
                />
                {/* Fade gradient for collapsed long messages */}
                <Show when={shouldCollapse()}>
                  <div
                    class="absolute bottom-0 left-0 right-0 h-16 bg-gradient-to-t to-transparent pointer-events-none"
                    classList={{
                      'from-[var(--chat-user-bg)]': isUser(),
                      'from-[var(--chat-assistant-bg)]': !isUser(),
                    }}
                  />
                </Show>
              </div>
              {/* Show more/less toggle */}
              <Show when={isLong()}>
                <button
                  type="button"
                  onClick={() => setExpanded((v) => !v)}
                  class="flex items-center gap-1 mt-1 text-xs text-[var(--accent)] hover:text-[var(--accent-hover)] transition-colors"
                >
                  <Show when={expanded()} fallback={<ChevronDown class="w-3 h-3" />}>
                    <ChevronUp class="w-3 h-3" />
                  </Show>
                  {expanded()
                    ? 'Show less'
                    : isUser()
                      ? `Show ${lineCount() - USER_COLLAPSE_LINES} more lines`
                      : 'Show more'}
                </button>
              </Show>
            </Show>

            {/* Token badge + cost for assistant messages */}
            <Show when={!isUser() && props.message.tokensUsed}>
              <div class="mt-2 font-[var(--font-ui-mono)] text-[10px] tracking-wide text-[var(--text-muted)] text-right tabular-nums">
                {props.message.tokensUsed?.toLocaleString()} tokens
                <Show when={props.message.costUSD}>
                  {' '}
                  &middot; {formatCost(props.message.costUSD!)}
                </Show>
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
                border border-[var(--error)]
                rounded-[var(--radius-md)]
              "
            >
              <div class="flex items-center justify-between gap-3">
                <div class="flex items-start gap-2 flex-1 min-w-0">
                  <AlertCircle class="w-4 h-4 text-[var(--error)] flex-shrink-0" />
                  <span class="text-sm text-[var(--error)] break-words whitespace-pre-wrap leading-relaxed">
                    {props.message.error!.message}
                  </span>
                </div>
                <button
                  type="button"
                  onClick={() => props.onRetry()}
                  disabled={props.isStreaming || props.isRetrying}
                  class="
                    px-3 py-1.5
                    bg-[var(--error)] hover:brightness-110
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
          <Show when={props.message.content}>
            <MessageActions
              message={props.message}
              isLastMessage={props.isLastMessage}
              onEdit={props.onStartEdit}
              onRegenerate={props.onRegenerate}
              onCopy={props.onCopy}
              onDelete={props.onDelete}
              isLoading={props.isStreaming}
            />
          </Show>

          {/* Tool calls — inside wrapper, visually attached to bubble */}
          <Show when={!isUser() && props.message.toolCalls?.length}>
            <ToolCallErrorBoundary>
              <ToolCallGroup
                toolCalls={props.message.toolCalls!}
                isStreaming={props.isStreaming && props.isLastMessage}
              />
            </ToolCallErrorBoundary>
          </Show>

          {/* Active tool indicator — replaces generic "Working..." */}
          <Show
            when={!isUser() && props.isStreaming && props.isLastMessage && props.message.content}
          >
            <ActiveToolIndicator
              toolCalls={props.message.toolCalls}
              isStreaming={props.isStreaming}
            />
          </Show>
        </div>
      </Show>
    </div>
  )
}
