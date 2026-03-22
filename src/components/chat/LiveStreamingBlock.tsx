/**
 * Live Streaming Block
 *
 * Renders all agent activity in real-time while the agent is running.
 * Shows thinking and tool calls interleaved in chronological order:
 *
 *   Thinking: Considering the request...
 *     → Read CLAUDE.md
 *     → Read docs/architecture/crate-map.md
 *   Thinking: Providing project overview...
 *     I need to give a concise overview...
 *
 * This matches the TUI's live rendering: users see thinking blocks,
 * tool activity, and streamed content as they happen -- not just a
 * "working on it..." spinner.
 */

import { type Component, createMemo, For, Show } from 'solid-js'
import type { ThinkingSegment } from '../../hooks/use-rust-agent'
import { useAgent } from '../../hooks/useAgent'
import { debugLog } from '../../lib/debug-log'
import type { ToolCall } from '../../types'
import { MarkdownContent } from './MarkdownContent'
import { ThinkingRow } from './message-rows/ThinkingRow'
import { ToolCallCard } from './ToolCallCard'
import { ContextGroupHeader } from './ToolCallGroup'
import { ToolPreview } from './ToolPreview'
import { partitionByContext } from './tool-call-utils'

/** Render a single interleaved segment: thinking block + the tools that followed */
const InterleavedSegment: Component<{
  segment: ThinkingSegment
  toolCallsById: Map<string, ToolCall>
  isLast: boolean
  isStreaming: boolean
}> = (props) => {
  const segmentTools = createMemo(() =>
    props.segment.toolCallIds
      .map((id) => props.toolCallsById.get(id))
      .filter((tc): tc is ToolCall => tc !== undefined)
  )

  return (
    <div class="flex flex-col gap-1">
      {/* Thinking block for this segment */}
      <Show when={props.segment.thinking}>
        <ThinkingRow
          thinking={props.segment.thinking}
          isStreaming={props.isLast && props.isStreaming && segmentTools().length === 0}
        />
      </Show>

      {/* Tool calls that happened after this thinking block — grouped by context */}
      <Show when={segmentTools().length > 0}>
        <div class="flex flex-col gap-1 ml-2">
          <For each={partitionByContext(segmentTools())}>
            {(seg) => (
              <Show
                when={
                  seg.kind === 'context' &&
                  (seg as ReturnType<typeof partitionByContext>[number] & { kind: 'context' }).calls
                    .length > 1
                }
                fallback={
                  <ToolCallCard
                    toolCall={
                      seg.kind === 'context'
                        ? (
                            seg as ReturnType<typeof partitionByContext>[number] & {
                              kind: 'context'
                            }
                          ).calls[0]
                        : (
                            seg as ReturnType<typeof partitionByContext>[number] & {
                              kind: 'single'
                            }
                          ).call
                    }
                  />
                }
              >
                <ContextGroupHeader
                  calls={
                    (seg as ReturnType<typeof partitionByContext>[number] & { kind: 'context' })
                      .calls
                  }
                  isStreaming={props.isStreaming}
                />
              </Show>
            )}
          </For>
        </div>
      </Show>
    </div>
  )
}

