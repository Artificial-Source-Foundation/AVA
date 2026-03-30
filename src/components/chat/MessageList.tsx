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
 *
 * When ChatModeContext is present (e.g. HQ Director mode), the default adapter
 * automatically uses the overridden data sources and disables mutation actions
 * when readOnly is set, so the exact same rendering path is shared.
 */

import { type Component, createMemo, createSignal, type JSX, Show } from 'solid-js'
import { useChatMode } from '../../contexts/chat-mode'
import { useNotification } from '../../contexts/notification'
import { useAgent } from '../../hooks/useAgent'
import { useChat } from '../../hooks/useChat'
import { useLayout } from '../../stores/layout'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import type { Message } from '../../types'
import { CompactionDivider } from './CompactionDivider'
import { FocusChainBar } from './FocusChainBar'
import { MessageListShell } from './MessageListShell'
import { ModelChangeIndicator } from './ModelChangeIndicator'
import { MessageListEmpty, MessageListLoading } from './message-list/sections'
import { useMessageActions } from './message-list/useMessageActions'
import { useMessageData } from './message-list/useMessageData'
import { useMessageScroll } from './message-list/useMessageScroll'
import { SearchBar } from './SearchBar'
import { TypingIndicator } from './TypingIndicator'

export interface MessageListAdapter {
  loading: () => boolean
  loadingState?: JSX.Element
  emptyState?: JSX.Element
  messages: () => Message[]
  seenMessageIds?: Set<string>
  shouldAnimateRows?: boolean
  showScrollToBottom?: () => boolean
  onScrollToBottom?: () => void
  searchBar?: JSX.Element
  topContent?: JSX.Element
  beforeRow?: (message: () => Message, index: () => number) => JSX.Element
  getRowProps: Parameters<typeof MessageListShell>[0]['getRowProps']
  bottomContent?: JSX.Element
  deleteDialog?: Parameters<typeof MessageListShell>[0]['deleteDialog']
  rewindDialog?: Parameters<typeof MessageListShell>[0]['rewindDialog']
  containerRef?: (el: HTMLDivElement) => void
  class?: string
  style?: JSX.CSSProperties
  onScroll?: JSX.EventHandler<HTMLDivElement, Event>
  prepend?: JSX.Element
}

// ============================================================================
// No-op helpers for read-only mode
// ============================================================================

// biome-ignore lint/suspicious/noExplicitAny: intentional noop cast
const noop = (() => {}) as (...args: any[]) => any
// biome-ignore lint/suspicious/noExplicitAny: intentional noop cast
const noopAsync = ((..._args: any[]) => Promise.resolve(0 as any)) as (...args: any[]) => any

// ============================================================================
// Component
// ============================================================================

