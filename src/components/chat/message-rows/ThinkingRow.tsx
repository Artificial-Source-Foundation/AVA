/**
 * Thinking Row
 *
 * Purple-accented card matching the Pencil "Tool States" design:
 *
 * Expanded: rounded-10, fill #0F0F12, border 1px #5E5CE620
 *   Header: 36px, fill #5E5CE608, brain icon (13px #5E5CE6),
 *           "Thinking" label (Geist 12px, weight 500, #C8C8CC),
 *           effort badge pill (rounded-4, fill #5E5CE615, Geist Mono 9px #5E5CE6)
 *   Body: padding 10px 12px, italic Geist 12px #86868B, line-height 1.5
 *
 * Collapsed: single 36px row, brain icon + "Thinking (Ns)" + chevron-right
 */

import { Brain, ChevronRight } from 'lucide-solid'
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
import type { ThinkingDisplay } from '../../../stores/settings/settings-types'

interface ThinkingRowProps {
  thinking: string
  isStreaming: boolean
  /** Duration of thinking in seconds (optional) */
  thinkingDuration?: number
  /** Effort level for the thinking badge (optional) */
  effortLevel?: 'low' | 'medium' | 'high'
}

export const ThinkingRow: Component<ThinkingRowProps> = (props) => {
  let contentRef: HTMLDivElement | undefined
  const { settings } = useSettings()
  const displayMode = (): ThinkingDisplay => settings().appearance.thinkingDisplay
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

  // Track the timestamp when streaming first started (stable -- never changes)
  const [startTime, setStartTime] = createSignal(Date.now())
  // Track the elapsed time at the moment streaming ends (set once, never updated again)
  const [completedDuration, setCompletedDuration] = createSignal<number | null>(null)
  // Whether we've ever been in streaming state (so we know to show duration on completion)
  const [wasStreaming, setWasStreaming] = createSignal(false)

  // Rendered markdown HTML for the thinking content
  const [renderedHtml, setRenderedHtml] = createSignal('')

  // Throttled streaming render -- avoids re-render of content on every token
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

  // Inject rendered HTML into the container div
  createEffect(
    on(renderedHtml, () => {
      if (!contentRef) return
      contentRef.innerHTML = renderedHtml()
    })
  )

  // Capture the wall-clock duration exactly once when streaming finishes.
  createEffect(
    on(
      () => props.isStreaming,
      (streaming) => {
        if (streaming) {
          setStartTime(Date.now())
          setWasStreaming(true)
        } else if (wasStreaming() && completedDuration() === null) {
          setCompletedDuration((Date.now() - startTime()) / 1000)
          setWasStreaming(false)
        }
      }
    )
  )

  /**
   * Stable duration for collapsed label.
   */
  const durationSeconds = createMemo(() => {
    if (props.isStreaming) return 0
    return props.thinkingDuration ?? completedDuration() ?? 0
  })

  const durationLabel = createMemo(() => {
    const d = durationSeconds()
    if (d > 0.5) return `${Math.round(d)} seconds`
    return null
  })

  /** Effort badge label */
  const effortLabel = createMemo(() => {
    if (!props.effortLevel) return null
    return props.effortLevel.charAt(0).toUpperCase() + props.effortLevel.slice(1)
  })

  // Expanded during streaming, collapsed when done
  const [isOpen, setIsOpen] = createSignal(false)

  createEffect(
    on(
      () => props.isStreaming,
      (streaming) => {
        setIsOpen(streaming)
      }
    )
  )

  return (
    <Show when={!hidden()}>
      <div class="animate-fade-in my-1">
        <Show
          when={isOpen()}
          fallback={
            /* ── Collapsed: single 36px row ── */
            /* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */
            <div
              role="button"
              tabIndex={0}
              class="thinking-surface flex h-9 cursor-pointer select-none items-center justify-between rounded-[10px] px-3 transition-colors hover:bg-[var(--thinking-subtle)]"
              onClick={() => setIsOpen(true)}
              onKeyDown={(e) => {
                if (e.key === 'Enter' || e.key === ' ') {
                  e.preventDefault()
                  setIsOpen(true)
                }
              }}
            >
              <div class="flex items-center gap-1.5">
                <Brain
                  class="flex-shrink-0"
                  style={{ width: '13px', height: '13px', color: 'var(--thinking-accent)' }}
                />
                <span
                  style={{
                    'font-family': 'var(--font-ui), Geist, sans-serif',
                    'font-size': '12px',
                    'font-weight': '500',
                    color: 'var(--text-tertiary)',
                  }}
                >
                  Thinking{durationLabel() ? ` (${durationLabel()})` : ''}
                </span>
              </div>
              <ChevronRight style={{ width: '14px', height: '14px', color: 'var(--text-muted)' }} />
            </div>
          }
        >
          {/* ── Expanded: purple card ── */}
          <div class="thinking-surface overflow-hidden rounded-[10px]">
            {/* Header: 36px */}
            {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
            <div
              role="button"
              tabIndex={0}
              class="thinking-surface-header flex h-9 cursor-pointer select-none items-center justify-between px-3"
              onClick={() => {
                if (!props.isStreaming) setIsOpen(false)
              }}
              onKeyDown={(e) => {
                if ((e.key === 'Enter' || e.key === ' ') && !props.isStreaming) {
                  e.preventDefault()
                  setIsOpen(false)
                }
              }}
            >
              <div class="flex items-center gap-1.5">
                <Brain
                  class="flex-shrink-0"
                  style={{ width: '13px', height: '13px', color: 'var(--thinking-accent)' }}
                />
                <span
                  style={{
                    'font-family': 'var(--font-ui), Geist, sans-serif',
                    'font-size': '12px',
                    'font-weight': '500',
                    color: 'var(--text-secondary)',
                  }}
                >
                  Thinking
                </span>
              </div>

              <div class="flex items-center gap-2">
                {/* Effort badge */}
                <Show when={effortLabel()}>
                  <span
                    class="thinking-badge inline-flex items-center"
                    style={{
                      'border-radius': '4px',
                      padding: '2px 6px',
                      'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                      'font-size': '9px',
                      'font-weight': '500',
                    }}
                  >
                    {effortLabel()}
                  </span>
                </Show>

                {/* Streaming indicator */}
                <Show when={props.isStreaming}>
                  <span
                    class="inline-block w-1.5 h-1.5 rounded-full animate-pulse"
                    style={{ background: 'var(--thinking-accent)' }}
                  />
                </Show>
              </div>
            </div>

            {/* Body: thinking content */}
            <Show when={props.thinking}>
              {/* biome-ignore lint/a11y/noStaticElementInteractions: prevent details toggle */}
              {/* biome-ignore lint/a11y/useKeyWithClickEvents: text selection area */}
              <div
                class="overflow-y-auto scrollbar-thin select-text"
                style={{ padding: '10px 12px', 'max-height': '320px' }}
                onClick={(e) => e.stopPropagation()}
              >
                <div
                  ref={contentRef}
                  class="message-content thinking-content"
                  style={{
                    color: 'var(--text-tertiary)',
                    'font-family': 'var(--font-ui), Geist, sans-serif',
                    'font-size': '12px',
                    'font-style': 'italic',
                    'line-height': '1.5',
                  }}
                />
                <Show when={props.isStreaming}>
                  <span class="streaming-cursor">&#9613;</span>
                </Show>
              </div>
            </Show>
          </div>
        </Show>
      </div>
    </Show>
  )
}
