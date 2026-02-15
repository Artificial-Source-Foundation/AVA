import { Bookmark } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import type { Message } from '../../../types'
import { MessageBubble } from '../MessageBubble'

interface MessageRowProps {
  message: Message
  isEditing: boolean
  isRetrying: boolean
  isStreaming: boolean
  isLastMessage: boolean
  checkpoint?: { id: string; description: string }
  onStartEdit: () => void
  onCancelEdit: () => void
  onSaveEdit: (content: string) => Promise<void>
  onRetry: () => void
  onRegenerate: () => void
  onDelete: () => void
  onRestoreCheckpoint: (id: string) => void
}

export const MessageRow: Component<MessageRowProps> = (props) => (
  <div class="density-py">
    <MessageBubble
      message={props.message}
      isEditing={props.isEditing}
      isRetrying={props.isRetrying}
      isStreaming={props.isStreaming}
      isLastMessage={props.isLastMessage}
      onStartEdit={props.onStartEdit}
      onCancelEdit={props.onCancelEdit}
      onSaveEdit={props.onSaveEdit}
      onRetry={props.onRetry}
      onRegenerate={props.onRegenerate}
      onCopy={() => {}}
      onDelete={props.onDelete}
    />
    <Show when={props.checkpoint}>
      {(checkpoint) => (
        <div class="flex items-center gap-2 py-1 text-[10px] text-[var(--text-muted)]">
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