export const MessageList: Component<{ adapter?: MessageListAdapter }> = (props) => {
  const { settings } = useSettings()
  const [searchMatchIds, setSearchMatchIds] = createSignal<Set<string>>(new Set())
  const [currentSearchId, setCurrentSearchId] = createSignal<string | null>(null)
  const { chatSearchOpen, closeChatSearch } = useLayout()

  // ── Chat mode context (director mode overrides) ────────────────────────
  const chatMode = useChatMode()

  const {
    messages: sessionMessages,
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

  // ── Effective data sources (context overrides or session defaults) ──────
  const effectiveMessages = () => chatMode?.messages() ?? sessionMessages()
  const effectiveLoading = () => chatMode?.isLoading() ?? isLoadingMessages()
  const effectiveIsStreaming = () => chatMode?.isStreaming() ?? (isStreaming() || agent.isRunning())
  const effectiveLiveMessageId = () => chatMode?.liveMessageId() ?? agent.liveMessageId()
  const effectiveToolCalls = () => chatMode?.streamingToolCalls() ?? agent.activeToolCalls()
  const effectiveStreamingContent = chatMode?.streamingContent ?? agent.streamingContent
  const effectiveThinkingSegments = () =>
    chatMode?.streamingThinkingSegments() ?? agent.thinkingSegments()
  const effectiveCheckpoints = () => (chatMode?.readOnly ? [] : checkpoints())
  const isReadOnly = chatMode?.readOnly ?? false

  // Track which messages have already animated in (persists across <For> re-creations)
  const animatedMessageIds = new Set<string>()
  const searchMessages = createMemo(() => effectiveMessages())

  // ── Computed data ──────────────────────────────────────────────────────
  const data = useMessageData({
    messages: effectiveMessages,
    checkpoints: effectiveCheckpoints,
  })

  // ── Scroll management ──────────────────────────────────────────────────
  const scroll = useMessageScroll({
    autoScrollEnabled: () => settings().behavior.autoScroll,
    isStreaming: effectiveIsStreaming,
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
    messages: effectiveMessages,
    lastMessageId: data.lastMessageId,
    rollbackToMessage: isReadOnly ? noop : rollbackToMessage,
    branchAtMessage: isReadOnly ? noopAsync : branchAtMessage,
    revertFilesAfter: isReadOnly ? noopAsync : revertFilesAfter,
    notifySuccess,
  })

  const handleSearchHighlight = (matchIds: Set<string>, currentId: string | null) => {
    // Use setTimeout(0) to fully escape SolidJS's reactive tracking scope.
    // SearchBar calls this from a createEffect — if we write signals synchronously
    // (or even via queueMicrotask which runs before the next paint), SolidJS detects
    // a write→read→write cycle through the <For> message rows that read these signals.
    setTimeout(() => {
      setSearchMatchIds(matchIds)
      setCurrentSearchId(currentId)
    }, 0)
  }

  const defaultAdapter = (): MessageListAdapter => ({
    loading: effectiveLoading,
    loadingState: <MessageListLoading />,
    emptyState: <MessageListEmpty />,
    messages: data.visibleMessages,
    seenMessageIds: animatedMessageIds,
    shouldAnimateRows: true,
    showScrollToBottom,
    onScrollToBottom: scroll.scrollToBottom,
    searchBar: (
      <Show when={chatSearchOpen()}>
        <SearchBar
          messages={searchMessages()}
          onClose={closeChatSearch}
          onNavigate={scroll.scrollToMessage}
          onHighlightChange={handleSearchHighlight}
        />
      </Show>
    ),
    topContent: (
      <>
        {/* Mode-specific top content (e.g. HQ status cards) */}
        <Show when={chatMode?.topContent}>{(tc) => <>{tc()()}</>}</Show>

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
      </>
    ),
    beforeRow: (message) => {
      const msg = () => message()
      const msgIndex = () => data.messageIndexById().get(msg().id) ?? -1
      const modelChange = () => data.modelChangeById().get(msg().id)
      const showDivider = () =>
        !isReadOnly &&
        (!!msg().metadata?.contextSummary ||
          (compactionIndex() > 0 && msgIndex() === compactionIndex()))

      return (
        <>
          <Show when={showDivider()}>
            <CompactionDivider />
          </Show>
          <Show when={!isReadOnly && modelChange()}>
            {(change) => <ModelChangeIndicator from={change().from} to={change().to} />}
          </Show>
        </>
      )
    },
    getRowProps: (message, index) => {
      const msg = () => message()
      const msgIndex = () => data.messageIndexById().get(msg().id) ?? -1
      const isRoleSwitch = () => {
        const i = index()
        if (i === 0) return undefined
        const prev = data.visibleMessages()[i - 1]
        return prev ? prev.role !== msg().role : undefined
      }

      const isLive = () => msg().id === effectiveLiveMessageId()

      return {
        extraClass:
          !isReadOnly &&
          (msg().metadata?.contextCompacted ||
            (compactionIndex() > 0 && msgIndex() < compactionIndex()))
            ? 'compaction-pre'
            : undefined,
        readOnly: isReadOnly || undefined,
        isEditing: isReadOnly ? false : editingMessageId() === msg().id,
        isRetrying: isReadOnly ? false : retryingMessageId() === msg().id,
        isRoleSwitch: isRoleSwitch(),
        isStreaming: effectiveIsStreaming() && isLive() && msg().role === 'assistant',
        isLastMessage: msg().id === data.lastMessageId(),
        isSearchMatch: searchMatchIds().has(msg().id),
        isCurrentSearchMatch: currentSearchId() === msg().id,
        checkpoint: isReadOnly ? undefined : (data.checkpointAtIndex(msgIndex()) ?? undefined),
        streamingToolCalls: isLive() ? effectiveToolCalls() : undefined,
        streamingContent: isLive() ? effectiveStreamingContent : undefined,
        streamingThinkingSegments: isLive() ? effectiveThinkingSegments() : undefined,
        onStartEdit: isReadOnly ? noop : () => startEditing(msg().id),
        onCancelEdit: isReadOnly ? noop : stopEditing,
        onSaveEdit: isReadOnly ? noopAsync : (content: string) => editAndResend(msg().id, content),
        onRetry: isReadOnly ? noop : () => retryMessage(msg().id),
        onRegenerate: isReadOnly ? noop : () => regenerateResponse(msg().id),
        onDelete: isReadOnly ? noop : () => actions.handleDeleteRequest(msg().id),
        onBranch: isReadOnly ? noop : () => actions.handleBranch(msg().id),
        onRewind: isReadOnly ? noop : () => actions.setRewindTarget(msg().id),
        onRestoreCheckpoint: isReadOnly ? noop : rollbackToCheckpoint,
      }
    },
    bottomContent: (
      <Show when={effectiveIsStreaming()}>
        <div class="px-7 py-3">
          <TypingIndicator
            label={chatMode?.mode === 'director' ? 'Director is thinking...' : 'AVA is thinking...'}
          />
        </div>
      </Show>
    ),
    deleteDialog: isReadOnly
      ? undefined
      : {
          open: actions.deleteTarget() !== null,
          onOpenChange: (open: boolean) => {
            if (!open) actions.setDeleteTarget(null)
          },
          title: actions.deleteTarget()?.isLast ? 'Delete message?' : 'Rollback conversation?',
          message: actions.deleteTarget()?.isLast
            ? 'This message will be permanently deleted.'
            : 'This will delete this message and all messages after it. This cannot be undone.',
          confirmText: actions.deleteTarget()?.isLast ? 'Delete' : 'Rollback',
          onConfirm: actions.handleDeleteConfirm,
        },
    rewindDialog: isReadOnly
      ? undefined
      : {
          open: actions.rewindTarget() !== null,
          onOpenChange: (open: boolean) => {
            if (!open) actions.setRewindTarget(null)
          },
          onConversationOnly: actions.handleRewindConversationOnly,
          onRevertFiles: actions.handleRewindAndRevert,
          onCancel: () => actions.setRewindTarget(null),
        },
    containerRef: scroll.setContainerRef,
    class: 'px-6 pt-8 pb-6',
    style: { 'overflow-anchor': 'none' },
    prepend: isReadOnly ? undefined : <FocusChainBar />,
  })

  // Compute adapter ONCE — not as a reactive getter that re-runs defaultAdapter()
  // on every signal change. Re-invoking defaultAdapter() creates new JSX elements
  // (e.g. SearchBar), which re-mount, fire effects, and cause infinite loops.
  const adapter = props.adapter ?? defaultAdapter()

  return (
    <>
      {adapter.prepend}
      <MessageListShell
        containerRef={adapter.containerRef}
        class={adapter.class}
        style={adapter.style}
        onScroll={adapter.onScroll}
        loading={adapter.loading}
        loadingState={adapter.loadingState}
        emptyState={adapter.emptyState}
        messages={adapter.messages}
        seenMessageIds={adapter.seenMessageIds}
        shouldAnimateRows={adapter.shouldAnimateRows}
        showScrollToBottom={adapter.showScrollToBottom}
        onScrollToBottom={adapter.onScrollToBottom}
        searchBar={adapter.searchBar}
        topContent={adapter.topContent}
        beforeRow={adapter.beforeRow}
        getRowProps={adapter.getRowProps}
        bottomContent={adapter.bottomContent}
        deleteDialog={adapter.deleteDialog}
        rewindDialog={adapter.rewindDialog}
      />
    </>
  )
}
