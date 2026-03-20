import { Crown, RefreshCw } from 'lucide-solid'
import { type Accessor, type Component, createMemo, For, Match, Show, Switch } from 'solid-js'
import type { ThinkingSegment } from '../../hooks/use-rust-agent'
import { formatCost } from '../../lib/cost'
import { debugLog } from '../../lib/debug-log'
import { formatMs } from '../../lib/format-time'
import { useTeam } from '../../stores/team'
import type { Message, ToolCall } from '../../types'
import type { PlanData } from '../../types/rust-ipc'
import { TEAM_DOMAINS, type TeamDomain } from '../../types/team'
import { EditForm } from './EditForm'
import { InlinePlanCard } from './InlinePlanCard'
import { MarkdownContent } from './MarkdownContent'
import { MessageActions } from './MessageActions'
import { CommandOutputRow, DiffRow, ErrorRow, ThinkingRow, ToolCallRow } from './message-rows'
import { type MessageSegment, segmentMessage } from './message-segments'
import { ContextGroupHeader } from './ToolCallGroup'
import { ToolPreview } from './ToolPreview'
import { TurnDiffSummary } from './TurnDiffSummary'
import { ToolCallErrorBoundary } from './tool-call-error-boundary'
import { partitionByContext } from './tool-call-utils'

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

// ============================================================================
// Lead Question Detection
// ============================================================================

/** Parsed lead question relayed through the Director */
interface LeadQuestion {
  name: string
  domain: TeamDomain
  domainLabel: string
  color: string
  question: string
}

/** Domain keyword map for inferring domain from lead names */
const DOMAIN_KEYWORDS: Array<{ pattern: RegExp; domain: TeamDomain }> = [
  { pattern: /frontend|FE|UI/i, domain: 'frontend' },
  { pattern: /backend|BE|API/i, domain: 'backend' },
  { pattern: /fullstack|FS/i, domain: 'fullstack' },
  { pattern: /QA|test|quality/i, domain: 'testing' },
  { pattern: /devops|ops|infra|deploy/i, domain: 'devops' },
  { pattern: /docs|documentation/i, domain: 'docs' },
  { pattern: /design|UX/i, domain: 'design' },
  { pattern: /data|analytics/i, domain: 'data' },
  { pattern: /security|sec/i, domain: 'security' },
  { pattern: /research/i, domain: 'general' },
]

/**
 * Detect a lead question pattern in message content.
 * Matches patterns like "Pedro (Backend Lead) asks:" or "Luna (QA Lead) asks:"
 */
function parseLeadQuestion(content: string): LeadQuestion | null {
  const match = content.match(/^(.+?)\s*\(([^)]+?)\s+Lead\)\s+asks:\s*\n?([\s\S]+)$/)
  if (!match) return null
  const [, name, domainHint, question] = match
  let domain: TeamDomain = 'general'
  for (const { pattern, domain: d } of DOMAIN_KEYWORDS) {
    if (pattern.test(domainHint)) {
      domain = d
      break
    }
  }
  const config = TEAM_DOMAINS[domain]
  return {
    name: name.trim(),
    domain,
    domainLabel: `${domainHint.trim()} Lead`,
    color: config.color,
    question: question.trim(),
  }
}

// ============================================================================
// Lead Question Relay Card
// ============================================================================

const LeadQuestionCard: Component<{ question: LeadQuestion }> = (props) => {
  return (
    <div
      class="w-full rounded-[var(--radius-xl)] bg-[var(--gray-3)] overflow-hidden my-1"
      style={{
        border: `1px solid color-mix(in srgb, ${props.question.color} 25%, transparent)`,
      }}
    >
      <div class="flex flex-col gap-2 px-4 py-3">
        {/* Header: domain dot + "Name (Domain Lead) asks:" */}
        <div class="flex items-center gap-1.5">
          <span
            class="inline-block w-2 h-2 rounded-full flex-shrink-0"
            style={{ background: props.question.color }}
          />
          <span class="text-[11px] font-semibold" style={{ color: props.question.color }}>
            {props.question.name} ({props.question.domainLabel}) asks:
          </span>
        </div>
        {/* Question body */}
        <div class="w-full">
          <MarkdownContent
            content={props.question.question}
            messageRole="assistant"
            isStreaming={false}
          />
        </div>
      </div>
    </div>
  )
}

