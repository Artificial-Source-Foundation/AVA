/**
 * Thinking Row
 *
 * Collapsed <details> pattern matching Goose's design:
 * - Summary: "Thinking..." while streaming, "Thought for Ns" after completion
 * - Collapsed by default for completed messages
 * - Expanded (open) during live streaming
 * - Muted secondary styling with subtle left border
 */

import { type Component, createEffect, createMemo, createSignal, Show } from 'solid-js'
import { debugLog } from '../../../lib/debug-log'
import { useSettings } from '../../../stores/settings'

interface ThinkingRowProps {
  thinking: string
  isStreaming: boolean
  /** Duration of thinking in seconds (optional) */
  thinkingDuration?: number
}

export const ThinkingRow: Component<ThinkingRowProps> = (props) => {
  const { settings } = useSettings()
  const displayMode = (): string => settings().appearance.thinkingDisplay
  const hidden = () => displayMode() === 'hidden'

  debugLog('thinking', 'render check:', {
    hidden: hidden(),
    thinkingLength: props.thinking?.length,
    isStreaming: props.isStreaming,
    thinkingDisplay: displayMode(),
  })

  const [wasStreaming, setWasStreaming] = createSignal(false)
  const [startTime] = createSignal(Date.now())
  // Track the elapsed time at the moment streaming ends
  const [completedDuration, setCompletedDuration] = createSignal<number | null>(null)

  const duration = createMemo(() => {
    if (props.thinkingDuration) return props.thinkingDuration
    if (!props.isStreaming) return completedDuration() ?? 0
    return (Date.now() - startTime()) / 1000
  })

  // Capture elapsed duration the moment streaming finishes
  createEffect(() => {
    if (!props.isStreaming && wasStreaming()) {
      setCompletedDuration((Date.now() - startTime()) / 1000)
    }
  })

  createEffect(() => {
    if (props.isStreaming && props.thinking) {
      setWasStreaming(true)
    }
  })

  createEffect(() => {
    if (!props.isStreaming && wasStreaming()) {
      setWasStreaming(false)
    }
  })

  const summaryText = createMemo(() => {
    if (props.isStreaming) return 'Thinking...'
    const d = duration()
    if (d > 0.5) return `Thought for ${d.toFixed(1)}s`
    return 'Thought'
  })

  // details element is open (expanded) during streaming, closed when done
  const isOpen = () => props.isStreaming

  return (
    <Show when={!hidden()}>
      <details
        class="mb-2 animate-fade-in"
        open={isOpen()}
        style={{
          'border-left': '2px solid var(--border-subtle, rgba(127,127,127,0.2))',
          'padding-left': '10px',
        }}
      >
        <summary
          class={props.isStreaming ? 'thinking-shimmer' : ''}
          style={{
            cursor: 'pointer',
            'font-size': '11px',
            'font-style': 'italic',
            color: 'var(--text-tertiary, var(--text-muted))',
            'user-select': 'none',
            'list-style': 'none',
            display: 'flex',
            'align-items': 'center',
            gap: '4px',
          }}
        >
          {summaryText()}
        </summary>
        <Show when={props.thinking}>
          <div
            class={`mt-1 max-h-[300px] overflow-y-auto scrollbar-thin ${props.isStreaming ? 'thinking-shimmer' : ''}`}
            style={{
              color: 'var(--text-tertiary, var(--text-muted))',
              'font-style': 'italic',
              'font-size': '12px',
              opacity: '0.7',
              'line-height': '1.5',
              'white-space': 'pre-wrap',
            }}
          >
            {props.thinking}
            <Show when={props.isStreaming}>
              <span class="streaming-cursor">{'\u2589'}</span>
            </Show>
          </div>
        </Show>
      </details>
    </Show>
  )
}
