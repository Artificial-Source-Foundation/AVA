/**
 * Live Streaming Block
 *
 * Renders all agent activity in real-time while the agent is running:
 * - Thinking content (collapsible, live-streaming)
 * - Tool calls (grouped with status, args summary, output)
 * - Streaming text content (token-by-token with cursor)
 *
 * This matches the TUI's live rendering: users see thinking blocks,
 * tool activity, and streamed content as they happen -- not just a
 * "working on it..." spinner.
 */

import { type Component, createMemo, Show } from 'solid-js'
import { useAgent } from '../../hooks/useAgent'
import { debugLog } from '../../lib/debug-log'
import { MarkdownContent } from './MarkdownContent'
import { ThinkingRow } from './message-rows/ThinkingRow'
import { ToolCallGroup } from './ToolCallGroup'
import { ToolPreview } from './ToolPreview'

export const LiveStreamingBlock: Component = () => {
  const agent = useAgent()

  const hasThinking = createMemo(() => {
    const thought = agent.currentThought()
    console.warn('[THINKING-DEBUG] LiveStreamingBlock hasThinking:', {
      hasThought: !!thought,
      thoughtLength: thought?.length ?? 0,
      isRunning: agent.isRunning(),
    })
    debugLog(
      'thinking',
      'LiveStreamingBlock hasThinking:',
      !!thought,
      thought ? `(${thought.length} chars)` : ''
    )
    return !!thought
  })
  const hasToolCalls = createMemo(() => (agent.activeToolCalls()?.length ?? 0) > 0)
  const hasContent = createMemo(() => !!agent.streamingContent())
  const hasAnyContent = createMemo(() => hasThinking() || hasToolCalls() || hasContent())

  // Group tool calls into completed and active
  const completedToolCalls = createMemo(() =>
    agent.activeToolCalls().filter((tc) => tc.status === 'success' || tc.status === 'error')
  )
  const activeToolCall = createMemo(() =>
    agent.activeToolCalls().find((tc) => tc.status === 'running' || tc.status === 'pending')
  )

  return (
    <div class="w-full animate-fade-in density-py">
      <div class="flex justify-start">
        <div class="relative group w-[90%] min-w-0">
          <div class="flex flex-col w-full min-w-0">
            {/* Thinking block -- live streaming */}
            <Show when={hasThinking()}>
              <ThinkingRow thinking={agent.currentThought()} isStreaming={true} />
            </Show>

            {/* Completed tool calls -- rendered as a group */}
            <Show when={completedToolCalls().length > 0}>
              <ToolCallGroup toolCalls={completedToolCalls()} isStreaming={true} />
            </Show>

            {/* Active tool call preview */}
            <Show when={activeToolCall()}>
              <ToolPreview toolCalls={agent.activeToolCalls()} isStreaming={true} />
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
