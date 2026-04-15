/**
 * Step 3: Make it Yours
 *
 * Dark/Light/System toggle strip, 3x2 grid of theme preset cards (70px height),
 * accent color swatches (6 circles), navigation.
 */

import { Monitor, Moon, Sun } from 'lucide-solid'
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
  /** Background color for the preview card */
  previewBg: string
}

// NOTE: Theme preset cards intentionally use hardcoded colors since they
// represent specific theme previews, not the active theme.
const THEME_PRESETS: OnboardingThemePreset[] = [
  {
    id: 'default',
    name: 'Default',
    dotColor: '#3B82F6',
    description: '',
    accentColor: 'blue',
    darkStyle: 'dark',
    borderRadius: 'default',
    barColors: ['#3B82F6', '#27272A', '#18181B'],
    previewBg: '#0A0A0C',
  },
  {
    id: 'tokyo-night',
    name: 'Tokyo Night',
    dotColor: '#7AA2F7',
    description: '',
    accentColor: 'blue',
    darkStyle: 'midnight',
    borderRadius: 'default',
    barColors: ['#7AA2F7', '#24283B', '#1A1B26'],
    previewBg: '#1a1b26',
  },
  {
    id: 'dracula',
    name: 'Dracula',
    dotColor: '#BD93F9',
    description: '',
    accentColor: 'violet',
    darkStyle: 'dark',
    borderRadius: 'default',
    barColors: ['#BD93F9', '#44475A', '#282A36'],
    previewBg: '#282a36',
  },
  {
    id: 'nord',
    name: 'Nord',
    dotColor: '#88C0D0',
    description: '',
    accentColor: 'cyan',
    darkStyle: 'charcoal',
    borderRadius: 'rounded',
    barColors: ['#88C0D0', '#3B4252', '#2E3440'],
    previewBg: '#2e3440',
  },
  {
    id: 'night-owl',
    name: 'Night Owl',
    dotColor: '#7FDBCA',
    description: '',
    accentColor: 'green',
    darkStyle: 'midnight',
    borderRadius: 'default',
    barColors: ['#7FDBCA', '#1D3B53', '#011628'],
    previewBg: '#011628',
  },
  {
    id: 'monokai',
    name: 'Monokai',
    dotColor: '#A6E22E',
    description: '',
    accentColor: 'green',
    darkStyle: 'charcoal',
    borderRadius: 'default',
    barColors: ['#A6E22E', '#3E3D32', '#272822'],
    previewBg: '#272822',
  },
]

// ---------------------------------------------------------------------------
// Accent swatches
// ---------------------------------------------------------------------------

// NOTE: Accent swatches use hardcoded colors since they represent
// specific color options the user is choosing between.
const ACCENT_SWATCHES: { id: AccentColor; color: string }[] = [
  { id: 'blue', color: '#3B82F6' },
  { id: 'violet', color: '#8B5CF6' },
  { id: 'green', color: '#22C55E' },
  { id: 'rose', color: '#F43F5E' },
  { id: 'amber', color: '#F59E0B' },
  { id: 'cyan', color: '#06B6D4' },
]

// ---------------------------------------------------------------------------
// Color scheme mode type
// ---------------------------------------------------------------------------

export type ColorSchemeMode = 'dark' | 'light' | 'system'

const COLOR_MODES = [
  { id: 'dark' as const, label: 'Dark', icon: Moon },
  { id: 'light' as const, label: 'Light', icon: Sun },
  { id: 'system' as const, label: 'System', icon: Monitor },
] as const

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

