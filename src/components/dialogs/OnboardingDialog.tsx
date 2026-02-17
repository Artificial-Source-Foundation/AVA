/**
 * Onboarding Screen
 *
 * Premium full-screen onboarding experience.
 * Inspired by Linear, Arc, Cursor, Warp, Raycast.
 * No scrolling — everything fits on screen.
 */

import {
  ArrowRight,
  Bot,
  Check,
  ChevronLeft,
  Eye,
  EyeOff,
  Moon,
  Palette,
  Rocket,
  Shield,
  Sparkles,
  Sun,
  Terminal,
  Wand2,
} from 'lucide-solid'
import {
  type Component,
  createEffect,
  createSignal,
  For,
  onCleanup,
  onMount,
  Show,
  untrack,
} from 'solid-js'
import { Dynamic } from 'solid-js/web'
import { useSettings } from '../../stores/settings'
import { applyAppearanceToDOM } from '../../stores/settings/settings-appearance'

// ============================================================================
// Types
// ============================================================================

export interface OnboardingProps {
  onComplete: (data: OnboardingData) => void
  onSkip?: () => void
}

export interface OnboardingData {
  theme: string
  mode: 'light' | 'dark'
  anthropicKey?: string
  openrouterKey?: string
}

type Step = 'welcome' | 'theme' | 'api-keys' | 'features' | 'complete'

// ============================================================================
// Data
// ============================================================================

const themes = [
  { id: 'glass', name: 'Glass', icon: Sparkles, desc: 'Subtle blur & depth' },
  { id: 'minimal', name: 'Minimal', icon: Palette, desc: 'Clean & focused' },
  { id: 'terminal', name: 'Terminal', icon: Terminal, desc: 'Hacker aesthetic' },
] as const

const features = [
  { icon: Bot, title: 'Multi-Agent Team', desc: 'Orchestrate AI agents that work together' },
  { icon: Wand2, title: 'Code Generation', desc: 'Generate, edit, and refactor with AI' },
  { icon: Terminal, title: 'Terminal Integration', desc: 'Execute commands from chat' },
  { icon: Shield, title: 'Permission System', desc: 'Full control over AI actions' },
]

// ============================================================================
// Component
// ============================================================================

