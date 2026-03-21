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

  // Rendered markdown HTML for the thinking content
  const [renderedHtml, setRenderedHtml] = createSignal('')

  // Throttled streaming render
  let streamRenderTimer: ReturnType<typeof setTimeout> | null = null
  let pendingContent = ''
  let lastRenderedContent = ''

  onCleanup(() => {
    if (streamRenderTimer !== null) clearTimeout(streamRenderTimer)
  })

  // Render thinking content as markdown
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

  // Inject rendered HTML into the container div
  createEffect(
    on(renderedHtml, () => {
      if (!contentRef) return
      contentRef.innerHTML = renderedHtml()
    })
  )

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
            class={`scroll-fade-mask mt-1 max-h-[300px] overflow-y-auto scrollbar-thin thinking-content`}
            style={{
              color: 'var(--text-tertiary, var(--text-muted))',
              'font-size': '12px',
              opacity: '0.7',
              'line-height': '1.5',
            }}
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
