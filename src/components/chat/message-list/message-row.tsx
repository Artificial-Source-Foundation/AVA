import { Bookmark } from 'lucide-solid'
import { type Accessor, type Component, Show } from 'solid-js'
import type { ThinkingSegment } from '../../../hooks/use-rust-agent'
import type { Message, ToolCall } from '../../../types'
import { MessageBubble } from '../MessageBubble'

interface MessageRowProps {
  message: Message
  shouldAnimate: boolean
  isEditing: boolean
  isRetrying: boolean
  isStreaming: boolean
  isLastMessage: boolean
  isSearchMatch?: boolean
  isCurrentSearchMatch?: boolean
  /** Whether the previous message had a different role (user→assistant or vice versa) */
  isRoleSwitch?: boolean
  checkpoint?: { id: string; description: string }
  streamingToolCalls?: ToolCall[]
  /** Live content signal — avoids store updates during streaming */
  streamingContent?: Accessor<string>
  /** Live thinking segments during streaming */
  streamingThinkingSegments?: ThinkingSegment[]
  onStartEdit: () => void
  onCancelEdit: () => void
  onSaveEdit: (content: string) => Promise<void>
  onRetry: () => void
  onRegenerate: () => void
  onDelete: () => void
  onBranch: () => void
  onRewind: () => void
  onRestoreCheckpoint: (id: string) => void
}

export const MessageRow: Component<MessageRowProps> = (props) => (
  <div
    class="density-py transition-colors duration-200"
    classList={{
      'bg-[var(--accent-subtle)] rounded-[var(--radius-md)]': props.isCurrentSearchMatch,
      'bg-[var(--alpha-white-3)] rounded-[var(--radius-md)]':
        !!props.isSearchMatch && !props.isCurrentSearchMatch,
      'mt-[4px]': props.isRoleSwitch === false,
      'mt-3': !!props.isRoleSwitch,
    }}
    data-message-id={props.message.id}
    style={{
      // Virtual scrolling foundation: browser can skip layout/paint for
      // off-screen messages. Provides ~80% of full virtualization benefit
      // without the complexity of a virtual list library.
      'content-visibility': 'auto',
      // Estimated intrinsic size prevents layout thrash when content becomes
      // visible. 120px covers a typical short message; tall messages expand
      // naturally without clipping because content-visibility only defers
      // rendering — it does not clip to this size.
      'contain-intrinsic-size': 'auto 120px',
    }}
  >
    <MessageBubble
      message={props.message}
      shouldAnimate={props.shouldAnimate}
      isEditing={props.isEditing}
      isRetrying={props.isRetrying}
      isStreaming={props.isStreaming}
      isLastMessage={props.isLastMessage}
      streamingToolCalls={props.streamingToolCalls}
      streamingContent={props.streamingContent}
      streamingThinkingSegments={props.streamingThinkingSegments}
      onStartEdit={props.onStartEdit}
      onCancelEdit={props.onCancelEdit}
      onSaveEdit={props.onSaveEdit}
      onRetry={props.onRetry}
      onRegenerate={props.onRegenerate}
      onCopy={() => {}}
      onDelete={props.onDelete}
      onBranch={props.onBranch}
      onRewind={props.onRewind}
    />
    <Show when={props.checkpoint}>
      {(checkpoint) => (
        <div class="flex items-center gap-2 py-1 text-[var(--text-2xs)] text-[var(--text-muted)]">
          <Bookmark class="w-3 h-3 text-[var(--accent)]" />
          <span>{checkpoint().description}</span>
          <button
            type="button"
            onClick={() => props.onRestoreCheckpoint(checkpoint().id)}
            class="text-[var(--accent)] hover:underline"
          >
            Restore
          </button>
        </div>
      )}
    </Show>
  </div>
)
