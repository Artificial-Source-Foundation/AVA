/**
 * Onboarding Screen
 *
 * Thin adapter that re-exports the new 5-step OnboardingFlow
 * while preserving the API contract expected by App.tsx.
 */

import type { Component } from 'solid-js'
import {
  type OnboardingData as NewOnboardingData,
  OnboardingFlow,
  type OnboardingFlowProps,
  type OnboardingProviderDraft,
} from '../onboarding/OnboardingFlow'

// ============================================================================
// Types — legacy shape consumed by App.tsx
// ============================================================================

export interface OnboardingData {
  theme: string
  mode: 'light' | 'dark' | 'system'
  anthropicKey?: string
  openrouterKey?: string
  /** Accent color selected during onboarding */
  accentColor?: string
  /** Dark style (dark, midnight, charcoal) */
  darkStyle?: string
  /** Border radius preset */
  borderRadius?: string
  /** All provider keys entered (keyed by provider id) */
  providerKeys?: Record<string, string>
  /** OAuth providers connected during onboarding */
  oauthProviders?: string[]
  /** Workspace trust choice */
  workspaceChoice?: string
}

export interface OnboardingProps {
  onComplete: (data: OnboardingData) => void
  onSkip?: () => void
  onDismiss?: (data?: OnboardingProviderDraft) => void
  mode?: 'first-run' | 'guide'
}

// ============================================================================
// Component
// ============================================================================

export const OnboardingScreen: Component<OnboardingProps> = (props) => {
  const handleComplete: OnboardingFlowProps['onComplete'] = (data: NewOnboardingData) => {
    // Map new shape back to legacy shape for App.tsx compatibility
    const legacy: OnboardingData = {
      theme: data.theme,
      mode: data.mode,
      anthropicKey: data.providerKeys.anthropic || undefined,
      openrouterKey: data.providerKeys.openrouter || undefined,
      accentColor: data.accentColor,
      darkStyle: data.darkStyle,
      borderRadius: data.borderRadius,
      providerKeys: data.providerKeys,
      oauthProviders: data.oauthProviders,
      workspaceChoice: data.workspaceChoice,
    }
    props.onComplete(legacy)
  }

  return (
    <OnboardingFlow
      onComplete={handleComplete}
      onSkip={props.onSkip}
      onDismiss={props.onDismiss}
      mode={props.mode}
    />
  )
}

// Re-export for backwards compatibility
export { OnboardingScreen as OnboardingDialog }
export type { OnboardingProps as OnboardingDialogProps }
