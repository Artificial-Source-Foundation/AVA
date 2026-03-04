/**
 * Markdown Content Component
 *
 * Renders markdown HTML for assistant messages.
 * User messages render as plain text (no parsing).
 *
 * During streaming: renders markdown with throttled updates (~150ms)
 * and streaming-safe fence handling. After streaming: renders once.
 */

import { type Component, createEffect, createSignal, on, onCleanup, Show } from 'solid-js'
import { renderMarkdown, renderMarkdownStreaming } from '../../lib/markdown'
import { useSettings } from '../../stores/settings'

interface MarkdownContentProps {
  content: string
  messageRole: 'user' | 'assistant' | 'system'
  isStreaming: boolean
}

/** Throttle interval for markdown rendering during streaming (ms) */
const STREAM_RENDER_INTERVAL = 150

export const MarkdownContent: Component<MarkdownContentProps> = (props) => {
  let containerRef: HTMLDivElement | undefined
  const [renderedHtml, setRenderedHtml] = createSignal('')
  const { settings } = useSettings()

  // Throttled streaming render — avoids re-parsing markdown on every token
  let streamRenderTimer: ReturnType<typeof setTimeout> | null = null
  let lastRenderedContent = ''

  onCleanup(() => {
    if (streamRenderTimer !== null) clearTimeout(streamRenderTimer)
  })

  // Render markdown when content changes
  createEffect(
    on(
      () => [props.content, props.isStreaming] as const,
      ([content, streaming]) => {
        if (!content || props.messageRole === 'user') {
          setRenderedHtml('')
          return
        }

        if (streaming) {
          // During streaming: throttle rendering to avoid DOM thrashing
          if (streamRenderTimer !== null) return // already scheduled
          streamRenderTimer = setTimeout(() => {
            streamRenderTimer = null
            const current = props.content
            if (current && current !== lastRenderedContent) {
              lastRenderedContent = current
              setRenderedHtml(renderMarkdownStreaming(current))
            }
          }, STREAM_RENDER_INTERVAL)
          // Render immediately on first content
          if (!lastRenderedContent && content) {
            lastRenderedContent = content
            setRenderedHtml(renderMarkdownStreaming(content))
          }
        } else {
          // Completed: full render
          if (streamRenderTimer !== null) {
            clearTimeout(streamRenderTimer)
            streamRenderTimer = null
          }
          lastRenderedContent = ''
          setRenderedHtml(renderMarkdown(content))
        }
      }
    )
  )

  // Apply rendered HTML and attach copy/expand handlers after render
  createEffect(
    on(renderedHtml, () => {
      if (!containerRef) return
      containerRef.innerHTML = renderedHtml()
      queueMicrotask(() => {
        attachCopyHandlers(containerRef)
        attachExpandHandlers(containerRef)
      })
    })
  )

  return (
    <>
      {props.messageRole === 'user' ? (
        /* User messages: plain text */
        <p
          class="whitespace-pre-wrap break-words leading-relaxed"
          style={{ 'font-size': 'var(--chat-font-size)' }}
        >
          {props.content}
        </p>
      ) : (
        /* Assistant messages: rendered markdown (both streaming and completed) */
        <div class="relative">
          <div
            ref={containerRef}
            class="message-content leading-relaxed"
            style={{ 'font-size': 'var(--chat-font-size)' }}
            data-line-numbers={settings().behavior.lineNumbers ? '' : undefined}
            data-word-wrap={settings().behavior.wordWrap ? '' : undefined}
          />
          <Show when={props.isStreaming}>
            <span class="streaming-cursor">▍</span>
          </Show>
        </div>
      )}
    </>
  )
}

// ============================================================================
// Copy Handler
// ============================================================================

const COPY_SVG = `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"/><path d="M5 15H4a2 2 0 01-2-2V4a2 2 0 012-2h9a2 2 0 012 2v1"/></svg>`
const CHECK_SVG = `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><polyline points="20 6 9 17 4 12"/></svg>`

function attachCopyHandlers(container: HTMLElement | undefined) {
  if (!container) return
  const buttons = container.querySelectorAll<HTMLButtonElement>('[data-copy-code]')
  for (const btn of buttons) {
    // Skip if already attached
    if (btn.dataset.copyAttached) continue
    btn.dataset.copyAttached = 'true'

    btn.addEventListener('click', async () => {
      // Find the code element in the sibling pre
      const wrapper = btn.closest('.code-block-wrapper')
      const codeEl = wrapper?.querySelector('pre code')
      if (!codeEl) return

      try {
        await navigator.clipboard.writeText(codeEl.textContent || '')
        const label = btn.querySelector('.code-copy-label')
        const originalText = label?.textContent
        if (label) label.textContent = 'Copied!'
        btn.classList.add('copied')

        // Swap copy icon → check icon
        const svgEl = btn.querySelector('svg')
        if (svgEl) svgEl.outerHTML = CHECK_SVG

        setTimeout(() => {
          if (label) label.textContent = originalText || 'Copy'
          btn.classList.remove('copied')
          // Restore copy icon
          const checkEl = btn.querySelector('svg')
          if (checkEl) checkEl.outerHTML = COPY_SVG
        }, 2000)
      } catch {
        // Clipboard API may fail in some contexts
      }
    })
  }
}

// ============================================================================
// Expand Handler — collapse long code blocks with "Show all" button
// ============================================================================

function attachExpandHandlers(container: HTMLElement | undefined) {
  if (!container) return
  const wrappers = container.querySelectorAll<HTMLElement>('.code-block-wrapper')
  for (const wrapper of wrappers) {
    if (wrapper.dataset.expandAttached) continue
    const pre = wrapper.querySelector('pre')
    if (!pre) continue
    const lineCount = (pre.textContent || '').split('\n').length
    if (lineCount <= 20) continue

    wrapper.dataset.expandAttached = 'true'
    wrapper.classList.add('code-collapsed')

    const expandBtn = document.createElement('button')
    expandBtn.type = 'button'
    expandBtn.className = 'code-expand-btn'
    expandBtn.textContent = `Show all (${lineCount} lines)`
    expandBtn.addEventListener('click', () => {
      wrapper.classList.toggle('code-collapsed')
      expandBtn.textContent = wrapper.classList.contains('code-collapsed')
        ? `Show all (${lineCount} lines)`
        : 'Show less'
    })
    wrapper.appendChild(expandBtn)
  }
}
