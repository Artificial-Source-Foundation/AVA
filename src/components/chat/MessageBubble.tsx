import { type Accessor, type Component, Show } from 'solid-js'
import type { ThinkingSegment } from '../../hooks/use-rust-agent'
import type { Message, ToolCall } from '../../types'
import { AssistantMessageBubble } from './AssistantMessageBubble'
import { EditForm } from './EditForm'
import { UserMessageBubble } from './UserMessageBubble'

interface MessageBubbleProps {
  message: Message
  readOnly?: boolean
  isEditing: boolean
  isRetrying: boolean
  isStreaming: boolean
  isLastMessage: boolean
  shouldAnimate: boolean
  /** Live tool calls from useAgent signal (avoids store re-renders during streaming) */
  streamingToolCalls?: ToolCall[]
  /** Live content signal — avoids store updates during streaming */
  streamingContent?: Accessor<string>
  /** Live thinking segments during streaming — enables real-time thinking display */
  streamingThinkingSegments?: ThinkingSegment[]
  onStartEdit: () => void
  onCancelEdit: () => void
  onSaveEdit: (content: string) => Promise<void>
  onRetry: () => void
  onRegenerate: () => void
  onCopy: () => void
  onDelete: () => void
  onBranch: () => void
  onRewind: () => void
}

export const MessageBubble: Component<MessageBubbleProps> = (props) => {
  const isUser = () => props.message.role === 'user'
  const shouldAnimateIn = () => props.shouldAnimate && !props.isEditing

  return (
    <div
      class={`chat-message-shell flex ${isUser() ? 'justify-end' : 'justify-start'} ${shouldAnimateIn() ? 'animate-message-in' : ''}`}
    >
      <Show
        when={!isUser()}
        fallback={
          <UserMessageBubble
            message={props.message}
            readOnly={props.readOnly}
            isEditing={props.isEditing}
            isStreaming={props.isStreaming}
            isLastMessage={props.isLastMessage}
            onStartEdit={props.onStartEdit}
            onCancelEdit={props.onCancelEdit}
            onSaveEdit={props.onSaveEdit}
            onRegenerate={props.onRegenerate}
            onCopy={props.onCopy}
            onDelete={props.onDelete}
            onBranch={props.onBranch}
            onRewind={props.onRewind}
          />
        }
      >
        <Show
          when={!props.isEditing}
          fallback={
            <EditForm
              initialContent={props.message.content}
              onSave={props.onSaveEdit}
              onCancel={props.onCancelEdit}
            />
          }
        >
          <AssistantMessageBubble
            message={props.message}
            readOnly={props.readOnly}
            isStreaming={props.isStreaming}
            isLastMessage={props.isLastMessage}
            isRetrying={props.isRetrying}
            streamingToolCalls={props.streamingToolCalls}
            streamingContent={props.streamingContent}
            streamingThinkingSegments={props.streamingThinkingSegments}
            onStartEdit={props.onStartEdit}
            onRegenerate={props.onRegenerate}
            onCopy={props.onCopy}
            onDelete={props.onDelete}
            onBranch={props.onBranch}
            onRewind={props.onRewind}
            onRetry={props.onRetry}
          />
        </Show>
      </Show>
    </div>
  )
}
