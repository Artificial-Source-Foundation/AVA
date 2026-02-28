/**
 * Message Actions Component
 *
 * Hover actions: copy, edit (user), regenerate (assistant), delete/rollback.
 * Premium floating toolbar with smooth transitions.
 */

import { Check, Copy, GitFork, Pencil, RefreshCw, Trash2, Undo2 } from 'lucide-solid'
import { type Component, createSignal, Show } from 'solid-js'
import { useNotification } from '../../contexts/notification'
import type { Message } from '../../types'

interface MessageActionsProps {
  message: Message
  isLastMessage: boolean
  onEdit: () => void
  onRegenerate: () => void
  onCopy: () => void
  onDelete: () => void
  onBranch: () => void
  onRewind: () => void
  isLoading: boolean
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

  const handleCopy = async () => {
    try {
      await navigator.clipboard.writeText(props.message.content)
      setCopied(true)
      success('Copied to clipboard')
      props.onCopy()
      setTimeout(() => setCopied(false), 2000)
    } catch {
      // Clipboard API may fail in some contexts
    }
  }

  return (
    <div
      class="
        absolute -top-3 right-0
        opacity-0 group-hover:opacity-100
        transition-opacity duration-[var(--duration-fast)]
        flex gap-0.5
        bg-[var(--surface-overlay)]
        border border-[var(--border-subtle)]
        rounded-[var(--radius-md)]
        p-0.5
        shadow-md
      "
    >
      {/* Copy button — all messages */}
      <button
        type="button"
        onClick={handleCopy}
        class={btnClass}
        title={copied() ? 'Copied!' : 'Copy message'}
        aria-label={copied() ? 'Copied' : 'Copy message'}
      >
        <Show when={copied()} fallback={<Copy class="w-3.5 h-3.5" />}>
          <Check class="w-3.5 h-3.5 text-[var(--success)]" />
        </Show>
      </button>

      {/* Edit button for user messages */}
      <Show when={props.message.role === 'user'}>
        <button
          type="button"
          onClick={() => props.onEdit()}
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
          onClick={() => props.onRegenerate()}
          disabled={props.isLoading}
          class={btnClass}
          title="Regenerate response"
          aria-label="Regenerate response"
        >
          <RefreshCw class="w-3.5 h-3.5" />
        </button>
      </Show>

      {/* Branch button — all messages */}
      <button
        type="button"
        onClick={() => props.onBranch()}
        disabled={props.isLoading}
        class={btnClass}
        title="Branch conversation here"
        aria-label="Branch conversation here"
      >
        <GitFork class="w-3.5 h-3.5" />
      </button>

      {/* Rewind button — non-last messages only */}
      <Show when={!props.isLastMessage}>
        <button
          type="button"
          onClick={() => props.onRewind()}
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
        onClick={() => props.onDelete()}
        disabled={props.isLoading}
        class={`${btnClass} hover:text-[var(--error)]`}
        title={props.isLastMessage ? 'Delete message' : 'Delete message and rollback'}
        aria-label={props.isLastMessage ? 'Delete message' : 'Delete and rollback'}
      >
        <Trash2 class="w-3.5 h-3.5" />
      </button>
    </div>
  )
}
