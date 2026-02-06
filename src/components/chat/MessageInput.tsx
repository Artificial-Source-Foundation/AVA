/**
 * Message Input Component
 *
 * Minimal input design with Plan/Act mode toggle.
 * Supports both simple chat and full agent mode.
 */

import { ArrowUp, Bot, FileSearch, Square, X, Zap } from 'lucide-solid'
import { type Component, createSignal, Show } from 'solid-js'
import { useAgent } from '../../hooks/useAgent'
import { useChat } from '../../hooks/useChat'

export const MessageInput: Component = () => {
  const [input, setInput] = createSignal('')
  const [useAgentMode, setUseAgentMode] = createSignal(false)
  let submitting = false
  // oxlint-disable-next-line no-unassigned-vars -- SolidJS ref pattern: assigned via ref={} in JSX
  let textareaRef: HTMLTextAreaElement | undefined

  // Chat mode (simple single-turn)
  const chat = useChat()

  // Agent mode (full autonomous loop)
  const agent = useAgent()

  const autoResize = () => {
    if (!textareaRef) return
    textareaRef.style.height = 'auto'
    textareaRef.style.height = `${Math.min(textareaRef.scrollHeight, 200)}px`
  }

  const isProcessing = () => chat.isStreaming() || agent.isRunning()

  const handleSubmit = async (e: Event) => {
    e.preventDefault()
    const message = input().trim()
    if (!message || isProcessing() || submitting) return

    submitting = true
    setInput('')
    if (textareaRef) textareaRef.style.height = 'auto'
    chat.clearError()
    agent.clearError()

    try {
      if (useAgentMode()) {
        await agent.run(message)
      } else {
        await chat.sendMessage(message)
      }
    } finally {
      submitting = false
    }
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault()
      handleSubmit(e)
    }
  }

  const handleCancel = () => {
    if (useAgentMode()) {
      agent.cancel()
    } else {
      chat.cancel()
    }
  }

  const currentError = () => {
    if (useAgentMode()) {
      return agent.lastError() ? { message: agent.lastError()! } : null
    }
    return chat.error()
  }

  const clearCurrentError = () => {
    if (useAgentMode()) {
      agent.clearError()
    } else {
      chat.clearError()
    }
  }

  return (
    <div class="p-4 border-t border-[var(--border-subtle)]">
      {/* Mode toggle and status bar */}
      <div class="flex items-center justify-between mb-3">
        {/* Plan/Act Mode Toggle */}
        <div class="flex items-center gap-2">
          <button
            type="button"
            onClick={() => agent.togglePlanMode()}
            disabled={isProcessing()}
            class={`
              flex items-center gap-1.5 px-3 py-1.5
              text-xs font-medium rounded-full
              transition-all duration-200
              ${
                agent.isPlanMode()
                  ? 'bg-[var(--warning-subtle)] text-[var(--warning)] border border-[var(--warning)]'
                  : 'bg-[var(--surface-raised)] text-[var(--text-secondary)] border border-[var(--border-subtle)] hover:border-[var(--accent)]'
              }
              disabled:opacity-50 disabled:cursor-not-allowed
            `}
          >
            <FileSearch class="w-3.5 h-3.5" />
            <span>{agent.isPlanMode() ? 'Plan' : 'Act'}</span>
          </button>

          {/* Agent/Chat Mode Toggle */}
          <button
            type="button"
            onClick={() => setUseAgentMode(!useAgentMode())}
            disabled={isProcessing()}
            class={`
              flex items-center gap-1.5 px-3 py-1.5
              text-xs font-medium rounded-full
              transition-all duration-200
              ${
                useAgentMode()
                  ? 'bg-[var(--accent-subtle)] text-[var(--accent)] border border-[var(--accent)]'
                  : 'bg-[var(--surface-raised)] text-[var(--text-secondary)] border border-[var(--border-subtle)] hover:border-[var(--accent)]'
              }
              disabled:opacity-50 disabled:cursor-not-allowed
            `}
            title={
              useAgentMode() ? 'Agent mode: Full autonomous loop' : 'Chat mode: Simple responses'
            }
          >
            {useAgentMode() ? <Bot class="w-3.5 h-3.5" /> : <Zap class="w-3.5 h-3.5" />}
            <span>{useAgentMode() ? 'Agent' : 'Chat'}</span>
          </button>
        </div>

        {/* Status indicators */}
        <div class="flex items-center gap-2 text-xs text-[var(--text-tertiary)]">
          <Show when={agent.isRunning()}>
            <span class="flex items-center gap-1">
              <span class="w-2 h-2 bg-[var(--accent)] rounded-full animate-pulse" />
              Turn {agent.currentTurn()}
            </span>
          </Show>
          <Show when={agent.doomLoopDetected()}>
            <span class="text-[var(--warning)]">Loop detected</span>
          </Show>
        </div>
      </div>

      {/* Error display */}
      <Show when={currentError()}>
        <div class="mb-3 p-3 bg-[var(--error-subtle)] border border-[var(--error)] rounded-lg flex items-center justify-between gap-3">
          <span class="text-sm text-[var(--error)]">{currentError()!.message}</span>
          <button
            type="button"
            onClick={clearCurrentError}
            class="p-1 rounded text-[var(--error)] hover:bg-[var(--error-subtle)] transition-colors"
          >
            <X class="w-4 h-4" />
          </button>
        </div>
      </Show>

      {/* Input form */}
      <form onSubmit={handleSubmit} class="flex gap-2 items-end">
        <div class="flex-1 relative">
          <textarea
            ref={textareaRef}
            value={input()}
            onInput={(e) => {
              setInput(e.currentTarget.value)
              autoResize()
            }}
            onKeyDown={handleKeyDown}
            placeholder={
              isProcessing()
                ? useAgentMode()
                  ? `Working... (turn ${agent.currentTurn()})`
                  : 'Generating...'
                : agent.isPlanMode()
                  ? 'Plan your approach...'
                  : 'Ask anything...'
            }
            disabled={isProcessing()}
            rows={1}
            class="
              w-full px-4 py-3
              bg-[var(--input-background)] text-[var(--text-primary)]
              placeholder-[var(--input-placeholder)]
              border border-[var(--input-border)] rounded-lg
              text-sm resize-none
              transition-colors
              focus:outline-none focus:border-[var(--input-border-focus)]
              disabled:opacity-50
            "
            style={{ 'min-height': '44px', 'max-height': '200px' }}
          />
        </div>

        <Show
          when={isProcessing()}
          fallback={
            <button
              type="submit"
              disabled={!input().trim()}
              class="
                p-3
                bg-[var(--accent)] hover:bg-[var(--accent-hover)]
                text-white
                rounded-lg
                transition-colors
                disabled:opacity-30 disabled:cursor-not-allowed
              "
            >
              <ArrowUp class="w-5 h-5" />
            </button>
          }
        >
          <button
            type="button"
            onClick={handleCancel}
            class="
              p-3
              bg-[var(--error)] hover:brightness-110
              text-white
              rounded-lg
              transition-colors
            "
          >
            <Square class="w-5 h-5" />
          </button>
        </Show>
      </form>
    </div>
  )
}
