import { type Component, For, Show } from 'solid-js'
import type { Message } from '../../types'
import { EditForm } from './EditForm'
import { MarkdownContent } from './MarkdownContent'
import { MessageActions } from './MessageActions'

interface UserMessageBubbleProps {
  message: Message
  isEditing: boolean
  isStreaming: boolean
  isLastMessage: boolean
  onStartEdit: () => void
  onCancelEdit: () => void
  onSaveEdit: (content: string) => Promise<void>
  onRegenerate: () => void
  onCopy: () => void
  onDelete: () => void
  onBranch: () => void
  onRewind: () => void
}

function formatTimestamp(msg: Message): string {
  const date = msg.createdAt ? new Date(msg.createdAt) : new Date()
  const h = date.getHours()
  const m = date.getMinutes().toString().padStart(2, '0')
  const ampm = h >= 12 ? 'PM' : 'AM'
  const h12 = h % 12 || 12
  return `${h12}:${m} ${ampm}`
}

const ImagesBlock: Component<{ message: Message }> = (props) => (
  <Show when={(props.message.metadata?.images as Array<{ data: string; mimeType: string }>) ?? []}>
    {(images) => (
      <Show when={images().length > 0}>
        <div class="flex gap-2 mb-2 flex-wrap">
          <For each={images()}>
            {(img) => (
              <img
                src={`data:${img.mimeType};base64,${img.data}`}
                alt="Attached"
                class="max-w-[200px] max-h-[200px] rounded object-contain"
              />
            )}
          </For>
        </div>
      </Show>
    )}
  </Show>
)

export const UserMessageBubble: Component<UserMessageBubbleProps> = (props) => {
  return (
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
      <div class="relative group" style={{ 'max-width': '70%' }}>
        <div class="flex flex-col">
          <div class="chat-user-bubble rounded-[16px] rounded-br-[4px] border border-[var(--border-subtle)] bg-[var(--chat-user-bg)] px-4 py-3 text-sm leading-relaxed text-[var(--chat-user-text)]">
            <ImagesBlock message={props.message} />
            <Show when={props.message.content}>
              <MarkdownContent
                content={props.message.content}
                messageRole="user"
                isStreaming={false}
              />
            </Show>
          </div>
          {/* Tier badge for mid-stream messages */}
          <Show when={props.message.metadata?.tier as string | undefined}>
            {(tier) => (
              <div class="flex justify-end mt-0.5">
                <span
                  class="px-1.5 py-0.5 rounded text-[9px] font-bold uppercase tracking-wider"
                  classList={{
                    'bg-[var(--blue-3)] text-[var(--blue-9)]':
                      tier() === 'queued' || tier() === 'follow-up',
                    'bg-[var(--amber-3)] text-[var(--amber-9)]':
                      tier() === 'interrupt' || tier() === 'steering',
                    'bg-[var(--violet-3)] text-[var(--violet-9)]': tier() === 'post-complete',
                  }}
                >
                  {tier() === 'queued' || tier() === 'follow-up'
                    ? 'QUEUED'
                    : tier() === 'interrupt' || tier() === 'steering'
                      ? 'INTERRUPT'
                      : 'QUEUED'}
                </span>
              </div>
            )}
          </Show>
          {/* Timestamp line */}
          <div class="relative h-[20px] flex justify-end">
            <Show when={!props.isStreaming}>
              <div class="font-[var(--font-ui-mono)] text-[11px] tracking-wider text-[var(--text-muted)] pt-1.5 opacity-0 transition-opacity duration-200 group-hover:opacity-100 tabular-nums">
                {formatTimestamp(props.message)}
              </div>
            </Show>
            <Show when={props.message.content && !props.isStreaming}>
              <div class="absolute left-0 top-0 pt-1">
                <MessageActions
                  message={props.message}
                  isLastMessage={props.isLastMessage}
                  onEdit={props.onStartEdit}
                  onRegenerate={props.onRegenerate}
                  onCopy={props.onCopy}
                  onDelete={props.onDelete}
                  onBranch={props.onBranch}
                  onRewind={props.onRewind}
                  isLoading={props.isStreaming}
                />
              </div>
            </Show>
          </div>
        </div>
      </div>
    </Show>
  )
}
