/**
 * Message List Component
 *
 * Displays conversation messages with virtual scrolling and date separators.
 * Uses a flattened ChatItem[] array for heterogeneous virtual list.
 *
 * Features:
 * - Virtual scrolling for 1000+ messages
 * - Date separators ("Today", "Yesterday", "Feb 7")
 * - Auto-scroll to new messages
 * - Dynamic height estimation
 * - Delete/rollback with confirmation dialog
 */

import { createVirtualizer } from '@tanstack/solid-virtual'
import { Bookmark, Sparkles } from 'lucide-solid'
import {
  type Component,
  createEffect,
  createMemo,
  createSignal,
  For,
  onMount,
  Show,
} from 'solid-js'
import { useChat } from '../../hooks/useChat'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import type { Message } from '../../types'
import { ConfirmDialog } from '../ui/ConfirmDialog'
import { DateSeparator, formatDateLabel } from './DateSeparator'
import { MessageBubble } from './MessageBubble'
import { ModelChangeIndicator } from './ModelChangeIndicator'

// ============================================================================
// Types
// ============================================================================

type ChatItem =
  | { type: 'separator'; label: string; key: string }
  | { type: 'message'; message: Message }
  | { type: 'model-change'; from: string; to: string; key: string }

// ============================================================================
// Constants
// ============================================================================

const ESTIMATED_MESSAGE_HEIGHT = 120
const ESTIMATED_SEPARATOR_HEIGHT = 40
const OVERSCAN = 5

// ============================================================================
// Component
// ============================================================================

