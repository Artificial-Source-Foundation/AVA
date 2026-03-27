/**
 * Thinking Row
 *
 * Collapsed <details> pattern matching Goose's design:
 * - Summary: "Thinking..." while streaming, "Thought for Ns" after completion
 * - Collapsed by default for completed messages
 * - Expanded (open) during live streaming
 * - Muted secondary styling with subtle left border
 * - Thinking content rendered as markdown (bold, italic, code, etc.)
 */

import {
  type Component,
  createEffect,
  createMemo,
  createSignal,
  on,
  onCleanup,
  Show,
} from 'solid-js'
import { debugLog } from '../../../lib/debug-log'
import { renderMarkdown, renderMarkdownStreaming } from '../../../lib/markdown'
import { useSettings } from '../../../stores/settings'

interface ThinkingRowProps {
  thinking: string
  isStreaming: boolean
  /** Duration of thinking in seconds (optional) */
  thinkingDuration?: number
}

export const ThinkingRow: Component<ThinkingRowProps> = (props) => {
  let contentRef: HTMLDivElement | undefined
  const { settings } = useSettings()
  const displayMode = (): string => settings().appearance.thinkingDisplay
  const hidden = () => displayMode() === 'hidden'
  const debugState = createMemo(() => ({
    hidden: hidden(),
    thinkingLength: props.thinking?.length,
    isStreaming: props.isStreaming,
    thinkingDisplay: displayMode(),
  }))

  createEffect(() => {
    debugLog('thinking', 'render check:', debugState())
  })

  // Track the timestamp when streaming first started (stable — never changes)
  const [startTime] = createSignal(Date.now())
  // Track the elapsed time at the moment streaming ends (set once, never updated again)
  const [completedDuration, setCompletedDuration] = createSignal<number | null>(null)
  // Whether we've ever been in streaming state (so we know to show duration on completion)
  const [wasStreaming, setWasStreaming] = createSignal(false)

  createEffect(() => {
    if (props.isStreaming) {
      setWasStreaming(true)
    }
  })

  // Rendered markdown HTML for the thinking content
  const [renderedHtml, setRenderedHtml] = createSignal('')

  // Throttled streaming render — avoids re-render of content on every token
  let streamRenderTimer: ReturnType<typeof setTimeout> | null = null
  let pendingContent = ''
  let lastRenderedContent = ''

  onCleanup(() => {
    if (streamRenderTimer !== null) clearTimeout(streamRenderTimer)
  })

  // Render thinking content as markdown (throttled while streaming)
  createEffect(
    on(
      () => [props.thinking, props.isStreaming] as const,
      ([thinking, streaming]) => {
        if (!thinking) {
          setRenderedHtml('')
          return
        }

        if (streaming) {
          pendingContent = thinking

          if (streamRenderTimer !== null) return

          if (!lastRenderedContent && thinking) {
            lastRenderedContent = thinking
            pendingContent = ''
            setRenderedHtml(renderMarkdownStreaming(thinking))
          }

          streamRenderTimer = setTimeout(() => {
            streamRenderTimer = null
            const current = pendingContent || props.thinking
            pendingContent = ''
            if (current && current !== lastRenderedContent) {
              lastRenderedContent = current
              setRenderedHtml(renderMarkdownStreaming(current))
            }
          }, 150)
        } else {
          if (streamRenderTimer !== null) {
            clearTimeout(streamRenderTimer)
            streamRenderTimer = null
          }
          pendingContent = ''
          lastRenderedContent = ''
          setRenderedHtml(renderMarkdown(thinking))
        }
      }
    )
  )

  // Inject rendered HTML into the container div (separate effect so it doesn't trigger re-renders
  // of the outer component — only touches the DOM node directly)
  createEffect(
    on(renderedHtml, () => {
      if (!contentRef) return
      contentRef.innerHTML = renderedHtml()
    })
  )

  // Capture the wall-clock duration exactly once when streaming finishes.
  // Using `on` with defer:false so it fires on first eval too (handles pre-completed thinking).
  createEffect(
    on(
      () => props.isStreaming,
      (streaming) => {
        if (streaming) {
          setWasStreaming(true)
        } else if (wasStreaming() && completedDuration() === null) {
          // Snapshot the elapsed time exactly once when streaming stops
          setCompletedDuration((Date.now() - startTime()) / 1000)
          setWasStreaming(false)
        }
      }
    )
  )

  /**
   * Stable summary text — only two states:
   * - "Thinking..." (while streaming, never changes per-token)
   * - "Thought for Ns" / "Thought" (after completion, computed once from completedDuration)
   *
   * The key insight: while streaming, we NEVER read `Date.now()` inside a reactive memo.
   * That would re-run on every reactive push and cause the <summary> to flicker.
   * Instead, we only show a static label, and capture elapsed time once on completion.
   */
  const summaryText = createMemo(() => {
    if (props.isStreaming) return 'Thinking...'
    // Use the prop-provided duration first (for settled messages from metadata)
    const d = props.thinkingDuration ?? completedDuration() ?? 0
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
          'border-left': '2px solid var(--border-default, rgba(255,255,255,0.1))',
          'padding-left': '10px',
        }}
      >
        {/*
         * The <summary> must NOT read any signal that changes per-token (e.g. props.thinking
         * directly, or Date.now()). summaryText() is stable while streaming — it only ever
         * returns "Thinking..." — so the <summary> DOM node is not touched per-token.
         */}
        <summary
          class={props.isStreaming ? 'thinking-shimmer' : ''}
          style={{
            cursor: 'pointer',
            'font-size': '11px',
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
          {/* biome-ignore lint/a11y/noStaticElementInteractions: prevent details toggle */}
          {/* biome-ignore lint/a11y/useKeyWithClickEvents: text selection area */}
          <div
            class="mt-1 overflow-y-auto scrollbar-thin message-content thinking-content select-text"
            style={{
              color: 'var(--text-secondary)',
              'font-size': '12.5px',
              opacity: '0.8',
              'line-height': '1.6',
              cursor: 'text',
            }}
            onClick={(e) => e.stopPropagation()}
          >
            <div ref={contentRef} class="message-content" />
            <Show when={props.isStreaming}>
              <span class="streaming-cursor">▍</span>
            </Show>
          </div>
        </Show>
      </details>
    </Show>
  )
}
