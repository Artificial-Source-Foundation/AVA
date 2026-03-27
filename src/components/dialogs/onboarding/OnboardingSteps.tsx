/**
 * Onboarding Step Components & Data
 *
 * Step-specific UI for the onboarding flow.
 * Extracted from OnboardingDialog.tsx to keep each module under 300 lines.
 */

import {
  ArrowRight,
  Bot,
  Check,
  ChevronLeft,
  Moon,
  Palette,
  Rocket,
  Shield,
  Sparkles,
  Sun,
  Terminal,
  Wand2,
} from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'

// ============================================================================
// Data
// ============================================================================

export const themes = [
  { id: 'glass', name: 'Glass', icon: Sparkles, desc: 'Subtle blur & depth' },
  { id: 'minimal', name: 'Minimal', icon: Palette, desc: 'Clean & focused' },
  { id: 'terminal', name: 'Terminal', icon: Terminal, desc: 'Hacker aesthetic' },
] as const

export const features = [
  { icon: Bot, title: 'Multi-Agent Team', desc: 'Orchestrate AI agents that work together' },
  { icon: Wand2, title: 'Code Generation', desc: 'Generate, edit, and refactor with AI' },
  { icon: Terminal, title: 'Terminal Integration', desc: 'Execute commands from chat' },
  { icon: Shield, title: 'Permission System', desc: 'Full control over AI actions' },
]

export type ThemeId = (typeof themes)[number]['id']

// ============================================================================
// Nav Buttons (shared across steps)
// ============================================================================

export const NavButtons: Component<{
  onPrev: () => void
  onNext: () => void
  nextLabel?: string
}> = (props) => (
  <div class="stagger-child flex items-center justify-between">
    <button
      type="button"
      onClick={() => props.onPrev()}
      class="inline-flex items-center gap-1 px-3 py-2 text-sm text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors rounded-lg hover:bg-[var(--surface-raised)]"
    >
      <ChevronLeft class="w-4 h-4" />
      Back
    </button>
    <button
      type="button"
      onClick={() => props.onNext()}
      class="onboarding-btn-primary inline-flex items-center gap-2 px-6 py-2.5 bg-[var(--accent)] text-white font-medium rounded-xl text-sm"
    >
      {props.nextLabel ?? 'Continue'}
      <ArrowRight class="w-4 h-4" />
    </button>
  </div>
)

// ============================================================================
// Welcome Step
// ============================================================================

export const WelcomeStep: Component<{
  onNext: () => void
  onSkip?: () => void
}> = (props) => (
  <div class="step-enter onboarding-hero flex flex-col items-center text-center">
    <div class="onboarding-logo w-24 h-24 mb-8 rounded-3xl bg-gradient-to-br from-[var(--accent)] to-[var(--blue-4)] flex items-center justify-center shadow-lg">
      <Sparkles class="w-11 h-11 text-white" />
    </div>

    <div class="stagger-child">
      <h1 class="onboarding-hero-title text-4xl md:text-5xl font-bold text-[var(--text-primary)] tracking-tight leading-[1.08] mb-0">
        Welcome to AVA
      </h1>
    </div>

    <div class="stagger-child onboarding-hero-copy">
      <p class="text-xl text-[var(--text-secondary)] leading-8 tracking-[0.01em]">
        Your multi-agent AI coding assistant.
      </p>
      <p class="text-base text-[var(--text-muted)] leading-7 tracking-[0.005em]">
        Let's get you set up in a few focused steps.
      </p>
    </div>

    <div class="stagger-child flex items-center gap-4">
      <button
        type="button"
        onClick={() => props.onNext()}
        class="onboarding-btn-primary inline-flex items-center gap-2.5 px-10 py-3.5 bg-[var(--accent)] text-white font-semibold rounded-xl text-lg"
      >
        Get Started
        <ArrowRight class="w-5 h-5" />
      </button>
    </div>

    <div class="stagger-child mt-6">
      <button
        type="button"
        onClick={() => props.onSkip?.()}
        class="text-sm font-medium text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
      >
        Skip setup
      </button>
    </div>
  </div>
)

// ============================================================================
// Theme Step
// ============================================================================

