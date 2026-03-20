/**
 * Thinking Row
 *
 * Minimal reasoning display matching TUI style:
 * - Collapsed: "💭 Thought for Xs" — small grey italic text
 * - Expanded: dimmed italic content with left border
 * - Preview: first 2 lines visible with expand hint
 * - Auto-expands during streaming, auto-collapses when complete
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

  console.warn('[THINKING-DEBUG] ThinkingRow render:', {
    hidden: hidden(),
    thinkingLength: props.thinking?.length,
    thinkingPreview: props.thinking?.slice(0, 100),
    isStreaming: props.isStreaming,
    thinkingDisplay: displayMode(),
  })
  debugLog('thinking', 'render check:', {
    hidden: hidden(),
    thinkingLength: props.thinking?.length,
    isStreaming: props.isStreaming,
    thinkingDisplay: displayMode(),
  })

  const [expanded, setExpanded] = createSignal(false)
  const [wasStreaming, setWasStreaming] = createSignal(false)
  const [startTime] = createSignal(Date.now())

  const duration = createMemo(() => {
    if (props.thinkingDuration) return props.thinkingDuration
    if (!props.isStreaming) return (Date.now() - startTime()) / 1000
    return 0
  })

  const previewLines = createMemo(() => {
    if (!props.thinking) return ''
    const lines = props.thinking.split('\n').filter(Boolean)
    return lines.slice(0, 2).join('\n')
  })

  // Auto-expand while streaming, auto-collapse when done
  createEffect(() => {
    if (props.isStreaming && props.thinking) {
      setExpanded(true)
      setWasStreaming(true)
    }
  })

  createEffect(() => {
    if (!props.isStreaming && wasStreaming()) {
      setExpanded(false)
      setWasStreaming(false)
    }
  })

  const labelText = createMemo(() => {
    if (props.isStreaming && !props.thinking) return 'Thinking...'
    if (props.isStreaming) return 'Thinking...'
    const d = duration()
    if (d > 0.5) return `Thought for ${d.toFixed(1)}s`
    return 'Thought'
  })

  const isPreview = () =>
    displayMode() === 'preview' && !expanded() && !!props.thinking && !props.isStreaming

  return (
    <Show when={!hidden()}>
      <div class="mb-1 animate-fade-in">
        {/* Label line: 💭 Thought for Xs */}
        <button
          type="button"
          onClick={() => setExpanded((v) => !v)}
          aria-expanded={expanded()}
          style={{
            background: 'none',
            border: 'none',
            padding: '0',
            margin: '0',
            cursor: props.thinking ? 'pointer' : 'default',
            color: 'var(--text-muted)',
            'font-size': '11px',
            'font-style': 'italic',
            'font-family': 'inherit',
            'line-height': '1.4',
            display: 'inline',
          }}
        >
          <span class={props.isStreaming ? 'thinking-shimmer' : ''}>
            {props.isStreaming ? '✦' : '💭'} {labelText()}
          </span>
        </button>

        {/* Preview mode: show first 2 lines */}
        <Show when={isPreview()}>
          <div
            class="mt-1 ml-4 pl-3"
            style={{
              'border-left': '2px solid var(--gray-5, #27272A)',
              color: 'var(--text-muted)',
              'font-style': 'italic',
              'font-size': '12px',
              opacity: '0.6',
              'line-height': '1.5',
              'white-space': 'pre-wrap',
            }}
          >
            {previewLines()}
            <div
              style={{
                'font-size': '10px',
                opacity: '0.5',
                'margin-top': '2px',
              }}
            >
              {'▸ Ctrl+O to see full thinking'}
            </div>
          </div>
        </Show>

        {/* Expanded: full thinking content */}
        <Show when={expanded() && props.thinking}>
          <div
            class={`mt-1 ml-4 pl-3 max-h-[300px] overflow-y-auto scrollbar-thin ${props.isStreaming ? 'thinking-shimmer' : ''}`}
            style={{
              'border-left': '2px solid var(--gray-5, #27272A)',
              color: 'var(--text-muted)',
              'font-style': 'italic',
              'font-size': '12px',
              opacity: '0.6',
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
      </div>
    </Show>
  )
}
