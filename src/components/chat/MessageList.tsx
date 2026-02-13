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

import { Bookmark, Sparkles } from 'lucide-solid'
import {
  type Component,
  createEffect,
  createMemo,
  createSignal,
  For,
  onCleanup,
  onMount,
  Show,
} from 'solid-js'
import { useChat } from '../../hooks/useChat'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import type { Message } from '../../types'
import { ConfirmDialog } from '../ui/ConfirmDialog'
import { MessageBubble } from './MessageBubble'
import { ModelChangeIndicator } from './ModelChangeIndicator'

// ============================================================================
// Component
// ============================================================================

export const MessageList: Component = () => {
  // oxlint-disable-next-line no-unassigned-vars -- SolidJS ref pattern: assigned via ref={} in JSX
  let containerRef: HTMLDivElement | undefined
  let lastAutoScrollAt = 0
  let scrollRaf: number | undefined
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

  const messageIndexById = createMemo(() => {
    const indexMap = new Map<string, number>()
    const msgs = messages()
    for (let i = 0; i < msgs.length; i++) {
      indexMap.set(msgs[i].id, i)
    }
    return indexMap
  })

  const checkpointByIndex = createMemo(() => {
    const map = new Map<number, { id: string; description: string }>()
    for (const c of checkpoints()) {
      map.set(c.messageCount - 1, { id: c.id, description: c.description })
    }
    return map
  })

  const modelChangeById = createMemo(() => {
    const map = new Map<string, { from: string; to: string }>()
    let lastAssistantModel = ''

    for (const msg of messages()) {
      if (msg.role !== 'assistant') continue

      const currentModel = (msg.metadata?.model as string) || msg.model || ''
      if (!currentModel) continue

      if (lastAssistantModel && lastAssistantModel !== currentModel) {
        map.set(msg.id, { from: lastAssistantModel, to: currentModel })
      }

      lastAssistantModel = currentModel
    }

    return map
  })

  // Match checkpoints to message indices
  const checkpointAtIndex = (msgIndex: number): { id: string; description: string } | null =>
    checkpointByIndex().get(msgIndex) ?? null

  // Track which message is the last one (for delete vs rollback label)
  const lastMessageId = createMemo(() => {
    const msgs = messages()
    return msgs.length > 0 ? msgs[msgs.length - 1].id : null
  })

  // Auto-scroll to bottom when new content arrives
  createEffect(() => {
    const msgs = messages()
    const lastMsg = msgs[msgs.length - 1]
    const streamKey = lastMsg ? `${lastMsg.id}:${lastMsg.content.length}` : 'none'
    streamKey

    const streaming = isStreaming()

    if (msgs.length > 0 && containerRef && shouldAutoScroll() && settings().behavior.autoScroll) {
      const now = performance.now()
      if (streaming && now - lastAutoScrollAt < 180) return
      lastAutoScrollAt = now

      requestAnimationFrame(() => {
        if (containerRef) {
          containerRef.scrollTop = containerRef.scrollHeight
        }
      })
    }
  })

  const handleScroll = () => {
    if (!containerRef) return
    if (scrollRaf !== undefined) return

    scrollRaf = requestAnimationFrame(() => {
      if (!containerRef) {
        scrollRaf = undefined
        return
      }

      const { scrollTop, scrollHeight, clientHeight } = containerRef
      const nextAutoScroll = scrollHeight - scrollTop - clientHeight < 100
      if (nextAutoScroll !== shouldAutoScroll()) {
        setShouldAutoScroll(nextAutoScroll)
      }

      scrollRaf = undefined
    })
  }

  onCleanup(() => {
    if (scrollRaf !== undefined) cancelAnimationFrame(scrollRaf)
  })

  onMount(() => {
    if (containerRef) containerRef.scrollTop = containerRef.scrollHeight
  })

  const scrollToBottom = () => {
    if (containerRef) {
      containerRef.scrollTop = containerRef.scrollHeight
      setShouldAutoScroll(true)
    }
  }

  const renderMessageRow = (
    msg: Message,
    msgIndex: number,
    isStreamingRow: boolean,
    showCheckpoint = true
  ) => {
    const ckpt = showCheckpoint ? checkpointAtIndex(msgIndex) : undefined

    return (
      <div class="density-py">
        <MessageBubble
          message={msg}
          isEditing={editingMessageId() === msg.id}
          isRetrying={retryingMessageId() === msg.id}
          isStreaming={isStreamingRow}
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
      </div>
    )
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
    <div class="relative flex-1 min-h-0 flex flex-col overflow-hidden">
      <div
        ref={containerRef}
        onScroll={handleScroll}
        class="flex-1 overflow-y-auto density-section-px density-section-py"
        style={{ 'overflow-anchor': 'none' }}
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

        {/* Message items */}
        <Show when={!isLoadingMessages() && messages().length > 0}>
          <div>
            <For each={messages()}>
              {(msg) => {
                const msgIndex = messageIndexById().get(msg.id) ?? -1
                const modelChange = modelChangeById().get(msg.id)

                return (
                  <>
                    <Show when={modelChange}>
                      <ModelChangeIndicator from={modelChange!.from} to={modelChange!.to} />
                    </Show>
                    {renderMessageRow(
                      msg,
                      msgIndex,
                      isStreaming() && msg.id === lastMessageId() && msg.role === 'assistant'
                    )}
                  </>
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
