/**
 * OnboardingFlow
 *
 * Full-screen 5-step onboarding stepper.
 * Steps: Welcome -> Connect a Provider -> Make it Yours -> Workspace -> All Set
 *
 * Manages all step state and emits a single OnboardingData payload on completion.
 */

import { type Component, createSignal, Show } from 'solid-js'
import type { AccentColor } from '../../stores/settings/settings-types'
import { StepDots } from './StepDots'
import { CompleteStep } from './steps/CompleteStep'
import { ProviderStep } from './steps/ProviderStep'
import { ONBOARDING_THEME_PRESETS, type OnboardingThemePreset, ThemeStep } from './steps/ThemeStep'
import { WelcomeStep } from './steps/WelcomeStep'
import { type WorkspaceChoice, WorkspaceStep } from './steps/WorkspaceStep'

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface OnboardingData {
  theme: string
  mode: 'light' | 'dark'
  accentColor: AccentColor
  darkStyle: 'dark' | 'midnight' | 'charcoal'
  borderRadius: 'sharp' | 'default' | 'rounded' | 'pill'
  providerKeys: Record<string, string>
  workspaceChoice: WorkspaceChoice
}

export interface OnboardingFlowProps {
  onComplete: (data: OnboardingData) => void
  onSkip?: () => void
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

const TOTAL_STEPS = 5

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const OnboardingFlow: Component<OnboardingFlowProps> = (props) => {
  const [step, setStep] = createSignal(0)

  // Step 2 state: provider keys
  const [providerKeys, setProviderKeys] = createSignal<Record<string, string>>({})
  const handleSetProviderKey = (id: string, key: string): void => {
    setProviderKeys((prev) => ({ ...prev, [id]: key }))
  }

  // Step 3 state: theme preset + accent
  const defaultPreset = ONBOARDING_THEME_PRESETS[0]
  const [selectedPreset, setSelectedPreset] = createSignal<string>(defaultPreset.id)
  const [selectedAccent, setSelectedAccent] = createSignal<AccentColor>(defaultPreset.accentColor)
  const [activePreset, setActivePreset] = createSignal<OnboardingThemePreset>(defaultPreset)

  const handleSelectPreset = (preset: OnboardingThemePreset): void => {
    setSelectedPreset(preset.id)
    setSelectedAccent(preset.accentColor)
    setActivePreset(preset)
  }

  const handleSelectAccent = (accent: AccentColor): void => {
    setSelectedAccent(accent)
  }

  // Step 4 state: workspace choice
  const [workspaceChoice, setWorkspaceChoice] = createSignal<WorkspaceChoice>('trust')

  // Resolve current working directory for workspace step
  const currentPath = (): string => {
    // In Tauri, we could use Tauri API. For now use a placeholder.
    return '~/Projects'
  }

  // Navigation
  const next = (): void => {
    if (step() < TOTAL_STEPS - 1) setStep((s) => s + 1)
  }

  const prev = (): void => {
    if (step() > 0) setStep((s) => s - 1)
  }

  const handleComplete = (): void => {
    const preset = activePreset()
    props.onComplete({
      theme: 'glass',
      mode: 'dark',
      accentColor: selectedAccent(),
      darkStyle: preset.darkStyle,
      borderRadius: preset.borderRadius,
      providerKeys: providerKeys(),
      workspaceChoice: workspaceChoice(),
    })
  }

  return (
    <div class="fixed inset-0 bg-[#09090B] flex flex-col items-center justify-center overflow-y-auto">
      {/* Step content */}
      <div class="flex-1 flex items-center justify-center w-full px-6 py-12">
        <Show when={step() === 0}>
          <WelcomeStep onNext={next} onImport={() => props.onSkip?.()} />
        </Show>
        <Show when={step() === 1}>
          <ProviderStep
            onPrev={prev}
            onNext={next}
            onSkip={next}
            providerKeys={providerKeys()}
            onSetProviderKey={handleSetProviderKey}
          />
        </Show>
        <Show when={step() === 2}>
          <ThemeStep
            selectedPreset={selectedPreset()}
            selectedAccent={selectedAccent()}
            onSelectPreset={handleSelectPreset}
            onSelectAccent={handleSelectAccent}
            onPrev={prev}
            onNext={next}
          />
        </Show>
        <Show when={step() === 3}>
          <WorkspaceStep
            selected={workspaceChoice()}
            currentPath={currentPath()}
            onSelect={setWorkspaceChoice}
            onPrev={prev}
            onNext={next}
          />
        </Show>
        <Show when={step() === 4}>
          <CompleteStep onComplete={handleComplete} />
        </Show>
      </div>

      {/* Step dots — fixed at bottom */}
      <div class="pb-8">
        <StepDots total={TOTAL_STEPS} current={step()} />
      </div>
    </div>
  )
}
