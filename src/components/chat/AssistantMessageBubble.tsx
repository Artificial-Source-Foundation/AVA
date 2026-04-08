import { RefreshCw } from 'lucide-solid'
import { type Accessor, type Component, createMemo, For, Match, Show, Switch } from 'solid-js'
import type { ThinkingSegment } from '../../hooks/use-rust-agent'
import { formatCost } from '../../lib/cost'
import { debugLog } from '../../lib/debug-log'
import { formatMs } from '../../lib/format-time'
import type { Message, ToolCall } from '../../types'
import type { PlanData } from '../../types/rust-ipc'
import { ContextSummaryCard } from './ContextSummaryCard'
import { InlinePlanCard } from './InlinePlanCard'
import {
  InterleavedThinkingSegments,
  type MessageSegment,
  segmentMessage,
  ToolSegmentDispatch,
} from './InterleavedThinkingSegments'
import { MarkdownContent } from './MarkdownContent'
import { MessageActions } from './MessageActions'
import { ErrorRow, ThinkingRow } from './message-rows'
import { StreamingContent } from './StreamingContent'
import { TurnDiffSummary } from './TurnDiffSummary'

// ============================================================================
// Helpers
// ============================================================================

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

// ============================================================================
// AssistantMessageBubble
// ============================================================================

interface AssistantMessageBubbleProps {
  message: Message
  readOnly?: boolean
  isStreaming: boolean
  isLastMessage: boolean
  isRetrying: boolean
  /** Live tool calls from useAgent signal (avoids store re-renders during streaming) */
  streamingToolCalls?: ToolCall[]
  /** Live content signal — avoids store updates during streaming */
  streamingContent?: Accessor<string>
  /** Live thinking segments during streaming — enables real-time thinking display */
  streamingThinkingSegments?: ThinkingSegment[]
  onStartEdit: () => void
  onRegenerate: () => void
  onCopy: () => void
  onDelete: () => void
  onBranch: () => void
  onRewind: () => void
  onRetry: () => void
}

interface ContextSummaryMetadata {
  source: 'manual' | 'auto'
  summary: string
  tokensSaved: number
  usageBeforePercent?: number
}

