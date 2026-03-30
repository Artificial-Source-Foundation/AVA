/**
 * Step 1: Welcome
 *
 * Centered vertically: 64px gradient logo (blue->purple) with "A", blue glow,
 * "Welcome to AVA" title, 2-line subtitle, "Get Started ->" button, import link.
 */

import type { Component } from 'solid-js'

export interface WelcomeStepProps {
  onNext: () => void
  onImport?: () => void
}

export const WelcomeStep: Component<WelcomeStepProps> = (props) => (
  <div class="flex flex-col items-center text-center">
    {/* AVA Logo - 64px rounded square with blue->purple gradient + glow */}
    <div
      class="w-16 h-16 rounded-[16px] flex items-center justify-center mb-8"
      style={{
        background: 'linear-gradient(135deg, #3B82F6, #8B5CF6)',
        'box-shadow': '0 0 32px rgba(59, 130, 246, 0.4)',
      }}
    >
      <span class="text-white text-3xl font-bold select-none">A</span>
    </div>

    {/* Title */}
    <h1 class="text-2xl font-bold text-[var(--text-primary)] leading-tight tracking-tight mb-3">
      Welcome to AVA
    </h1>

    {/* Subtitle - 2 lines, muted */}
    <p class="text-sm text-[var(--text-muted)] mb-10 max-w-[280px] leading-relaxed">
      Your AI dev team — lean by default,
      <br />
      infinitely extensible.
    </p>

    {/* Get Started button - blue filled, rounded-10, arrow */}
    <button
      type="button"
      onClick={() => props.onNext()}
      class="px-8 py-3 bg-[var(--accent)] hover:bg-[var(--accent-hover)] text-white text-sm font-semibold rounded-[10px] transition-colors flex items-center gap-2"
    >
      Get Started
      <span aria-hidden="true">&rarr;</span>
    </button>

    {/* Import link - muted */}
    <button
      type="button"
      onClick={() => props.onImport?.()}
      class="mt-6 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors opacity-60 hover:opacity-80"
    >
      Already have config? Import .ava/ folder
    </button>
  </div>
)