// ============================================================================
// Director Label
// ============================================================================

const DirectorLabel: Component = () => {
  return (
    <div class="flex items-center gap-1.5 mb-1">
      <Crown class="w-3.5 h-3.5 flex-shrink-0" style={{ color: 'var(--amber-4)' }} />
      <span class="text-[11px] font-semibold tracking-wide" style={{ color: 'var(--amber-4)' }}>
        Director
      </span>
    </div>
  )
}

// ============================================================================
// Tool Segment Dispatch
// ============================================================================

interface ToolSegmentProps {
  toolCalls: ToolCall[]
  isStreaming: boolean
}

/**
 * Render a single non-context tool call with specialized row components
 * (CommandOutputRow for bash, DiffRow for edits with diffs, ToolCallRow otherwise).
 */
const SingleToolCallRow: Component<{ toolCall: ToolCall }> = (props) => {
  return (
    <ToolCallErrorBoundary>
      <Switch fallback={<ToolCallRow toolCall={props.toolCall} />}>
        <Match when={props.toolCall.name === 'bash' && props.toolCall}>
          {(call) => <CommandOutputRow toolCall={call()} />}
        </Match>
        <Match when={props.toolCall.diff && props.toolCall.name !== 'bash' && props.toolCall}>
          {(call) => <DiffRow toolCall={call()} />}
        </Match>
      </Switch>
    </ToolCallErrorBoundary>
  )
}

const ToolSegmentDispatch: Component<ToolSegmentProps> = (props) => {
  const segments = () => partitionByContext(props.toolCalls)

  return (
    <div class="flex flex-col gap-1.5 my-1">
      <For each={segments()}>
        {(seg) => (
          <Show
            when={seg.kind === 'context'}
            fallback={
              <SingleToolCallRow
                toolCall={
                  (seg as ReturnType<typeof partitionByContext>[number] & { kind: 'single' }).call
                }
              />
            }
          >
            {/* Context segment: group if >1 call, else render single */}
            <Show
              when={
                (seg as ReturnType<typeof partitionByContext>[number] & { kind: 'context' }).calls
                  .length > 1
              }
              fallback={
                <SingleToolCallRow
                  toolCall={
                    (seg as ReturnType<typeof partitionByContext>[number] & { kind: 'context' })
                      .calls[0]
                  }
                />
              }
            >
              <ToolCallErrorBoundary>
                <ContextGroupHeader
                  calls={
                    (seg as ReturnType<typeof partitionByContext>[number] & { kind: 'context' })
                      .calls
                  }
                  isStreaming={props.isStreaming}
                />
              </ToolCallErrorBoundary>
            </Show>
          </Show>
        )}
      </For>
    </div>
  )
}

// ============================================================================
// Interleaved Thinking + Tools Renderer
// ============================================================================

/**
 * Renders thinking segments interleaved with their associated tool calls.
 * Used when `message.metadata.thinkingSegments` is present (thinking models with tool calls).
 *
 * Produces layout like:
 *   💭 Thought for 2.3s
 *     → Read CLAUDE.md
 *     → Read docs/architecture/crate-map.md
 *   💭 Thought for 1.1s
 *   I need to give a concise overview...
 */
const InterleavedThinkingSegments: Component<{
  segments: ThinkingSegment[]
  toolCallsById: Map<string, ToolCall>
}> = (props) => {
  return (
    <div class="flex flex-col gap-1.5">
      <For each={props.segments}>
        {(segment) => {
          const segmentTools = () =>
            segment.toolCallIds
              .map((id) => props.toolCallsById.get(id))
              .filter((tc): tc is ToolCall => tc !== undefined)

          return (
            <div class="flex flex-col gap-1">
              {/* Thinking block for this segment */}
              <Show when={segment.thinking}>
                <ThinkingRow thinking={segment.thinking} isStreaming={false} />
              </Show>

              {/* Tool calls that happened after this thinking block */}
              <Show when={segmentTools().length > 0}>
                <div class="flex flex-col gap-1.5 ml-2 my-0.5">
                  <For each={segmentTools()}>
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
              </Show>
            </div>
          )
        }}
      </For>
    </div>
  )
}

