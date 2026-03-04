/**
 * Appearance Settings Tab
 *
 * Color mode (system + dark variants), accent (+ custom hex), UI scale, border radius,
 * density, font (sans + mono + ligatures + chat size), code theme (6 presets + preview),
 * and accessibility (high contrast + reduce motion).
 */

import {
  Accessibility,
  ChevronDown,
  ChevronUp,
  Code2,
  Maximize2,
  Moon,
  Palette,
  PanelLeft,
  Radius,
  SlidersHorizontal,
  Type,
} from 'lucide-solid'
import { type Component, createMemo, For, Show } from 'solid-js'
import { THEME_PRESETS, type ThemePreset } from '../../../config/theme-presets'
import type {
  AccentColor,
  BorderRadius,
  CodeTheme,
  DarkStyle,
  MonoFont,
  SansFont,
  UIDensity,
} from '../../../stores/settings'
import { isDarkMode, useSettings } from '../../../stores/settings'
import { SettingsCard } from '../SettingsCard'

// ============================================================================
// Constants
// ============================================================================

const ACCENT_PRESETS: { id: AccentColor; label: string; color: string }[] = [
  { id: 'violet', label: 'Violet', color: '#8b5cf6' },
  { id: 'blue', label: 'Blue', color: '#3b82f6' },
  { id: 'green', label: 'Green', color: '#22c55e' },
  { id: 'rose', label: 'Rose', color: '#f43f5e' },
  { id: 'amber', label: 'Amber', color: '#f59e0b' },
  { id: 'cyan', label: 'Cyan', color: '#06b6d4' },
]

const MONO_FONT_OPTIONS: { id: MonoFont; label: string }[] = [
  { id: 'default', label: 'Geist Mono' },
  { id: 'jetbrains', label: 'JetBrains Mono' },
  { id: 'fira', label: 'Fira Code' },
]

const SANS_FONT_OPTIONS: { id: SansFont; label: string }[] = [
  { id: 'default', label: 'Geist' },
  { id: 'inter', label: 'Inter' },
  { id: 'outfit', label: 'Outfit' },
  { id: 'nunito', label: 'Nunito Sans' },
]

const SCALE_STEPS = [0.85, 0.9, 0.95, 1.0, 1.05, 1.1, 1.15, 1.2]

const RADIUS_OPTIONS: { id: BorderRadius; label: string }[] = [
  { id: 'sharp', label: 'Sharp' },
  { id: 'default', label: 'Default' },
  { id: 'rounded', label: 'Rounded' },
  { id: 'pill', label: 'Pill' },
]

const DENSITY_OPTIONS: { id: UIDensity; label: string }[] = [
  { id: 'compact', label: 'Compact' },
  { id: 'default', label: 'Normal' },
  { id: 'comfortable', label: 'Comfortable' },
]

const DARK_STYLE_OPTIONS: { id: DarkStyle; label: string }[] = [
  { id: 'dark', label: 'Default' },
  { id: 'midnight', label: 'Midnight' },
  { id: 'charcoal', label: 'Charcoal' },
]

