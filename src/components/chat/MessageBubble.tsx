/**
 * Message Bubble Component
 *
 * Goose-inspired message rendering:
 * - User messages: right-aligned filled bubble
 * - Assistant messages: left-aligned plain text (no bubble), tool cards inline
 * - Timestamps on hover with fade animation
 * - Thinking block as collapsible details
 */

import { AlertCircle, Loader2, RotateCcw } from 'lucide-solid'
import {
  type Accessor,
  type Component,
  createEffect,
  createMemo,
  createSignal,
  For,
  Match,
  on,
  onCleanup,
  Show,
  Switch,
} from 'solid-js'
import { formatCost } from '../../lib/cost'
import type { Message, ToolCall } from '../../types'
import { EditForm } from './EditForm'
import { MarkdownContent } from './MarkdownContent'
import { MessageActions } from './MessageActions'
import { type MessageSegment, segmentMessage } from './message-segments'
import { ThinkingBlock } from './ThinkingBlock'
import { ToolCallGroup } from './ToolCallGroup'
import { ToolCallErrorBoundary } from './tool-call-error-boundary'

interface MessageBubbleProps {
  message: Message
  isEditing: boolean
  isRetrying: boolean
  isStreaming: boolean
  isLastMessage: boolean
  shouldAnimate: boolean
  /** Live tool calls from useAgent signal (avoids store re-renders during streaming) */
  streamingToolCalls?: ToolCall[]
  /** Live content signal — avoids store updates during streaming */
  streamingContent?: Accessor<string>
  onStartEdit: () => void
  onCancelEdit: () => void
  onSaveEdit: (content: string) => Promise<void>
  onRetry: () => void
  onRegenerate: () => void
  onCopy: () => void
  onDelete: () => void
  onBranch: () => void
  onRewind: () => void
}

/** Format raw model ID into a compact display name */
function formatModelName(modelId: string): string {
  let name = modelId.replace(/-\d{8}$/, '')
  const slash = name.lastIndexOf('/')
  if (slash >= 0) name = name.slice(slash + 1)
  return name
}

/** Format timestamp from message */
function formatTimestamp(msg: Message): string {
  const date = msg.createdAt ? new Date(msg.createdAt) : new Date()
  const h = date.getHours()
  const m = date.getMinutes().toString().padStart(2, '0')
  const ampm = h >= 12 ? 'PM' : 'AM'
  const h12 = h % 12 || 12
  return `${h12}:${m} ${ampm}`
}

// ============================================================================
// Main component
// ============================================================================