export const MessageBubble: Component<MessageBubbleProps> = (props) => {
  const team = useTeam()
  const isUser = () => props.message.role === 'user'
  const shouldAnimateIn = () => props.shouldAnimate && !props.isEditing

  const isActiveStreaming = () => props.isStreaming && props.isLastMessage && !isUser()

  /** True when team mode is active and this is an assistant message (Director's message) */
  const isDirectorMessage = () => !isUser() && team.hierarchy() !== null

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

  const planData = createMemo((): PlanData | null => {
    const meta = props.message.metadata
    if (!meta?.plan) return null
    const plan = meta.plan as PlanData
    if (plan.steps && Array.isArray(plan.steps) && plan.summary) return plan
    return null
  })

  /** Detect if this message is relaying a lead's question */
  const leadQuestion = createMemo((): LeadQuestion | null => {
    if (isUser()) return null
    const content = props.message.content
    if (!content) return null
    return parseLeadQuestion(content)
  })

  const segments = createMemo((): MessageSegment[] | null => {
    if (isUser()) return null
    if (isActiveStreaming()) return null
    if (!hasToolCalls() && !props.message.content) return null
    return segmentMessage(props.message.content, effectiveToolCalls())
  })

  /** Interleaved thinking segments from metadata (present when thinking model used tools) */
  const thinkingSegments = createMemo((): ThinkingSegment[] | null => {
    if (isUser()) return null
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
    if (isUser() || isActiveStreaming() || props.isStreaming) return false
    const content = props.message.content
    if (!content || content.length < 80) return false
    // Skip if message ended with an error
    if (props.message.error) return false
    const trimmed = content.trimEnd()
    // Check last non-whitespace character
    const lastChar = trimmed[trimmed.length - 1]
    return !/[.!?:)\]}"'`]/.test(lastChar)
  })

  /** Map from tool call ID to ToolCall for interleaved rendering */
  const toolCallsById = createMemo((): Map<string, ToolCall> => {
    const map = new Map<string, ToolCall>()
    for (const tc of effectiveToolCalls() ?? []) {
      map.set(tc.id, tc)
    }
    return map
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
          <div
            class="relative group w-[90%] min-w-0"
            classList={{
              'pl-3': isDirectorMessage(),
            }}
            style={
              isDirectorMessage()
                ? {
                    'border-left': '2px solid var(--amber-4)',
                  }
                : undefined
            }
          >
            <div class="flex flex-col w-full min-w-0">
              {/* Director label (team mode only) */}
              <Show when={isDirectorMessage()}>
                <DirectorLabel />
              </Show>

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

              {/* Lead question relay card */}
              <Show when={leadQuestion()}>{(q) => <LeadQuestionCard question={q()} />}</Show>

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
                {/* When content is a lead question, skip normal rendering (card handles it) */}
                <Show when={!leadQuestion()}>
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
                                      content={
                                        (textSeg() as MessageSegment & { type: 'text' }).content
                                      }
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
              <Show
                when={!isActiveStreaming() && !isUser() && (effectiveToolCalls()?.length ?? 0) > 0}
              >
                <TurnDiffSummary
                  toolCalls={effectiveToolCalls()!}
                  isStreaming={props.isStreaming}
                />
              </Show>

              <TimestampLine align="left" />

              {/* Truncation hint — response appears cut off mid-sentence */}
              <Show when={isTruncated() && props.isLastMessage}>
                <button
                  type="button"
                  onClick={props.onRegenerate}
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
        </Show>
      </Show>
    </div>
  )
}
