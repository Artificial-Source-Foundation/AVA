/**
 * Thinking Row
 *
 * Minimal reasoning display inspired by OpenCode/Goose:
 * - Collapsed: "💭 Thought for Xs" clickable badge
 * - Expanded: dimmed italic content with left border
 * - Auto-expands during streaming, auto-collapses when complete
 * - No copy button (thinking is internal, not for copying)
 * - Shimmer animation while streaming
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
  // Only use the appearance setting (Settings -> Appearance -> Thinking Display)
  const hidden = () => settings().appearance.thinkingDisplay === 'hidden'
  debugLog('thinking', 'render check:', {
    hidden: hidden(),
    thinkingLength: props.thinking?.length,
    isStreaming: props.isStreaming,
    thinkingDisplay: settings().appearance.thinkingDisplay,
  })
  const [expanded, setExpanded] = createSignal(false)
  const [wasStreaming, setWasStreaming] = createSignal(false)
  const [startTime] = createSignal(Date.now())

  const lineCount = createMemo(() => (props.thinking || '').split('\n').filter(Boolean).length)

  const duration = createMemo(() => {
    if (props.thinkingDuration) return props.thinkingDuration
    if (!props.isStreaming) return (Date.now() - startTime()) / 1000
    return 0
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

  const badgeText = createMemo(() => {
    if (props.isStreaming && !props.thinking) return 'Thinking...'
    if (props.isStreaming) return 'Thinking...'
    const d = duration()
    const lines = lineCount()
    if (d > 0.5) return `Thought for ${d.toFixed(1)}s`
    if (lines > 0) return `Thought (${lines} line${lines !== 1 ? 's' : ''})`
    return 'Thought'
  })

  return (
    <Show when={!hidden()}>
      <div class="mb-2 animate-fade-in">
        {/* Collapsed: minimal badge */}
        <button
          type="button"
          onClick={() => setExpanded((v) => !v)}
          aria-expanded={expanded()}
          class="inline-flex items-center gap-1.5 px-2 py-0.5 rounded-full text-[11px] transition-all cursor-pointer select-none"
          style={{
            background: expanded() ? 'var(--alpha-white-5)' : 'transparent',
            color: 'var(--text-muted)',
            border: 'none',
            font: 'inherit',
          }}
        >
          <span style={{ 'font-size': '12px', opacity: '0.7' }}>
            {props.isStreaming ? '✦' : '💭'}
          </span>
          <span
            style={{ 'font-style': 'italic' }}
            class={props.isStreaming ? 'thinking-shimmer' : ''}
          >
            {badgeText()}
          </span>
          <Show when={!props.isStreaming && props.thinking}>
            <span style={{ opacity: '0.4', 'font-size': '10px' }}>{expanded() ? '▾' : '▸'}</span>
          </Show>
        </button>

        {/* Expanded: thinking content */}
        <Show when={expanded() && props.thinking}>
          <div
            class={`mt-1.5 ml-2 pl-3 border-l-2 border-[var(--border-subtle)] whitespace-pre-wrap leading-relaxed max-h-[300px] overflow-y-auto scrollbar-thin ${props.isStreaming ? 'thinking-shimmer' : ''}`}
            style={{
              color: 'var(--text-muted)',
              'font-style': 'italic',
              'font-size': '12px',
              opacity: '0.6',
              'line-height': '1.5',
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