export const MessageList: Component = () => {
  // oxlint-disable-next-line no-unassigned-vars -- SolidJS ref pattern: assigned via ref={} in JSX
  let containerRef: HTMLDivElement | undefined
  const { settings } = useSettings()
  const [shouldAutoScroll, setShouldAutoScroll] = createSignal(true)
  const [deleteTarget, setDeleteTarget] = createSignal<{
    messageId: string
    isLast: boolean
  } | null>(null)

  const {
    messages,
    isLoadingMessages,
    editingMessageId,
    retryingMessageId,
    startEditing,
    stopEditing,
    rollbackToMessage,
    checkpoints,
    rollbackToCheckpoint,
  } = useSession()
  const { isStreaming, retryMessage, editAndResend, regenerateResponse } = useChat()

  // Match checkpoints to message indices
  const checkpointAtIndex = (msgIndex: number): { id: string; description: string } | null => {
    const ckpts = checkpoints()
    const match = ckpts.find((c) => c.messageCount === msgIndex + 1)
    return match ? { id: match.id, description: match.description } : null
  }

  // Build flattened items with date separators and model change indicators
  const chatItems = createMemo((): ChatItem[] => {
    const msgs = messages()
    const items: ChatItem[] = []
    let lastDate = ''
    let lastModel = ''

    for (const msg of msgs) {
      const dateLabel = formatDateLabel(msg.createdAt)
      if (dateLabel !== lastDate) {
        items.push({ type: 'separator', label: dateLabel, key: `sep-${dateLabel}` })
        lastDate = dateLabel
      }

      // Insert model change indicator between assistant messages with different models
      const msgModel = (msg.metadata?.model as string) || msg.model || ''
      if (msg.role === 'assistant' && msgModel && lastModel && msgModel !== lastModel) {
        items.push({
          type: 'model-change',
          from: lastModel,
          to: msgModel,
          key: `model-${msg.id}`,
        })
      }
      if (msg.role === 'assistant' && msgModel) {
        lastModel = msgModel
      }

      items.push({ type: 'message', message: msg })
    }
    return items
  })

  // Track which message is the last one (for delete vs rollback label)
  const lastMessageId = createMemo(() => {
    const msgs = messages()
    return msgs.length > 0 ? msgs[msgs.length - 1].id : null
  })

  // Create virtualizer
  const virtualizer = createMemo(() => {
    const items = chatItems()
    return createVirtualizer({
      get count() {
        return items.length
      },
      getScrollElement: () => containerRef ?? null,
      estimateSize: (index) => {
        const t = items[index]?.type
        return t === 'separator' || t === 'model-change'
          ? ESTIMATED_SEPARATOR_HEIGHT
          : ESTIMATED_MESSAGE_HEIGHT
      },
      overscan: OVERSCAN,
      scrollMargin: 0,
    })
  })

  const virtualItems = createMemo(() => virtualizer().getVirtualItems())
  const totalSize = createMemo(() => virtualizer().getTotalSize())

  // Auto-scroll to bottom when new messages arrive
  createEffect(() => {
    const items = chatItems()
    const streaming = isStreaming()

    if (items.length > 0 && containerRef && shouldAutoScroll() && settings().behavior.autoScroll) {
      requestAnimationFrame(() => {
        if (containerRef) {
          containerRef.scrollTo({
            top: containerRef.scrollHeight,
            behavior: streaming ? 'auto' : 'smooth',
          })
        }
      })
    }
  })

  const handleScroll = () => {
    if (!containerRef) return
    const { scrollTop, scrollHeight, clientHeight } = containerRef
    setShouldAutoScroll(scrollHeight - scrollTop - clientHeight < 100)
  }

  onMount(() => {
    if (containerRef) containerRef.scrollTop = containerRef.scrollHeight
  })

  const scrollToBottom = () => {
    if (containerRef) {
      containerRef.scrollTo({ top: containerRef.scrollHeight, behavior: 'smooth' })
      setShouldAutoScroll(true)
    }
  }

  // Delete/rollback handlers
  const handleDeleteRequest = (messageId: string) => {
    setDeleteTarget({ messageId, isLast: messageId === lastMessageId() })
  }

  const handleDeleteConfirm = async () => {
    const target = deleteTarget()
    if (!target) return
    setDeleteTarget(null)
    await rollbackToMessage(target.messageId)
  }

  return (
    <div class="relative flex-1 flex flex-col">
      <div
        ref={containerRef}
        onScroll={handleScroll}
        class="flex-1 overflow-y-auto density-section-px density-section-py"
      >
        {/* Loading skeleton */}
        <Show when={isLoadingMessages()}>
          <div class="space-y-4 animate-pulse">
            <div class="h-16 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-2/3" />
            <div class="h-24 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-3/4 ml-auto" />
            <div class="h-16 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-2/3" />
          </div>
        </Show>

        {/* Empty state */}
        <Show when={!isLoadingMessages() && messages().length === 0}>
          <div class="flex flex-col items-center justify-center h-full">
            <div
              class="
                w-16 h-16 mb-6
                rounded-[var(--radius-xl)]
                bg-[var(--accent-subtle)]
                flex items-center justify-center
              "
            >
              <Sparkles class="w-8 h-8 text-[var(--accent)]" />
            </div>
            <h2 class="text-xl font-semibold text-[var(--text-primary)] font-display">
              Welcome to Estela
            </h2>
            <p class="text-sm text-[var(--text-tertiary)] mt-2 max-w-sm text-center">
              Your AI coding assistant is ready. Start a conversation to begin.
            </p>
          </div>
        </Show>

        {/* Virtualized items (messages + date separators) */}
        <Show when={!isLoadingMessages() && chatItems().length > 0}>
          <div
            style={{
              height: `${totalSize()}px`,
              width: '100%',
              position: 'relative',
            }}
          >
            <For each={virtualItems()}>
              {(virtualItem) => {
                const item = () => chatItems()[virtualItem.index]
                return (
                  <div
                    data-index={virtualItem.index}
                    ref={(el) => {
                      // eslint-disable-next-line solid/reactivity -- ref callback runs once per element mount
                      queueMicrotask(() => virtualizer().measureElement(el))
                    }}
                    style={{
                      position: 'absolute',
                      top: 0,
                      left: 0,
                      width: '100%',
                      transform: `translateY(${virtualItem.start}px)`,
                    }}
                  >
                    <Show when={item()}>
                      {item()!.type === 'separator' ? (
                        <DateSeparator label={(item() as ChatItem & { type: 'separator' }).label} />
                      ) : item()!.type === 'model-change' ? (
                        <ModelChangeIndicator
                          from={(item() as ChatItem & { type: 'model-change' }).from}
                          to={(item() as ChatItem & { type: 'model-change' }).to}
                        />
                      ) : (
                        <div class="density-py">
                          {(() => {
                            const msg = (item() as ChatItem & { type: 'message' }).message
                            const msgIndex = messages().findIndex((m) => m.id === msg.id)
                            const ckpt = checkpointAtIndex(msgIndex)
                            return (
                              <>
                                <MessageBubble
                                  message={msg}
                                  isEditing={editingMessageId() === msg.id}
                                  isRetrying={retryingMessageId() === msg.id}
                                  isStreaming={isStreaming()}
                                  isLastMessage={msg.id === lastMessageId()}
                                  onStartEdit={() => startEditing(msg.id)}
                                  onCancelEdit={stopEditing}
                                  onSaveEdit={(content) => editAndResend(msg.id, content)}
                                  onRetry={() => retryMessage(msg.id)}
                                  onRegenerate={() => regenerateResponse(msg.id)}
                                  onCopy={() => {}}
                                  onDelete={() => handleDeleteRequest(msg.id)}
                                />
                                <Show when={ckpt}>
                                  <div class="flex items-center gap-2 py-1 text-[10px] text-[var(--text-muted)]">
                                    <Bookmark class="w-3 h-3 text-[var(--accent)]" />
                                    <span>{ckpt!.description}</span>
                                    <button
                                      type="button"
                                      onClick={() => rollbackToCheckpoint(ckpt!.id)}
                                      class="text-[var(--accent)] hover:underline"
                                    >
                                      Restore
                                    </button>
                                  </div>
                                </Show>
                              </>
                            )
                          })()}
                        </div>
                      )}
                    </Show>
                  </div>
                )
              }}
            </For>
          </div>
        </Show>
      </div>

      {/* Scroll to bottom button */}
      <Show when={!shouldAutoScroll() && messages().length > 0}>
        <button
          type="button"
          onClick={scrollToBottom}
          class="
            absolute bottom-4 right-8
            p-2 rounded-full
            bg-[var(--surface-raised)] border border-[var(--border-subtle)]
            shadow-md
            text-[var(--text-secondary)]
            hover:bg-[var(--accent)] hover:text-white hover:border-[var(--accent)]
            transition-all duration-[var(--duration-fast)]
            z-10
          "
          title="Scroll to bottom"
        >
          <svg
            class="w-5 h-5"
            fill="none"
            viewBox="0 0 24 24"
            stroke="currentColor"
            aria-labelledby="scroll-icon-title"
          >
            <title id="scroll-icon-title">Scroll to bottom</title>
            <path
              stroke-linecap="round"
              stroke-linejoin="round"
              stroke-width="2"
              d="M19 14l-7 7m0 0l-7-7m7 7V3"
            />
          </svg>
        </button>
      </Show>

      {/* Delete confirmation dialog */}
      <ConfirmDialog
        open={deleteTarget() !== null}
        onOpenChange={(open) => {
          if (!open) setDeleteTarget(null)
        }}
        title={deleteTarget()?.isLast ? 'Delete message?' : 'Rollback conversation?'}
        message={
          deleteTarget()?.isLast
            ? 'This message will be permanently deleted.'
            : 'This will delete this message and all messages after it. This cannot be undone.'
        }
        confirmText={deleteTarget()?.isLast ? 'Delete' : 'Rollback'}
        variant="danger"
        onConfirm={handleDeleteConfirm}
      />
    </div>
  )
}
