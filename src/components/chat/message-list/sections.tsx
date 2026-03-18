import { Bug, Code, FlaskConical, Wand2 } from 'lucide-solid'
import { type Component, For } from 'solid-js'

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

export const MessageListLoading: Component = () => (
  <div class="space-y-4 animate-pulse">
    <div class="h-16 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-2/3" />
    <div class="h-24 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-3/4 ml-auto" />
    <div class="h-16 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-2/3" />
  </div>
)

export const MessageListEmpty: Component = () => {
  const insertSuggestion = (prompt: string): void => {
    window.dispatchEvent(new CustomEvent('ava:set-input', { detail: { text: prompt } }))
  }

  return (
    <div class="flex flex-col items-center justify-center h-full select-none">
      {/* Logo */}
      <div
        class="w-16 h-16 mb-5 rounded-2xl flex items-center justify-center"
        style={{ background: '#A78BFA15' }}
      >
        <span class="text-2xl font-bold" style={{ color: '#A78BFA' }}>
          A
        </span>
      </div>

      {/* Heading */}
      <h2 class="font-semibold" style={{ 'font-size': '20px', color: '#FAFAFA' }}>
        How can I help?
      </h2>

      {/* Subtitle */}
      <p class="mt-2 text-center max-w-sm" style={{ 'font-size': '13px', color: '#71717A' }}>
        Ask anything about your codebase, or try one of these:
      </p>

      {/* Suggestion cards — 2x2 grid */}
      <div class="mt-5 grid grid-cols-2 gap-2.5 w-full" style={{ 'max-width': '500px' }}>
        <For each={SUGGESTION_CARDS}>
          {(card) => (
            <button
              type="button"
              onClick={() => insertSuggestion(card.prompt)}
              class="
                flex items-center gap-3 text-left
                rounded-xl
                transition-colors
                group
              "
              style={{
                background: '#18181B',
                border: '1px solid #27272A',
                padding: '12px 16px',
              }}
              onMouseEnter={(e) => {
                e.currentTarget.style.borderColor = '#3f3f46'
                e.currentTarget.style.background = '#1c1c1f'
              }}
              onMouseLeave={(e) => {
                e.currentTarget.style.borderColor = '#27272A'
                e.currentTarget.style.background = '#18181B'
              }}
            >
              <card.icon class="w-4 h-4 flex-shrink-0" style={{ color: '#52525B' }} />
              <span style={{ 'font-size': '13px', color: '#A1A1AA' }}>{card.label}</span>
            </button>
          )}
        </For>
      </div>

      {/* Keyboard shortcut hints */}
      <div class="mt-6 flex items-center gap-1" style={{ 'font-size': '11px', color: '#3F3F46' }}>
        <span>
          <kbd class="font-mono">Ctrl+/</kbd> commands
        </span>
        <span aria-hidden="true" class="mx-1.5">
          &middot;
        </span>
        <span>
          <kbd class="font-mono">Ctrl+M</kbd> model
        </span>
        <span aria-hidden="true" class="mx-1.5">
          &middot;
        </span>
        <span>
          <kbd class="font-mono">Ctrl+T</kbd> thinking
        </span>
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
    onClick={props.onClick}
    class="
      absolute bottom-4 right-8
      p-2 rounded-full
      bg-[var(--surface-raised)] border border-[var(--border-subtle)]
      shadow-md
      text-[var(--text-secondary)]
      hover:bg-[var(--accent)] hover:text-white hover:border-[var(--accent)]
      transition-all duration-[var(--duration-fast)]
      z-10
    "
    title="Scroll to bottom"
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
