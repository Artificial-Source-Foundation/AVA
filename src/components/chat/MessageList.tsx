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

import { type Component, createMemo, createSignal, Show } from 'solid-js'
import { useNotification } from '../../contexts/notification'
import { useAgent } from '../../hooks/useAgent'
import { useChat } from '../../hooks/useChat'
import { useLayout } from '../../stores/layout'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import { ConfirmDialog } from '../ui/ConfirmDialog'
import { Dialog } from '../ui/Dialog'
import { ChatMessageStream } from './ChatMessageStream'
import { CompactionDivider } from './CompactionDivider'
import { FocusChainBar } from './FocusChainBar'
import { ModelChangeIndicator } from './ModelChangeIndicator'
import { MessageListEmpty, MessageListLoading } from './message-list/sections'
import { useMessageActions } from './message-list/useMessageActions'
import { useMessageData } from './message-list/useMessageData'
import { useMessageScroll } from './message-list/useMessageScroll'
import { SearchBar } from './SearchBar'
import { TypingIndicator } from './TypingIndicator'

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
  const searchMessages = createMemo(() => messages())

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
  const loadOlderCount = createMemo(() => {
    if (typeof window === 'undefined') return Math.min(100, data.hiddenMessageCount())
    const computed = Math.max(100, Math.min(400, Math.floor(window.innerHeight / 60) * 2))
    return Math.min(computed, data.hiddenMessageCount())
  })
  const showScrollToBottom = createMemo(
    () => !scroll.shouldAutoScroll() && searchMessages().length > 0
  )

  // ── Message actions (delete/rewind/branch) ─────────────────────────────
  const actions = useMessageActions({
    messages,
    lastMessageId: data.lastMessageId,
    rollbackToMessage,
    branchAtMessage,
    revertFilesAfter,
    notifySuccess,
  })

  const handleSearchHighlight = (matchIds: Set<string>, currentId: string | null) => {
    setSearchMatchIds(matchIds)
    setCurrentSearchId(currentId)
  }

  return (
    <div class="relative flex-1 min-h-0 flex flex-col overflow-hidden">
      <FocusChainBar />

      <Show when={chatSearchOpen()}>
        <div class="absolute top-3 right-4 z-10">
          <SearchBar
            messages={searchMessages()}
            onClose={closeChatSearch}
            onNavigate={scroll.scrollToMessage}
            onHighlightChange={handleSearchHighlight}
          />
        </div>
      </Show>

      <ChatMessageStream
        containerRef={scroll.setContainerRef}
        class="px-6 pt-8 pb-6"
        style={{
          'overflow-anchor': 'none',
        }}
        loading={isLoadingMessages}
        loadingState={<MessageListLoading />}
        emptyState={<MessageListEmpty />}
        messages={data.visibleMessages}
        seenMessageIds={animatedMessageIds}
        shouldAnimateRows={true}
        showScrollToBottom={showScrollToBottom}
        onScrollToBottom={scroll.scrollToBottom}
        topContent={
          <Show when={data.hiddenMessageCount() > 0}>
            <div class="mb-2 flex items-center justify-center">
              <button
                type="button"
                onClick={data.loadOlderMessages}
                class="rounded-[var(--radius-sm)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] px-2.5 py-1 text-[var(--text-2xs)] text-[var(--text-secondary)] transition-colors hover:border-[var(--border-default)] hover:text-[var(--text-primary)]"
                aria-label={`Load ${loadOlderCount()} older messages`}
              >
                Load {loadOlderCount()} older messages ({data.hiddenMessageCount()} hidden)
              </button>
            </div>
          </Show>
        }
        beforeRow={(message) => {
          const msg = () => message()
          const msgIndex = () => data.messageIndexById().get(msg().id) ?? -1
          const modelChange = () => data.modelChangeById().get(msg().id)
          const showDivider = () => compactionIndex() > 0 && msgIndex() === compactionIndex()

          return (
            <>
              <Show when={showDivider()}>
                <CompactionDivider />
              </Show>
              <Show when={modelChange()}>
                {(change) => <ModelChangeIndicator from={change().from} to={change().to} />}
              </Show>
            </>
          )
        }}
        getRowProps={(message, index) => {
          const msg = () => message()
          const msgIndex = () => data.messageIndexById().get(msg().id) ?? -1
          const isRoleSwitch = () => {
            const i = index()
            if (i === 0) return undefined
            const prev = data.visibleMessages()[i - 1]
            return prev ? prev.role !== msg().role : undefined
          }

          return {
            extraClass:
              compactionIndex() > 0 && msgIndex() < compactionIndex()
                ? 'compaction-pre'
                : undefined,
            isEditing: editingMessageId() === msg().id,
            isRetrying: retryingMessageId() === msg().id,
            isRoleSwitch: isRoleSwitch(),
            isStreaming:
              (isStreaming() || agent.isRunning()) &&
              msg().id === agent.liveMessageId() &&
              msg().role === 'assistant',
            isLastMessage: msg().id === data.lastMessageId(),
            isSearchMatch: searchMatchIds().has(msg().id),
            isCurrentSearchMatch: currentSearchId() === msg().id,
            checkpoint: data.checkpointAtIndex(msgIndex()) ?? undefined,
            streamingToolCalls:
              msg().id === agent.liveMessageId() ? agent.activeToolCalls() : undefined,
            streamingContent:
              msg().id === agent.liveMessageId() ? agent.streamingContent : undefined,
            streamingThinkingSegments:
              msg().id === agent.liveMessageId() ? agent.thinkingSegments() : undefined,
            onStartEdit: () => startEditing(msg().id),
            onCancelEdit: stopEditing,
            onSaveEdit: (content) => editAndResend(msg().id, content),
            onRetry: () => retryMessage(msg().id),
            onRegenerate: () => regenerateResponse(msg().id),
            onDelete: () => actions.handleDeleteRequest(msg().id),
            onBranch: () => actions.handleBranch(msg().id),
            onRewind: () => actions.setRewindTarget(msg().id),
            onRestoreCheckpoint: rollbackToCheckpoint,
          }
        }}
        bottomContent={
          <Show when={agent.isRunning()}>
            <div class="px-7 py-3">
              <TypingIndicator label="AVA is thinking..." />
            </div>
          </Show>
        }
      />

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
