/**
 * Onboarding Dialog Component
 *
 * Multi-step setup wizard for first-time users.
 * Guides through API key setup, theme selection, and feature introduction.
 */

import {
  ArrowRight,
  Bot,
  Check,
  ChevronLeft,
  ChevronRight,
  Eye,
  EyeOff,
  Heart,
  Key,
  Monitor,
  Moon,
  Palette,
  Rocket,
  Shield,
  Sparkles,
  Sun,
  Terminal,
  Wand2,
} from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import { Button } from '../ui/Button'
import { Dialog } from '../ui/Dialog'

// ============================================================================
// Types
// ============================================================================

export interface OnboardingDialogProps {
  /** Whether dialog is open */
  open: boolean
  /** Called when onboarding is completed */
  onComplete: (data: OnboardingData) => void
  /** Called when dialog is closed/skipped */
  onSkip?: () => void
}

export interface OnboardingData {
  theme: string
  mode: 'light' | 'dark'
  anthropicKey?: string
  openrouterKey?: string
}

type OnboardingStep = 'welcome' | 'theme' | 'api-keys' | 'features' | 'complete'

// ============================================================================
// Step Components
// ============================================================================

const themes = [
  { id: 'glass', name: 'Glass', icon: Sparkles, description: 'Apple-inspired with subtle blur' },
  { id: 'minimal', name: 'Minimal', icon: Monitor, description: 'Clean and focused like Linear' },
  {
    id: 'terminal',
    name: 'Terminal',
    icon: Terminal,
    description: 'Catppuccin-inspired hacker vibe',
  },
  { id: 'soft', name: 'Soft', icon: Heart, description: 'Warm and friendly aesthetic' },
] as const

const features = [
  {
    icon: Bot,
    title: 'Multi-Agent System',
    description: 'Orchestrate multiple AI agents for complex tasks',
  },
  {
    icon: Wand2,
    title: 'Code Generation',
    description: 'Generate, edit, and refactor code with AI assistance',
  },
  {
    icon: Terminal,
    title: 'Terminal Integration',
    description: 'Execute commands directly from the chat interface',
  },
  {
    icon: Shield,
    title: 'Permission System',
    description: 'Full control over what actions AI can perform',
  },
]

// ============================================================================
// Onboarding Dialog Component
// ============================================================================

