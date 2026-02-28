/**
 * Expanded Editor (Ctrl+E)
 *
 * Full-screen modal textarea for composing long prompts.
 * Ctrl+Enter to apply, Esc to cancel.
 */

import { Dialog } from '@kobalte/core/dialog'
import { type Component, createEffect, createSignal, on } from 'solid-js'

interface ExpandedEditorProps {
  open: boolean
  initialText: string
  onApply: (text: string) => void
  onClose: () => void
}

export const ExpandedEditor: Component<ExpandedEditorProps> = (props) => {
  let textareaRef: HTMLTextAreaElement | undefined
  const [text, setText] = createSignal('')

  createEffect(
    on(
      () => props.open,
      (open) => {
        if (open) {
          setText(props.initialText)
          requestAnimationFrame(() => {
            textareaRef?.focus()
            const len = props.initialText.length
            textareaRef?.setSelectionRange(len, len)
          })
        }
      }
    )
  )

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
      e.preventDefault()
      props.onApply(text())
    }
  }

  return (
    <Dialog open={props.open} onOpenChange={(open) => !open && props.onClose()}>
      <Dialog.Portal>
        <Dialog.Overlay class="fixed inset-0 z-[var(--z-modal)] bg-black/40" />
        <Dialog.Content
          class="
            fixed z-[var(--z-modal)]
            top-[10%] left-1/2 -translate-x-1/2
            w-[min(720px,90vw)]
            max-h-[70vh]
            bg-[var(--surface-overlay)] border border-[var(--border-default)]
            rounded-[var(--radius-xl)] shadow-[var(--shadow-xl)]
            flex flex-col overflow-hidden
          "
        >
          <div class="px-4 py-2 border-b border-[var(--border-subtle)] flex items-center justify-between flex-shrink-0">
            <span class="text-xs font-semibold text-[var(--text-secondary)]">Expanded Editor</span>
            <span class="text-[9px] text-[var(--text-muted)]">
              <kbd class="font-mono">Ctrl+Enter</kbd> to apply
            </span>
          </div>

          <textarea
            ref={textareaRef}
            value={text()}
            onInput={(e) => setText(e.currentTarget.value)}
            onKeyDown={handleKeyDown}
            class="
              flex-1 w-full p-4 bg-transparent resize-none
              text-sm text-[var(--text-primary)]
              font-mono leading-relaxed
              placeholder:text-[var(--text-muted)]
              focus:outline-none
              min-h-[300px]
            "
            placeholder="Write your prompt..."
          />

          <div class="px-4 py-2 border-t border-[var(--border-subtle)] flex items-center justify-between flex-shrink-0">
            <span class="text-[9px] text-[var(--text-muted)]">
              <kbd class="font-mono">Esc</kbd> cancel
            </span>
            <button
              type="button"
              onClick={() => props.onApply(text())}
              class="px-3 py-1 text-xs bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:bg-[var(--accent-hover)] transition-colors"
            >
              Apply
            </button>
          </div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog>
  )
}