export const OnboardingScreen: Component<OnboardingProps> = (props) => {
  const { settings } = useSettings()
  const [step, setStep] = createSignal<Step>('welcome')
  const [selectedTheme, setSelectedTheme] = createSignal<(typeof themes)[number]['id']>('glass')
  const [selectedMode, setSelectedMode] = createSignal<'light' | 'dark'>('dark')
  const [anthropicKey, setAnthropicKey] = createSignal('')
  const [openrouterKey, setOpenrouterKey] = createSignal('')
  const [showAnthropicKey, setShowAnthropicKey] = createSignal(false)
  const [showOpenrouterKey, setShowOpenrouterKey] = createSignal(false)

  const themePreviewMap = {
    glass: { accentColor: 'violet', codeTheme: 'default' },
    minimal: { accentColor: 'blue', codeTheme: 'github-dark' },
    terminal: { accentColor: 'green', codeTheme: 'monokai' },
  } as const

  const steps: Step[] = ['welcome', 'theme', 'api-keys', 'features', 'complete']
  const currentIndex = () => steps.indexOf(step())

  const canGoNext = () => {
    if (step() === 'api-keys') return anthropicKey().length > 0 || openrouterKey().length > 0
    return true
  }

  const nextStep = () => {
    const idx = currentIndex()
    if (idx < steps.length - 1) setStep(steps[idx + 1])
  }

  const prevStep = () => {
    const idx = currentIndex()
    if (idx > 0) setStep(steps[idx - 1])
  }

  const handleComplete = () => {
    props.onComplete({
      theme: selectedTheme(),
      mode: selectedMode(),
      anthropicKey: anthropicKey() || undefined,
      openrouterKey: openrouterKey() || undefined,
    })
  }

  onMount(() => {
    const originalTheme = untrack(() => settings().theme)
    onCleanup(() => {
      applyAppearanceToDOM(settings())
      document.documentElement.dataset.onboardingTheme = originalTheme
    })
  })

  createEffect(() => {
    const base = untrack(() => settings())
    const preview = themePreviewMap[selectedTheme()]
    document.documentElement.dataset.onboardingTheme = selectedTheme()
    applyAppearanceToDOM({
      ...base,
      mode: selectedMode(),
      theme: selectedTheme(),
      appearance: {
        ...base.appearance,
        accentColor: preview.accentColor,
        codeTheme: preview.codeTheme,
      },
    })
  })

  return (
    <div class="onboarding-bg flex items-center justify-center">
      {/* Main card */}
      <div class="onboarding-card w-full max-w-2xl mx-6 p-10 md:p-12">
        {/* Step indicators */}
        <div class="flex items-center justify-center gap-2.5 mb-10">
          <For each={steps}>
            {(_, index) => (
              <div
                class="onboarding-dot"
                data-active={index() === currentIndex()}
                data-completed={index() < currentIndex()}
              />
            )}
          </For>
        </div>

        {/* Step content */}
        <div>
          {/* ========== Welcome ========== */}
          <Show when={step() === 'welcome'}>
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
                  onClick={nextStep}
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
          </Show>

          {/* ========== Theme ========== */}
          <Show when={step() === 'theme'}>
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
                      onClick={() => setSelectedTheme(t.id)}
                      class="selection-card flex flex-col items-center gap-2.5 p-4 rounded-xl border border-[var(--border-subtle)] text-center"
                      data-selected={selectedTheme() === t.id}
                    >
                      <div
                        class={`w-10 h-10 rounded-lg flex items-center justify-center transition-colors ${
                          selectedTheme() === t.id
                            ? 'bg-[var(--accent)] text-white'
                            : 'bg-[var(--surface-raised)] text-[var(--text-tertiary)]'
                        }`}
                      >
                        <Dynamic component={t.icon} class="w-5 h-5" />
                      </div>
                      <div>
                        <p
                          class={`text-sm font-medium ${
                            selectedTheme() === t.id
                              ? 'text-[var(--accent)]'
                              : 'text-[var(--text-primary)]'
                          }`}
                        >
                          {t.name}
                        </p>
                        <p class="text-xs text-[var(--text-muted)] mt-0.5">{t.desc}</p>
                      </div>
                      <Show when={selectedTheme() === t.id}>
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
                  onClick={() => setSelectedMode('dark')}
                  class={`selection-card flex-1 flex items-center justify-center gap-2 px-4 py-3 rounded-xl border text-sm font-medium ${
                    selectedMode() === 'dark'
                      ? 'border-[var(--accent)] text-[var(--accent)]'
                      : 'border-[var(--border-subtle)] text-[var(--text-secondary)]'
                  }`}
                  data-selected={selectedMode() === 'dark'}
                >
                  <Moon class="w-4 h-4" />
                  Dark
                </button>
                <button
                  type="button"
                  onClick={() => setSelectedMode('light')}
                  class={`selection-card flex-1 flex items-center justify-center gap-2 px-4 py-3 rounded-xl border text-sm font-medium ${
                    selectedMode() === 'light'
                      ? 'border-[var(--accent)] text-[var(--accent)]'
                      : 'border-[var(--border-subtle)] text-[var(--text-secondary)]'
                  }`}
                  data-selected={selectedMode() === 'light'}
                >
                  <Sun class="w-4 h-4" />
                  Light
                </button>
              </div>

              <NavButtons onPrev={prevStep} onNext={nextStep} />
            </div>
          </Show>

          {/* ========== API Keys ========== */}
          <Show when={step() === 'api-keys'}>
            <div class="step-enter flex flex-col">
              <div class="stagger-child text-center mb-6">
                <h2 class="text-2xl font-bold text-[var(--text-primary)] tracking-tight mb-1">
                  Connect Your API
                </h2>
                <p class="text-sm text-[var(--text-muted)]">Add at least one key to get started</p>
              </div>

              {/* Anthropic */}
              <div class="stagger-child mb-4">
                <span class="block text-sm font-medium text-[var(--text-secondary)] mb-1.5">
                  Anthropic API Key
                </span>
                <div class="relative">
                  <input
                    type={showAnthropicKey() ? 'text' : 'password'}
                    value={anthropicKey()}
                    onInput={(e) => setAnthropicKey(e.currentTarget.value)}
                    placeholder="sk-ant-api03-..."
                    class="onboarding-input w-full px-4 py-2.5 pr-10 bg-[var(--surface-glass)] text-[var(--text-primary)] placeholder-[var(--text-muted)] border border-[var(--border-default)] rounded-xl text-sm transition-all outline-none"
                  />
                  <button
                    type="button"
                    onClick={() => setShowAnthropicKey(!showAnthropicKey())}
                    class="absolute right-3 top-1/2 -translate-y-1/2 text-[var(--text-tertiary)] hover:text-[var(--text-primary)] transition-colors"
                  >
                    <Show when={showAnthropicKey()} fallback={<Eye class="w-4 h-4" />}>
                      <EyeOff class="w-4 h-4" />
                    </Show>
                  </button>
                </div>
                <p class="text-xs text-[var(--text-muted)] mt-1">Direct access to Claude models</p>
              </div>

              {/* OpenRouter */}
              <div class="stagger-child mb-4">
                <span class="block text-sm font-medium text-[var(--text-secondary)] mb-1.5">
                  OpenRouter API Key
                </span>
                <div class="relative">
                  <input
                    type={showOpenrouterKey() ? 'text' : 'password'}
                    value={openrouterKey()}
                    onInput={(e) => setOpenrouterKey(e.currentTarget.value)}
                    placeholder="sk-or-v1-..."
                    class="onboarding-input w-full px-4 py-2.5 pr-10 bg-[var(--surface-glass)] text-[var(--text-primary)] placeholder-[var(--text-muted)] border border-[var(--border-default)] rounded-xl text-sm transition-all outline-none"
                  />
                  <button
                    type="button"
                    onClick={() => setShowOpenrouterKey(!showOpenrouterKey())}
                    class="absolute right-3 top-1/2 -translate-y-1/2 text-[var(--text-tertiary)] hover:text-[var(--text-primary)] transition-colors"
                  >
                    <Show when={showOpenrouterKey()} fallback={<Eye class="w-4 h-4" />}>
                      <EyeOff class="w-4 h-4" />
                    </Show>
                  </button>
                </div>
                <p class="text-xs text-[var(--text-muted)] mt-1">Access to 100+ models</p>
              </div>

              {/* Security note */}
              <div class="stagger-child flex items-center gap-2.5 px-3 py-2.5 bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-xl mb-6">
                <Shield class="w-4 h-4 text-[var(--success)] flex-shrink-0" />
                <p class="text-xs text-[var(--text-secondary)]">
                  Keys are stored locally and never sent to any server except the provider.
                </p>
              </div>

              <NavButtons
                onPrev={prevStep}
                onNext={nextStep}
                nextLabel={canGoNext() ? 'Continue' : 'Skip for now'}
              />
            </div>
          </Show>

          {/* ========== Features ========== */}
          <Show when={step() === 'features'}>
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
                      <h3 class="text-sm font-medium text-[var(--text-primary)] mb-0.5">
                        {f.title}
                      </h3>
                      <p class="text-xs text-[var(--text-muted)] leading-relaxed">{f.desc}</p>
                    </div>
                  )}
                </For>
              </div>

              <NavButtons onPrev={prevStep} onNext={nextStep} />
            </div>
          </Show>

          {/* ========== Complete ========== */}
          <Show when={step() === 'complete'}>
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
                  onClick={handleComplete}
                  class="onboarding-btn-primary inline-flex items-center gap-2 px-8 py-3 bg-[var(--accent)] text-white font-medium rounded-xl text-base"
                >
                  <Sparkles class="w-5 h-5" />
                  Launch AVA
                </button>
              </div>
            </div>
          </Show>
        </div>
      </div>
    </div>
  )
}

// ============================================================================
// Nav Buttons (shared across steps)
// ============================================================================

const NavButtons: Component<{
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

// Re-export for backwards compatibility
export { OnboardingScreen as OnboardingDialog }
export type { OnboardingProps as OnboardingDialogProps }
