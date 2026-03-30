/**
 * Question Dock Component
 *
 * Inline, non-modal widget that sits between MessageList and MessageInput
 * in the chat area. Card-style design matching Approval Dock. Displays when
 * the agent uses the `question` tool to ask the user a clarifying question.
 *
 * Supports multiple-choice radio options (first selected with blue border/dot,
 * others muted) with a final freeform "Type your own answer..." italic option.
 *
 * Keyboard: Enter = Submit, Escape = Dismiss with empty answer
 */

import { CircleHelp } from 'lucide-solid'
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
  const [freeformActive, setFreeformActive] = createSignal(false)

  const isMultipleChoice = (): boolean => {
    const req = props.request
    return !!req && req.options.length > 0
  }

  // Reset state when a new question arrives
  createEffect(() => {
    if (props.request) {
      setAnswer('')
      setSelectedOption(null)
      setFreeformActive(false)
    }
  })

  const handleSubmit = (): void => {
    if (freeformActive()) {
      const text = answer().trim()
      if (text) props.onResolve(text)
      return
    }
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

  const canSubmit = (): boolean => {
    if (freeformActive()) return answer().trim().length > 0
    if (isMultipleChoice()) return selectedOption() !== null
    return answer().trim().length > 0
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

      // Freeform input in multiple-choice mode
      if (freeformActive() && target instanceof HTMLInputElement) {
        if (e.key === 'Enter') {
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
      if (
        isMultipleChoice() &&
        !freeformActive() &&
        e.key === 'Enter' &&
        selectedOption() !== null
      ) {
        e.preventDefault()
        handleSubmit()
      }
    }

    document.addEventListener('keydown', handleKeyDown)
    onCleanup(() => document.removeEventListener('keydown', handleKeyDown))
  })

  let textareaRef: HTMLTextAreaElement | undefined
  let freeformInputRef: HTMLInputElement | undefined

  // Auto-focus the textarea when a free-text question appears
  createEffect(() => {
    if (props.request && !isMultipleChoice() && textareaRef) {
      textareaRef.focus()
    }
  })

  // Auto-focus the freeform input when activated
  createEffect(() => {
    if (freeformActive() && freeformInputRef) {
      freeformInputRef.focus()
    }
  })

  return (
    <Show when={props.request}>
      <div
        role="dialog"
        aria-label="Agent question"
        aria-labelledby="question-dock-text"
        style={{
          width: '620px',
          'max-width': '100%',
          'border-radius': '12px',
          background: 'var(--surface)',
          border: '1px solid var(--border-default)',
          'box-shadow': '0 12px 24px rgba(0, 0, 0, 0.4)',
          overflow: 'hidden',
          'align-self': 'center',
          animation: 'approvalSlideUp 150ms ease-out',
        }}
      >
        {/* Header bar */}
        <div
          class="flex items-center justify-between"
          style={{
            height: '44px',
            padding: '0 16px',
            background: 'var(--background-subtle)',
          }}
        >
          <div class="flex items-center gap-2.5" style={{ height: '100%' }}>
            <CircleHelp class="w-4 h-4" style={{ color: 'var(--accent)' }} />
            <span class="text-[13px] font-semibold" style={{ color: 'var(--text-primary)' }}>
              AVA has a question
            </span>
          </div>
        </div>

        {/* Body */}
        <div
          style={{
            padding: '16px',
            display: 'flex',
            'flex-direction': 'column',
            gap: '14px',
          }}
        >
          {/* Question text */}
          <p
            id="question-dock-text"
            style={{
              color: 'var(--text-secondary)',
              'font-size': '13px',
              'line-height': '1.5',
              margin: '0',
              'white-space': 'pre-wrap',
            }}
          >
            {props.request!.question}
          </p>

          {/* Answer area */}
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
                  style={{
                    flex: '1',
                    resize: 'none',
                    'border-radius': '6px',
                    border: '1px solid var(--border-default)',
                    background: 'var(--background-subtle)',
                    padding: '8px 12px',
                    'font-size': '13px',
                    color: 'var(--text-primary)',
                    outline: 'none',
                  }}
                />
              </div>
            }
          >
            {/* Multiple-choice radio options */}
            <div style={{ display: 'flex', 'flex-direction': 'column', gap: '6px' }}>
              <For each={props.request!.options}>
                {(option, index) => {
                  const isSelected = () => selectedOption() === index() && !freeformActive()
                  return (
                    <button
                      type="button"
                      onClick={() => {
                        setSelectedOption(index())
                        setFreeformActive(false)
                      }}
                      class="flex items-center transition-colors"
                      style={{
                        width: '100%',
                        height: '36px',
                        padding: '0 12px',
                        gap: '10px',
                        'border-radius': '6px',
                        background: 'var(--background-subtle)',
                        border: `1px solid ${isSelected() ? 'var(--accent-border)' : 'var(--border-subtle)'}`,
                        cursor: 'pointer',
                        'text-align': 'left',
                      }}
                    >
                      {/* Radio circle */}
                      <div
                        style={{
                          width: '14px',
                          height: '14px',
                          'border-radius': '50%',
                          border: `${isSelected() ? '2px' : '1.5px'} solid ${isSelected() ? 'var(--accent)' : 'var(--text-muted)'}`,
                          display: 'flex',
                          'align-items': 'center',
                          'justify-content': 'center',
                          'flex-shrink': '0',
                          position: 'relative',
                        }}
                      >
                        <Show when={isSelected()}>
                          <div
                            style={{
                              width: '6px',
                              height: '6px',
                              'border-radius': '50%',
                              background: 'var(--accent)',
                            }}
                          />
                        </Show>
                      </div>
                      <span
                        style={{
                          color: isSelected() ? 'var(--text-primary)' : 'var(--text-secondary)',
                          'font-family': 'var(--font-mono)',
                          'font-size': '11px',
                        }}
                      >
                        {option}
                      </span>
                    </button>
                  )
                }}
              </For>

              {/* Freeform option — always last */}
              <button
                type="button"
                onClick={() => {
                  setFreeformActive(true)
                  setSelectedOption(null)
                }}
                class="flex items-center transition-colors"
                style={{
                  width: '100%',
                  height: '36px',
                  padding: '0 12px',
                  gap: '10px',
                  'border-radius': '6px',
                  background: 'var(--background-subtle)',
                  border: `1px solid ${freeformActive() ? 'var(--accent-border)' : 'var(--border-subtle)'}`,
                  cursor: 'pointer',
                  'text-align': 'left',
                }}
              >
                {/* Radio circle */}
                <div
                  style={{
                    width: '14px',
                    height: '14px',
                    'border-radius': '50%',
                    border: `${freeformActive() ? '2px' : '1.5px'} solid ${freeformActive() ? 'var(--accent)' : 'var(--text-muted)'}`,
                    display: 'flex',
                    'align-items': 'center',
                    'justify-content': 'center',
                    'flex-shrink': '0',
                  }}
                >
                  <Show when={freeformActive()}>
                    <div
                      style={{
                        width: '6px',
                        height: '6px',
                        'border-radius': '50%',
                        background: 'var(--accent)',
                      }}
                    />
                  </Show>
                </div>
                <Show
                  when={freeformActive()}
                  fallback={
                    <span
                      style={{
                        color: 'var(--text-muted)',
                        'font-size': '11px',
                        'font-style': 'italic',
                      }}
                    >
                      Type your own answer...
                    </span>
                  }
                >
                  <input
                    ref={freeformInputRef}
                    type="text"
                    value={answer()}
                    onInput={(e) => setAnswer(e.currentTarget.value)}
                    placeholder="Type your own answer..."
                    style={{
                      flex: '1',
                      background: 'transparent',
                      border: 'none',
                      outline: 'none',
                      color: 'var(--text-primary)',
                      'font-size': '11px',
                      'font-style': 'italic',
                    }}
                  />
                </Show>
              </button>
            </div>
          </Show>

          {/* Action buttons — right-aligned */}
          <div class="flex items-center justify-end gap-2">
            {/* Skip — ghost */}
            <button
              type="button"
              onClick={handleDismiss}
              class="inline-flex items-center justify-center transition-colors"
              style={{
                padding: '8px 16px',
                'border-radius': '6px',
                background: 'rgba(255, 255, 255, 0.024)',
                border: '1px solid var(--border-default)',
                color: 'var(--text-secondary)',
                'font-size': '12px',
                'font-weight': '500',
                cursor: 'pointer',
              }}
              title="Skip question (Escape)"
            >
              Skip
            </button>

            {/* Answer — blue filled */}
            <button
              type="button"
              onClick={handleSubmit}
              disabled={!canSubmit()}
              class="inline-flex items-center justify-center transition-colors"
              style={{
                padding: '8px 20px',
                'border-radius': '6px',
                background: canSubmit() ? 'var(--accent)' : 'var(--accent-muted)',
                color: 'white',
                'font-size': '12px',
                'font-weight': '600',
                cursor: canSubmit() ? 'pointer' : 'default',
                opacity: canSubmit() ? '1' : '0.5',
                border: 'none',
              }}
              title="Submit answer (Enter)"
            >
              Answer
            </button>
          </div>
        </div>
      </div>
    </Show>
  )
}

export default QuestionDock
