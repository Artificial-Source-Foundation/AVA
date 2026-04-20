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
import {
  type Component,
  createEffect,
  createSignal,
  createUniqueId,
  For,
  onCleanup,
  Show,
} from 'solid-js'
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

  // Unique IDs for this QuestionDock instance (supports multiple QuestionDocks on page)
  const groupId = createUniqueId()
  const textId = createUniqueId()

  // Ref to the dock root for focus containment checks
  let dockRef: HTMLDivElement | undefined

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
  //   Enter           → Submit selected option / freeform (only when QuestionDock owns focus)
  //   Escape          → Dismiss with empty answer (always works)
  createEffect(() => {
    if (!props.request) return

    const handleKeyDown = (e: KeyboardEvent): void => {
      // Escape always dismisses regardless of focus
      if (e.key === 'Escape') {
        e.preventDefault()
        handleDismiss()
        return
      }

      // Enter submission is gated by dock focus ownership
      if (e.key === 'Enter') {
        // Check if focus is within the dock (dock-root containment gate)
        const activeElement = document.activeElement
        const dockOwnsFocus = dockRef && activeElement && dockRef.contains(activeElement)

        // If focus is outside the dock, do not intercept Enter
        if (!dockOwnsFocus) return

        // When focus is on a button, preserve native button activation
        if (activeElement instanceof HTMLButtonElement) return

        // For free-text mode, Enter submits (unless in textarea with shift)
        if (!isMultipleChoice() && activeElement instanceof HTMLTextAreaElement) {
          if (!e.shiftKey) {
            e.preventDefault()
            handleSubmit()
          }
          return
        }

        // Freeform input in multiple-choice mode
        if (freeformActive() && activeElement instanceof HTMLInputElement) {
          e.preventDefault()
          handleSubmit()
          return
        }

        // For multiple-choice with a selected option, Enter submits
        if (isMultipleChoice() && !freeformActive() && selectedOption() !== null) {
          e.preventDefault()
          handleSubmit()
        }
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
      <section
        ref={dockRef}
        aria-labelledby={textId}
        data-testid="question-dock"
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
            id={textId}
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
                  aria-labelledby={textId}
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
                  class="question-dock-textarea"
                />
                <style>{`
                  .question-dock-textarea:focus-visible {
                    outline: 2px solid var(--accent);
                    outline-offset: -2px;
                  }
                `}</style>
              </div>
            }
          >
            {/* Multiple-choice radio options — native radio inputs for proper accessibility */}
            <div
              role="radiogroup"
              aria-labelledby={textId}
              style={{ display: 'flex', 'flex-direction': 'column', gap: '6px' }}
            >
              <For each={props.request!.options}>
                {(option, index) => {
                  const isSelected = () => selectedOption() === index() && !freeformActive()
                  const optionId = `${groupId}-opt-${index()}`
                  return (
                    <label
                      for={optionId}
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
                        position: 'relative',
                      }}
                    >
                      {/* Native radio input (visually hidden but keyboard accessible) */}
                      <input
                        id={optionId}
                        type="radio"
                        name={`${groupId}-question`}
                        checked={isSelected()}
                        onChange={() => {
                          setSelectedOption(index())
                          setFreeformActive(false)
                        }}
                        style={{
                          position: 'absolute',
                          opacity: '0',
                          width: '0',
                          height: '0',
                          margin: '0',
                          padding: '0',
                          border: 'none',
                        }}
                      />
                      {/* Custom radio circle visual */}
                      <div
                        aria-hidden="true"
                        style={{
                          width: '14px',
                          height: '14px',
                          'border-radius': '50%',
                          border: `${isSelected() ? '2px' : '1.5px'} solid ${isSelected() ? 'var(--accent)' : 'var(--text-muted)'}`,
                          display: 'flex',
                          'align-items': 'center',
                          'justify-content': 'center',
                          'flex-shrink': '0',
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
                      {/* Focus-visible indicator for the label when input is focused */}
                      <style>{`
                        input[type="radio"]:focus-visible + div + span::before,
                        input[type="radio"]:focus-visible + div::before {
                          content: '';
                          position: absolute;
                          inset: -2px;
                          border-radius: 8px;
                          border: 2px solid var(--accent);
                          pointer-events: none;
                        }
                      `}</style>
                    </label>
                  )
                }}
              </For>

              {/* Freeform option — always last */}
              <Show
                when={freeformActive()}
                fallback={
                  <label
                    for={`${groupId}-freeform`}
                    class="flex items-center transition-colors"
                    style={{
                      width: '100%',
                      height: '36px',
                      padding: '0 12px',
                      gap: '10px',
                      'border-radius': '6px',
                      background: 'var(--background-subtle)',
                      border: '1px solid var(--border-subtle)',
                      cursor: 'pointer',
                      'text-align': 'left',
                      position: 'relative',
                    }}
                  >
                    {/* Native radio input (visually hidden but keyboard accessible) */}
                    <input
                      id={`${groupId}-freeform`}
                      type="radio"
                      name={`${groupId}-question`}
                      checked={false}
                      onChange={() => {
                        setFreeformActive(true)
                        setSelectedOption(null)
                      }}
                      style={{
                        position: 'absolute',
                        opacity: '0',
                        width: '0',
                        height: '0',
                        margin: '0',
                        padding: '0',
                        border: 'none',
                      }}
                    />
                    {/* Custom radio circle visual */}
                    <div
                      aria-hidden="true"
                      style={{
                        width: '14px',
                        height: '14px',
                        'border-radius': '50%',
                        border: '1.5px solid var(--text-muted)',
                        display: 'flex',
                        'align-items': 'center',
                        'justify-content': 'center',
                        'flex-shrink': '0',
                      }}
                    />
                    <span
                      style={{
                        color: 'var(--text-muted)',
                        'font-size': '11px',
                        'font-style': 'italic',
                      }}
                    >
                      Type your own answer...
                    </span>
                    {/* Focus-visible indicator */}
                    <style>{`
                      input[type="radio"]:focus-visible + div + span::before {
                        content: '';
                        position: absolute;
                        inset: -2px;
                        border-radius: 8px;
                        border: 2px solid var(--accent);
                        pointer-events: none;
                      }
                    `}</style>
                  </label>
                }
              >
                {/* Active freeform input row */}
                <div
                  class="flex items-center transition-colors"
                  style={{
                    width: '100%',
                    height: '36px',
                    padding: '0 12px',
                    gap: '10px',
                    'border-radius': '6px',
                    background: 'var(--background-subtle)',
                    border: '1px solid var(--accent-border)',
                  }}
                >
                  {/* Selected radio circle visual */}
                  <div
                    aria-hidden="true"
                    style={{
                      width: '14px',
                      height: '14px',
                      'border-radius': '50%',
                      border: '2px solid var(--accent)',
                      display: 'flex',
                      'align-items': 'center',
                      'justify-content': 'center',
                      'flex-shrink': '0',
                    }}
                  >
                    <div
                      style={{
                        width: '6px',
                        height: '6px',
                        'border-radius': '50%',
                        background: 'var(--accent)',
                      }}
                    />
                  </div>
                  <input
                    ref={freeformInputRef}
                    type="text"
                    value={answer()}
                    onInput={(e) => setAnswer(e.currentTarget.value)}
                    onFocus={() => {
                      setFreeformActive(true)
                      setSelectedOption(null)
                    }}
                    aria-label="Type your own answer"
                    placeholder="Type your own answer..."
                    class="question-dock-freeform-input"
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
                  <style>{`
                    .question-dock-freeform-input:focus-visible {
                      outline: 2px solid var(--accent);
                      outline-offset: 2px;
                    }
                  `}</style>
                </div>
              </Show>
            </div>
          </Show>

          {/* Action buttons — right-aligned */}
          <div class="flex items-center justify-end gap-2">
            {/* Skip — ghost */}
            <button
              type="button"
              onClick={handleDismiss}
              class="inline-flex items-center justify-center transition-colors question-dock-btn-ghost"
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
              class="inline-flex items-center justify-center transition-colors question-dock-btn-primary"
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
            <style>{`
              .question-dock-btn-ghost:focus-visible {
                outline: 2px solid var(--accent);
                outline-offset: 2px;
              }
              .question-dock-btn-primary:focus-visible {
                outline: 2px solid white;
                outline-offset: 2px;
              }
              .question-dock-btn-primary:focus-visible:not(:disabled) {
                outline-color: white;
              }
            `}</style>
          </div>
        </div>
      </section>
    </Show>
  )
}

export default QuestionDock
