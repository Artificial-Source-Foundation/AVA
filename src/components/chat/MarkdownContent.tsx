/**
 * Markdown Content Component
 *
 * Renders markdown HTML for assistant messages.
 * User messages render as plain text (no parsing).
 * Handles streaming with debounced rendering and
 * attaches copy handlers to code blocks.
 */

import { type Component, createEffect, createSignal, on, onCleanup } from 'solid-js'
import { renderMarkdown, renderMarkdownStreaming } from '../../lib/markdown'
import { useSettings } from '../../stores/settings'

interface MarkdownContentProps {
  content: string
  role: 'user' | 'assistant' | 'system'
  isStreaming: boolean
}

export const MarkdownContent: Component<MarkdownContentProps> = (props) => {
  let containerRef: HTMLDivElement | undefined
  const [renderedHtml, setRenderedHtml] = createSignal('')
  const { settings } = useSettings()
  let rafId: number | null = null

  // Render markdown when content changes
  createEffect(
    on(
      () => props.content,
      (content) => {
        if (!content || props.role === 'user') {
          setRenderedHtml('')
          return
        }

        if (props.isStreaming) {
          // Debounce rendering during streaming to avoid jank
          if (rafId !== null) cancelAnimationFrame(rafId)
          rafId = requestAnimationFrame(() => {
            setRenderedHtml(renderMarkdownStreaming(content))
            rafId = null
          })
        } else {
          // Immediate render for completed messages
          if (rafId !== null) {
            cancelAnimationFrame(rafId)
            rafId = null
          }
          setRenderedHtml(renderMarkdown(content))
        }
      }
    )
  )

  onCleanup(() => {
    if (rafId !== null) cancelAnimationFrame(rafId)
  })

  // Attach code block copy handlers after render
  createEffect(
    on(renderedHtml, () => {
      if (!containerRef) return
      // Wait for DOM update
      queueMicrotask(() => attachCopyHandlers(containerRef!))
    })
  )

  return (
    <>
      {/* User messages: plain text */}
      {props.role === 'user' ? (
        <p
          class="whitespace-pre-wrap break-words leading-relaxed"
          style={{ 'font-size': 'var(--chat-font-size)' }}
        >
          {props.content}
        </p>
      ) : (
        /* Assistant messages: rendered markdown */
        <div
          ref={containerRef}
          class="message-content leading-relaxed"
          style={{ 'font-size': 'var(--chat-font-size)' }}
          data-line-numbers={settings().behavior.lineNumbers ? '' : undefined}
          data-word-wrap={settings().behavior.wordWrap ? '' : undefined}
          innerHTML={renderedHtml()}
        />
      )}
    </>
  )
}

// ============================================================================
// Copy Handler
// ============================================================================

function attachCopyHandlers(container: HTMLElement) {
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

        setTimeout(() => {
          if (label) label.textContent = originalText || 'Copy'
          btn.classList.remove('copied')
        }, 2000)
      } catch {
        // Clipboard API may fail in some contexts
      }
    })
  }
}
