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
import { ToolPreview } from './ToolPreview'

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

      {/* Tool calls that happened after this thinking block */}
      <Show when={segmentTools().length > 0}>
        <div class="flex flex-col gap-1 ml-2">
          <For each={segmentTools()}>{(tc) => <ToolCallCard toolCall={tc} />}</For>
        </div>
      </Show>
    </div>
  )
}

export const LiveStreamingBlock: Component = () => {
  const agent = useAgent()

  const hasThinking = createMemo(() => {
    const segments = agent.thinkingSegments()
    const hasAny = segments.length > 0 && segments.some((s) => s.thinking.length > 0)
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
                        .join('')}
                      isStreaming={true}
                    />
                  </Show>

                  {/* Completed tool calls */}
                  <Show when={hasToolCalls()}>
                    <div class="flex flex-col gap-1 my-1">
                      <For
                        each={agent
                          .activeToolCalls()
                          .filter((tc) => tc.status === 'success' || tc.status === 'error')}
                      >
                        {(tc) => <ToolCallCard toolCall={tc} />}
                      </For>
                    </div>
                  </Show>

                  {/* Active tool call preview */}
                  <Show when={activeToolCall()}>
                    <ToolPreview toolCalls={agent.activeToolCalls()} isStreaming={true} />
                  </Show>
                </>
              }
            >
              {/* Interleaved: thinking → tools → thinking → tools */}
              <div class="flex flex-col gap-1.5">
                {/* Orphan tool calls (happened before any thinking) */}
                <Show when={orphanToolCalls().length > 0}>
                  <div class="flex flex-col gap-1 ml-2">
                    <For each={orphanToolCalls()}>{(tc) => <ToolCallCard toolCall={tc} />}</For>
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
              <div class="flex items-center gap-2 text-xs text-[var(--text-secondary)] py-2">
                <div class="flex items-center gap-[5px]">
                  <span class="typing-dot" style={{ 'animation-delay': '0ms' }} />
                  <span class="typing-dot" style={{ 'animation-delay': '160ms' }} />
                  <span class="typing-dot" style={{ 'animation-delay': '320ms' }} />
                </div>
                <span class="font-[var(--font-ui-mono)] tracking-wide">ava is thinking...</span>
              </div>
            </Show>
          </div>
        </div>
      </div>
    </div>
  )
}
