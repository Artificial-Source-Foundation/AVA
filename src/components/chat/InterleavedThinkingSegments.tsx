import { type Component, createEffect, createSignal, Index, Match, Show, Switch } from 'solid-js'
import type { ThinkingSegment } from '../../hooks/use-rust-agent'
import { useSettings } from '../../stores/settings'
import type { ToolCall } from '../../types'
import { CommandOutputRow, DiffRow, ThinkingRow, ToolCallRow } from './message-rows'
import type { MessageSegment } from './message-segments'
import { ContextGroupHeader } from './ToolCallGroup'
import { ToolCallErrorBoundary } from './tool-call-error-boundary'
import { partitionByContext } from './tool-call-utils'

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

export const ToolSegmentDispatch: Component<ToolSegmentProps> = (props) => {
  const segments = () => partitionByContext(props.toolCalls)

  return (
    <div class="flex flex-col gap-1.5 my-1">
      <Index each={segments()}>
        {(seg) => (
          <Show
            when={seg().kind === 'context'}
            fallback={
              <SingleToolCallRow
                toolCall={
                  (seg() as ReturnType<typeof partitionByContext>[number] & { kind: 'single' }).call
                }
              />
            }
          >
            {/* Context segment: group if >1 call, else render single */}
            <Show
              when={
                (seg() as ReturnType<typeof partitionByContext>[number] & { kind: 'context' }).calls
                  .length > 1
              }
              fallback={
                <SingleToolCallRow
                  toolCall={
                    (seg() as ReturnType<typeof partitionByContext>[number] & { kind: 'context' })
                      .calls[0]
                  }
                />
              }
            >
              <ToolCallErrorBoundary>
                <ContextGroupHeader
                  calls={
                    (seg() as ReturnType<typeof partitionByContext>[number] & { kind: 'context' })
                      .calls
                  }
                  isStreaming={props.isStreaming}
                />
              </ToolCallErrorBoundary>
            </Show>
          </Show>
        )}
      </Index>
    </div>
  )
}

// ============================================================================
// Interleaved Thinking + Tools Renderer
// ============================================================================

/**
 * Wraps interleaved thinking + tool calls in a collapsible card.
 * - While streaming: open, shows live summary "Working... (N tools)"
 * - After completion: collapsed to one line "Agent activity (3 thoughts, 8 tool calls)"
 * - Respects `activityDisplay` setting: collapsed (default), expanded, hidden
 */