export const AssistantMessageBubble: Component<AssistantMessageBubbleProps> = (props) => {
  const isActiveStreaming = () => props.isStreaming && props.isLastMessage

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
  const hasToolCalls = () => (effectiveToolCalls()?.length ?? 0) > 0

  const planData = createMemo((): PlanData | null => {
    const meta = props.message.metadata
    if (!meta?.plan) return null
    const plan = meta.plan as PlanData
    if (plan.steps && Array.isArray(plan.steps) && plan.summary) return plan
    return null
  })

  const contextSummary = createMemo((): ContextSummaryMetadata | null => {
    const summary = props.message.metadata?.contextSummary as ContextSummaryMetadata | undefined
    if (!summary?.summary) return null
    return summary
  })

  const segments = createMemo((): MessageSegment[] | null => {
    if (isActiveStreaming()) return null
    if (!hasToolCalls() && !props.message.content) return null
    return segmentMessage(props.message.content, effectiveToolCalls())
  })

  /** Interleaved thinking segments from metadata (present when thinking model used tools) */
  const thinkingSegments = createMemo((): ThinkingSegment[] | null => {
    const segs = props.message.metadata?.thinkingSegments
    if (!segs || !Array.isArray(segs) || (segs as ThinkingSegment[]).length <= 1) return null
    return segs as ThinkingSegment[]
  })

  /**
   * Detect if the assistant response appears truncated.
   * Heuristic: no terminal punctuation (.!?:) and content is non-trivial.
   * Only applies to completed (non-streaming) assistant messages with content.
   */
  const isTruncated = createMemo((): boolean => {
    if (isActiveStreaming() || props.isStreaming) return false
    const content = props.message.content
    if (!content || content.length < 80) return false
    if (props.message.error) return false
    if (props.message.toolCalls && props.message.toolCalls.length > 0) return false
    if (props.message.metadata?.thinkingSegments) return false
    const trimmed = content.trimEnd()
    const lastChar = trimmed[trimmed.length - 1]
    return !/[.!?:)\]}"'`\n]/.test(lastChar)
  })

  /** Map from tool call ID to ToolCall for interleaved rendering */
  const toolCallsById = createMemo((): Map<string, ToolCall> => {
    const map = new Map<string, ToolCall>()
    for (const tc of effectiveToolCalls() ?? []) {
      map.set(tc.id, tc)
    }
    return map
  })

  const TimestampLine = () => {
    return (
      <div class="relative h-[20px] flex justify-start overflow-visible">
        <Show when={!props.isStreaming}>
          <div class="font-[var(--font-ui-mono)] text-[11px] tracking-wider text-[var(--text-muted)] pt-1.5 opacity-0 transition-opacity duration-200 group-hover:opacity-100 tabular-nums">
            {formatTimestamp(props.message)}
            <Show when={props.message.model}>
              {' '}
              &middot; <span class="font-semibold">{formatModelName(props.message.model!)}</span>
            </Show>
            <Show when={props.message.tokensUsed}>
              {' '}
              &middot; {props.message.tokensUsed?.toLocaleString()} tokens
            </Show>
            <Show when={props.message.costUSD}> &middot; {formatCost(props.message.costUSD!)}</Show>
            <Show when={props.message.metadata?.elapsedMs as number | undefined}>
              {' '}
              &middot; {formatMs(props.message.metadata!.elapsedMs as number)}
            </Show>
            <Show when={props.message.metadata?.mode}>
              {' '}
              &middot; {props.message.metadata!.mode as string}
            </Show>
          </div>
        </Show>
        <Show when={props.message.content && !props.isStreaming}>
          <div class="absolute left-0 top-0 pt-1">
            <MessageActions
              message={props.message}
              readOnly={props.readOnly}
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
    <div class="relative group w-full min-w-0">
      <div class="flex flex-col w-full min-w-0">
        {/* Interleaved thinking+tools (thinking model with tool calls) */}
        <Show when={thinkingSegments()}>
          {(segs) => (
            <InterleavedThinkingSegments segments={segs()} toolCallsById={toolCallsById()} />
          )}
        </Show>

        {/* Fallback: single thinking block (no interleaving needed) */}
        <Show
          when={
            !thinkingSegments() &&
            (() => {
              const t = props.message.metadata?.thinking as string
              if (t) debugLog('thinking', 'message metadata: yes', 'msgId:', props.message.id)
              return t
            })()
          }
        >
          <ThinkingRow
            thinking={props.message.metadata!.thinking as string}
            isStreaming={props.isStreaming}
          />
        </Show>

        {/* Plan card (rendered from message metadata) */}
        <Show when={planData()}>
          {(plan) => (
            <div class="w-full my-1.5">
              <InlinePlanCard plan={plan()} />
            </div>
          )}
        </Show>

        <Show when={contextSummary()}>
          {(summary) => (
            <ContextSummaryCard
              summaryLine={props.message.content}
              summary={summary().summary}
              source={summary().source}
              tokensSaved={summary().tokensSaved}
              usageBeforePercent={summary().usageBeforePercent}
            />
          )}
        </Show>

        <Show when={isActiveStreaming()}>
          <StreamingContent
            displayContent={displayContent()}
            effectiveToolCalls={effectiveToolCalls()}
            hasToolCalls={hasToolCalls()}
            toolCallsById={toolCallsById()}
            streamingThinkingSegments={props.streamingThinkingSegments}
          />
        </Show>

        <Show when={!isActiveStreaming()}>
          <Show when={!contextSummary()}>
            {/* When interleaved thinking segments handle tools, only render text segments */}
            <Show when={thinkingSegments()}>
              <Show when={props.message.content}>
                <div class="w-full mb-1">
                  <MarkdownContent
                    content={props.message.content}
                    messageRole="assistant"
                    isStreaming={false}
                  />
                </div>
              </Show>
            </Show>

            {/* Normal rendering (no interleaved thinking segments) */}
            <Show when={!thinkingSegments()}>
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
          </Show>
        </Show>

        {/* Per-turn file diff summary (shown after message content, collapsed by default) */}
        <Show when={!isActiveStreaming() && (effectiveToolCalls()?.length ?? 0) > 0}>
          <TurnDiffSummary toolCalls={effectiveToolCalls()!} isStreaming={props.isStreaming} />
        </Show>

        {/* Timestamp + message actions — only shown for completed (non-streaming) messages */}
        <Show when={!isActiveStreaming()}>
          <TimestampLine />
        </Show>

        {/* Truncation hint — response appears cut off mid-sentence */}
        <Show when={isTruncated() && props.isLastMessage}>
          <button
            type="button"
            onClick={() => props.onRegenerate()}
            class="mt-1 flex items-center gap-1.5 text-[11px] text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
            title="Response may be truncated — continue generation"
          >
            <RefreshCw class="w-3 h-3" />
            <span>Continue generation</span>
          </button>
        </Show>

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
  )
}
