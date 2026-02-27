/**
 * Markdown Content Component
 *
 * Renders markdown HTML for assistant messages.
 * User messages render as plain text (no parsing).
 * Handles streaming with debounced rendering and
 * attaches copy handlers to code blocks.
 */

import { type Component, createEffect, createSignal, on } from 'solid-js'
import { renderMarkdown } from '../../lib/markdown'
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

  // Render markdown when content changes or streaming ends
  createEffect(
    on(
      () => [props.content, props.isStreaming] as const,
      ([content, streaming]) => {
        if (!content || props.role === 'user' || streaming) {
          setRenderedHtml('')
          return
        }

        setRenderedHtml(renderMarkdown(content))
      }
    )
  )

  // Apply rendered HTML and attach copy handlers after render
  createEffect(
    on(renderedHtml, () => {
      if (!containerRef) return
      containerRef.innerHTML = renderedHtml()
      queueMicrotask(() => attachCopyHandlers(containerRef))
    })
  )

  return (
    <>
      {/* User messages: plain text */}
      {props.role === 'user' || props.isStreaming ? (
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