export const ThemeStep: Component<{
  selectedTheme: ThemeId
  selectedMode: 'light' | 'dark'
  onSelectTheme: (id: ThemeId) => void
  onSelectMode: (mode: 'light' | 'dark') => void
  onPrev: () => void
  onNext: () => void
}> = (props) => (
  <div class="step-enter flex flex-col">
    <div class="stagger-child text-center mb-6">
      <h2 class="text-2xl font-bold text-[var(--text-primary)] tracking-tight mb-1">
        Choose Your Style
      </h2>
      <p class="text-sm text-[var(--text-muted)]">Pick a visual theme (live preview)</p>
    </div>

    {/* Theme cards */}
    <div class="stagger-child grid grid-cols-3 gap-3 mb-6">
      <For each={themes}>
        {(t) => (
          <button
            type="button"
            onClick={() => props.onSelectTheme(t.id)}
            class="selection-card flex flex-col items-center gap-2.5 p-4 rounded-xl border border-[var(--border-subtle)] text-center"
            data-selected={props.selectedTheme === t.id}
          >
            <div
              class={`w-10 h-10 rounded-lg flex items-center justify-center transition-colors ${
                props.selectedTheme === t.id
                  ? 'bg-[var(--accent)] text-white'
                  : 'bg-[var(--surface-raised)] text-[var(--text-tertiary)]'
              }`}
            >
              <Dynamic component={t.icon} class="w-5 h-5" />
            </div>
            <div>
              <p
                class={`text-sm font-medium ${
                  props.selectedTheme === t.id
                    ? 'text-[var(--accent)]'
                    : 'text-[var(--text-primary)]'
                }`}
              >
                {t.name}
              </p>
              <p class="text-xs text-[var(--text-muted)] mt-0.5">{t.desc}</p>
            </div>
            <Show when={props.selectedTheme === t.id}>
              <Check class="w-4 h-4 text-[var(--accent)]" />
            </Show>
          </button>
        )}
      </For>
    </div>

    {/* Mode toggle */}
    <div class="stagger-child flex gap-2 mb-6">
      <button
        type="button"
        onClick={() => props.onSelectMode('dark')}
        class={`selection-card flex-1 flex items-center justify-center gap-2 px-4 py-3 rounded-xl border text-sm font-medium ${
          props.selectedMode === 'dark'
            ? 'border-[var(--accent)] text-[var(--accent)]'
            : 'border-[var(--border-subtle)] text-[var(--text-secondary)]'
        }`}
        data-selected={props.selectedMode === 'dark'}
      >
        <Moon class="w-4 h-4" />
        Dark
      </button>
      <button
        type="button"
        onClick={() => props.onSelectMode('light')}
        class={`selection-card flex-1 flex items-center justify-center gap-2 px-4 py-3 rounded-xl border text-sm font-medium ${
          props.selectedMode === 'light'
            ? 'border-[var(--accent)] text-[var(--accent)]'
            : 'border-[var(--border-subtle)] text-[var(--text-secondary)]'
        }`}
        data-selected={props.selectedMode === 'light'}
      >
        <Sun class="w-4 h-4" />
        Light
      </button>
    </div>

    <NavButtons onPrev={props.onPrev} onNext={props.onNext} />
  </div>
)

// ============================================================================
// Features Step
// ============================================================================

export const FeaturesStep: Component<{
  onPrev: () => void
  onNext: () => void
}> = (props) => (
  <div class="step-enter flex flex-col">
    <div class="stagger-child text-center mb-6">
      <h2 class="text-2xl font-bold text-[var(--text-primary)] tracking-tight mb-1">
        What You Can Do
      </h2>
      <p class="text-sm text-[var(--text-muted)]">AVA's core capabilities</p>
    </div>

    <div class="grid grid-cols-2 gap-3 mb-6">
      <For each={features}>
        {(f) => (
          <div class="stagger-child feature-card p-4 bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-xl">
            <div class="w-9 h-9 mb-3 rounded-lg bg-[var(--accent-subtle)] flex items-center justify-center">
              <Dynamic component={f.icon} class="w-4.5 h-4.5 text-[var(--accent)]" />
            </div>
            <h3 class="text-sm font-medium text-[var(--text-primary)] mb-0.5">{f.title}</h3>
            <p class="text-xs text-[var(--text-muted)] leading-relaxed">{f.desc}</p>
          </div>
        )}
      </For>
    </div>

    <NavButtons onPrev={props.onPrev} onNext={props.onNext} />
  </div>
)

// ============================================================================
// Complete Step
// ============================================================================

export const CompleteStep: Component<{
  onComplete: () => void
}> = (props) => (
  <div class="step-enter flex flex-col items-center text-center">
    <div class="stagger-child w-20 h-20 mb-6 rounded-full bg-[var(--success-subtle)] flex items-center justify-center">
      <Rocket class="w-10 h-10 text-[var(--success)]" />
    </div>

    <div class="stagger-child">
      <h2 class="text-3xl font-bold text-[var(--text-primary)] tracking-tight mb-3">
        You're All Set
      </h2>
    </div>

    <div class="stagger-child">
      <p class="text-base text-[var(--text-secondary)] max-w-sm leading-relaxed mb-8">
        AVA is ready. Start a conversation to begin coding with your AI team.
      </p>
    </div>

    <div class="stagger-child">
      <button
        type="button"
        onClick={() => props.onComplete()}
        class="onboarding-btn-primary inline-flex items-center gap-2 px-8 py-3 bg-[var(--accent)] text-white font-medium rounded-xl text-base"
      >
        <Sparkles class="w-5 h-5" />
        Launch AVA
      </button>
    </div>
  </div>
)
