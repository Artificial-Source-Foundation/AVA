/**
 * OnboardingFlow
 *
 * Full-screen 5-step onboarding stepper.
 * Steps: Welcome -> Connect a Provider -> Make it Yours -> Workspace -> All Set
 *
 * Manages all step state and emits a single OnboardingData payload on completion.
 */

import { Dialog } from '@kobalte/core/dialog'
import { X } from 'lucide-solid'
import { type Component, createEffect, createSignal, onCleanup, onMount, Show } from 'solid-js'
import type { AccentColor } from '../../stores/settings/settings-types'
import { StepDots } from './StepDots'
import { CompleteStep } from './steps/CompleteStep'
import { ProviderStep } from './steps/ProviderStep'
import {
  type ColorSchemeMode,
  ONBOARDING_THEME_PRESETS,
  type OnboardingThemePreset,
  ThemeStep,
} from './steps/ThemeStep'
import { WelcomeStep } from './steps/WelcomeStep'
import { type WorkspaceChoice, WorkspaceStep } from './steps/WorkspaceStep'

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface OnboardingData {
  theme: string
  mode: ColorSchemeMode
  accentColor: AccentColor
  darkStyle: 'dark' | 'midnight' | 'charcoal'
  borderRadius: 'sharp' | 'default' | 'rounded' | 'pill'
  providerKeys: Record<string, string>
  oauthProviders: string[]
  workspaceChoice: WorkspaceChoice
}

export interface OnboardingProviderDraft {
  providerKeys: Record<string, string>
  oauthProviders: string[]
}

export interface OnboardingFlowProps {
  onComplete: (data: OnboardingData) => void
  onSkip?: () => void
  onDismiss?: (data?: OnboardingProviderDraft) => void
  mode?: 'first-run' | 'guide'
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
  let contentRef: HTMLDivElement | undefined
  const isGuideMode = (): boolean => props.mode === 'guide'

  // Step 2 state: provider keys
  const [providerKeys, setProviderKeys] = createSignal<Record<string, string>>({})
  const [oauthProviders, setOauthProviders] = createSignal<string[]>([])
  const handleSetProviderKey = (id: string, key: string): void => {
    setProviderKeys((prev) => ({ ...prev, [id]: key }))
    if (key.trim()) {
      setOauthProviders((prev) => prev.filter((providerId) => providerId !== id))
    }
  }
  const handleSetProviderConnected = (id: string, connected: boolean): void => {
    setOauthProviders((prev) => {
      const next = prev.filter((providerId) => providerId !== id)
      return connected ? [...next, id] : next
    })
  }

  // Step 3 state: theme preset + accent
  const defaultPreset = ONBOARDING_THEME_PRESETS[0]
  const [selectedPreset, setSelectedPreset] = createSignal<string>(defaultPreset.id)
  const [selectedAccent, setSelectedAccent] = createSignal<AccentColor>(defaultPreset.accentColor)
  const [selectedMode, setSelectedMode] = createSignal<ColorSchemeMode>('dark')
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
  const [currentPath, setCurrentPath] = createSignal('~/Projects')

  onMount(() => {
    void (async () => {
      try {
        const { homeDir } = await import('@tauri-apps/api/path')
        const home = await homeDir()
        setCurrentPath(home)
      } catch {
        // Non-Tauri (web) environment — keep placeholder
      }
    })()
  })

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
      theme: selectedPreset(),
      mode: selectedMode(),
      accentColor: selectedAccent(),
      darkStyle: preset.darkStyle,
      borderRadius: preset.borderRadius,
      providerKeys: providerKeys(),
      oauthProviders: oauthProviders(),
      workspaceChoice: workspaceChoice(),
    })
  }

  const handleDismiss = (): void => {
    props.onDismiss?.({
      providerKeys: providerKeys(),
      oauthProviders: oauthProviders(),
    })
  }

  createEffect(() => {
    step()

    const focusTimer = window.setTimeout(() => {
      window.requestAnimationFrame(() => {
        window.requestAnimationFrame(() => {
          const target = contentRef?.querySelector<HTMLElement>('[data-onboarding-focus="true"]')
          if (!target || !target.isConnected) return
          target.focus()
        })
      })
    }, 0)

    onCleanup(() => window.clearTimeout(focusTimer))
  })

  return (
    <Dialog open modal onOpenChange={() => {}}>
      <Dialog.Portal>
        <Dialog.Overlay
          class="fixed inset-0 z-[60] data-[expanded]:animate-in data-[expanded]:fade-in-0 data-[closed]:animate-out data-[closed]:fade-out-0"
          style={{ background: 'var(--modal-overlay)' }}
          data-testid="onboarding-overlay"
        />
        <Dialog.Content
          class="fixed inset-0 z-[60] flex flex-col bg-[var(--background)] outline-none"
          role="dialog"
          aria-modal="true"
          aria-label="Onboarding"
          onInteractOutside={(event) => event.preventDefault()}
          onEscapeKeyDown={(event) => {
            event.preventDefault()
            if (isGuideMode()) handleDismiss()
          }}
        >
          <Dialog.Title class="sr-only">Onboarding</Dialog.Title>
          <Dialog.Description class="sr-only">
            {isGuideMode()
              ? 'Browse the optional AVA onboarding guide. Use Close guide at any time to return to the app.'
              : 'Complete the initial AVA setup flow.'}
          </Dialog.Description>

          <Show when={isGuideMode()}>
            <div class="absolute right-4 top-4 z-[1] sm:right-6 sm:top-6">
              <button
                type="button"
                onClick={handleDismiss}
                class="inline-flex items-center gap-2 rounded-full border border-[var(--border-subtle)] bg-[color-mix(in_srgb,var(--surface)_88%,transparent)] px-3 py-2 text-sm font-medium text-[var(--text-secondary)] transition-colors hover:text-[var(--text-primary)] focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-2 focus-visible:ring-offset-[var(--background)]"
                aria-label="Close onboarding guide"
              >
                <X class="h-4 w-4" />
                <span>Close guide</span>
              </button>
            </div>
          </Show>

          <div
            ref={contentRef}
            class="flex-1 flex items-center justify-center overflow-y-auto px-6 py-12"
          >
            <Show when={step() === 0}>
              <WelcomeStep
                onNext={next}
                onSkip={() => (isGuideMode() ? handleDismiss() : props.onSkip?.())}
                dismissLabel={isGuideMode() ? 'Close guide' : undefined}
              />
            </Show>
            <Show when={step() === 1}>
              <ProviderStep
                onPrev={prev}
                onNext={next}
                onSkip={next}
                providerKeys={providerKeys()}
                oauthProviders={oauthProviders()}
                onSetProviderKey={handleSetProviderKey}
                onSetProviderConnected={handleSetProviderConnected}
              />
            </Show>
            <Show when={step() === 2}>
              <ThemeStep
                selectedPreset={selectedPreset()}
                selectedAccent={selectedAccent()}
                selectedMode={selectedMode()}
                onSelectPreset={handleSelectPreset}
                onSelectAccent={handleSelectAccent}
                onSelectMode={setSelectedMode}
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

          <div class="pb-8">
            <StepDots total={TOTAL_STEPS} current={step()} />
          </div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog>
  )
}