const CODE_THEME_OPTIONS: { id: CodeTheme; label: string }[] = [
  { id: 'default', label: 'Default' },
  { id: 'github-dark', label: 'GitHub Dark' },
  { id: 'monokai', label: 'Monokai' },
  { id: 'nord', label: 'Nord' },
  { id: 'solarized-dark', label: 'Solarized' },
  { id: 'catppuccin', label: 'Catppuccin' },
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
// Section header
// ============================================================================

const SectionHeader: Component<{ title: string }> = (props) => (
  <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
    {props.title}
  </h3>
)

// ============================================================================
// Toggle switch
// ============================================================================

const Toggle: Component<{ checked: boolean; onChange: (v: boolean) => void }> = (props) => (
  <button
    type="button"
    onClick={() => props.onChange(!props.checked)}
    class={`
      relative w-8 h-[18px] rounded-full transition-colors
      ${props.checked ? 'bg-[var(--accent)]' : 'bg-[var(--border-strong)]'}
    `}
  >
    <span
      class="absolute top-[2px] left-[2px] w-[14px] h-[14px] rounded-full bg-white transition-transform"
      style={{
        transform: props.checked ? 'translateX(14px)' : 'translateX(0)',
      }}
    />
  </button>
)

// ============================================================================
// Color Mode Section
// ============================================================================

const ColorModeSection: Component = () => {
  const { settings, updateSettings, updateAppearance, previewAppearance, restoreAppearance } =
    useSettings()
  const showDarkStyle = createMemo(() => isDarkMode())

  return (
    <div>
      <SectionHeader title="Color Mode" />
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
        <div class="flex items-center justify-between py-1.5 mt-1">
          <span class="text-xs text-[var(--text-secondary)]">Dark style</span>
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

// ============================================================================
// Accent Color Section
// ============================================================================

const AccentSection: Component = () => {
  const { settings, updateAppearance, previewAppearance, restoreAppearance } = useSettings()
  const isCustom = createMemo(() => settings().appearance.accentColor === 'custom')

  const accentLabel = createMemo(() => {
    if (isCustom()) return settings().appearance.customAccentColor
    return ACCENT_PRESETS.find((p) => p.id === settings().appearance.accentColor)?.label ?? 'Violet'
  })

  return (
    <div>
      <SectionHeader title="Accent Color" />
      <div class="flex items-center justify-between py-1.5">
        <span class="text-xs text-[var(--text-secondary)]">Color</span>
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
                  w-5 h-5 rounded-full transition-[transform,opacity] duration-150
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
              w-5 h-5 rounded-full transition-[transform,opacity] duration-150
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
            class="w-20 px-2 py-1 text-[11px] font-mono bg-[var(--surface-raised)] text-[var(--text-secondary)] border border-[var(--border-default)] rounded-[var(--radius-md)] outline-none focus:border-[var(--accent)]"
            placeholder="#8b5cf6"
          />
        </div>
      </Show>
      <p class="text-[10px] text-[var(--text-muted)] mt-1">{accentLabel()}</p>
    </div>
  )
}

// ============================================================================
// Font Section
// ============================================================================

const FontSection: Component = () => {
  const { settings, updateAppearance } = useSettings()

  return (
    <div>
      <SectionHeader title="Font" />
      {/* Sans / UI font */}
      <div class="flex items-center justify-between py-1.5">
        <span class="text-xs text-[var(--text-secondary)]">UI Font</span>
        <div class="flex gap-1">
          <For each={SANS_FONT_OPTIONS}>
            {(font) => (
              <button
                type="button"
                onClick={() => updateAppearance({ fontSans: font.id })}
                class={segmentedBtn(settings().appearance.fontSans === font.id)}
              >
                {font.label}
              </button>
            )}
          </For>
        </div>
      </div>
      {/* Monospace font */}
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
      {/* Mono font preview */}
      <p
        class="mt-1 p-2 rounded-[var(--radius-md)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] text-xs text-[var(--text-secondary)]"
        style={{ 'font-family': 'var(--font-mono)' }}
      >
        {'const hello = "Preview text in mono font";'}
      </p>
      {/* Ligatures */}
      <div class="flex items-center justify-between py-1.5 mt-1">
        <div>
          <span class="text-xs text-[var(--text-secondary)]">Ligatures</span>
          <p class="text-[10px] text-[var(--text-muted)] mt-0.5">
            {'Enables => and !== style ligatures (Fira Code, JetBrains Mono)'}
          </p>
        </div>
        <Toggle
          checked={settings().appearance.fontLigatures}
          onChange={(v) => updateAppearance({ fontLigatures: v })}
        />
      </div>
      {/* Chat font size */}
      <div class="flex items-center justify-between py-1.5 mt-1">
        <span class="text-xs text-[var(--text-secondary)]">Chat font size</span>
        <span class="text-xs font-mono text-[var(--text-primary)]">
          {settings().appearance.chatFontSize}px
        </span>
      </div>
      <div class="flex items-center gap-2 py-1">
        <span class="text-[10px] text-[var(--text-muted)] w-6">11</span>
        <input
          type="range"
          min="11"
          max="20"
          step="1"
          value={settings().appearance.chatFontSize}
          onInput={(e) =>
            updateAppearance({ chatFontSize: Number.parseInt(e.currentTarget.value, 10) })
          }
          class="flex-1 h-1 appearance-none bg-[var(--border-default)] rounded-full cursor-pointer accent-[var(--accent)]"
        />
        <span class="text-[10px] text-[var(--text-muted)] w-6 text-right">20</span>
      </div>
    </div>
  )
}

// ============================================================================
// Code Theme Section
// ============================================================================

const CodeThemeSection: Component = () => {
  const { settings, updateAppearance, previewAppearance, restoreAppearance } = useSettings()

  return (
    <div>
      <SectionHeader title="Code Theme" />
      <div class="flex flex-wrap gap-1 py-1.5">
        <For each={CODE_THEME_OPTIONS}>
          {(theme) => (
            <button
              type="button"
              onClick={() => updateAppearance({ codeTheme: theme.id })}
              onMouseEnter={() => previewAppearance({ codeTheme: theme.id })}
              onMouseLeave={restoreAppearance}
              class={segmentedBtn(settings().appearance.codeTheme === theme.id)}
            >
              {theme.label}
            </button>
          )}
        </For>
      </div>
      {/* Live preview */}
      <pre
        class="mt-2 p-3 rounded-[var(--radius-md)] border border-[var(--border-subtle)] text-xs leading-relaxed overflow-hidden"
        style={{
          background: 'var(--code-background)',
          color: 'var(--code-text)',
          'font-family': 'var(--font-mono)',
        }}
      >
        <code>
          <span style={{ color: 'var(--syntax-keyword)' }}>const</span>{' '}
          <span style={{ color: 'var(--syntax-variable)' }}>greeting</span>
          {' = '}
          <span style={{ color: 'var(--syntax-string)' }}>"hello world"</span>
          {';\n'}
          <span style={{ color: 'var(--syntax-keyword)' }}>function</span>{' '}
          <span style={{ color: 'var(--syntax-function)' }}>render</span>
          {'('}
          <span style={{ color: 'var(--syntax-variable)' }}>n</span>
          {': '}
          <span style={{ color: 'var(--syntax-type)' }}>number</span>
          {') {\n  '}
          <span style={{ color: 'var(--syntax-keyword)' }}>return</span>{' '}
          <span style={{ color: 'var(--syntax-variable)' }}>n</span>{' '}
          <span style={{ color: 'var(--syntax-operator)' }}>*</span>{' '}
          <span style={{ color: 'var(--syntax-number)' }}>42</span>
          {'; '}
          <span style={{ color: 'var(--syntax-comment)' }}>{'// magic'}</span>
          {'\n}'}
        </code>
      </pre>
    </div>
  )
}

// ============================================================================
// Accessibility Section
// ============================================================================

const AccessibilitySection: Component = () => {
  const { settings, updateAppearance } = useSettings()

  return (
    <div>
      <SectionHeader title="Accessibility" />
      {/* High contrast */}
      <div class="flex items-center justify-between py-1.5">
        <div>
          <span class="text-xs text-[var(--text-secondary)]">High contrast</span>
          <p class="text-[10px] text-[var(--text-muted)] mt-0.5">Stronger text and borders</p>
        </div>
        <Toggle
          checked={settings().appearance.highContrast}
          onChange={(v) => updateAppearance({ highContrast: v })}
        />
      </div>
      {/* Reduce motion */}
      <div class="flex items-center justify-between py-1.5">
        <div>
          <span class="text-xs text-[var(--text-secondary)]">Reduce motion</span>
          <p class="text-[10px] text-[var(--text-muted)] mt-0.5">
            Disables all animations and transitions
          </p>
        </div>
        <Toggle
          checked={settings().appearance.reduceMotion}
          onChange={(v) => updateAppearance({ reduceMotion: v })}
        />
      </div>
    </div>
  )
}

// ============================================================================
// Sidebar Order Section
// ============================================================================

const SIDEBAR_LABELS: Record<string, string> = {
  sessions: 'Sessions',
  explorer: 'Explorer',
}

const SidebarOrderSection: Component = () => {
  const { settings, updateUI } = useSettings()

  const order = () => {
    const saved = settings().ui.sidebarOrder
    return saved?.length ? saved : ['sessions', 'explorer']
  }

  const moveItem = (index: number, direction: -1 | 1) => {
    const current = [...order()]
    const target = index + direction
    if (target < 0 || target >= current.length) return
    ;[current[index], current[target]] = [current[target], current[index]]
    updateUI({ sidebarOrder: current })
  }

  return (
    <div>
      <SectionHeader title="Sidebar Order" />
      <div class="space-y-1">
        <For each={order()}>
          {(id, index) => (
            <div class="flex items-center justify-between py-1 px-2 rounded-[var(--radius-md)] bg-[var(--surface-raised)]">
              <span class="text-xs text-[var(--text-secondary)]">{SIDEBAR_LABELS[id] ?? id}</span>
              <div class="flex gap-0.5">
                <button
                  type="button"
                  onClick={() => moveItem(index(), -1)}
                  disabled={index() === 0}
                  class="p-0.5 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--alpha-white-05)] disabled:opacity-30 disabled:pointer-events-none transition-colors"
                  title="Move up"
                >
                  <ChevronUp class="w-3.5 h-3.5" />
                </button>
                <button
                  type="button"
                  onClick={() => moveItem(index(), 1)}
                  disabled={index() === order().length - 1}
                  class="p-0.5 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--alpha-white-05)] disabled:opacity-30 disabled:pointer-events-none transition-colors"
                  title="Move down"
                >
                  <ChevronDown class="w-3.5 h-3.5" />
                </button>
              </div>
            </div>
          )}
        </For>
      </div>
    </div>
  )
}

// ============================================================================
// Appearance Tab Component
// ============================================================================

export const AppearanceTab: Component = () => {
  const { settings, updateSettings, updateAppearance, previewAppearance, restoreAppearance } =
    useSettings()

  const applyPreset = (preset: ThemePreset) => {
    // Apply mode setting
    updateSettings({ mode: preset.mode })
    // Apply appearance settings
    const appearance: Record<string, unknown> = {
      accentColor: preset.accentColor,
      codeTheme: preset.codeTheme,
      borderRadius: preset.borderRadius,
    }
    if (preset.customAccentColor) {
      appearance.customAccentColor = preset.customAccentColor
    }
    if (preset.darkStyle) {
      appearance.darkStyle = preset.darkStyle
    }
    updateAppearance(appearance)
  }

  return (
    <div class="grid grid-cols-1 gap-4">
      <SettingsCard icon={Moon} title="Color Mode" description="Theme and dark style variant">
        <ColorModeSection />
      </SettingsCard>

      <SettingsCard
        icon={Palette}
        title="Theme Presets"
        description="One-click theme configurations"
      >
        <div class="grid grid-cols-3 gap-1.5">
          <For each={THEME_PRESETS}>
            {(preset) => (
              <button
                type="button"
                onClick={() => applyPreset(preset)}
                class="flex items-center gap-2 px-2 py-1.5 rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--surface-base)] hover:border-[var(--accent-muted)] transition-colors text-left"
                title={`${preset.name} (${preset.mode})`}
              >
                <div
                  class="w-5 h-5 rounded-full flex-shrink-0 border border-[var(--border-subtle)] flex items-center justify-center"
                  style={{ background: preset.swatchAlt }}
                >
                  <div class="w-2.5 h-2.5 rounded-full" style={{ background: preset.swatch }} />
                </div>
                <div class="min-w-0">
                  <p class="text-[10px] font-medium text-[var(--text-primary)] truncate">
                    {preset.name}
                  </p>
                  <p class="text-[9px] text-[var(--text-muted)]">{preset.mode}</p>
                </div>
              </button>
            )}
          </For>
        </div>
      </SettingsCard>

      <SettingsCard
        icon={Palette}
        title="Accent Color"
        description="Primary accent color throughout the UI"
      >
        <AccentSection />
      </SettingsCard>

      <SettingsCard
        icon={Maximize2}
        title="Interface Scale"
        description="Adjust overall UI zoom level"
      >
        <div>
          <SectionHeader title="Scale" />
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
      </SettingsCard>

      <SettingsCard icon={Radius} title="Border Radius" description="Corner rounding style">
        <div class="flex items-center justify-between py-1.5">
          <span class="text-xs text-[var(--text-secondary)]">Corners</span>
          <div class="flex gap-1">
            <For each={RADIUS_OPTIONS}>
              {(opt) => (
                <button
                  type="button"
                  onClick={() => updateAppearance({ borderRadius: opt.id })}
                  onMouseEnter={() => previewAppearance({ borderRadius: opt.id })}
                  onMouseLeave={restoreAppearance}
                  class={segmentedBtn(settings().appearance.borderRadius === opt.id)}
                >
                  {opt.label}
                </button>
              )}
            </For>
          </div>
        </div>
        <div class="flex gap-2 mt-2">
          <div class="w-8 h-8 bg-[var(--accent-subtle)] border border-[var(--accent-border)] rounded-[var(--radius-sm)]" />
          <div class="w-8 h-8 bg-[var(--accent-subtle)] border border-[var(--accent-border)] rounded-[var(--radius-md)]" />
          <div class="w-8 h-8 bg-[var(--accent-subtle)] border border-[var(--accent-border)] rounded-[var(--radius-lg)]" />
          <div class="w-8 h-8 bg-[var(--accent-subtle)] border border-[var(--accent-border)] rounded-[var(--radius-xl)]" />
        </div>
      </SettingsCard>

      <SettingsCard
        icon={SlidersHorizontal}
        title="UI Density"
        description="Spacing between elements"
      >
        <div class="flex items-center justify-between py-1.5">
          <span class="text-xs text-[var(--text-secondary)]">Spacing</span>
          <div class="flex gap-1">
            <For each={DENSITY_OPTIONS}>
              {(opt) => (
                <button
                  type="button"
                  onClick={() => updateAppearance({ density: opt.id })}
                  onMouseEnter={() => previewAppearance({ density: opt.id })}
                  onMouseLeave={restoreAppearance}
                  class={segmentedBtn(settings().appearance.density === opt.id)}
                >
                  {opt.label}
                </button>
              )}
            </For>
          </div>
        </div>
      </SettingsCard>

      <SettingsCard icon={Type} title="Font" description="UI and monospace font settings">
        <FontSection />
      </SettingsCard>

      <SettingsCard icon={Code2} title="Code Theme" description="Syntax highlighting theme">
        <CodeThemeSection />
      </SettingsCard>

      <SettingsCard
        icon={Accessibility}
        title="Accessibility"
        description="High contrast and motion preferences"
      >
        <AccessibilitySection />
      </SettingsCard>

      <SettingsCard icon={PanelLeft} title="Sidebar Order" description="Reorder sidebar sections">
        <SidebarOrderSection />
      </SettingsCard>
    </div>
  )
}
