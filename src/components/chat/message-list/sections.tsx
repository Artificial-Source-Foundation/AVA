import { Bug, Code, FlaskConical, Wand2 } from 'lucide-solid'
import { type Component, createMemo, For } from 'solid-js'
import { Dynamic } from 'solid-js/web'

const SUGGESTION_CARDS = [
  {
    icon: Code,
    label: 'Explain this codebase',
    prompt:
      'Give me a high-level overview of this project. What are the key files, architecture patterns, and how does everything fit together?',
  },
  {
    icon: Bug,
    label: 'Fix a bug',
    prompt: 'Help me find and fix a bug. Let me describe the issue: ',
  },
  {
    icon: FlaskConical,
    label: 'Write tests',
    prompt:
      'Generate comprehensive tests for the most critical functions. Focus on edge cases, error handling, and boundary conditions.',
  },
  {
    icon: Wand2,
    label: 'Refactor code',
    prompt:
      'Help me refactor code to improve readability, maintainability, and performance. Let me describe what needs refactoring: ',
  },
]

function getGreeting(): string {
  const hour = new Date().getHours()
  if (hour < 12) return 'Good morning'
  if (hour < 18) return 'Good afternoon'
  return 'Good evening'
}

export const MessageListLoading: Component = () => (
  <div class="space-y-4 animate-pulse-subtle">
    <div class="h-16 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-2/3" />
    <div class="h-24 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-3/4 ml-auto" />
    <div class="h-16 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-2/3" />
  </div>
)

export const MessageListEmpty: Component = () => {
  const insertSuggestion = (prompt: string): void => {
    window.dispatchEvent(new CustomEvent('ava:set-input', { detail: { text: prompt } }))
  }

  const greeting = createMemo(() => getGreeting())

  return (
    <div class="flex flex-col items-center justify-center h-full select-none">
      <div class="flex flex-col items-center w-full" style={{ 'max-width': '600px' }}>
        {/* AVA logo mark */}
        <div class="welcome-logo-mark" />

        {/* Time-based greeting */}
        <p
          class="mt-4"
          style={{
            'font-size': '13px',
            color: 'var(--gray-6)',
            'letter-spacing': '0.01em',
          }}
        >
          {greeting()}
        </p>

        {/* Heading */}
        <h2
          class="mt-2"
          style={{
            'font-size': '20px',
            'font-weight': '600',
            color: 'var(--gray-12)',
            'letter-spacing': '-0.02em',
            'line-height': '1.3',
          }}
        >
          What are you working on?
        </h2>

        {/* Subtitle */}
        <p
          class="text-center"
          style={{
            'margin-top': '8px',
            'font-size': '14px',
            color: 'var(--gray-8)',
            'line-height': '1.5',
          }}
        >
          Describe a task, or start with one of these.
        </p>

        {/* Suggestion cards — 2x2 grid */}
        <div class="mt-6 grid grid-cols-2 w-full" style={{ gap: '10px', 'max-width': '480px' }}>
          <For each={SUGGESTION_CARDS}>
            {(card) => (
              <button
                type="button"
                onClick={() => insertSuggestion(card.prompt)}
                class="welcome-suggestion-card"
              >
                <Dynamic
                  component={card.icon}
                  class="flex-shrink-0"
                  style={{ width: '18px', height: '18px', color: 'var(--gray-6)' }}
                />
                <span
                  style={{
                    'font-size': '14px',
                    color: 'var(--gray-9)',
                    'line-height': '1.4',
                  }}
                >
                  {card.label}
                </span>
              </button>
            )}
          </For>
        </div>

        {/* Keyboard shortcut hints */}
        <div class="mt-8 flex items-center justify-center" style={{ gap: '20px' }}>
          <span class="welcome-hint">
            <kbd>Ctrl+/</kbd> commands
          </span>
          <span class="welcome-hint">
            <kbd>Ctrl+M</kbd> model
          </span>
          <span class="welcome-hint">
            <kbd>Ctrl+T</kbd> thinking
          </span>
        </div>
      </div>
    </div>
  )
}

interface ScrollToBottomButtonProps {
  onClick: () => void
}

export const ScrollToBottomButton: Component<ScrollToBottomButtonProps> = (props) => (
  <button
    type="button"
    onClick={() => props.onClick()}
    class="
      absolute bottom-4 right-8
      p-2 rounded-full
      bg-[var(--surface-raised)] border border-[var(--border-subtle)]
      shadow-md
      text-[var(--text-secondary)]
      hover:bg-[var(--accent)] hover:text-white hover:border-[var(--accent)]
      transition-[background-color,border-color,color,transform] duration-[var(--duration-fast)]
      z-10
    "
    title="Scroll to bottom"
    aria-label="Scroll to bottom"
  >
    <svg
      class="w-5 h-5"
      fill="none"
      viewBox="0 0 24 24"
      stroke="currentColor"
      aria-labelledby="scroll-icon-title"
    >
      <title id="scroll-icon-title">Scroll to bottom</title>
      <path
        stroke-linecap="round"
        stroke-linejoin="round"
        stroke-width="2"
        d="M19 14l-7 7m0 0l-7-7m7 7V3"
      />
    </svg>
  </button>
)