export const OnboardingDialog: Component<OnboardingDialogProps> = (props) => {
  const [step, setStep] = createSignal<OnboardingStep>('welcome')
  const [selectedTheme, setSelectedTheme] = createSignal('glass')
  const [selectedMode, setSelectedMode] = createSignal<'light' | 'dark'>('dark')
  const [anthropicKey, setAnthropicKey] = createSignal('')
  const [openrouterKey, setOpenrouterKey] = createSignal('')
  const [showAnthropicKey, setShowAnthropicKey] = createSignal(false)
  const [showOpenrouterKey, setShowOpenrouterKey] = createSignal(false)

  const steps: OnboardingStep[] = ['welcome', 'theme', 'api-keys', 'features', 'complete']

  const currentStepIndex = () => steps.indexOf(step())

  const canGoNext = () => {
    if (step() === 'api-keys') {
      return anthropicKey().length > 0 || openrouterKey().length > 0
    }
    return true
  }

  const nextStep = () => {
    const idx = currentStepIndex()
    if (idx < steps.length - 1) {
      setStep(steps[idx + 1])
    }
  }

  const prevStep = () => {
    const idx = currentStepIndex()
    if (idx > 0) {
      setStep(steps[idx - 1])
    }
  }

  const handleComplete = () => {
    props.onComplete({
      theme: selectedTheme(),
      mode: selectedMode(),
      anthropicKey: anthropicKey() || undefined,
      openrouterKey: openrouterKey() || undefined,
    })
  }

  const handleOpenChange = (open: boolean) => {
    if (!open && props.onSkip) {
      props.onSkip()
    }
  }

  return (
    <Dialog
      open={props.open}
      onOpenChange={handleOpenChange}
      title=""
      size="lg"
      showCloseButton={false}
    >
      <div class="min-h-[400px] flex flex-col">
        {/* Progress Indicator */}
        <div class="flex items-center justify-center gap-2 mb-6">
          <For each={steps}>
            {(_, index) => (
              <div
                class={`
                  h-1.5 rounded-full transition-all duration-300
                  ${
                    index() <= currentStepIndex()
                      ? 'w-8 bg-[var(--accent)]'
                      : 'w-4 bg-[var(--surface-sunken)]'
                  }
                `}
              />
            )}
          </For>
        </div>

        {/* Step Content */}
        <div class="flex-1">
          {/* Welcome Step */}
          <Show when={step() === 'welcome'}>
            <div class="text-center py-6">
              <div class="w-20 h-20 mx-auto mb-6 rounded-[var(--radius-xl)] bg-[var(--accent)] flex items-center justify-center shadow-lg">
                <Sparkles class="w-10 h-10 text-white" />
              </div>
              <h2 class="text-2xl font-bold text-[var(--text-primary)] font-display mb-3">
                Welcome to Estela
              </h2>
              <p class="text-[var(--text-secondary)] max-w-sm mx-auto leading-relaxed">
                Your multi-agent AI coding assistant. Let's get you set up in just a few steps.
              </p>
            </div>
          </Show>

          {/* Theme Step */}
          <Show when={step() === 'theme'}>
            <div class="space-y-5">
              <div class="text-center mb-6">
                <div class="w-12 h-12 mx-auto mb-3 rounded-[var(--radius-lg)] bg-[var(--accent-subtle)] flex items-center justify-center">
                  <Palette class="w-6 h-6 text-[var(--accent)]" />
                </div>
                <h2 class="text-xl font-semibold text-[var(--text-primary)] font-display">
                  Choose Your Theme
                </h2>
                <p class="text-sm text-[var(--text-muted)] mt-1">
                  Select a visual style that suits you
                </p>
              </div>

              {/* Theme Grid */}
              <div class="grid grid-cols-2 gap-3">
                <For each={themes}>
                  {(t) => (
                    <button
                      type="button"
                      onClick={() => setSelectedTheme(t.id)}
                      class={`
                        flex items-start gap-3 p-3
                        rounded-[var(--radius-lg)] border text-left
                        transition-all duration-[var(--duration-fast)]
                        ${
                          selectedTheme() === t.id
                            ? 'border-[var(--accent)] bg-[var(--accent-subtle)]'
                            : 'border-[var(--border-subtle)] hover:border-[var(--border-default)] hover:bg-[var(--surface-raised)]'
                        }
                      `}
                    >
                      <div
                        class={`
                          p-2 rounded-[var(--radius-md)]
                          ${
                            selectedTheme() === t.id
                              ? 'bg-[var(--accent)] text-white'
                              : 'bg-[var(--surface-raised)] text-[var(--text-tertiary)]'
                          }
                        `}
                      >
                        <Dynamic component={t.icon} class="w-4 h-4" />
                      </div>
                      <div class="flex-1 min-w-0">
                        <p
                          class={`text-sm font-medium ${
                            selectedTheme() === t.id
                              ? 'text-[var(--accent)]'
                              : 'text-[var(--text-primary)]'
                          }`}
                        >
                          {t.name}
                        </p>
                        <p class="text-xs text-[var(--text-muted)] truncate">{t.description}</p>
                      </div>
                      <Show when={selectedTheme() === t.id}>
                        <Check class="w-4 h-4 text-[var(--accent)] flex-shrink-0" />
                      </Show>
                    </button>
                  )}
                </For>
              </div>

              {/* Mode Toggle */}
              <div class="flex gap-2">
                <button
                  type="button"
                  onClick={() => setSelectedMode('light')}
                  class={`
                    flex-1 flex items-center justify-center gap-2 px-4 py-3
                    rounded-[var(--radius-lg)] border text-sm font-medium
                    transition-all duration-[var(--duration-fast)]
                    ${
                      selectedMode() === 'light'
                        ? 'border-[var(--accent)] bg-[var(--accent-subtle)] text-[var(--accent)]'
                        : 'border-[var(--border-subtle)] text-[var(--text-secondary)] hover:border-[var(--border-default)] hover:bg-[var(--surface-raised)]'
                    }
                  `}
                >
                  <Sun class="w-4 h-4" />
                  Light
                </button>
                <button
                  type="button"
                  onClick={() => setSelectedMode('dark')}
                  class={`
                    flex-1 flex items-center justify-center gap-2 px-4 py-3
                    rounded-[var(--radius-lg)] border text-sm font-medium
                    transition-all duration-[var(--duration-fast)]
                    ${
                      selectedMode() === 'dark'
                        ? 'border-[var(--accent)] bg-[var(--accent-subtle)] text-[var(--accent)]'
                        : 'border-[var(--border-subtle)] text-[var(--text-secondary)] hover:border-[var(--border-default)] hover:bg-[var(--surface-raised)]'
                    }
                  `}
                >
                  <Moon class="w-4 h-4" />
                  Dark
                </button>
              </div>
            </div>
          </Show>

          {/* API Keys Step */}
          <Show when={step() === 'api-keys'}>
            <div class="space-y-5">
              <div class="text-center mb-6">
                <div class="w-12 h-12 mx-auto mb-3 rounded-[var(--radius-lg)] bg-[var(--warning-subtle)] flex items-center justify-center">
                  <Key class="w-6 h-6 text-[var(--warning)]" />
                </div>
                <h2 class="text-xl font-semibold text-[var(--text-primary)] font-display">
                  Connect Your API
                </h2>
                <p class="text-sm text-[var(--text-muted)] mt-1">
                  Add at least one API key to get started
                </p>
              </div>

              {/* Anthropic Key */}
              <div>
                <label
                  for="onboarding-anthropic-key"
                  class="block text-sm font-medium text-[var(--text-secondary)] mb-2"
                >
                  Anthropic API Key
                </label>
                <div class="relative">
                  <input
                    id="onboarding-anthropic-key"
                    type={showAnthropicKey() ? 'text' : 'password'}
                    value={anthropicKey()}
                    onInput={(e) => setAnthropicKey(e.currentTarget.value)}
                    placeholder="sk-ant-api03-..."
                    class="
                      w-full px-4 py-2.5 pr-10
                      bg-[var(--input-background)]
                      text-[var(--text-primary)]
                      placeholder-[var(--text-muted)]
                      border border-[var(--input-border)]
                      rounded-[var(--radius-lg)]
                      text-sm
                      transition-all duration-[var(--duration-fast)]
                      focus:outline-none focus:border-[var(--input-border-focus)] focus:ring-2 focus:ring-[var(--accent-subtle)]
                    "
                  />
                  <button
                    type="button"
                    onClick={() => setShowAnthropicKey(!showAnthropicKey())}
                    class="
                      absolute right-3 top-1/2 -translate-y-1/2
                      text-[var(--text-tertiary)] hover:text-[var(--text-primary)]
                      transition-colors duration-[var(--duration-fast)]
                    "
                  >
                    <Show when={showAnthropicKey()} fallback={<Eye class="w-4 h-4" />}>
                      <EyeOff class="w-4 h-4" />
                    </Show>
                  </button>
                </div>
                <p class="text-xs text-[var(--text-muted)] mt-1">Direct access to Claude models</p>
              </div>

              {/* OpenRouter Key */}
              <div>
                <label
                  for="onboarding-openrouter-key"
                  class="block text-sm font-medium text-[var(--text-secondary)] mb-2"
                >
                  OpenRouter API Key
                </label>
                <div class="relative">
                  <input
                    id="onboarding-openrouter-key"
                    type={showOpenrouterKey() ? 'text' : 'password'}
                    value={openrouterKey()}
                    onInput={(e) => setOpenrouterKey(e.currentTarget.value)}
                    placeholder="sk-or-v1-..."
                    class="
                      w-full px-4 py-2.5 pr-10
                      bg-[var(--input-background)]
                      text-[var(--text-primary)]
                      placeholder-[var(--text-muted)]
                      border border-[var(--input-border)]
                      rounded-[var(--radius-lg)]
                      text-sm
                      transition-all duration-[var(--duration-fast)]
                      focus:outline-none focus:border-[var(--input-border-focus)] focus:ring-2 focus:ring-[var(--accent-subtle)]
                    "
                  />
                  <button
                    type="button"
                    onClick={() => setShowOpenrouterKey(!showOpenrouterKey())}
                    class="
                      absolute right-3 top-1/2 -translate-y-1/2
                      text-[var(--text-tertiary)] hover:text-[var(--text-primary)]
                      transition-colors duration-[var(--duration-fast)]
                    "
                  >
                    <Show when={showOpenrouterKey()} fallback={<Eye class="w-4 h-4" />}>
                      <EyeOff class="w-4 h-4" />
                    </Show>
                  </button>
                </div>
                <p class="text-xs text-[var(--text-muted)] mt-1">
                  Access to 100+ models via OpenRouter
                </p>
              </div>

              {/* Security Note */}
              <div class="flex items-start gap-3 p-3 bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-lg)]">
                <Shield class="w-5 h-5 text-[var(--success)] flex-shrink-0 mt-0.5" />
                <p class="text-sm text-[var(--text-secondary)]">
                  API keys are stored locally and never sent to any server except the provider.
                </p>
              </div>
            </div>
          </Show>

          {/* Features Step */}
          <Show when={step() === 'features'}>
            <div class="space-y-5">
              <div class="text-center mb-6">
                <div class="w-12 h-12 mx-auto mb-3 rounded-[var(--radius-lg)] bg-[var(--info-subtle)] flex items-center justify-center">
                  <Rocket class="w-6 h-6 text-[var(--info)]" />
                </div>
                <h2 class="text-xl font-semibold text-[var(--text-primary)] font-display">
                  Powerful Features
                </h2>
                <p class="text-sm text-[var(--text-muted)] mt-1">
                  Here's what you can do with Estela
                </p>
              </div>

              <div class="grid grid-cols-2 gap-3">
                <For each={features}>
                  {(feature) => (
                    <div class="p-4 bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-lg)]">
                      <div class="w-10 h-10 mb-3 rounded-[var(--radius-md)] bg-[var(--accent-subtle)] flex items-center justify-center">
                        <Dynamic component={feature.icon} class="w-5 h-5 text-[var(--accent)]" />
                      </div>
                      <h3 class="text-sm font-medium text-[var(--text-primary)] mb-1">
                        {feature.title}
                      </h3>
                      <p class="text-xs text-[var(--text-muted)]">{feature.description}</p>
                    </div>
                  )}
                </For>
              </div>
            </div>
          </Show>

          {/* Complete Step */}
          <Show when={step() === 'complete'}>
            <div class="text-center py-6">
              <div class="w-20 h-20 mx-auto mb-6 rounded-full bg-[var(--success-subtle)] flex items-center justify-center">
                <Check class="w-10 h-10 text-[var(--success)]" />
              </div>
              <h2 class="text-2xl font-bold text-[var(--text-primary)] font-display mb-3">
                You're All Set!
              </h2>
              <p class="text-[var(--text-secondary)] max-w-sm mx-auto leading-relaxed mb-6">
                Estela is ready to help you with your coding projects. Start a conversation to
                begin!
              </p>
              <Button variant="primary" size="lg" onClick={handleComplete}>
                <Sparkles class="w-5 h-5 mr-2" />
                Get Started
              </Button>
            </div>
          </Show>
        </div>

        {/* Navigation */}
        <Show when={step() !== 'complete'}>
          <div class="flex items-center justify-between pt-6 mt-6 border-t border-[var(--border-subtle)]">
            <Show
              when={step() !== 'welcome'}
              fallback={
                <button
                  type="button"
                  onClick={props.onSkip}
                  class="text-sm text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
                >
                  Skip setup
                </button>
              }
            >
              <Button variant="ghost" onClick={prevStep}>
                <ChevronLeft class="w-4 h-4 mr-1" />
                Back
              </Button>
            </Show>

            <Show
              when={step() !== 'api-keys' || canGoNext()}
              fallback={
                <Button variant="ghost" onClick={nextStep}>
                  Skip for now
                  <ArrowRight class="w-4 h-4 ml-1" />
                </Button>
              }
            >
              <Button variant="primary" onClick={nextStep}>
                Continue
                <ChevronRight class="w-4 h-4 ml-1" />
              </Button>
            </Show>
          </div>
        </Show>
      </div>
    </Dialog>
  )
}
