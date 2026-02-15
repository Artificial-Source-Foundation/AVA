import { Sparkles } from 'lucide-solid'
import type { Component } from 'solid-js'

export const MessageListLoading: Component = () => (
  <div class="space-y-4 animate-pulse">
    <div class="h-16 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-2/3" />
    <div class="h-24 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-3/4 ml-auto" />
    <div class="h-16 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-2/3" />
  </div>
)

export const MessageListEmpty: Component = () => (
  <div class="flex flex-col items-center justify-center h-full">
    <div
      class="
        w-16 h-16 mb-6
        rounded-[var(--radius-xl)]
        bg-[var(--accent-subtle)]
        flex items-center justify-center
      "
    >
      <Sparkles class="w-8 h-8 text-[var(--accent)]" />
    </div>
    <h2 class="text-xl font-semibold text-[var(--text-primary)] font-display">Welcome to AVA</h2>
    <p class="text-sm text-[var(--text-tertiary)] mt-2 max-w-sm text-center">
      Your AI coding assistant is ready. Start a conversation to begin.
    </p>
  </div>
)

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
