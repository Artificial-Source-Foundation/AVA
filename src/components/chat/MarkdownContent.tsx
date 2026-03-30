/**
 * Markdown Content Component
 *
 * Renders markdown HTML for assistant messages.
 * User messages render as plain text (no parsing).
 *
 * During streaming: renders markdown with throttled updates (~150ms)
 * and streaming-safe fence handling. After streaming: renders once.
 *
 * Security: markdown.ts configures markdown-it with html:false (raw HTML
 * pass-through disabled) and runs DOMPurify on all rendered output.
 * Additionally, sanitizeModelOutput() below strips dangerous HTML tags and
 * event handler attributes from model output before it reaches the renderer,
 * providing defense-in-depth against XSS from LLM-generated content.
 */

import { type Component, createEffect, createSignal, on, onCleanup, Show } from 'solid-js'
import { renderMarkdown, renderMarkdownStreaming } from '../../lib/markdown'
import { useSettings } from '../../stores/settings'

// ============================================================================
// Input sanitization (defense-in-depth before markdown rendering)
// ============================================================================

/**
 * Strip dangerous HTML tags and inline event handlers from raw model output
 * before passing it to the markdown renderer. The markdown renderer already
 * uses DOMPurify; this is a belt-and-suspenders pre-sanitization step.
 *
 * Targets the most common XSS vectors in model-generated content:
 *   <script>, <iframe>, <object>, <embed>, <form>, on* attributes
 */
function sanitizeModelOutput(content: string): string {
  return (
    content
      // Remove dangerous block-level tags and their content
      .replace(/<script\b[^>]*>[\s\S]*?<\/script>/gi, '')
      .replace(/<iframe\b[^>]*>[\s\S]*?<\/iframe>/gi, '')
      .replace(/<object\b[^>]*>[\s\S]*?<\/object>/gi, '')
      .replace(/<embed\b[^>]*\/?>/gi, '')
      .replace(/<form\b[^>]*>[\s\S]*?<\/form>/gi, '')
      // Strip inline event handler attributes (onclick=, onerror=, etc.)
      .replace(/\bon\w+\s*=\s*(?:"[^"]*"|'[^']*'|[^\s>]*)/gi, '')
  )
}

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
  // Track the latest content that needs rendering so the timer always picks
  // up the most recent value even if multiple tokens arrived while it was
  // pending. This prevents the final streamed token from being dropped when
  // a new token arrives but is silently skipped by the early-return guard.
  let pendingContent = ''
  let lastRenderedContent = ''

  const needsPostRenderEnhancements = (html: string): boolean =>
    html.includes('code-block-wrapper') ||
    html.includes('data-copy-code') ||
    html.includes('data-apply-code')

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

        // Pre-sanitize model output before passing to the markdown renderer
        const safeContent = sanitizeModelOutput(content)

        if (streaming) {
          // Always record the latest content so the timer can flush it
          pendingContent = safeContent

          // During streaming: throttle rendering to avoid DOM thrashing
          if (streamRenderTimer !== null) return // already scheduled; timer will pick up pendingContent

          // Render immediately on first content so there's no blank flash
          if (!lastRenderedContent && safeContent) {
            lastRenderedContent = safeContent
            pendingContent = ''
            setRenderedHtml(renderMarkdownStreaming(safeContent))
          }

          streamRenderTimer = setTimeout(() => {
            streamRenderTimer = null
            // Use pendingContent (latest) rather than a stale closure value
            const current = pendingContent || sanitizeModelOutput(props.content)
            pendingContent = ''
            if (current && current !== lastRenderedContent) {
              lastRenderedContent = current
              setRenderedHtml(renderMarkdownStreaming(current))
            }
          }, STREAM_RENDER_INTERVAL)
        } else {
          // Completed: cancel any pending throttle and do a final full render
          if (streamRenderTimer !== null) {
            clearTimeout(streamRenderTimer)
            streamRenderTimer = null
          }
          pendingContent = ''
          lastRenderedContent = ''
          setRenderedHtml(renderMarkdown(safeContent))
        }
      }
    )
  )

  // Apply rendered HTML and attach copy/expand/apply handlers after render
  createEffect(
    on(renderedHtml, () => {
      if (!containerRef) return
      const html = renderedHtml()
      containerRef.innerHTML = html
      if (props.isStreaming || !needsPostRenderEnhancements(html)) return
      queueMicrotask(() => {
        attachCopyHandlers(containerRef)
        attachExpandHandlers(containerRef)
        attachApplyHandlers(containerRef)
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
            class="message-content markdown-render-surface leading-relaxed"
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
// Apply Handler — write code block content to a file path
// ============================================================================

function attachApplyHandlers(container: HTMLElement | undefined) {
  if (!container) return
  const buttons = container.querySelectorAll<HTMLButtonElement>('[data-apply-code]')
  for (const btn of buttons) {
    if (btn.dataset.applyAttached) continue
    btn.dataset.applyAttached = 'true'

    btn.addEventListener('click', async () => {
      const wrapper = btn.closest('.code-block-wrapper')
      const codeEl = wrapper?.querySelector('pre code')
      if (!codeEl) return

      const filePath = btn.dataset.filePath || ''
      const content = codeEl.textContent || ''

      // Dispatch an event so the parent app can handle the file write
      // (avoids needing to import Tauri invoke here)
      window.dispatchEvent(new CustomEvent('ava:apply-code', { detail: { filePath, content } }))

      const label = btn.querySelector('.code-apply-label')
      if (label) label.textContent = 'Applied!'
      btn.classList.add('applied')
      setTimeout(() => {
        if (label) label.textContent = 'Apply'
        btn.classList.remove('applied')
      }, 2000)
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
