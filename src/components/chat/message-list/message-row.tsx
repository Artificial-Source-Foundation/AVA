import { Bookmark, Copy, GitBranch, Pencil, RefreshCw, Trash2 } from 'lucide-solid'
import { type Accessor, type Component, createSignal, Show } from 'solid-js'
import type { ThinkingSegment } from '../../../hooks/use-rust-agent'
import type { Message, ToolCall } from '../../../types'
import { ContextMenu, type ContextMenuItem } from '../../ui/ContextMenu'
import { MessageBubble } from '../MessageBubble'

export interface MessageRowProps {
  message: Message
  extraClass?: string
  readOnly?: boolean
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
  /** Whether branching at a message is supported in the current environment */
  canBranch?: boolean
}

export const MessageRow: Component<MessageRowProps> = (props) => {
  const [ctxMenu, setCtxMenu] = createSignal<{ x: number; y: number } | null>(null)
  const ariaRoleLabel = (): string => {
    switch (props.message.role) {
      case 'assistant':
        return 'Assistant'
      case 'system':
        return 'System'
      case 'tool':
        return 'Tool'
      default:
        return 'User'
    }
  }

  const handleContextMenu = (e: MouseEvent): void => {
    if (props.readOnly) return
    e.preventDefault()
    e.stopPropagation()
    setCtxMenu({ x: e.clientX, y: e.clientY })
  }

  const buildContextMenuItems = (): ContextMenuItem[] => {
    const items: ContextMenuItem[] = [
      {
        label: 'Copy',
        icon: Copy,
        kbd: 'Ctrl+C',
        action: () => {
          void navigator.clipboard.writeText(props.message.content)
        },
      },
    ]

    if (props.readOnly) return items

    // Edit — user messages only
    if (props.message.role === 'user') {
      items.push({
        label: 'Edit',
        icon: Pencil,
        action: () => props.onStartEdit(),
        disabled: props.isStreaming,
      })
    }

    if (props.canBranch !== false) {
      items.push({
        label: 'Branch from here',
        icon: GitBranch,
        action: () => props.onBranch(),
        disabled: props.isStreaming,
      })
    }

    // Retry — assistant messages with errors, or regenerate for assistant without errors
    if (props.message.role === 'assistant') {
      items.push({
        label: 'Retry',
        icon: RefreshCw,
        action: () => (props.message.error ? props.onRetry() : props.onRegenerate()),
        disabled: props.isStreaming,
      })
    }

    items.push({ label: '', action: () => {}, separator: true })

    items.push({
      label: 'Delete message',
      icon: Trash2,
      danger: true,
      action: () => props.onDelete(),
      disabled: props.isStreaming,
    })

    return items
  }

  return (
    <article
      class={`${props.extraClass ?? ''}`}
      classList={{
        'bg-[var(--accent-subtle)] rounded-[var(--radius-md)]': props.isCurrentSearchMatch,
        'bg-[var(--alpha-white-3)] rounded-[var(--radius-md)]':
          !!props.isSearchMatch && !props.isCurrentSearchMatch,
        'mt-6': props.isRoleSwitch !== undefined,
      }}
      data-message-id={props.message.id}
      onContextMenu={handleContextMenu}
      aria-label={`${ariaRoleLabel()} message`}
      aria-current={props.isCurrentSearchMatch ? 'true' : undefined}
      style={{
        // Virtual scrolling foundation: browser skips layout/paint for off-screen
        // messages. The `auto` keyword in contain-intrinsic-size caches last-known
        // height so returning to a previously rendered row is instant.
        // Note: content-visibility: auto implies contain: layout style paint which
        // creates a stacking context; we keep it for perf but add z-index isolation
        // so message action toolbars remain clickable above the scroll container.
        'content-visibility': 'auto',
        'contain-intrinsic-size': 'auto 120px',
        position: 'relative',
      }}
    >
      <MessageBubble
        message={props.message}
        readOnly={props.readOnly}
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
        canBranch={props.canBranch}
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

      {/* Right-click context menu */}
      <Show when={ctxMenu()}>
        <ContextMenu
          x={ctxMenu()!.x}
          y={ctxMenu()!.y}
          items={buildContextMenuItems()}
          onClose={() => setCtxMenu(null)}
        />
      </Show>
    </article>
  )
}
