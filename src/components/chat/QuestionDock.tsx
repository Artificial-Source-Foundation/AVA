/**
 * Question Dock Component
 *
 * Inline, non-modal widget that sits between MessageList and MessageInput
 * in the chat area. Displays when the agent uses the `question` tool to
 * ask the user a clarifying question. Supports both free-text input and
 * multiple-choice options.
 *
 * Keyboard: Enter = Submit (free-text), Escape = Dismiss with empty answer
 */

import { Check, CircleHelp, Send, X } from 'lucide-solid'
import { type Component, createEffect, createSignal, For, onCleanup, Show } from 'solid-js'
import type { QuestionRequest } from '../../hooks/useAgent'

// ============================================================================
// Types
// ============================================================================

export interface QuestionDockProps {
  request: QuestionRequest | null
  onResolve: (answer: string) => void
}

// ============================================================================
// Component
// ============================================================================

export const QuestionDock: Component<QuestionDockProps> = (props) => {
  const [answer, setAnswer] = createSignal('')
  const [selectedOption, setSelectedOption] = createSignal<number | null>(null)

  const isMultipleChoice = (): boolean => {
    const req = props.request
    return !!req && req.options.length > 0
  }

  // Reset state when a new question arrives
  createEffect(() => {
    if (props.request) {
      setAnswer('')
      setSelectedOption(null)
    }
  })

  const handleSubmit = (): void => {
    if (isMultipleChoice()) {
      const idx = selectedOption()
      if (idx !== null && props.request) {
        props.onResolve(props.request.options[idx]!)
      }
    } else {
      const text = answer().trim()
      if (text) {
        props.onResolve(text)
      }
    }
  }

  const handleDismiss = (): void => {
    props.onResolve('')
  }

  // Keyboard shortcuts
  createEffect(() => {
    if (!props.request) return

    const handleKeyDown = (e: KeyboardEvent): void => {
      const target = e.target

      // For free-text mode, Enter submits (unless in textarea with shift)
      if (!isMultipleChoice() && target instanceof HTMLTextAreaElement) {
        if (e.key === 'Enter' && !e.shiftKey) {
          e.preventDefault()
          handleSubmit()
        }
      }

      // Escape always dismisses
      if (e.key === 'Escape') {
        e.preventDefault()
        handleDismiss()
      }

      // For multiple-choice, Enter submits the selected option
      if (isMultipleChoice() && e.key === 'Enter' && selectedOption() !== null) {
        e.preventDefault()
        handleSubmit()
      }
    }

    document.addEventListener('keydown', handleKeyDown)
    onCleanup(() => document.removeEventListener('keydown', handleKeyDown))
  })

  let textareaRef: HTMLTextAreaElement | undefined

  // Auto-focus the textarea when a free-text question appears
  createEffect(() => {
    if (props.request && !isMultipleChoice() && textareaRef) {
      textareaRef.focus()
    }
  })

  return (
    <Show when={props.request}>
      <div
        role="dialog"
        aria-label="Agent question"
        aria-labelledby="question-dock-text"
        class="border-t border-b border-[var(--border-subtle)] bg-[var(--surface-raised)]"
        style={{ animation: 'approvalSlideUp 150ms ease-out' }}
      >
        {/* Header row */}
        <div class="flex items-center gap-2.5 px-4 py-2">
          {/* Question icon */}
          <div
            class="p-1.5 rounded-[var(--radius-md)] flex-shrink-0"
            style={{ background: 'var(--accent-subtle)' }}
          >
            <CircleHelp class="w-4 h-4" style={{ color: 'var(--accent)' }} />
          </div>

          {/* Label */}
          <span class="text-sm font-medium text-[var(--text-primary)]">Agent Question</span>

          {/* Spacer */}
          <div class="flex-1" />

          {/* Dismiss button */}
          <button
            type="button"
            onClick={handleDismiss}
            class="inline-flex items-center gap-1 rounded-[var(--radius-sm)] border border-[var(--border-default)] bg-[var(--surface)] px-2 py-1 text-[11px] font-medium text-[var(--text-secondary)] hover:text-[var(--text-primary)] hover:bg-[var(--surface-raised)] transition-colors"
            title="Skip question (Escape)"
          >
            <X class="w-3 h-3" />
            Skip
          </button>
        </div>

        {/* Question text */}
        <div class="px-4 pb-3">
          <p
            id="question-dock-text"
            class="text-sm text-[var(--text-primary)] leading-relaxed whitespace-pre-wrap"
          >
            {props.request!.question}
          </p>
        </div>

        {/* Answer area */}
        <div class="px-4 pb-3">
          <Show
            when={isMultipleChoice()}
            fallback={
              /* Free-text input */
              <div class="flex gap-2">
                <textarea
                  ref={textareaRef}
                  value={answer()}
                  onInput={(e) => setAnswer(e.currentTarget.value)}
                  placeholder="Type your answer..."
                  rows={2}
                  class="
                    flex-1 resize-none
                    rounded-[var(--radius-md)]
                    border border-[var(--border-default)]
                    bg-[var(--surface)]
                    px-3 py-2
                    text-sm text-[var(--text-primary)]
                    placeholder:text-[var(--text-muted)]
                    focus:outline-none focus:border-[var(--accent)]
                    transition-colors
                  "
                />
                <button
                  type="button"
                  onClick={handleSubmit}
                  disabled={!answer().trim()}
                  class="
                    self-end
                    inline-flex items-center gap-1
                    rounded-[var(--radius-sm)]
                    border border-[var(--accent)]
                    px-3 py-2
                    text-[12px] font-medium
                    text-[var(--accent)]
                    hover:bg-[var(--accent)] hover:text-white
                    disabled:opacity-40 disabled:pointer-events-none
                    transition-colors
                  "
                  title="Submit answer (Enter)"
                >
                  <Send class="w-3.5 h-3.5" />
                  Send
                </button>
              </div>
            }
          >
            {/* Multiple-choice options */}
            <div class="space-y-1.5">
              <For each={props.request!.options}>
                {(option, index) => (
                  <button
                    type="button"
                    onClick={() => {
                      setSelectedOption(index())
                      // Submit immediately on click for multiple-choice
                      props.onResolve(option)
                    }}
                    class="
                      w-full flex items-center gap-3
                      px-3 py-2
                      rounded-[var(--radius-md)]
                      border text-left text-sm
                      transition-colors duration-[var(--duration-fast)]
                    "
                    classList={{
                      'border-[var(--accent)] bg-[var(--accent-subtle)] text-[var(--accent)]':
                        selectedOption() === index(),
                      'border-[var(--border-default)] text-[var(--text-primary)] hover:border-[var(--accent)] hover:bg-[var(--surface-raised)]':
                        selectedOption() !== index(),
                    }}
                  >
                    <div
                      class="
                        w-5 h-5 rounded-full border-2 flex items-center justify-center flex-shrink-0
                        transition-colors duration-[var(--duration-fast)]
                      "
                      classList={{
                        'border-[var(--accent)] bg-[var(--accent)]': selectedOption() === index(),
                        'border-[var(--border-default)]': selectedOption() !== index(),
                      }}
                    >
                      <Show when={selectedOption() === index()}>
                        <Check class="w-3 h-3 text-white" />
                      </Show>
                    </div>
                    <span>{option}</span>
                  </button>
                )}
              </For>
            </div>
          </Show>
        </div>
      </div>
    </Show>
  )
}

export default QuestionDock
