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
import { Dialog } from '../ui/Dialog'
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

  // ── ResizeObserver-based auto-scroll (like OpenCode) ──────────────────
  // Fires after layout, before paint — keeps bottom locked without jumps.
  // Much more reliable than tracking content changes via reactive effects.
  let resizeObserver: ResizeObserver | undefined
  let userScrolledUp = false

  // Reset when streaming starts
  createEffect(
    on(
      () => isStreaming() || agent.isRunning(),
      (streaming) => {
        if (streaming) {
          userScrolledUp = false
          setShouldAutoScroll(true)
        }
      }
    )
  )

  const setupResizeObserver = () => {
    if (!containerRef) return
    resizeObserver = new ResizeObserver(() => {
      if (!containerRef || !settings().behavior.autoScroll) return
      if (userScrolledUp) return
      if (!shouldAutoScroll()) return
      // Direct assignment (bypasses smooth scroll CSS)
      containerRef.scrollTop = containerRef.scrollHeight
    })
    // Observe the scrollable content (first child) — its resize = content growth
    const content = containerRef.firstElementChild
    if (content) resizeObserver.observe(content)
    // Also observe the container itself (viewport resize)
    resizeObserver.observe(containerRef)
  }

  const handleScroll = () => {
    if (!containerRef) return
    if (scrollRaf !== undefined) return

    scrollRaf = requestAnimationFrame(() => {
      scrollRaf = undefined
      if (!containerRef) return

      const { scrollTop, scrollHeight, clientHeight } = containerRef
      const distanceFromBottom = scrollHeight - scrollTop - clientHeight

      const streaming = isStreaming() || agent.isRunning()

      if (streaming) {
        // During streaming: detect if user scrolled up (away from bottom)
        userScrolledUp = distanceFromBottom > 300
        if (!userScrolledUp) setShouldAutoScroll(true)
      } else {
        const nextAutoScroll = distanceFromBottom < 100
        if (nextAutoScroll !== shouldAutoScroll()) {
          setShouldAutoScroll(nextAutoScroll)
        }
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
      setupResizeObserver()
    }
  })

  onCleanup(() => {
    if (scrollRaf !== undefined) cancelAnimationFrame(scrollRaf)
    containerRef?.removeEventListener('scroll', handleScroll)
    resizeObserver?.disconnect()
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
                      streamingToolCalls={
                        msg.id === lastMessageId() &&
                        msg.role === 'assistant' &&
                        (isStreaming() || agent.isRunning())
                          ? agent.activeToolCalls()
                          : undefined
                      }
                      streamingContent={
                        msg.id === lastMessageId() &&
                        msg.role === 'assistant' &&
                        (isStreaming() || agent.isRunning())
                          ? agent.streamingContent
                          : undefined
                      }
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

            {/* "ava is working on it..." indicator (Goose-style) */}
            <div aria-live="polite" aria-atomic="true">
              <Show when={isStreaming() || agent.isRunning()}>
                <div class="w-full animate-fade-in py-2">
                  <div class="flex items-center gap-2 text-xs text-[var(--text-secondary)]">
                    <div class="flex items-center gap-[5px]">
                      <span class="typing-dot" style={{ 'animation-delay': '0ms' }} />
                      <span class="typing-dot" style={{ 'animation-delay': '160ms' }} />
                      <span class="typing-dot" style={{ 'animation-delay': '320ms' }} />
                    </div>
                    <span class="font-[var(--font-ui-mono)] tracking-wide">
                      {agent.currentThought()
                        ? 'ava is working on it...'
                        : agent.toolActivity().some((t) => t.status === 'running')
                          ? 'ava is working on it...'
                          : 'ava is thinking...'}
                    </span>
                  </div>
                </div>
              </Show>
            </div>
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

      <Dialog
        open={rewindTarget() !== null}
        onOpenChange={(open) => {
          if (!open) setRewindTarget(null)
        }}
        title="Rewind conversation?"
        description="Messages after this point will be removed. Choose whether to also revert file changes."
        size="sm"
        showCloseButton={false}
      >
        <div class="space-y-3">
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
          <button
            type="button"
            onClick={() => setRewindTarget(null)}
            class="w-full text-center text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
          >
            Cancel
          </button>
        </div>
      </Dialog>
    </div>
  )
}
