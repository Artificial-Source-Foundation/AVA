/**
 * Message Actions Component
 *
 * Hover actions: copy, edit (user), regenerate (assistant), delete/rollback.
 * Premium floating toolbar with smooth transitions.
 *
 * When readOnly is set, only Copy is shown.
 */

import { Check, Copy, GitFork, Pencil, RefreshCw, Trash2, Undo2 } from 'lucide-solid'
import { type Component, createSignal, Show } from 'solid-js'
import { useNotification } from '../../contexts/notification'
import type { Message } from '../../types'

interface MessageActionsProps {
  message: Message
  isLastMessage: boolean
  readOnly?: boolean
  onEdit: () => void
  onRegenerate: () => void
  onCopy: () => void
  onDelete: () => void
  onBranch: () => void
  onRewind: () => void
  isLoading: boolean
  /** Whether branching at a message is supported in the current environment.
   * If false, the branch button is hidden. Defaults to true for backward compatibility. */
  canBranch?: boolean
}

const btnClass = `
  p-1.5
  rounded-[var(--radius-sm)]
  text-[var(--text-tertiary)]
  hover:text-[var(--text-primary)]
  hover:bg-[var(--surface-raised)]
  transition-colors duration-[var(--duration-fast)]
  disabled:opacity-50 disabled:cursor-not-allowed
`

export const MessageActions: Component<MessageActionsProps> = (props) => {
  const [copied, setCopied] = createSignal(false)
  const { success } = useNotification()

  const handleCopy = async (e: MouseEvent) => {
    e.stopPropagation()
    e.preventDefault()
    try {
      const plainText = props.message.content
      // Feature: Rich clipboard copy — write both HTML (preserves formatting) and plain text
      if (typeof ClipboardItem !== 'undefined' && navigator.clipboard.write) {
        try {
          // Render markdown to HTML for rich copy
          const { renderMarkdown } = await import('../../lib/markdown')
          const htmlContent =
            props.message.role === 'assistant' ? renderMarkdown(plainText) : plainText
          const htmlBlob = new Blob([htmlContent], { type: 'text/html' })
          const textBlob = new Blob([plainText], { type: 'text/plain' })
          await navigator.clipboard.write([
            new ClipboardItem({ 'text/html': htmlBlob, 'text/plain': textBlob }),
          ])
        } catch {
          // Fall back to plain text if ClipboardItem fails
          await navigator.clipboard.writeText(plainText)
        }
      } else {
        await navigator.clipboard.writeText(plainText)
      }
      setCopied(true)
      success('Copied to clipboard')
      props.onCopy()
      setTimeout(() => setCopied(false), 2000)
    } catch {
      // Clipboard API may fail in some contexts
    }
  }

  /**
   * Wrap an action handler to stop propagation.
   *
   * Message rows use `content-visibility: auto` for virtual-scrolling
   * performance.  That CSS property implies `contain: layout style paint`
   * which creates a new stacking context.  SolidJS's delegated `onClick`
   * (registered on the document root) can mis-route the event when the
   * target lives inside a paint-contained subtree, causing toolbar clicks
   * to accidentally trigger unrelated global shortcut actions.
   *
   * Using the non-delegated `on:click` binding (see JSX below) attaches
   * the handler directly on the DOM element, bypassing delegation
   * entirely.  We also call `stopPropagation` + `preventDefault` so the
   * event never reaches other document-level listeners.
   */
  const runAction = (e: MouseEvent, fn: () => void) => {
    e.stopPropagation()
    e.preventDefault()
    fn()
  }

  return (
    <div
      class="
        absolute -top-3 right-0 z-10
        opacity-0 pointer-events-none group-hover:opacity-100 group-hover:pointer-events-auto focus-within:opacity-100 focus-within:pointer-events-auto
        transition-opacity duration-[var(--duration-fast)]
        flex gap-0.5
        p-0.5
      "
      style={{
        background: 'var(--dropdown-surface)',
        border: '1px solid var(--dropdown-border)',
        'border-radius': 'var(--dropdown-radius)',
        'box-shadow': 'var(--modal-shadow)',
      }}
      role="toolbar"
      aria-label="Message actions"
      on:click={(e: MouseEvent) => e.stopPropagation()}
    >
      {/* Copy button — always visible, even in readOnly */}
      <button
        type="button"
        on:click={handleCopy}
        class={btnClass}
        title={copied() ? 'Copied!' : 'Copy message'}
        aria-label={copied() ? 'Copied' : 'Copy message'}
      >
        <Show when={copied()} fallback={<Copy class="w-3.5 h-3.5" />}>
          <Check class="w-3.5 h-3.5 text-[var(--success)]" />
        </Show>
      </button>

      {/* Mutation actions — hidden in readOnly mode */}
      <Show when={!props.readOnly}>
        {/* Edit button for user messages */}
        <Show when={props.message.role === 'user'}>
          <button
            type="button"
            on:click={(e: MouseEvent) => runAction(e, props.onEdit)}
            disabled={props.isLoading}
            class={btnClass}
            title="Edit message"
            aria-label="Edit message"
          >
            <Pencil class="w-3.5 h-3.5" />
          </button>
        </Show>

        {/* Regenerate button for assistant messages (without errors) */}
        <Show when={props.message.role === 'assistant' && !props.message.error}>
          <button
            type="button"
            on:click={(e: MouseEvent) => runAction(e, props.onRegenerate)}
            disabled={props.isLoading}
            class={btnClass}
            title="Regenerate response"
            aria-label="Regenerate response"
          >
            <RefreshCw class="w-3.5 h-3.5" />
          </button>
        </Show>

        {/* Branch button — all messages (hidden when not supported, e.g., web mode) */}
        <Show when={props.canBranch !== false}>
          <button
            type="button"
            on:click={(e: MouseEvent) => runAction(e, props.onBranch)}
            disabled={props.isLoading}
            class={btnClass}
            title="Branch conversation here"
            aria-label="Branch conversation here"
          >
            <GitFork class="w-3.5 h-3.5" />
          </button>
        </Show>

        {/* Rewind button — non-last messages only */}
        <Show when={!props.isLastMessage}>
          <button
            type="button"
            on:click={(e: MouseEvent) => runAction(e, props.onRewind)}
            disabled={props.isLoading}
            class={btnClass}
            title="Rewind to here"
            aria-label="Rewind conversation to this point"
          >
            <Undo2 class="w-3.5 h-3.5" />
          </button>
        </Show>

        {/* Delete / Rollback button — all messages */}
        <button
          type="button"
          on:click={(e: MouseEvent) => runAction(e, props.onDelete)}
          disabled={props.isLoading}
          class={`${btnClass} hover:text-[var(--error)]`}
          title={props.isLastMessage ? 'Delete message' : 'Delete message and rollback'}
          aria-label={props.isLastMessage ? 'Delete message' : 'Delete and rollback'}
        >
          <Trash2 class="w-3.5 h-3.5" />
        </button>
      </Show>
    </div>
  )
}
