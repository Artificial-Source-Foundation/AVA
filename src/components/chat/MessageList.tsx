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
  untrack,
} from 'solid-js'
import { useNotification } from '../../contexts/notification'
import { useAgent } from '../../hooks/useAgent'
import { useChat } from '../../hooks/useChat'
import { useLayout } from '../../stores/layout'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import { ConfirmDialog } from '../ui/ConfirmDialog'
import { FocusChainBar } from './FocusChainBar'
import { ModelChangeIndicator } from './ModelChangeIndicator'
import { MessageRow } from './message-list/message-row'
import { MessageListEmpty, MessageListLoading, ScrollToBottomButton } from './message-list/sections'
import { SearchBar } from './SearchBar'

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
  // Adaptive visible limit based on viewport height (~60px per message row)
  const adaptiveChunk = () => Math.max(50, Math.min(300, Math.floor(window.innerHeight / 60)))
  const [visibleLimit, setVisibleLimit] = createSignal(adaptiveChunk())
  const [deleteTarget, setDeleteTarget] = createSignal<{
    messageId: string
    isLast: boolean
  } | null>(null)
  const [rewindTarget, setRewindTarget] = createSignal<string | null>(null)
  const [searchMatchIds, setSearchMatchIds] = createSignal<Set<string>>(new Set())
  const [currentSearchId, setCurrentSearchId] = createSignal<string | null>(null)

  const { chatSearchOpen, closeChatSearch } = useLayout()

  const {
    messages,
    isLoadingMessages,
    editingMessageId,
    retryingMessageId,
    startEditing,
    stopEditing,
    rollbackToMessage,
    branchAtMessage,
    checkpoints,
    rollbackToCheckpoint,
    revertFilesAfter,
  } = useSession()
  const agent = useAgent()
  const { isStreaming, retryMessage, editAndResend, regenerateResponse } = useChat()
  const { success: notifySuccess } = useNotification()

  // Track which messages have already animated in (persists across <For> re-creations)
  const animatedMessageIds = new Set<string>()

  const messageCount = createMemo(() => messages().length)

  const messageIndexById = createMemo(() => {
    messageCount() // tracked: only re-run when count changes
    return untrack(() => {
      const indexMap = new Map<string, number>()
      const msgs = messages()
      for (let i = 0; i < msgs.length; i++) {
        indexMap.set(msgs[i].id, i)
      }
      return indexMap
    })
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

  const visibleMessages = createMemo(() => {
    const all = messages()
    const limit = visibleLimit()
    if (all.length <= limit) return all
    return all.slice(-limit)
  })

  const hiddenMessageCount = createMemo(() =>
    Math.max(0, messages().length - visibleMessages().length)
  )

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
      scrollRaf = undefined // clear lock first
      if (!containerRef) return

      const { scrollTop, scrollHeight, clientHeight } = containerRef
      const nextAutoScroll = scrollHeight - scrollTop - clientHeight < 100
      if (nextAutoScroll !== shouldAutoScroll()) {
        setShouldAutoScroll(nextAutoScroll)
      }

      // Scroll-up backfill: load older messages when near top
      if (scrollTop < 200 && hiddenMessageCount() > 0) {
        loadOlderMessages()
      }
    })
  }

  onMount(() => {
    if (containerRef) {
      containerRef.scrollTop = containerRef.scrollHeight
      // Passive listener — critical for smooth scrolling in WebKitGTK.
      // SolidJS onScroll doesn't set { passive: true }, which blocks the
      // browser's scroll thread while the JS handler runs.
      containerRef.addEventListener('scroll', handleScroll, { passive: true })
    }
  })

  onCleanup(() => {
    if (scrollRaf !== undefined) cancelAnimationFrame(scrollRaf)
    containerRef?.removeEventListener('scroll', handleScroll)
  })

  const scrollToBottom = () => {
    if (containerRef) {
      containerRef.scrollTop = containerRef.scrollHeight
      setShouldAutoScroll(true)
    }
  }

  const loadOlderMessages = () => {
    const increment = Math.max(100, Math.min(400, Math.floor(window.innerHeight / 60) * 2))
    setVisibleLimit((limit) => limit + increment)
  }

  const scrollToMessage = (messageId: string) => {
    if (!containerRef) return
    const el = containerRef.querySelector(`[data-message-id="${messageId}"]`)
    if (el) {
      el.scrollIntoView({ behavior: 'smooth', block: 'center' })
    }
  }

  const handleSearchHighlight = (matchIds: Set<string>, currentId: string | null) => {
    setSearchMatchIds(matchIds)
    setCurrentSearchId(currentId)
  }

  // Branch handler
  const handleBranch = async (messageId: string) => {
    await branchAtMessage(messageId)
    notifySuccess('Conversation branched')
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

  // Rewind handlers (Item 5)
  const handleRewindConversationOnly = async () => {
    const msgId = rewindTarget()
    if (!msgId) return
    setRewindTarget(null)
    // Keep messages up to and including the target
    const msgs = messages()
    const index = msgs.findIndex((m) => m.id === msgId)
    if (index === -1) return
    // Delete everything after this message
    const nextMsg = msgs[index + 1]
    if (nextMsg) await rollbackToMessage(nextMsg.id)
    notifySuccess('Conversation rewound')
  }

  const handleRewindAndRevert = async () => {
    const msgId = rewindTarget()
    if (!msgId) return
    setRewindTarget(null)
    const reverted = await revertFilesAfter(msgId)
    const msgs = messages()
    const index = msgs.findIndex((m) => m.id === msgId)
    if (index === -1) return
    const nextMsg = msgs[index + 1]
    if (nextMsg) await rollbackToMessage(nextMsg.id)
    notifySuccess(`Rewound${reverted > 0 ? ` and reverted ${reverted} file(s)` : ''}`)
  }

  return (
    <div class="relative flex-1 min-h-0 flex flex-col overflow-hidden">
      {/* Focus chain progress bar (Item 7) */}
      <FocusChainBar />

      {/* Search bar */}
      <Show when={chatSearchOpen()}>
        <SearchBar
          messages={messages()}
          onClose={closeChatSearch}
          onNavigate={scrollToMessage}
          onHighlightChange={handleSearchHighlight}
        />
      </Show>

      <div
        ref={containerRef}
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
            <Show when={hiddenMessageCount() > 0}>
              <div class="mb-2 flex items-center justify-center">
                <button
                  type="button"
                  onClick={loadOlderMessages}
                  class="rounded-[var(--radius-sm)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] px-2.5 py-1 text-[10px] text-[var(--text-secondary)] hover:text-[var(--text-primary)]"
                >
                  Load{' '}
                  {Math.min(
                    Math.max(100, Math.min(400, Math.floor(window.innerHeight / 60) * 2)),
                    hiddenMessageCount()
                  )}{' '}
                  older messages ({hiddenMessageCount()} hidden)
                </button>
              </div>
            </Show>

            <For each={visibleMessages()}>
              {(msg) => {
                const msgIndex = () => messageIndexById().get(msg.id) ?? -1
                const modelChange = () => modelChangeById().get(msg.id)

                // Compute shouldAnimate ONCE per new message (persists across <For> re-creations)
                const shouldAnimate = !animatedMessageIds.has(msg.id)
                if (shouldAnimate) animatedMessageIds.add(msg.id)

                return (
                  <>
                    <Show when={modelChange()}>
                      {(change) => <ModelChangeIndicator from={change().from} to={change().to} />}
                    </Show>
                    <MessageRow
                      message={msg}
                      shouldAnimate={shouldAnimate}
                      isEditing={editingMessageId() === msg.id}
                      isRetrying={retryingMessageId() === msg.id}
                      isStreaming={
                        (isStreaming() || agent.isRunning()) &&
                        msg.id === lastMessageId() &&
                        msg.role === 'assistant'
                      }
                      isLastMessage={msg.id === lastMessageId()}
                      isSearchMatch={searchMatchIds().has(msg.id)}
                      isCurrentSearchMatch={currentSearchId() === msg.id}
                      checkpoint={checkpointAtIndex(msgIndex()) ?? undefined}
                      onStartEdit={() => startEditing(msg.id)}
                      onCancelEdit={stopEditing}
                      onSaveEdit={(content) => editAndResend(msg.id, content)}
                      onRetry={() => retryMessage(msg.id)}
                      onRegenerate={() => regenerateResponse(msg.id)}
                      onDelete={() => handleDeleteRequest(msg.id)}
                      onBranch={() => handleBranch(msg.id)}
                      onRewind={() => setRewindTarget(msg.id)}
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

      {/* Rewind dialog (Item 5) */}
      <Show when={rewindTarget() !== null}>
        <div class="fixed inset-0 z-50 flex items-center justify-center bg-black/60">
          <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] p-6 max-w-sm w-full shadow-2xl space-y-4">
            <h3 class="text-sm font-semibold text-[var(--text-primary)]">Rewind conversation?</h3>
            <p class="text-xs text-[var(--text-secondary)]">
              Messages after this point will be removed. Choose whether to also revert file changes.
            </p>
            <div class="flex flex-col gap-2">
              <button
                type="button"
                onClick={handleRewindConversationOnly}
                class="w-full px-3 py-2 text-xs font-medium rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] hover:bg-[var(--accent-subtle)] transition-colors text-left"
              >
                Rewind conversation only
              </button>
              <button
                type="button"
                onClick={handleRewindAndRevert}
                class="w-full px-3 py-2 text-xs font-medium rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] hover:bg-[var(--accent-subtle)] transition-colors text-left"
              >
                Rewind and revert files
              </button>
            </div>
            <button
              type="button"
              onClick={() => setRewindTarget(null)}
              class="w-full text-center text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
            >
              Cancel
            </button>
          </div>
        </div>
      </Show>
    </div>
  )
}