export const InterleavedThinkingSegments: Component<{
  segments: ThinkingSegment[]
  toolCallsById: Map<string, ToolCall>
  isStreaming?: boolean
}> = (props) => {
  const { settings } = useSettings()
  const activityMode = () => settings().appearance.activityDisplay ?? 'collapsed'

  const [isOpen, setIsOpen] = createSignal(false)

  createEffect(() => {
    if (props.isStreaming) {
      setIsOpen(true)
    }
  })

  // Live counts
  const totalTools = () => props.segments.reduce((sum, seg) => sum + seg.toolCallIds.length, 0)
  const thinkingCount = () => props.segments.filter((s) => s.thinking).length

  // Tool status counts from toolCallsById
  const toolStats = () => {
    let done = 0,
      failed = 0,
      running = 0
    for (const seg of props.segments) {
      for (const id of seg.toolCallIds) {
        const tc = props.toolCallsById.get(id)
        if (!tc) continue
        if (tc.status === 'error') failed++
        else if (tc.status === 'success') done++
        else running++
      }
    }
    return { done, failed, running }
  }

  // Summary with live stats
  const summaryLabel = () => {
    const tools = totalTools()
    const thoughts = thinkingCount()
    const stats = toolStats()
    if (props.isStreaming) {
      const parts: string[] = []
      if (thoughts > 0) parts.push(`${thoughts} thought${thoughts !== 1 ? 's' : ''}`)
      if (tools > 0) {
        let toolStr = `${tools} tool${tools !== 1 ? 's' : ''}`
        const extra: string[] = []
        if (stats.done > 0) extra.push(`${stats.done} done`)
        if (stats.failed > 0) extra.push(`${stats.failed} failed`)
        if (stats.running > 0) extra.push(`${stats.running} running`)
        if (extra.length > 0) toolStr += ` — ${extra.join(', ')}`
        parts.push(toolStr)
      }
      return parts.length > 0 ? `Working... (${parts.join(', ')})` : 'Working...'
    }
    const parts: string[] = []
    if (thoughts > 0) parts.push(`${thoughts} thought${thoughts !== 1 ? 's' : ''}`)
    if (tools > 0) {
      let toolStr = `${tools} tool call${tools !== 1 ? 's' : ''}`
      if (stats.failed > 0) toolStr += `, ${stats.failed} failed`
      parts.push(toolStr)
    }
    return parts.length > 0 ? `Agent activity (${parts.join(', ')})` : 'Agent activity'
  }

  // When streaming starts, auto-open; keep user's toggle otherwise
  const detailsOpen = () => {
    if (activityMode() === 'expanded') return true
    if (props.isStreaming) return true
    return isOpen()
  }

  const inner = (
    <Index each={props.segments}>
      {(segment, idx) => {
        const segmentTools = () =>
          segment()
            .toolCallIds.map((id) => props.toolCallsById.get(id))
            .filter((tc): tc is ToolCall => tc !== undefined)
        const isLastSegment = () => idx === props.segments.length - 1
        const segStreaming = () =>
          !!(props.isStreaming && isLastSegment() && segment().toolCallIds.length === 0)

        return (
          <div class="flex flex-col gap-1">
            <Show when={segment().thinking}>
              <ThinkingRow thinking={segment().thinking} isStreaming={segStreaming()} />
            </Show>
            <Show when={segmentTools().length > 0}>
              <div class="ml-2 my-0.5">
                <ToolSegmentDispatch toolCalls={segmentTools()} isStreaming={false} />
              </div>
            </Show>
          </div>
        )
      }}
    </Index>
  )

  return (
    <Show when={activityMode() !== 'hidden'}>
      <details
        class="mb-1 rounded-lg animate-fade-in"
        open={detailsOpen()}
        onToggle={(e) => setIsOpen((e.currentTarget as HTMLDetailsElement).open)}
        style={{
          border: '1px solid var(--border-default, rgba(255,255,255,0.08))',
          background: 'var(--bg-subtle, rgba(255,255,255,0.02))',
        }}
      >
        <summary
          style={{
            cursor: 'pointer',
            'font-size': '12px',
            color: 'var(--text-secondary, var(--text-muted))',
            'user-select': 'none',
            'list-style': 'none',
            padding: '8px 12px',
            display: 'flex',
            'align-items': 'center',
            gap: '6px',
          }}
        >
          <Show
            when={props.isStreaming}
            fallback={
              <span
                style={{ 'font-size': '10px', opacity: '0.5', transition: 'transform 150ms' }}
                class={detailsOpen() ? 'rotate-90' : ''}
              >
                ▶
              </span>
            }
          >
            <span class="w-3.5 h-3.5 border-2 border-[var(--accent)] border-t-transparent rounded-full animate-spin flex-shrink-0" />
          </Show>
          <span style={{ flex: '1' }}>{summaryLabel()}</span>
          <Show when={!detailsOpen()}>
            <span style={{ 'font-size': '11px', opacity: '0.35' }}>click to expand</span>
          </Show>
        </summary>
        <div class="flex flex-col gap-1.5 px-3 pb-3">{inner}</div>
      </details>
    </Show>
  )
}

// Re-export MessageSegment type for use in AssistantMessageBubble
export type { MessageSegment }
// Re-export segmentMessage for use in AssistantMessageBubble
export { segmentMessage } from './message-segments'
