/**
 * Message Input Component
 *
 * Captures user input and sends messages via useChat hook.
 * Premium input design with themed styling.
 */

import { AlertCircle, Send, Square, X, Zap } from 'lucide-solid'
import { type Component, createSignal, Show } from 'solid-js'
import { useChat } from '../../hooks/useChat'

export const MessageInput: Component = () => {
  const [input, setInput] = createSignal('')
  const { sendMessage, isStreaming, error, currentProvider, cancel, clearError } = useChat()

  const handleSubmit = async (e: Event) => {
    e.preventDefault()
    const message = input().trim()
    if (!message || isStreaming()) return

    setInput('')
    clearError()
    await sendMessage(message)
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault()
      handleSubmit(e)
    }
  }

  return (
    <form
      onSubmit={handleSubmit}
      class="
        px-6 py-4
        border-t border-[var(--border-subtle)]
        bg-[var(--surface-base)]
        transition-colors duration-[var(--duration-normal)]
      "
    >
      {/* Error display */}
      <Show when={error()}>
        <div
          class="
            mb-3 p-3
            bg-[var(--error-subtle)]
            border border-[var(--error-muted)]
            rounded-[var(--radius-lg)]
            flex items-center justify-between gap-3
          "
        >
          <div class="flex items-center gap-2 text-sm text-[var(--error)]">
            <AlertCircle class="w-4 h-4 flex-shrink-0" />
            <span>{error()!.message}</span>
          </div>
          <button
            type="button"
            onClick={clearError}
            class="
              p-1 rounded-[var(--radius-md)]
              text-[var(--error)] hover:text-[var(--error-hover)]
              hover:bg-[var(--error-muted)]
              transition-colors duration-[var(--duration-fast)]
            "
          >
            <X class="w-4 h-4" />
          </button>
        </div>
      </Show>

      <div class="flex gap-3">
        <input
          type="text"
          value={input()}
          onInput={(e) => setInput(e.currentTarget.value)}
          onKeyDown={handleKeyDown}
          placeholder={isStreaming() ? 'Thinking...' : 'Type a message...'}
          disabled={isStreaming()}
          class="
            flex-1 px-4 py-3
            bg-[var(--input-background)]
            text-[var(--text-primary)]
            placeholder-[var(--text-muted)]
            border border-[var(--input-border)]
            rounded-[var(--radius-lg)]
            text-sm
            transition-all duration-[var(--duration-fast)]
            focus:outline-none focus:border-[var(--input-border-focus)] focus:ring-2 focus:ring-[var(--accent-subtle)]
            disabled:opacity-50 disabled:cursor-not-allowed
          "
        />

        <Show
          when={isStreaming()}
          fallback={
            <button
              type="submit"
              disabled={!input().trim()}
              class="
                px-5 py-3
                bg-[var(--accent)] hover:bg-[var(--accent-hover)]
                text-white font-medium text-sm
                rounded-[var(--radius-lg)]
                transition-all duration-[var(--duration-fast)]
                focus:outline-none focus:ring-2 focus:ring-[var(--accent-subtle)]
                disabled:opacity-50 disabled:cursor-not-allowed
                flex items-center gap-2
                active:scale-[0.98]
              "
            >
              <Send class="w-4 h-4" />
              Send
            </button>
          }
        >
          <button
            type="button"
            onClick={cancel}
            class="
              px-5 py-3
              bg-[var(--error)] hover:bg-[var(--error-hover)]
              text-white font-medium text-sm
              rounded-[var(--radius-lg)]
              transition-all duration-[var(--duration-fast)]
              focus:outline-none focus:ring-2 focus:ring-[var(--error-subtle)]
              flex items-center gap-2
              active:scale-[0.98]
            "
          >
            <Square class="w-4 h-4" />
            Stop
          </button>
        </Show>
      </div>

      {/* Provider indicator */}
      <Show when={currentProvider()}>
        <div class="mt-2 flex items-center gap-1.5 text-xs text-[var(--text-muted)]">
          <Zap class="w-3 h-3" />
          <span>Using: {currentProvider()}</span>
        </div>
      </Show>
    </form>
  )
}
