import { type Accessor, type Component, createMemo, For, Match, Show, Switch } from 'solid-js'
import { formatCost } from '../../lib/cost'
import { formatMs } from '../../lib/format-time'
import type { Message, ToolCall } from '../../types'
import { EditForm } from './EditForm'
import { MarkdownContent } from './MarkdownContent'
import { MessageActions } from './MessageActions'
import { CommandOutputRow, DiffRow, ErrorRow, ThinkingRow, ToolCallRow } from './message-rows'
import { type MessageSegment, segmentMessage } from './message-segments'
import { ToolPreview } from './ToolPreview'
import { ToolCallErrorBoundary } from './tool-call-error-boundary'

interface MessageBubbleProps {
  message: Message
  isEditing: boolean
  isRetrying: boolean
  isStreaming: boolean
  isLastMessage: boolean
  shouldAnimate: boolean
  /** Live tool calls from useAgent signal (avoids store re-renders during streaming) */
  streamingToolCalls?: ToolCall[]
  /** Live content signal — avoids store updates during streaming */
  streamingContent?: Accessor<string>
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

function formatModelName(modelId: string): string {
  let name = modelId.replace(/-\d{8}$/, '')
  const slash = name.lastIndexOf('/')
  if (slash >= 0) name = name.slice(slash + 1)
  return name
}

function formatTimestamp(msg: Message): string {
  const date = msg.createdAt ? new Date(msg.createdAt) : new Date()
  const h = date.getHours()
  const m = date.getMinutes().toString().padStart(2, '0')
  const ampm = h >= 12 ? 'PM' : 'AM'
  const h12 = h % 12 || 12
  return `${h12}:${m} ${ampm}`
}

interface ToolSegmentProps {
  toolCalls: ToolCall[]
  isStreaming: boolean
}

const ToolSegmentDispatch: Component<ToolSegmentProps> = (props) => {
  return (
    <div class="flex flex-col gap-1.5 my-1">
      <For each={props.toolCalls}>
        {(tc) => (
          <ToolCallErrorBoundary>
            <Switch fallback={<ToolCallRow toolCall={tc} />}>
              <Match when={tc.name === 'bash' && tc}>
                {(call) => <CommandOutputRow toolCall={call()} />}
              </Match>
              <Match when={tc.diff && tc.name !== 'bash' && tc}>
                {(call) => <DiffRow toolCall={call()} />}
              </Match>
            </Switch>
          </ToolCallErrorBoundary>
        )}
      </For>
    </div>
  )
}

export const MessageBubble: Component<MessageBubbleProps> = (props) => {
  const isUser = () => props.message.role === 'user'
  const shouldAnimateIn = () => props.shouldAnimate && !props.isEditing

  const isActiveStreaming = () => props.isStreaming && props.isLastMessage && !isUser()

  const displayContent = () => {
    if (isActiveStreaming() && props.streamingContent) {
      return props.streamingContent()
    }
    return props.message.content
  }

  const effectiveToolCalls = () => {
    if (isActiveStreaming() && props.streamingToolCalls?.length) {
      return props.streamingToolCalls
    }
    return props.message.toolCalls
  }
  const hasToolCalls = () => !isUser() && (effectiveToolCalls()?.length ?? 0) > 0

  const segments = createMemo((): MessageSegment[] | null => {
    if (isUser()) return null
    if (isActiveStreaming()) return null
    if (!hasToolCalls() && !props.message.content) return null
    return segmentMessage(props.message.content, effectiveToolCalls())
  })

  const ImagesBlock = () => (
    <Show
      when={(props.message.metadata?.images as Array<{ data: string; mimeType: string }>) ?? []}
    >
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

  const TimestampLine = (lineProps: { align?: 'left' | 'right' }) => {
    const align = lineProps.align ?? (isUser() ? 'right' : 'left')
    return (
      <div class={`relative h-[20px] flex ${align === 'right' ? 'justify-end' : 'justify-start'}`}>
        <Show when={!props.isStreaming}>
          <div
            class={`font-[var(--font-ui-mono)] text-[10px] tracking-wide text-[var(--gray-6)] pt-1 transition-all duration-200 group-hover:-translate-y-3 group-hover:opacity-0 tabular-nums`}
          >
            {formatTimestamp(props.message)}
            <Show when={!isUser() && props.message.model}>
              {' '}
              &middot; {formatModelName(props.message.model!)}
            </Show>
            <Show when={!isUser() && props.message.tokensUsed}>
              {' '}
              &middot; {props.message.tokensUsed?.toLocaleString()} tokens
            </Show>
            <Show when={!isUser() && props.message.costUSD}>
              {' '}
              &middot; {formatCost(props.message.costUSD!)}
            </Show>
            <Show when={!isUser() && (props.message.metadata?.elapsedMs as number | undefined)}>
              {' '}
              &middot; {formatMs(props.message.metadata!.elapsedMs as number)}
            </Show>
            <Show when={!isUser() && props.message.metadata?.mode}>
              {' '}
              &middot; {props.message.metadata!.mode as string}
            </Show>
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
    )
  }

  return (
    <div
      class={`flex ${isUser() ? 'justify-end' : 'justify-start'} ${shouldAnimateIn() ? 'animate-message-in' : ''}`}
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
        <Show when={isUser()}>
          <div class="relative group max-w-[85%]">
            <div class="flex flex-col">
              <div class="bg-[var(--chat-user-bg)] text-[var(--chat-user-text)] rounded-[var(--radius-2xl)] rounded-br-[var(--radius-sm)] py-2.5 px-4 shadow-[var(--shadow-sm)]">
                <ImagesBlock />
                <Show when={props.message.content}>
                  <MarkdownContent
                    content={props.message.content}
                    messageRole="user"
                    isStreaming={false}
                  />
                </Show>
              </div>
              <TimestampLine align="right" />
            </div>
          </div>
        </Show>

        <Show when={!isUser()}>
          <div class="relative group w-[90%] min-w-0">
            <div class="flex flex-col w-full min-w-0">
              <Show when={props.message.metadata?.thinking as string}>
                <ThinkingRow
                  thinking={props.message.metadata!.thinking as string}
                  isStreaming={props.isStreaming}
                />
              </Show>

              <Show when={isActiveStreaming()}>
                <Show when={hasToolCalls()}>
                  <ToolCallErrorBoundary>
                    <ToolSegmentDispatch toolCalls={effectiveToolCalls()!} isStreaming={true} />
                  </ToolCallErrorBoundary>
                </Show>
                <ToolPreview toolCalls={effectiveToolCalls()} isStreaming={true} />
                <Show when={displayContent()}>
                  <div class="w-full">
                    <MarkdownContent
                      content={displayContent()}
                      messageRole="assistant"
                      isStreaming={true}
                    />
                  </div>
                </Show>
              </Show>

              <Show when={!isActiveStreaming()}>
                <Show when={segments()}>
                  {(segs) => (
                    <For each={segs()}>
                      {(seg) => (
                        <Switch>
                          <Match when={seg.type === 'text' && seg}>
                            {(textSeg) => (
                              <div class="w-full mb-1">
                                <MarkdownContent
                                  content={(textSeg() as MessageSegment & { type: 'text' }).content}
                                  messageRole="assistant"
                                  isStreaming={false}
                                />
                              </div>
                            )}
                          </Match>
                          <Match when={seg.type === 'tools' && seg}>
                            {(toolSeg) => (
                              <ToolSegmentDispatch
                                toolCalls={
                                  (toolSeg() as MessageSegment & { type: 'tools' }).toolCalls
                                }
                                isStreaming={false}
                              />
                            )}
                          </Match>
                        </Switch>
                      )}
                    </For>
                  )}
                </Show>

                <Show when={!segments() && props.message.content}>
                  <div class="w-full">
                    <MarkdownContent
                      content={props.message.content}
                      messageRole="assistant"
                      isStreaming={false}
                    />
                  </div>
                </Show>
              </Show>

              <TimestampLine align="left" />

              <Show when={props.message.error}>
                <ErrorRow
                  error={props.message.error!}
                  isStreaming={props.isStreaming}
                  isRetrying={props.isRetrying}
                  onRetry={props.onRetry}
                />
              </Show>
            </div>
          </div>
        </Show>
      </Show>
    </div>
  )
}
