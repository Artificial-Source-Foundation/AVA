/**
 * MessageInput Component
 * Captures user input and sends messages via useChat hook
 */

import { type Component, createSignal, Show } from 'solid-js'
import { useChat } from '../../hooks/useChat'

export const MessageInput: Component = () => {
  const [input, setInput] = createSignal('')
  const { sendMessage, isStreaming, error, currentProvider, cancel, clearError } = useChat()

  const handleSubmit = async (e: Event) => {
    e.preventDefault()
    const message = input().trim()
    if (!message || isStreaming()) return

    // Clear input immediately for better UX
    setInput('')
    clearError()

    // Send message (async, will update store)
    await sendMessage(message)
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    // Submit on Enter (without Shift)
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault()
      handleSubmit(e)
    }
  }

  return (
    <form onSubmit={handleSubmit} class="p-4 border-t border-gray-700">
      {/* Error display */}
      <Show when={error()}>
        <div class="mb-3 p-3 bg-red-900/50 border border-red-700 rounded-lg text-red-200 text-sm flex items-center justify-between">
          <span>{error()!.message}</span>
          <button type="button" onClick={clearError} class="text-red-400 hover:text-red-200">
            ✕
          </button>
        </div>
      </Show>

      <div class="flex space-x-4">
        <input
          type="text"
          value={input()}
          onInput={(e) => setInput(e.currentTarget.value)}
          onKeyDown={handleKeyDown}
          placeholder={isStreaming() ? 'Thinking...' : 'Type a message...'}
          disabled={isStreaming()}
          class="flex-1 bg-gray-700 text-white placeholder-gray-400 rounded-lg px-4 py-3 focus:outline-none focus:ring-2 focus:ring-blue-500 disabled:opacity-50 disabled:cursor-not-allowed"
        />

        <Show
          when={isStreaming()}
          fallback={
            <button
              type="submit"
              disabled={!input().trim()}
              class="px-6 py-3 bg-blue-600 text-white rounded-lg hover:bg-blue-500 focus:outline-none focus:ring-2 focus:ring-blue-500 transition disabled:opacity-50 disabled:cursor-not-allowed"
            >
              Send
            </button>
          }
        >
          <button
            type="button"
            onClick={cancel}
            class="px-6 py-3 bg-red-600 text-white rounded-lg hover:bg-red-500 focus:outline-none focus:ring-2 focus:ring-red-500 transition"
          >
            Stop
          </button>
        </Show>
      </div>

      {/* Provider indicator */}
      <Show when={currentProvider()}>
        <div class="mt-2 text-xs text-gray-500">Using: {currentProvider()}</div>
      </Show>
    </form>
  )
}
