import {
  type Accessor,
  type Component,
  createMemo,
  For,
  type JSX,
  onMount,
  Show,
  untrack,
} from 'solid-js'
import type { Message } from '../../types'
import { MessageRow, type MessageRowProps } from './message-list/message-row'
import { MessageListLoading, ScrollToBottomButton } from './message-list/sections'

interface ChatMessageStreamProps {
  messages: Accessor<Message[]>
  getRowProps: (
    message: Accessor<Message>,
    index: Accessor<number>
  ) => Omit<MessageRowProps, 'message' | 'shouldAnimate'>
  beforeRow?: (message: Accessor<Message>, index: Accessor<number>) => JSX.Element
  seenMessageIds?: Set<string>
  shouldAnimateRows?: boolean
  containerRef?: (el: HTMLDivElement) => void
  loading?: Accessor<boolean>
  loadingState?: JSX.Element
  emptyState?: JSX.Element
  topContent?: JSX.Element
  bottomContent?: JSX.Element
  showScrollToBottom?: Accessor<boolean>
  onScrollToBottom?: () => void
  class?: string
  style?: JSX.CSSProperties
  onScroll?: JSX.EventHandler<HTMLDivElement, Event>
}

const ChatMessageStreamRow: Component<{
  message: Message
  index: Accessor<number>
  getRowProps: ChatMessageStreamProps['getRowProps']
  beforeRow?: ChatMessageStreamProps['beforeRow']
  seenMessageIds?: Set<string>
  shouldAnimateRows?: boolean
}> = (props) => {
  const messageAccessor = () => props.message
  const shouldAnimate = untrack(() =>
    props.shouldAnimateRows ? !props.seenMessageIds?.has(props.message.id) : false
  )

  onMount(() => {
    if (props.shouldAnimateRows) props.seenMessageIds?.add(props.message.id)
  })

  return (
    <>
      {props.beforeRow?.(messageAccessor, props.index)}
      <MessageRow
        {...props.getRowProps(messageAccessor, props.index)}
        message={props.message}
        shouldAnimate={shouldAnimate}
      />
    </>
  )
}

export const ChatMessageStream: Component<ChatMessageStreamProps> = (props) => {
  const classes = () =>
    ['chat-scroll-viewport flex-1 overflow-y-auto', props.class].filter(Boolean).join(' ')
  const handleScroll: JSX.EventHandler<HTMLDivElement, Event> = (event) => props.onScroll?.(event)
  const messageCount = createMemo(() => props.messages().length)

  return (
    <div class="relative flex-1 min-h-0" style={{ display: 'flex', 'flex-direction': 'column' }}>
      <div
        ref={props.containerRef}
        class={classes()}
        style={{ ...props.style, 'min-height': '0', flex: '1 1 0' }}
        onScroll={handleScroll}
        role="log"
        aria-live="polite"
        aria-relevant="additions text"
        aria-label="Conversation messages"
      >
        <Show when={props.topContent}>{props.topContent}</Show>

        <Show when={props.loading?.()}>{props.loadingState ?? <MessageListLoading />}</Show>

        <Show when={!props.loading?.() && messageCount() === 0}>{props.emptyState}</Show>

        <Show when={!props.loading?.() && messageCount() > 0}>
          <div class="max-w-[min(94%,1400px)] mx-auto w-full">
            <For each={props.messages()}>
              {(message, index) => (
                <ChatMessageStreamRow
                  message={message}
                  index={index}
                  getRowProps={props.getRowProps}
                  beforeRow={props.beforeRow}
                  seenMessageIds={props.seenMessageIds}
                  shouldAnimateRows={props.shouldAnimateRows}
                />
              )}
            </For>
            <Show when={props.bottomContent}>{props.bottomContent}</Show>
            <div aria-hidden="true" style={{ 'overflow-anchor': 'auto', height: '1px' }} />
          </div>
        </Show>
      </div>

      <Show when={props.showScrollToBottom?.() && props.onScrollToBottom}>
        <ScrollToBottomButton onClick={() => props.onScrollToBottom?.()} />
      </Show>
    </div>
  )
}
