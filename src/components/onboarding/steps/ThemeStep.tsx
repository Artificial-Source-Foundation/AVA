/**
 * Step 3: Make it Yours
 *
 * 2x2 grid of theme preset cards + accent color swatch row.
 * Each preset card shows a dot, name, description, and mini color bar.
 */

import { type Component, For } from 'solid-js'
import type { AccentColor } from '../../../stores/settings/settings-types'

// ---------------------------------------------------------------------------
// Theme Presets for Onboarding
// ---------------------------------------------------------------------------

export interface OnboardingThemePreset {
  id: string
  name: string
  dotColor: string
  description: string
  accentColor: AccentColor
  customAccentColor?: string
  darkStyle: 'dark' | 'midnight' | 'charcoal'
  borderRadius: 'sharp' | 'default' | 'rounded' | 'pill'
  barColors: [string, string, string]
}

// NOTE: Theme preset cards intentionally use hardcoded colors since they
// represent specific theme previews, not the active theme.
const THEME_PRESETS: OnboardingThemePreset[] = [
  {
    id: 'soft-zinc',
    name: 'Soft Zinc',
    dotColor: '#8B5CF6',
    description: 'Dark \u00B7 Violet accent \u00B7 Default radius',
    accentColor: 'violet',
    darkStyle: 'dark',
    borderRadius: 'default',
    barColors: ['#8B5CF6', '#27272A', '#18181B'],
  },
  {
    id: 'ocean-blue',
    name: 'Ocean Blue',
    dotColor: '#3B82F6',
    description: 'Midnight \u00B7 Blue accent \u00B7 Rounded',
    accentColor: 'blue',
    darkStyle: 'midnight',
    borderRadius: 'rounded',
    barColors: ['#3B82F6', '#1E293B', '#0F172A'],
  },
  {
    id: 'forest',
    name: 'Forest',
    dotColor: '#22C55E',
    description: 'Charcoal \u00B7 Green accent \u00B7 Pill',
    accentColor: 'green',
    darkStyle: 'charcoal',
    borderRadius: 'pill',
    barColors: ['#22C55E', '#1C1C1F', '#111113'],
  },
  {
    id: 'rose',
    name: 'Rose',
    dotColor: '#F43F5E',
    description: 'Dark \u00B7 Rose accent \u00B7 Sharp',
    accentColor: 'rose',
    darkStyle: 'dark',
    borderRadius: 'sharp',
    barColors: ['#F43F5E', '#27272A', '#18181B'],
  },
]

// ---------------------------------------------------------------------------
// Accent swatches
// ---------------------------------------------------------------------------

// NOTE: Accent swatches use hardcoded colors since they represent
// specific color options the user is choosing between.
const ACCENT_SWATCHES: { id: AccentColor; color: string }[] = [
  { id: 'violet', color: '#8B5CF6' },
  { id: 'blue', color: '#3B82F6' },
  { id: 'green', color: '#22C55E' },
  { id: 'rose', color: '#F43F5E' },
  { id: 'amber', color: '#F59E0B' },
  { id: 'cyan', color: '#06B6D4' },
]

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

export interface ThemeStepProps {
  selectedPreset: string
  selectedAccent: AccentColor
  onSelectPreset: (preset: OnboardingThemePreset) => void
  onSelectAccent: (accent: AccentColor) => void
  onPrev: () => void
  onNext: () => void
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const ThemeStep: Component<ThemeStepProps> = (props) => (
  <div class="flex flex-col items-center">
    {/* Header */}
    <h2 class="text-2xl font-bold text-[var(--text-primary)] tracking-tight mb-2">Make it Yours</h2>
    <p class="text-sm text-[var(--text-muted)] mb-6">Choose a look that fits your style</p>

    {/* Theme Presets label */}
    <p class="text-xs font-medium text-[var(--text-muted)] uppercase tracking-wider mb-3 self-start max-w-[640px] w-full mx-auto">
      Theme Presets
    </p>

    {/* 2x2 grid */}
    <div class="w-full max-w-[640px] grid grid-cols-2 gap-3 mb-6">
      <For each={THEME_PRESETS}>
        {(preset) => (
          <button
            type="button"
            onClick={() => props.onSelectPreset(preset)}
            class="bg-[var(--surface-raised)] border rounded-xl p-4 text-left transition-all hover:border-[var(--gray-6)]"
            classList={{
              'border-[var(--accent)]': props.selectedPreset === preset.id,
              'border-[var(--gray-5)]': props.selectedPreset !== preset.id,
            }}
          >
            {/* Dot + name */}
            <div class="flex items-center gap-2 mb-1.5">
              <div
                class="w-3 h-3 rounded-full flex-shrink-0"
                style={{ background: preset.dotColor }}
              />
              <span class="text-sm font-medium text-[var(--text-primary)]">{preset.name}</span>
            </div>

            {/* Description */}
            <p class="text-xs text-[var(--text-muted)] mb-3">{preset.description}</p>

            {/* Mini color bar preview */}
            <div class="flex gap-1 h-2 rounded-full overflow-hidden">
              <div class="flex-1 rounded-full" style={{ background: preset.barColors[0] }} />
              <div class="flex-[2] rounded-full" style={{ background: preset.barColors[1] }} />
              <div class="flex-1 rounded-full" style={{ background: preset.barColors[2] }} />
            </div>
          </button>
        )}
      </For>
    </div>

    {/* Accent color swatches */}
    <p class="text-xs text-[var(--text-muted)] mb-3 self-start max-w-[640px] w-full mx-auto">
      Or pick an accent color
    </p>
    <div class="flex gap-3 mb-10 max-w-[640px] w-full">
      <For each={ACCENT_SWATCHES}>
        {(swatch) => (
          <button
            type="button"
            onClick={() => props.onSelectAccent(swatch.id)}
            class="w-8 h-8 rounded-full transition-all"
            classList={{
              'ring-2 ring-white ring-offset-2 ring-offset-[var(--background)]':
                props.selectedAccent === swatch.id,
            }}
            style={{ background: swatch.color }}
            title={swatch.id}
          />
        )}
      </For>
    </div>

    {/* Navigation */}
    <div class="w-full max-w-[640px] flex items-center justify-between">
      <button
        type="button"
        onClick={props.onPrev}
        class="text-sm text-[var(--text-muted)] hover:text-[var(--text-primary)] transition-colors"
      >
        Back
      </button>
      <button
        type="button"
        onClick={props.onNext}
        class="px-6 py-2.5 bg-[var(--accent)] hover:bg-[var(--violet-8)] text-white text-sm font-medium rounded-xl transition-colors"
      >
        Continue
      </button>
    </div>
  </div>
)

export { THEME_PRESETS as ONBOARDING_THEME_PRESETS }
