/**
 * Theme Selector Components
 *
 * ColorModeSection, AccentSection, and ThemePresets grid.
 */

import { type Component, createMemo, For, Show } from 'solid-js'
import { THEME_PRESETS, type ThemePreset } from '../../../../config/theme-presets'
import type { AccentColor, DarkStyle } from '../../../../stores/settings'
import { isDarkMode, useSettings } from '../../../../stores/settings'
import { SectionHeader, segmentedBtn } from './appearance-utils'

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

const ACCENT_PRESETS: { id: AccentColor; label: string; color: string }[] = [
  { id: 'violet', label: 'Violet', color: '#8b5cf6' },
  { id: 'blue', label: 'Blue', color: '#3b82f6' },
  { id: 'green', label: 'Green', color: '#22c55e' },
  { id: 'rose', label: 'Rose', color: '#f43f5e' },
  { id: 'amber', label: 'Amber', color: '#f59e0b' },
  { id: 'cyan', label: 'Cyan', color: '#06b6d4' },
]

const DARK_STYLE_OPTIONS: { id: DarkStyle; label: string }[] = [
  { id: 'dark', label: 'Default' },
  { id: 'midnight', label: 'Midnight' },
  { id: 'charcoal', label: 'Charcoal' },
]

// ---------------------------------------------------------------------------
// Color Mode Section
// ---------------------------------------------------------------------------

