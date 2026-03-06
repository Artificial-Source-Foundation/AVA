/**
 * Onboarding Screen
 *
 * Premium full-screen onboarding experience.
 * Inspired by Linear, Arc, Cursor, Warp, Raycast.
 * No scrolling — everything fits on screen.
 */

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
import { useSettings } from '../../stores/settings'
import { applyAppearanceToDOM } from '../../stores/settings/settings-appearance'
import { ApiKeysStep } from './onboarding/ApiKeysStep'
import {
  CompleteStep,
  FeaturesStep,
  type ThemeId,
  ThemeStep,
  WelcomeStep,
} from './onboarding/OnboardingSteps'

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
// Component
// ============================================================================

const themePreviewMap = {
  glass: { accentColor: 'violet', codeTheme: 'default' },
  minimal: { accentColor: 'blue', codeTheme: 'github-dark' },
  terminal: { accentColor: 'green', codeTheme: 'monokai' },
} as const

export const OnboardingScreen: Component<OnboardingProps> = (props) => {
  const { settings } = useSettings()
  const [step, setStep] = createSignal<Step>('welcome')
  const [selectedTheme, setSelectedTheme] = createSignal<ThemeId>('glass')
  const [selectedMode, setSelectedMode] = createSignal<'light' | 'dark'>('dark')
  const [anthropicKey, setAnthropicKey] = createSignal('')
  const [openrouterKey, setOpenrouterKey] = createSignal('')
  const [showAnthropicKey, setShowAnthropicKey] = createSignal(false)
  const [showOpenrouterKey, setShowOpenrouterKey] = createSignal(false)

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
          <Show when={step() === 'welcome'}>
            <WelcomeStep onNext={nextStep} onSkip={props.onSkip} />
          </Show>

          <Show when={step() === 'theme'}>
            <ThemeStep
              selectedTheme={selectedTheme()}
              selectedMode={selectedMode()}
              onSelectTheme={setSelectedTheme}
              onSelectMode={setSelectedMode}
              onPrev={prevStep}
              onNext={nextStep}
            />
          </Show>

          <Show when={step() === 'api-keys'}>
            <ApiKeysStep
              anthropicKey={anthropicKey}
              setAnthropicKey={setAnthropicKey}
              showAnthropicKey={showAnthropicKey}
              setShowAnthropicKey={setShowAnthropicKey}
              openrouterKey={openrouterKey}
              setOpenrouterKey={setOpenrouterKey}
              showOpenrouterKey={showOpenrouterKey}
              setShowOpenrouterKey={setShowOpenrouterKey}
              canGoNext={canGoNext}
              onPrev={prevStep}
              onNext={nextStep}
            />
          </Show>

          <Show when={step() === 'features'}>
            <FeaturesStep onPrev={prevStep} onNext={nextStep} />
          </Show>

          <Show when={step() === 'complete'}>
            <CompleteStep onComplete={handleComplete} />
          </Show>
        </div>
      </div>
    </div>
  )
}

// Re-export for backwards compatibility
export { OnboardingScreen as OnboardingDialog }
export type { OnboardingProps as OnboardingDialogProps }
