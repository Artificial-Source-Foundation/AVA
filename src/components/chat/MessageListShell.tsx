import { type Accessor, type Component, type JSX, Show } from 'solid-js'
import type { Message } from '../../types'
import { ConfirmDialog } from '../ui/ConfirmDialog'
import { Dialog } from '../ui/Dialog'
import { ChatMessageStream } from './ChatMessageStream'

export interface MessageListShellRewindState {
  open: boolean
  onOpenChange: (open: boolean) => void
  onConversationOnly: () => void
  onRevertFiles: () => void
  onCancel: () => void
}

export interface MessageListShellDeleteState {
  open: boolean
  onOpenChange: (open: boolean) => void
  title: string
  message: string
  confirmText: string
  onConfirm: () => void
}

export interface MessageListShellProps {
  loading: Accessor<boolean>
  loadingState?: JSX.Element
  emptyState?: JSX.Element
  messages: Accessor<Message[]>
  seenMessageIds?: Set<string>
  shouldAnimateRows?: boolean
  showScrollToBottom?: Accessor<boolean>
  onScrollToBottom?: () => void
  topContent?: JSX.Element
  beforeRow?: (message: Accessor<Message>, index: Accessor<number>) => JSX.Element
  getRowProps: Parameters<typeof ChatMessageStream>[0]['getRowProps']
  bottomContent?: JSX.Element
  class?: string
  style?: JSX.CSSProperties
  containerRef?: (el: HTMLDivElement) => void
  onScroll?: JSX.EventHandler<HTMLDivElement, Event>
  searchBar?: JSX.Element
  rewindDialog?: MessageListShellRewindState
  deleteDialog?: MessageListShellDeleteState
}

export const MessageListShell: Component<MessageListShellProps> = (props) => {
  return (
    <div class="relative flex-1 min-h-0 flex flex-col overflow-hidden">
      <Show when={props.searchBar}>
        <div class="absolute top-3 right-4 z-10">{props.searchBar}</div>
      </Show>

      <ChatMessageStream
        containerRef={props.containerRef}
        class={props.class ?? 'px-6 pt-8 pb-6'}
        style={props.style}
        loading={props.loading}
        loadingState={props.loadingState}
        emptyState={props.emptyState}
        messages={props.messages}
        seenMessageIds={props.seenMessageIds}
        shouldAnimateRows={props.shouldAnimateRows}
        showScrollToBottom={props.showScrollToBottom}
        onScrollToBottom={props.onScrollToBottom}
        topContent={props.topContent}
        beforeRow={props.beforeRow}
        getRowProps={props.getRowProps}
        bottomContent={props.bottomContent}
        onScroll={props.onScroll}
      />

      <Show when={props.deleteDialog}>
        {(dialog) => (
          <ConfirmDialog
            open={dialog().open}
            onOpenChange={dialog().onOpenChange}
            title={dialog().title}
            message={dialog().message}
            confirmText={dialog().confirmText}
            variant="danger"
            onConfirm={dialog().onConfirm}
          />
        )}
      </Show>

      <Show when={props.rewindDialog}>
        {(dialog) => (
          <Dialog
            open={dialog().open}
            onOpenChange={dialog().onOpenChange}
            title="Rewind conversation?"
            description="Messages after this point will be removed. Choose whether to also revert file changes."
            size="sm"
            showCloseButton={false}
          >
            <div class="space-y-3">
              <button
                type="button"
                onClick={dialog().onConversationOnly}
                class="w-full px-3 py-2 text-xs font-medium rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] hover:bg-[var(--accent-subtle)] transition-colors text-left"
              >
                Rewind conversation only
              </button>
              <button
                type="button"
                onClick={dialog().onRevertFiles}
                class="w-full px-3 py-2 text-xs font-medium rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] hover:bg-[var(--accent-subtle)] transition-colors text-left"
              >
                Rewind and revert files
              </button>
              <button
                type="button"
                onClick={dialog().onCancel}
                class="w-full text-center text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
              >
                Cancel
              </button>
            </div>
          </Dialog>
        )}
      </Show>
    </div>
  )
}