export interface ThemeStepProps {
  selectedPreset: string
  selectedAccent: AccentColor
  selectedMode: ColorSchemeMode
  onSelectPreset: (preset: OnboardingThemePreset) => void
  onSelectAccent: (accent: AccentColor) => void
  onSelectMode: (mode: ColorSchemeMode) => void
  onPrev: () => void
  onNext: () => void
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const ThemeStep: Component<ThemeStepProps> = (props) => {
  return (
    <div class="flex flex-col items-center w-full max-w-[520px]">
      {/* Header */}
      <h2
        tabindex="-1"
        data-onboarding-focus="true"
        class="text-2xl font-bold text-[var(--text-primary)] tracking-tight mb-2"
      >
        Make It Yours
      </h2>
      <p class="text-sm text-[var(--text-muted)] mb-6">Choose a theme preset</p>

      {/* Dark / Light / System toggle strip */}
      <div
        class="w-full grid grid-cols-3 gap-0 mb-6 rounded-lg overflow-hidden"
        style={{
          background: 'var(--surface)',
          border: '1px solid var(--border-subtle)',
          height: '40px',
        }}
      >
        <For each={COLOR_MODES}>
          {(mode) => {
            const Icon = mode.icon
            return (
              <button
                type="button"
                onClick={() => props.onSelectMode(mode.id)}
                class="flex h-full items-center justify-center gap-2 text-sm font-medium transition-colors"
                style={{
                  color:
                    props.selectedMode === mode.id ? 'var(--text-primary)' : 'var(--text-muted)',
                  background: props.selectedMode === mode.id ? 'var(--background)' : 'transparent',
                  'border-right': mode.id !== 'system' ? '1px solid var(--border-subtle)' : 'none',
                  ...(props.selectedMode === mode.id
                    ? { 'box-shadow': 'inset 0 0 0 1px var(--accent)' }
                    : {}),
                }}
              >
                <Icon class="w-3.5 h-3.5" />
                {mode.label}
              </button>
            )
          }}
        </For>
      </div>

      {/* 3x2 grid of theme preset cards */}
      <div class="w-full grid grid-cols-3 gap-2 mb-6">
        <For each={THEME_PRESETS}>
          {(preset) => (
            <button
              type="button"
              onClick={() => props.onSelectPreset(preset)}
              class="overflow-hidden rounded-xl text-left transition-[border-color,transform] duration-[var(--duration-fast)] hover:-translate-y-[1px]"
              style={{
                background: preset.previewBg,
                border:
                  props.selectedPreset === preset.id
                    ? '1px solid var(--accent)'
                    : '1px solid var(--border-subtle)',
                height: '70px',
                padding: '10px 12px',
              }}
            >
              {/* Name at top */}
              <p class="text-xs font-medium text-[var(--text-primary)] mb-auto">{preset.name}</p>

              {/* 3 color dots at bottom */}
              <div class="flex gap-1.5 mt-6">
                <div class="w-2.5 h-2.5 rounded-full" style={{ background: preset.barColors[0] }} />
                <div class="w-2.5 h-2.5 rounded-full" style={{ background: preset.barColors[1] }} />
                <div class="w-2.5 h-2.5 rounded-full" style={{ background: preset.barColors[2] }} />
              </div>
            </button>
          )}
        </For>
      </div>

      {/* ACCENT COLOR label + swatches */}
      <p class="text-[9px] font-semibold uppercase tracking-[0.1em] text-[var(--text-muted)] mb-3 self-start">
        Accent Color
      </p>
      <div class="flex gap-3 mb-10 w-full justify-center">
        <For each={ACCENT_SWATCHES}>
          {(swatch) => (
            <button
              type="button"
              onClick={() => props.onSelectAccent(swatch.id)}
              class="h-6 w-6 rounded-full transition-[transform,box-shadow] duration-[var(--duration-fast)] hover:scale-105"
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

      {/* Navigation: Back <- | (dots in parent) | Continue */}
      <div class="w-full flex items-center justify-between">
        <button
          type="button"
          onClick={() => props.onPrev()}
          class="text-sm text-[var(--text-muted)] hover:text-[var(--text-primary)] transition-colors flex items-center gap-1"
        >
          <span aria-hidden="true">&larr;</span>
          Back
        </button>
        <button
          type="button"
          onClick={() => props.onNext()}
          class="px-6 py-2 bg-[var(--accent)] hover:bg-[var(--accent-hover)] text-white text-sm font-medium rounded-[10px] transition-colors"
        >
          Continue
        </button>
      </div>
    </div>
  )
}

export { THEME_PRESETS as ONBOARDING_THEME_PRESETS }
