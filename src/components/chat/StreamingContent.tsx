import { type Component, Show } from 'solid-js'
import type { ThinkingSegment } from '../../hooks/use-rust-agent'
import { useSettings } from '../../stores/settings'
import type { ToolCall } from '../../types'
import { InterleavedThinkingSegments, ToolSegmentDispatch } from './InterleavedThinkingSegments'
import { MarkdownContent } from './MarkdownContent'
import { ToolPreview } from './ToolPreview'
import { ToolCallErrorBoundary } from './tool-call-error-boundary'

interface StreamingContentProps {
  displayContent: string
  effectiveToolCalls: ToolCall[] | undefined
  hasToolCalls: boolean
  toolCallsById: Map<string, ToolCall>
  streamingThinkingSegments?: ThinkingSegment[]
}

export const StreamingContent: Component<StreamingContentProps> = (props) => {
  const { settings } = useSettings()
  const activityMode = () => settings().appearance.activityDisplay ?? 'collapsed'

  return (
    <>
      {/* Interleaved thinking segments during streaming (thinking model with tool calls) */}
      <Show
        when={
          props.streamingThinkingSegments &&
          props.streamingThinkingSegments.length > 0 &&
          props.streamingThinkingSegments
        }
      >
        {(segs) => (
          <InterleavedThinkingSegments
            segments={segs()}
            toolCallsById={props.toolCallsById}
            isStreaming={true}
          />
        )}
      </Show>

      {/* Fallback tool calls when no interleaved thinking segments */}
      <Show when={!props.streamingThinkingSegments || props.streamingThinkingSegments.length === 0}>
        <Show when={props.hasToolCalls && activityMode() !== 'hidden'}>
          <details
            class="mb-1 rounded-lg"
            open={true}
            style={{
              border: '1px solid var(--border-default)',
              background: 'var(--surface-sunken)',
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
              <span class="w-3.5 h-3.5 border-2 border-[var(--accent)] border-t-transparent rounded-full animate-spin flex-shrink-0" />
              <span style={{ flex: '1' }}>
                Working... ({props.effectiveToolCalls!.length} tool
                {props.effectiveToolCalls!.length !== 1 ? 's' : ''})
              </span>
            </summary>
            <div class="px-3 pb-3">
              <ToolCallErrorBoundary>
                <ToolSegmentDispatch toolCalls={props.effectiveToolCalls!} isStreaming={true} />
              </ToolCallErrorBoundary>
            </div>
          </details>
        </Show>
        <ToolPreview toolCalls={props.effectiveToolCalls} isStreaming={true} />
      </Show>

      <Show when={props.displayContent}>
        <div class="w-full">
          <MarkdownContent
            content={props.displayContent}
            messageRole="assistant"
            isStreaming={true}
          />
        </div>
      </Show>

      {/* Typing indicator when nothing has arrived yet */}
      <Show
        when={
          !props.displayContent &&
          !props.hasToolCalls &&
          (!props.streamingThinkingSegments || props.streamingThinkingSegments.length === 0)
        }
      >
        <div class="fixed bottom-14 left-4 z-40 flex items-center gap-2 px-3 py-1.5 rounded-full bg-[var(--surface-overlay)] border border-[var(--border-subtle)] shadow-lg text-[11px] text-[var(--text-secondary)]">
          <span class="w-3.5 h-3.5 border-2 border-[var(--accent)] border-t-transparent rounded-full animate-spin" />
          <span>ava is thinking...</span>
        </div>
      </Show>
    </>
  )
}