export const MessageBubble: Component<MessageBubbleProps> = (props) => {
  const isUser = () => props.message.role === 'user'
  const shouldAnimateIn = () => props.shouldAnimate && !props.isEditing

  // Whether this is an actively streaming assistant message
  const isActiveStreaming = () => props.isStreaming && props.isLastMessage && !isUser()

  // Content: during streaming, read from signal (no store dependency); otherwise from message
  const displayContent = () => {
    if (isActiveStreaming() && props.streamingContent) {
      return props.streamingContent()
    }
    return props.message.content
  }

  // Tool calls: during streaming, read from signal; otherwise from message
  const effectiveToolCalls = () => {
    if (isActiveStreaming() && props.streamingToolCalls?.length) {
      return props.streamingToolCalls
    }
    return props.message.toolCalls
  }
  const hasToolCalls = () => !isUser() && (effectiveToolCalls()?.length ?? 0) > 0

  // Segments: ONLY for completed messages. During streaming, render separately.
  const segments = createMemo((): MessageSegment[] | null => {
    if (isUser()) return null
    if (isActiveStreaming()) return null
    if (!hasToolCalls() && !props.message.content) return null
    return segmentMessage(props.message.content, effectiveToolCalls())
  })

  // Retry countdown timer
  const [countdown, setCountdown] = createSignal(0)
  createEffect(
    on(
      () => props.message.error?.retryAfter,
      (retryAfter) => {
        if (!retryAfter || retryAfter <= 0) {
          setCountdown(0)
          return
        }
        setCountdown(retryAfter)
        const timer = setInterval(() => {
          setCountdown((prev) => {
            if (prev <= 1) {
              clearInterval(timer)
              return 0
            }
            return prev - 1
          })
        }, 1000)
        onCleanup(() => clearInterval(timer))
      }
    )
  )

  // ── Shared sub-components ──────────────────────────────────────────────

  const ImagesBlock = () => (
    <Show
      when={(props.message.metadata?.images as Array<{ data: string; mimeType: string }>) ?? []}
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
  )

  /** Timestamp + meta line with hover fade (Goose-style) */
  const TimestampLine = (lineProps: { align?: 'left' | 'right' }) => {
    const align = lineProps.align ?? (isUser() ? 'right' : 'left')
    return (
      <div class={`relative h-[20px] flex ${align === 'right' ? 'justify-end' : 'justify-start'}`}>
        {/* Timestamp — fades out on hover, slides up */}
        <Show when={!props.isStreaming}>
          <div
            class={`font-[var(--font-ui-mono)] text-[10px] tracking-wide text-[var(--text-muted)] pt-1 transition-all duration-200 group-hover:-translate-y-3 group-hover:opacity-0 tabular-nums`}
          >
            {formatTimestamp(props.message)}
            <Show when={!isUser() && props.message.model}>
              {' '}
              &middot; {formatModelName(props.message.model!)}
            </Show>
            <Show when={!isUser() && props.message.tokensUsed}>
              {' '}
              &middot; {props.message.tokensUsed?.toLocaleString()} tokens
            </Show>
            <Show when={!isUser() && props.message.costUSD}>
              {' '}
              &middot; {formatCost(props.message.costUSD!)}
            </Show>
          </div>
        </Show>
        {/* Actions — appear on hover, slide in */}
        <Show when={props.message.content && !props.isStreaming}>
          <div class="absolute left-0 top-0 pt-1">
            <MessageActions
              message={props.message}
              isLastMessage={props.isLastMessage}
              onEdit={props.onStartEdit}
              onRegenerate={props.onRegenerate}
              onCopy={props.onCopy}
              onDelete={props.onDelete}
              onBranch={props.onBranch}
              onRewind={props.onRewind}
              isLoading={props.isStreaming}
            />
          </div>
        </Show>
      </div>
    )
  }

  const ErrorBlock = () => (
    <Show when={props.message.error}>
      <div class="mt-2 p-3 bg-[var(--error-subtle)] border border-[var(--error)] rounded-[var(--radius-md)]">
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
            class="px-3 py-1.5 bg-[var(--error)] hover:brightness-110 text-white text-xs font-medium rounded-[var(--radius-md)] transition-colors duration-[var(--duration-fast)] disabled:opacity-50 disabled:cursor-not-allowed flex items-center gap-1.5"
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
        <Show when={countdown() > 0}>
          <p class="text-xs text-[var(--error)] opacity-75 mt-2">
            Retry available in {countdown()}s
          </p>
        </Show>
      </div>
    </Show>
  )

  // ── Render ─────────────────────────────────────────────────────────────

  return (
    <div
      class={`flex ${isUser() ? 'justify-end' : 'justify-start'} ${shouldAnimateIn() ? 'animate-message-in' : ''}`}
    >
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
        {/* ── User message: right-aligned filled bubble ─── */}
        <Show when={isUser()}>
          <div class="relative group max-w-[85%]">
            <div class="flex flex-col">
              <div class="bg-[var(--chat-user-bg)] text-[var(--chat-user-text)] rounded-[var(--radius-lg)] py-2.5 px-4 shadow-[var(--shadow-sm)]">
                <ImagesBlock />
                <Show when={props.message.content}>
                  <MarkdownContent
                    content={props.message.content}
                    messageRole="user"
                    isStreaming={false}
                  />
                </Show>
              </div>
              <TimestampLine align="right" />
            </div>
          </div>
        </Show>

        {/* ── Assistant message: left-aligned, no bubble ─── */}
        <Show when={!isUser()}>
          <div class="relative group w-[90%] min-w-0">
            <div class="flex flex-col w-full min-w-0">
              {/* Thinking block — collapsible */}
              <Show when={props.message.metadata?.thinking as string}>
                <ThinkingBlock
                  thinking={props.message.metadata!.thinking as string}
                  isStreaming={props.isStreaming}
                />
              </Show>

              {/* ── Streaming layout: tools + text from signals (no store dependency) ── */}
              <Show when={isActiveStreaming()}>
                {/* Tool cards from signal (stable, won't re-render on content changes) */}
                <Show when={hasToolCalls()}>
                  <div class="my-1.5">
                    <ToolCallErrorBoundary>
                      <ToolCallGroup toolCalls={effectiveToolCalls()!} isStreaming={true} />
                    </ToolCallErrorBoundary>
                  </div>
                </Show>
                {/* Streaming text content — reads from signal, renders markdown */}
                <Show when={displayContent()}>
                  <div class="w-full">
                    <MarkdownContent
                      content={displayContent()}
                      messageRole="assistant"
                      isStreaming={true}
                    />
                  </div>
                </Show>
              </Show>

              {/* ── Completed layout: segmented (tools interleaved at correct positions) ── */}
              <Show when={!isActiveStreaming()}>
                <Show when={segments()}>
                  {(segs) => (
                    <For each={segs()}>
                      {(seg) => (
                        <Switch>
                          <Match when={seg.type === 'text' && seg}>
                            {(textSeg) => (
                              <div class="w-full mb-1">
                                <MarkdownContent
                                  content={(textSeg() as MessageSegment & { type: 'text' }).content}
                                  messageRole="assistant"
                                  isStreaming={false}
                                />
                              </div>
                            )}
                          </Match>
                          <Match when={seg.type === 'tools' && seg}>
                            {(toolSeg) => (
                              <div class="my-1.5">
                                <ToolCallErrorBoundary>
                                  <ToolCallGroup
                                    toolCalls={
                                      (toolSeg() as MessageSegment & { type: 'tools' }).toolCalls
                                    }
                                    isStreaming={false}
                                  />
                                </ToolCallErrorBoundary>
                              </div>
                            )}
                          </Match>
                        </Switch>
                      )}
                    </For>
                  )}
                </Show>

                <Show when={!segments() && props.message.content}>
                  <div class="w-full">
                    <MarkdownContent
                      content={props.message.content}
                      messageRole="assistant"
                      isStreaming={false}
                    />
                  </div>
                </Show>
              </Show>

              {/* Timestamp + meta (fades on hover, actions appear) */}
              <TimestampLine align="left" />

              <ErrorBlock />
            </div>
          </div>
        </Show>
      </Show>
    </div>
  )
}
