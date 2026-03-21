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

import { type Component, createEffect, createSignal, For, onCleanup, Show } from 'solid-js'
import { useNotification } from '../../contexts/notification'
import { useAgent } from '../../hooks/useAgent'
import { useChat } from '../../hooks/useChat'
import { useLayout } from '../../stores/layout'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import { ConfirmDialog } from '../ui/ConfirmDialog'
import { Dialog } from '../ui/Dialog'
import { CompactionDivider } from './CompactionDivider'
import { FocusChainBar } from './FocusChainBar'
import { LiveStreamingBlock } from './LiveStreamingBlock'
import { ModelChangeIndicator } from './ModelChangeIndicator'
import { MessageRow } from './message-list/message-row'
import { MessageListEmpty, MessageListLoading, ScrollToBottomButton } from './message-list/sections'
import { useMessageActions } from './message-list/useMessageActions'
import { useMessageData } from './message-list/useMessageData'
import { useMessageScroll } from './message-list/useMessageScroll'
import { SearchBar } from './SearchBar'

// ============================================================================
// Component
// ============================================================================

export const MessageList: Component = () => {
  const { settings } = useSettings()
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
    compactionIndex,
  } = useSession()
  const agent = useAgent()
  const { isStreaming, retryMessage, editAndResend, regenerateResponse } = useChat()
  const { success: notifySuccess } = useNotification()

  // Track which messages have already animated in (persists across <For> re-creations)
  const animatedMessageIds = new Set<string>()

  // ── Computed data ──────────────────────────────────────────────────────
  const data = useMessageData({ messages, checkpoints })

  // ── Scroll management ──────────────────────────────────────────────────
  const scroll = useMessageScroll({
    autoScrollEnabled: () => settings().behavior.autoScroll,
    isStreaming: () => isStreaming() || agent.isRunning(),
    hiddenMessageCount: data.hiddenMessageCount,
    onLoadOlder: data.loadOlderMessages,
  })
  scroll.setup()

  // ── Message actions (delete/rewind/branch) ─────────────────────────────
  const actions = useMessageActions({
    messages,
    lastMessageId: data.lastMessageId,
    rollbackToMessage,
    branchAtMessage,
    revertFilesAfter,
    notifySuccess,
  })

  // ── Streaming linger for smooth transition ─────────────────────────────
  // Keep LiveStreamingBlock visible for a brief moment after the agent finishes
  // so the completed MessageRow can render and paint before we unmount the
  // streaming block. This prevents the visible flash / layout jump.
  const [streamingLinger, setStreamingLinger] = createSignal(false)
  createEffect(() => {
    const active = isStreaming() || agent.isRunning()
    if (active) {
      setStreamingLinger(true)
    } else {
      // Delay unmount by two animation frames + a small buffer so the browser
      // has time to paint the settled MessageRow before hiding the streaming block.
      const id = setTimeout(() => setStreamingLinger(false), 80)
      onCleanup(() => clearTimeout(id))
    }
  })

  const handleSearchHighlight = (matchIds: Set<string>, currentId: string | null) => {
    setSearchMatchIds(matchIds)
    setCurrentSearchId(currentId)
  }

  return (
    <div class="relative flex-1 min-h-0 flex flex-col overflow-hidden">
      <FocusChainBar />

      <Show when={chatSearchOpen()}>
        <SearchBar
          messages={messages()}
          onClose={closeChatSearch}
          onNavigate={scroll.scrollToMessage}
          onHighlightChange={handleSearchHighlight}
        />
      </Show>

      <div
        ref={scroll.setContainerRef}
        class="flex-1 overflow-y-auto px-12 py-7"
        style={{ 'overflow-anchor': 'none', 'will-change': 'scroll-position' }}
      >
        <Show when={isLoadingMessages()}>
          <MessageListLoading />
        </Show>

        <Show when={!isLoadingMessages() && messages().length === 0}>
          <MessageListEmpty />
        </Show>

        <Show when={!isLoadingMessages() && messages().length > 0}>
          <div>
            <Show when={data.hiddenMessageCount() > 0}>
              <div class="mb-2 flex items-center justify-center">
                <button
                  type="button"
                  onClick={data.loadOlderMessages}
                  class="rounded-[var(--radius-sm)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] px-2.5 py-1 text-[10px] text-[var(--text-secondary)] hover:text-[var(--text-primary)]"
                >
                  Load{' '}
                  {Math.min(
                    Math.max(100, Math.min(400, Math.floor(window.innerHeight / 60) * 2)),
                    data.hiddenMessageCount()
                  )}{' '}
                  older messages ({data.hiddenMessageCount()} hidden)
                </button>
              </div>
            </Show>

            <For each={data.visibleMessages()}>
              {(msg) => {
                const msgIndex = () => data.messageIndexById().get(msg.id) ?? -1
                const modelChange = () => data.modelChangeById().get(msg.id)
                const shouldAnimate = !animatedMessageIds.has(msg.id)
                if (shouldAnimate) animatedMessageIds.add(msg.id)

                // Show divider before the first message that arrived after compaction.
                // compactionIndex() === totalMessages at the time of compaction, so
                // the first message whose index equals that value is post-compaction.
                const showDivider = () => compactionIndex() > 0 && msgIndex() === compactionIndex()

                // Messages whose index is below the compaction boundary get dimmed.
                const isPreCompaction = () =>
                  compactionIndex() > 0 && msgIndex() < compactionIndex()

                return (
                  <>
                    <Show when={showDivider()}>
                      <CompactionDivider />
                    </Show>
                    <Show when={modelChange()}>
                      {(change) => <ModelChangeIndicator from={change().from} to={change().to} />}
                    </Show>
                    <div classList={{ 'compaction-pre': isPreCompaction() }}>
                      <MessageRow
                        message={msg}
                        shouldAnimate={shouldAnimate}
                        isEditing={editingMessageId() === msg.id}
                        isRetrying={retryingMessageId() === msg.id}
                        isStreaming={
                          (isStreaming() || agent.isRunning()) &&
                          msg.id === data.lastMessageId() &&
                          msg.role === 'assistant'
                        }
                        isLastMessage={msg.id === data.lastMessageId()}
                        isSearchMatch={searchMatchIds().has(msg.id)}
                        isCurrentSearchMatch={currentSearchId() === msg.id}
                        checkpoint={data.checkpointAtIndex(msgIndex()) ?? undefined}
                        streamingToolCalls={undefined}
                        streamingContent={undefined}
                        onStartEdit={() => startEditing(msg.id)}
                        onCancelEdit={stopEditing}
                        onSaveEdit={(content) => editAndResend(msg.id, content)}
                        onRetry={() => retryMessage(msg.id)}
                        onRegenerate={() => regenerateResponse(msg.id)}
                        onDelete={() => actions.handleDeleteRequest(msg.id)}
                        onBranch={() => actions.handleBranch(msg.id)}
                        onRewind={() => actions.setRewindTarget(msg.id)}
                        onRestoreCheckpoint={rollbackToCheckpoint}
                      />
                    </div>
                  </>
                )
              }}
            </For>

            {/* Live streaming block — shows thinking, tool calls, and content in real-time.
                Uses streamingLinger (80ms delay after agent stops) so the browser can
                paint the settled MessageRow before this unmounts, preventing the flash. */}
            <div
              aria-live="polite"
              aria-atomic="true"
              style={{
                opacity: isStreaming() || agent.isRunning() ? '1' : '0',
                transition: 'opacity 60ms ease-out',
                'pointer-events': isStreaming() || agent.isRunning() ? undefined : 'none',
              }}
            >
              <Show when={streamingLinger()}>
                <LiveStreamingBlock />
              </Show>
            </div>

            {/* Scroll anchor sentinel — overflow-anchor:auto keeps the viewport
                pinned to new content at the bottom during streaming. */}
            <div aria-hidden="true" style={{ 'overflow-anchor': 'auto', height: '1px' }} />
          </div>
        </Show>
      </div>

      <Show when={!scroll.shouldAutoScroll() && messages().length > 0}>
        <ScrollToBottomButton onClick={scroll.scrollToBottom} />
      </Show>

      <ConfirmDialog
        open={actions.deleteTarget() !== null}
        onOpenChange={(open) => {
          if (!open) actions.setDeleteTarget(null)
        }}
        title={actions.deleteTarget()?.isLast ? 'Delete message?' : 'Rollback conversation?'}
        message={
          actions.deleteTarget()?.isLast
            ? 'This message will be permanently deleted.'
            : 'This will delete this message and all messages after it. This cannot be undone.'
        }
        confirmText={actions.deleteTarget()?.isLast ? 'Delete' : 'Rollback'}
        variant="danger"
        onConfirm={actions.handleDeleteConfirm}
      />

      <Dialog
        open={actions.rewindTarget() !== null}
        onOpenChange={(open) => {
          if (!open) actions.setRewindTarget(null)
        }}
        title="Rewind conversation?"
        description="Messages after this point will be removed. Choose whether to also revert file changes."
        size="sm"
        showCloseButton={false}
      >
        <div class="space-y-3">
          <button
            type="button"
            onClick={actions.handleRewindConversationOnly}
            class="w-full px-3 py-2 text-xs font-medium rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] hover:bg-[var(--accent-subtle)] transition-colors text-left"
          >
            Rewind conversation only
          </button>
          <button
            type="button"
            onClick={actions.handleRewindAndRevert}
            class="w-full px-3 py-2 text-xs font-medium rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] hover:bg-[var(--accent-subtle)] transition-colors text-left"
          >
            Rewind and revert files
          </button>
          <button
            type="button"
            onClick={() => actions.setRewindTarget(null)}
            class="w-full text-center text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
          >
            Cancel
          </button>
        </div>
      </Dialog>
    </div>
  )
}
