/**
 * Appearance Settings Tab
 *
 * Color mode, accent color, UI scale, font, border radius, density, and motion.
 * Flat, minimal design matching all other settings tabs.
 */

import { type Component, For } from 'solid-js'
import type { AccentColor, BorderRadius, MonoFont, UIDensity } from '../../../stores/settings'
import { useSettings } from '../../../stores/settings'

// ============================================================================
// Accent Color Presets
// ============================================================================

const ACCENT_PRESETS: { id: AccentColor; label: string; color: string }[] = [
  { id: 'violet', label: 'Violet', color: '#8b5cf6' },
  { id: 'blue', label: 'Blue', color: '#3b82f6' },
  { id: 'green', label: 'Green', color: '#22c55e' },
  { id: 'rose', label: 'Rose', color: '#f43f5e' },
  { id: 'amber', label: 'Amber', color: '#f59e0b' },
  { id: 'cyan', label: 'Cyan', color: '#06b6d4' },
]

// ============================================================================
// Font Options
// ============================================================================

const MONO_FONT_OPTIONS: { id: MonoFont; label: string }[] = [
  { id: 'default', label: 'Geist Mono' },
  { id: 'jetbrains', label: 'JetBrains Mono' },
  { id: 'fira', label: 'Fira Code' },
]

// ============================================================================
// Scale presets
// ============================================================================

const SCALE_STEPS = [0.85, 0.9, 0.95, 1.0, 1.05, 1.1, 1.15, 1.2]

// ============================================================================
// Border Radius Options
// ============================================================================

const RADIUS_OPTIONS: { id: BorderRadius; label: string }[] = [
  { id: 'sharp', label: 'Sharp' },
  { id: 'default', label: 'Default' },
  { id: 'rounded', label: 'Rounded' },
  { id: 'pill', label: 'Pill' },
]

// ============================================================================
// Density Options
// ============================================================================

const DENSITY_OPTIONS: { id: UIDensity; label: string }[] = [
  { id: 'compact', label: 'Compact' },
  { id: 'default', label: 'Normal' },
  { id: 'comfortable', label: 'Comfortable' },
]

// ============================================================================
// Shared button style helper
// ============================================================================

function segmentedBtn(active: boolean): string {
  return `px-2.5 py-1 text-[11px] rounded-[var(--radius-md)] transition-colors ${
    active
      ? 'bg-[var(--accent)] text-white'
      : 'bg-[var(--surface-raised)] text-[var(--text-secondary)] hover:bg-[var(--alpha-white-8)]'
  }`
}

// ============================================================================
// Appearance Tab Component
// ============================================================================