export const LiveStreamingBlock: Component = () => {
  const agent = useAgent()

  const hasThinking = createMemo(() => {
    const segments = agent.thinkingSegments()
    const hasAny = segments.some((s) => s.thinking.length > 0)
    debugLog(
      'thinking',
      'LiveStreamingBlock hasThinking:',
      hasAny,
      hasAny ? `(${segments.length} segments)` : ''
    )
    return hasAny
  })
  const hasToolCalls = createMemo(() => (agent.activeToolCalls()?.length ?? 0) > 0)
  const hasContent = createMemo(() => !!agent.streamingContent())
  const hasAnyContent = createMemo(() => hasThinking() || hasToolCalls() || hasContent())

  // Build a lookup map from tool call ID to ToolCall object
  const toolCallsById = createMemo((): Map<string, ToolCall> => {
    const map = new Map<string, ToolCall>()
    for (const tc of agent.activeToolCalls()) {
      map.set(tc.id, tc)
    }
    return map
  })

  // Determine if we have interleaved segments (thinking + tools mixed)
  const hasInterleavedSegments = createMemo(() => {
    const segments = agent.thinkingSegments()
    return (
      segments.length > 0 && (segments.some((s) => s.toolCallIds.length > 0) || segments.length > 1)
    )
  })

  // Tool calls not associated with any thinking segment (appeared before thinking started)
  const orphanToolCalls = createMemo((): ToolCall[] => {
    const segments = agent.thinkingSegments()
    const associatedIds = new Set(segments.flatMap((s) => s.toolCallIds))
    return agent.activeToolCalls().filter((tc) => !associatedIds.has(tc.id))
  })

  // Active tool call preview (running/pending and not in a segment yet)
  const activeToolCall = createMemo(() =>
    agent.activeToolCalls().find((tc) => tc.status === 'running' || tc.status === 'pending')
  )

  return (
    <div class="w-full animate-fade-in density-py">
      <div class="flex justify-start">
        <div class="relative group w-[90%] min-w-0">
          <div class="flex flex-col w-full min-w-0">
            <Show
              when={hasInterleavedSegments()}
              fallback={
                <>
                  {/* Fallback: legacy layout — thinking block then tools */}
                  <Show when={hasThinking()}>
                    <ThinkingRow
                      thinking={agent
                        .thinkingSegments()
                        .map((s) => s.thinking)
                        .join('\n\n')}
                      isStreaming={true}
                    />
                  </Show>

                  {/* All tool calls — grouped by context (read/glob/grep collapse together).
                      Running/pending calls are included so groups appear immediately during streaming. */}
                  <Show when={hasToolCalls()}>
                    <div class="flex flex-col gap-1 my-1">
                      <For each={partitionByContext(agent.activeToolCalls())}>
                        {(seg) => (
                          <Show
                            when={
                              seg.kind === 'context' &&
                              (
                                seg as ReturnType<typeof partitionByContext>[number] & {
                                  kind: 'context'
                                }
                              ).calls.length > 1
                            }
                            fallback={
                              <ToolCallCard
                                toolCall={
                                  seg.kind === 'context'
                                    ? (
                                        seg as ReturnType<typeof partitionByContext>[number] & {
                                          kind: 'context'
                                        }
                                      ).calls[0]
                                    : (
                                        seg as ReturnType<typeof partitionByContext>[number] & {
                                          kind: 'single'
                                        }
                                      ).call
                                }
                              />
                            }
                          >
                            <ContextGroupHeader
                              calls={
                                (
                                  seg as ReturnType<typeof partitionByContext>[number] & {
                                    kind: 'context'
                                  }
                                ).calls
                              }
                              isStreaming={true}
                            />
                          </Show>
                        )}
                      </For>
                    </div>
                  </Show>

                  {/* Active tool call preview — only shown when there are no completed/grouped calls yet */}
                  <Show when={activeToolCall() && !hasToolCalls()}>
                    <ToolPreview toolCalls={agent.activeToolCalls()} isStreaming={true} />
                  </Show>
                </>
              }
            >
              {/* Interleaved: thinking → tools → thinking → tools */}
              <div class="flex flex-col gap-1.5">
                {/* Orphan tool calls (happened before any thinking) — grouped by context */}
                <Show when={orphanToolCalls().length > 0}>
                  <div class="flex flex-col gap-1 ml-2">
                    <For each={partitionByContext(orphanToolCalls())}>
                      {(seg) => (
                        <Show
                          when={
                            seg.kind === 'context' &&
                            (
                              seg as ReturnType<typeof partitionByContext>[number] & {
                                kind: 'context'
                              }
                            ).calls.length > 1
                          }
                          fallback={
                            <ToolCallCard
                              toolCall={
                                seg.kind === 'context'
                                  ? (
                                      seg as ReturnType<typeof partitionByContext>[number] & {
                                        kind: 'context'
                                      }
                                    ).calls[0]
                                  : (
                                      seg as ReturnType<typeof partitionByContext>[number] & {
                                        kind: 'single'
                                      }
                                    ).call
                              }
                            />
                          }
                        >
                          <ContextGroupHeader
                            calls={
                              (
                                seg as ReturnType<typeof partitionByContext>[number] & {
                                  kind: 'context'
                                }
                              ).calls
                            }
                            isStreaming={true}
                          />
                        </Show>
                      )}
                    </For>
                  </div>
                </Show>

                {/* Interleaved segments */}
                <For each={agent.thinkingSegments()}>
                  {(segment, index) => (
                    <InterleavedSegment
                      segment={segment}
                      toolCallsById={toolCallsById()}
                      isLast={index() === agent.thinkingSegments().length - 1}
                      isStreaming={true}
                    />
                  )}
                </For>

                {/* Active tool call (being worked on right now, not yet in a segment) */}
                <Show
                  when={
                    activeToolCall() &&
                    !agent
                      .thinkingSegments()
                      .some((s) => s.toolCallIds.includes(activeToolCall()!.id))
                  }
                >
                  <ToolPreview toolCalls={agent.activeToolCalls()} isStreaming={true} />
                </Show>
              </div>
            </Show>

            {/* Streaming text content -- token by token */}
            <Show when={hasContent()}>
              <div class="w-full">
                <MarkdownContent
                  content={agent.streamingContent()}
                  messageRole="assistant"
                  isStreaming={true}
                />
              </div>
            </Show>

            {/* Fallback: show typing indicator when no content is streaming yet */}
            <Show when={!hasAnyContent()}>
              <div class="fixed bottom-14 left-4 z-40 flex items-center gap-2 px-3 py-1.5 rounded-full bg-[var(--surface-overlay)] border border-[var(--border-subtle)] shadow-lg text-[11px] text-[var(--text-secondary)]">
                <span class="w-3.5 h-3.5 border-2 border-[var(--accent)] border-t-transparent rounded-full animate-spin" />
                <span>ava is thinking...</span>
              </div>
            </Show>
          </div>
        </div>
      </div>
    </div>
  )
}
