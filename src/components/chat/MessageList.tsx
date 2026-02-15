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

import {
  type Component,
  createEffect,
  createMemo,
  createSignal,
  For,
  on,
  onCleanup,
  onMount,
  Show,
} from 'solid-js'
import { useChat } from '../../hooks/useChat'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import { ConfirmDialog } from '../ui/ConfirmDialog'
import { ModelChangeIndicator } from './ModelChangeIndicator'
import { MessageRow } from './message-list/message-row'
import { MessageListEmpty, MessageListLoading, ScrollToBottomButton } from './message-list/sections'

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
  createEffect(
    on(
      () => {
        const msgs = messages()
        const lastMsg = msgs[msgs.length - 1]
        return lastMsg ? `${lastMsg.id}:${lastMsg.content.length}` : 'none'
      },
      () => {
        const msgs = messages()
        const streaming = isStreaming()

        if (
          msgs.length > 0 &&
          containerRef &&
          shouldAutoScroll() &&
          settings().behavior.autoScroll
        ) {
          const now = performance.now()
          if (streaming && now - lastAutoScrollAt < 180) return
          lastAutoScrollAt = now

          requestAnimationFrame(() => {
            if (containerRef) {
              containerRef.scrollTop = containerRef.scrollHeight
            }
          })
        }
      }
    )
  )

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
          <MessageListLoading />
        </Show>

        {/* Empty state */}
        <Show when={!isLoadingMessages() && messages().length === 0}>
          <MessageListEmpty />
        </Show>

        {/* Message items */}
        <Show when={!isLoadingMessages() && messages().length > 0}>
          <div>
            <For each={messages()}>
              {(msg) => {
                const msgIndex = () => messageIndexById().get(msg.id) ?? -1
                const modelChange = () => modelChangeById().get(msg.id)

                return (
                  <>
                    <Show when={modelChange()}>
                      {(change) => <ModelChangeIndicator from={change().from} to={change().to} />}
                    </Show>
                    <MessageRow
                      message={msg}
                      isEditing={editingMessageId() === msg.id}
                      isRetrying={retryingMessageId() === msg.id}
                      isStreaming={
                        isStreaming() && msg.id === lastMessageId() && msg.role === 'assistant'
                      }
                      isLastMessage={msg.id === lastMessageId()}
                      checkpoint={checkpointAtIndex(msgIndex()) ?? undefined}
                      onStartEdit={() => startEditing(msg.id)}
                      onCancelEdit={stopEditing}
                      onSaveEdit={(content) => editAndResend(msg.id, content)}
                      onRetry={() => retryMessage(msg.id)}
                      onRegenerate={() => regenerateResponse(msg.id)}
                      onDelete={() => handleDeleteRequest(msg.id)}
                      onRestoreCheckpoint={rollbackToCheckpoint}
                    />
                  </>
                )
              }}
            </For>
          </div>
        </Show>
      </div>

      {/* Scroll to bottom button */}
      <Show when={!shouldAutoScroll() && messages().length > 0}>
        <ScrollToBottomButton onClick={scrollToBottom} />
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