export const ColorModeSection: Component = () => {
  const { settings, updateSettings, updateAppearance, previewAppearance, restoreAppearance } =
    useSettings()
  const showDarkStyle = createMemo(() => isDarkMode())

  return (
    <div>
      <SectionHeader title="Color Mode" />
      <div class="flex items-center justify-between py-2">
        <span class="text-[var(--settings-text-label)] text-[var(--text-secondary)]">Theme</span>
        <div class="flex gap-1">
          <button
            type="button"
            onClick={() => updateSettings({ mode: 'dark' })}
            class={segmentedBtn(settings().mode === 'dark')}
          >
            Dark
          </button>
          <button
            type="button"
            onClick={() => updateSettings({ mode: 'light' })}
            class={segmentedBtn(settings().mode === 'light')}
          >
            Light
          </button>
          <button
            type="button"
            onClick={() => updateSettings({ mode: 'system' })}
            class={segmentedBtn(settings().mode === 'system')}
          >
            System
          </button>
        </div>
      </div>
      <Show when={showDarkStyle()}>
        <div class="flex items-center justify-between py-2 mt-1">
          <span class="text-[var(--settings-text-label)] text-[var(--text-secondary)]">
            Dark style
          </span>
          <div class="flex gap-1">
            <For each={DARK_STYLE_OPTIONS}>
              {(opt) => (
                <button
                  type="button"
                  onClick={() => updateAppearance({ darkStyle: opt.id })}
                  onMouseEnter={() => previewAppearance({ darkStyle: opt.id })}
                  onMouseLeave={restoreAppearance}
                  class={segmentedBtn(settings().appearance.darkStyle === opt.id)}
                >
                  {opt.label}
                </button>
              )}
            </For>
          </div>
        </div>
      </Show>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Accent Color Section
// ---------------------------------------------------------------------------

export const AccentSection: Component = () => {
  const { settings, updateAppearance, previewAppearance, restoreAppearance } = useSettings()
  const isCustom = createMemo(() => settings().appearance.accentColor === 'custom')

  const accentLabel = createMemo(() => {
    if (isCustom()) return settings().appearance.customAccentColor
    return ACCENT_PRESETS.find((p) => p.id === settings().appearance.accentColor)?.label ?? 'Violet'
  })

  return (
    <div>
      <SectionHeader title="Accent Color" />
      <div class="flex items-center justify-between py-2">
        <span class="text-[var(--settings-text-label)] text-[var(--text-secondary)]">Color</span>
        <div class="flex gap-2 items-center">
          <For each={ACCENT_PRESETS}>
            {(preset) => (
              <button
                type="button"
                onClick={() => updateAppearance({ accentColor: preset.id })}
                onMouseEnter={() => previewAppearance({ accentColor: preset.id })}
                onMouseLeave={restoreAppearance}
                title={preset.label}
                class={`
                  w-6 h-6 rounded-full transition-[transform,opacity] duration-150
                  ${
                    settings().appearance.accentColor === preset.id
                      ? 'scale-110'
                      : 'hover:scale-110 opacity-70 hover:opacity-100'
                  }
                `}
                style={{
                  background: preset.color,
                  'box-shadow':
                    settings().appearance.accentColor === preset.id
                      ? `0 0 0 2px var(--surface-overlay), 0 0 0 4px ${preset.color}`
                      : 'none',
                }}
              />
            )}
          </For>
          {/* Custom accent swatch */}
          <button
            type="button"
            onClick={() => updateAppearance({ accentColor: 'custom' })}
            title="Custom color"
            class={`
              w-6 h-6 rounded-full transition-[transform,opacity] duration-150
              ${isCustom() ? 'scale-110' : 'hover:scale-110 opacity-70 hover:opacity-100'}
            `}
            style={{
              background:
                'conic-gradient(#f43f5e, #f59e0b, #22c55e, #06b6d4, #3b82f6, #8b5cf6, #f43f5e)',
              'box-shadow': isCustom()
                ? `0 0 0 2px var(--surface-overlay), 0 0 0 4px ${settings().appearance.customAccentColor}`
                : 'none',
            }}
          />
        </div>
      </div>
      <Show when={isCustom()}>
        <div class="flex items-center gap-2 mt-2">
          <input
            type="color"
            value={settings().appearance.customAccentColor}
            onInput={(e) => updateAppearance({ customAccentColor: e.currentTarget.value })}
            class="w-7 h-7 rounded-[var(--radius-sm)] border border-[var(--border-default)] cursor-pointer bg-transparent p-0"
          />
          <input
            type="text"
            value={settings().appearance.customAccentColor}
            maxLength={7}
            onInput={(e) => {
              const val = e.currentTarget.value
              if (/^#[0-9a-fA-F]{6}$/.test(val)) {
                updateAppearance({ customAccentColor: val })
              }
            }}
            class="w-24 px-3 py-2 text-[var(--settings-text-description)] font-mono bg-[var(--surface-raised)] text-[var(--text-secondary)] border border-[var(--border-default)] rounded-[var(--radius-md)] outline-none focus:border-[var(--accent)]"
            placeholder="#8b5cf6"
          />
        </div>
      </Show>
      <p class="text-[var(--settings-text-description)] text-[var(--text-muted)] mt-1.5">
        {accentLabel()}
      </p>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Theme Presets Grid
// ---------------------------------------------------------------------------

export interface ThemePresetsGridProps {
  onApply: (preset: ThemePreset) => void
}

export const ThemePresetsGrid: Component<ThemePresetsGridProps> = (props) => (
  <div class="grid grid-cols-3 gap-2">
    <For each={THEME_PRESETS}>
      {(preset) => (
        <button
          type="button"
          onClick={() => props.onApply(preset)}
          class="flex items-center gap-2.5 px-3 py-2.5 rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--surface-base)] hover:border-[var(--accent-muted)] transition-colors text-left"
          title={`${preset.name} (${preset.mode})`}
        >
          <div
            class="w-6 h-6 rounded-full flex-shrink-0 border border-[var(--border-subtle)] flex items-center justify-center"
            style={{ background: preset.swatchAlt }}
          >
            <div class="w-3 h-3 rounded-full" style={{ background: preset.swatch }} />
          </div>
          <div class="min-w-0">
            <p class="text-[var(--settings-text-input)] font-medium text-[var(--text-primary)] truncate">
              {preset.name}
            </p>
            <p class="text-[var(--settings-text-button)] text-[var(--text-muted)]">{preset.mode}</p>
          </div>
        </button>
      )}
    </For>
  </div>
)