export const AppearanceTab: Component = () => {
  const { settings, updateSettings, updateAppearance } = useSettings()

  return (
    <div class="space-y-5">
      {/* Color Mode */}
      <div>
        <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
          Color Mode
        </h3>
        <div class="flex items-center justify-between py-1.5">
          <span class="text-xs text-[var(--text-secondary)]">Theme</span>
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
          </div>
        </div>
      </div>

      {/* Accent Color */}
      <div>
        <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
          Accent Color
        </h3>
        <div class="flex items-center justify-between py-1.5">
          <span class="text-xs text-[var(--text-secondary)]">Color</span>
          <div class="flex gap-2">
            <For each={ACCENT_PRESETS}>
              {(preset) => (
                <button
                  type="button"
                  onClick={() => updateAppearance({ accentColor: preset.id })}
                  title={preset.label}
                  class={`
                    w-5 h-5 rounded-full transition-all duration-150
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
          </div>
        </div>
        <p class="text-[10px] text-[var(--text-muted)] mt-1">
          {ACCENT_PRESETS.find((p) => p.id === settings().appearance.accentColor)?.label ??
            'Violet'}
        </p>
      </div>

      {/* UI Scale */}
      <div>
        <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
          Interface Scale
        </h3>
        <div class="flex items-center justify-between py-1.5">
          <span class="text-xs text-[var(--text-secondary)]">Scale</span>
          <span class="text-xs font-mono text-[var(--text-primary)]">
            {Math.round(settings().appearance.uiScale * 100)}%
          </span>
        </div>
        <div class="flex items-center gap-2 py-1">
          <span class="text-[10px] text-[var(--text-muted)] w-8">85%</span>
          <input
            type="range"
            min="0"
            max={SCALE_STEPS.length - 1}
            step="1"
            value={SCALE_STEPS.indexOf(
              SCALE_STEPS.reduce((prev, curr) =>
                Math.abs(curr - settings().appearance.uiScale) <
                Math.abs(prev - settings().appearance.uiScale)
                  ? curr
                  : prev
              )
            )}
            onInput={(e) => {
              const idx = Number.parseInt(e.currentTarget.value, 10)
              updateAppearance({ uiScale: SCALE_STEPS[idx] })
            }}
            class="flex-1 h-1 appearance-none bg-[var(--border-default)] rounded-full cursor-pointer accent-[var(--accent)]"
          />
          <span class="text-[10px] text-[var(--text-muted)] w-8 text-right">120%</span>
        </div>
        <div class="flex gap-1 mt-1.5">
          <For each={SCALE_STEPS}>
            {(step) => (
              <button
                type="button"
                onClick={() => updateAppearance({ uiScale: step })}
                class={`
                  px-1.5 py-0.5 text-[10px] rounded-[var(--radius-sm)] transition-colors
                  ${
                    Math.abs(settings().appearance.uiScale - step) < 0.01
                      ? 'bg-[var(--accent)] text-white'
                      : 'bg-[var(--surface-raised)] text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--alpha-white-8)]'
                  }
                `}
              >
                {Math.round(step * 100)}
              </button>
            )}
          </For>
        </div>
      </div>

      {/* Border Radius */}
      <div>
        <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
          Border Radius
        </h3>
        <div class="flex items-center justify-between py-1.5">
          <span class="text-xs text-[var(--text-secondary)]">Corners</span>
          <div class="flex gap-1">
            <For each={RADIUS_OPTIONS}>
              {(opt) => (
                <button
                  type="button"
                  onClick={() => updateAppearance({ borderRadius: opt.id })}
                  class={segmentedBtn(settings().appearance.borderRadius === opt.id)}
                >
                  {opt.label}
                </button>
              )}
            </For>
          </div>
        </div>
        {/* Visual preview of the current radius */}
        <div class="flex gap-2 mt-2">
          <div class="w-8 h-8 bg-[var(--accent-subtle)] border border-[var(--accent-border)] rounded-[var(--radius-sm)]" />
          <div class="w-8 h-8 bg-[var(--accent-subtle)] border border-[var(--accent-border)] rounded-[var(--radius-md)]" />
          <div class="w-8 h-8 bg-[var(--accent-subtle)] border border-[var(--accent-border)] rounded-[var(--radius-lg)]" />
          <div class="w-8 h-8 bg-[var(--accent-subtle)] border border-[var(--accent-border)] rounded-[var(--radius-xl)]" />
        </div>
      </div>

      {/* UI Density */}
      <div>
        <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
          UI Density
        </h3>
        <div class="flex items-center justify-between py-1.5">
          <span class="text-xs text-[var(--text-secondary)]">Spacing</span>
          <div class="flex gap-1">
            <For each={DENSITY_OPTIONS}>
              {(opt) => (
                <button
                  type="button"
                  onClick={() => updateAppearance({ density: opt.id })}
                  class={segmentedBtn(settings().appearance.density === opt.id)}
                >
                  {opt.label}
                </button>
              )}
            </For>
          </div>
        </div>
      </div>

      {/* Font */}
      <div>
        <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
          Font
        </h3>
        <div class="flex items-center justify-between py-1.5">
          <span class="text-xs text-[var(--text-secondary)]">Monospace</span>
          <div class="flex gap-1">
            <For each={MONO_FONT_OPTIONS}>
              {(font) => (
                <button
                  type="button"
                  onClick={() => updateAppearance({ fontMono: font.id })}
                  class={segmentedBtn(settings().appearance.fontMono === font.id)}
                >
                  {font.label}
                </button>
              )}
            </For>
          </div>
        </div>
        <p
          class="mt-2 p-2 rounded-[var(--radius-md)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] text-xs text-[var(--text-secondary)]"
          style={{ 'font-family': 'var(--font-mono)' }}
        >
          const hello = "Preview text in mono font";
        </p>
      </div>

      {/* Reduce Motion */}
      <div>
        <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
          Motion
        </h3>
        <div class="flex items-center justify-between py-1.5">
          <div>
            <span class="text-xs text-[var(--text-secondary)]">Reduce motion</span>
            <p class="text-[10px] text-[var(--text-muted)] mt-0.5">
              Disables all animations and transitions
            </p>
          </div>
          <button
            type="button"
            onClick={() =>
              updateAppearance({
                reduceMotion: !settings().appearance.reduceMotion,
              })
            }
            class={`
              relative w-8 h-[18px] rounded-full transition-colors
              ${
                settings().appearance.reduceMotion
                  ? 'bg-[var(--accent)]'
                  : 'bg-[var(--border-strong)]'
              }
            `}
          >
            <span
              class="absolute top-[2px] left-[2px] w-[14px] h-[14px] rounded-full bg-white transition-transform"
              style={{
                transform: settings().appearance.reduceMotion
                  ? 'translateX(14px)'
                  : 'translateX(0)',
              }}
            />
          </button>
        </div>
      </div>
    </div>
  )
}
