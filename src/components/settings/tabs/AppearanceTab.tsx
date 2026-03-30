/**
 * Appearance Settings Tab
 *
 * Card-based section layout matching BehaviorTab design.
 * Five cards: Theme, Typography, Layout, Display, Accessibility.
 */

import { Accessibility, Eye, LayoutGrid, Palette, Type } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import type { ThemePreset } from '../../../config/theme-presets'
import type {
  BorderRadius,
  CodeTheme,
  DarkStyle,
  FontSize,
  MonoFont,
  SansFont,
  UIDensity,
} from '../../../stores/settings'
import { isDarkMode, useSettings } from '../../../stores/settings'
import type { ActivityDisplay } from '../../../stores/settings/settings-types'
import { segmentedBtnClass } from '../../ui/SegmentedControl'
import { ToggleRow } from '../../ui/ToggleRow'
import { SettingsCard } from '../SettingsCard'
import { AccentSection, ThemeSelectorCompact } from './appearance-tab'

// ---------------------------------------------------------------------------
// Constants (moved here from sub-components for inline use)
// ---------------------------------------------------------------------------

const FONT_SIZE_OPTIONS: { id: FontSize; label: string }[] = [
  { id: 'small', label: 'Small' },
  { id: 'medium', label: 'Medium' },
  { id: 'large', label: 'Large' },
]

const FONT_SIZE_DESCRIPTIONS: Record<FontSize, string> = {
  small: 'Compact text for more content density',
  medium: 'Default text size for comfortable reading',
  large: 'Larger text for improved readability',
}

const SANS_FONT_OPTIONS: { id: SansFont; label: string }[] = [
  { id: 'default', label: 'Geist' },
  { id: 'inter', label: 'Inter' },
  { id: 'outfit', label: 'Outfit' },
  { id: 'nunito', label: 'Nunito Sans' },
]

const MONO_FONT_OPTIONS: { id: MonoFont; label: string }[] = [
  { id: 'default', label: 'Geist Mono' },
  { id: 'jetbrains', label: 'JetBrains Mono' },
  { id: 'fira', label: 'Fira Code' },
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

const CODE_THEME_OPTIONS: { id: CodeTheme; label: string }[] = [
  { id: 'default', label: 'Default' },
  { id: 'github-dark', label: 'GitHub Dark' },
  { id: 'monokai', label: 'Monokai' },
  { id: 'nord', label: 'Nord' },
  { id: 'solarized-dark', label: 'Solarized' },
  { id: 'catppuccin', label: 'Catppuccin' },
]

const ACTIVITY_OPTIONS: { value: ActivityDisplay; label: string }[] = [
  { value: 'collapsed', label: 'Collapsed' },
  { value: 'expanded', label: 'Expanded' },
  { value: 'hidden', label: 'Hidden' },
]

// ---------------------------------------------------------------------------
// Inline row helper — label + description on left, control on right
// ---------------------------------------------------------------------------

const SettingsRow: Component<{
  label: string
  description?: string
  children: import('solid-js').JSX.Element
}> = (props) => (
  <div class="flex items-center justify-between gap-4">
    <div class="flex flex-col min-w-0" style={{ gap: '2px' }}>
      <span style={{ 'font-family': 'Geist, sans-serif', 'font-size': '13px', color: '#C8C8CC' }}>
        {props.label}
      </span>
      <Show when={props.description}>
        <span style={{ 'font-family': 'Geist, sans-serif', 'font-size': '12px', color: '#48484A' }}>
          {props.description}
        </span>
      </Show>
    </div>
    {props.children}
  </div>
)

// ---------------------------------------------------------------------------
// Main Tab
// ---------------------------------------------------------------------------

export const AppearanceTab: Component = () => {
  const { settings, updateSettings, updateAppearance, previewAppearance, restoreAppearance } =
    useSettings()
  const [activeThemeName, setActiveThemeName] = createSignal<string | undefined>(undefined)

  const applyPreset = (preset: ThemePreset): void => {
    updateSettings({ mode: preset.mode })
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
    setActiveThemeName(preset.name)
  }

  const showDarkStyle = createMemo(() => isDarkMode())

  return (
    <div class="flex flex-col" style={{ gap: '24px' }}>
      {/* Page title */}
      <h2
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '22px',
          'font-weight': '600',
          color: '#F5F5F7',
          margin: '0',
        }}
      >
        Appearance
      </h2>

      {/* ================================================================ */}
      {/* Theme Card                                                       */}
      {/* ================================================================ */}
      <SettingsCard
        icon={Palette}
        title="Theme"
        description="Color mode, theme presets, and accent color"
      >
        {/* Color Mode */}
        <SettingsRow label="Color mode" description="Choose dark, light, or match your system">
          <div class="flex gap-1">
            <button
              type="button"
              onClick={() => updateSettings({ mode: 'dark' })}
              class={segmentedBtnClass(settings().mode === 'dark')}
            >
              Dark
            </button>
            <button
              type="button"
              onClick={() => updateSettings({ mode: 'light' })}
              class={segmentedBtnClass(settings().mode === 'light')}
            >
              Light
            </button>
            <button
              type="button"
              onClick={() => updateSettings({ mode: 'system' })}
              class={segmentedBtnClass(settings().mode === 'system')}
            >
              System
            </button>
          </div>
        </SettingsRow>

        {/* Dark style variant */}
        <Show when={showDarkStyle()}>
          <SettingsRow label="Dark style" description="Variant of dark mode appearance">
            <div class="flex gap-1">
              <For
                each={[
                  { id: 'dark' as DarkStyle, label: 'Default' },
                  { id: 'midnight' as DarkStyle, label: 'Midnight' },
                  { id: 'charcoal' as DarkStyle, label: 'Charcoal' },
                ]}
              >
                {(opt) => (
                  <button
                    type="button"
                    onClick={() => updateAppearance({ darkStyle: opt.id })}
                    onMouseEnter={() => previewAppearance({ darkStyle: opt.id })}
                    onMouseLeave={restoreAppearance}
                    class={segmentedBtnClass(settings().appearance.darkStyle === opt.id)}
                  >
                    {opt.label}
                  </button>
                )}
              </For>
            </div>
          </SettingsRow>
        </Show>

        {/* Theme Preset */}
        <ThemeSelectorCompact onApply={applyPreset} activeThemeName={activeThemeName()} />

        {/* Accent Color — reuse existing component */}
        <AccentSection />
      </SettingsCard>

      {/* ================================================================ */}
      {/* Typography Card                                                  */}
      {/* ================================================================ */}
      <SettingsCard
        icon={Type}
        title="Typography"
        description="Font size, family, and text rendering"
      >
        {/* Font Size */}
        <SettingsRow
          label="Font size"
          description={FONT_SIZE_DESCRIPTIONS[settings().appearance.fontSize ?? 'medium']}
        >
          <div class="flex gap-1">
            <For each={FONT_SIZE_OPTIONS}>
              {(opt) => (
                <button
                  type="button"
                  onClick={() => updateAppearance({ fontSize: opt.id })}
                  class={segmentedBtnClass(settings().appearance.fontSize === opt.id)}
                >
                  {opt.label}
                </button>
              )}
            </For>
          </div>
        </SettingsRow>

        {/* UI Font */}
        <SettingsRow label="UI font" description="Sans-serif font for interface text">
          <div class="flex gap-1">
            <For each={SANS_FONT_OPTIONS}>
              {(font) => (
                <button
                  type="button"
                  onClick={() => updateAppearance({ fontSans: font.id })}
                  class={segmentedBtnClass(settings().appearance.fontSans === font.id)}
                >
                  {font.label}
                </button>
              )}
            </For>
          </div>
        </SettingsRow>

        {/* Monospace Font */}
        <SettingsRow label="Monospace font" description="Font for code blocks and terminal">
          <div class="flex gap-1">
            <For each={MONO_FONT_OPTIONS}>
              {(font) => (
                <button
                  type="button"
                  onClick={() => updateAppearance({ fontMono: font.id })}
                  class={segmentedBtnClass(settings().appearance.fontMono === font.id)}
                >
                  {font.label}
                </button>
              )}
            </For>
          </div>
        </SettingsRow>

        {/* Mono font preview */}
        <p
          style={{
            'margin-top': '0',
            padding: '8px',
            'border-radius': '8px',
            background: 'var(--alpha-white-8)',
            border: '1px solid var(--border-default)',
            'font-family': 'var(--font-mono)',
            'font-size': '12px',
            color: 'var(--text-secondary)',
          }}
        >
          {'const hello = "Preview text in mono font";'}
        </p>

        {/* Ligatures */}
        <ToggleRow
          label="Ligatures"
          description="Enables => and !== style ligatures (Fira Code, JetBrains Mono)"
          checked={settings().appearance.fontLigatures}
          onChange={(v) => updateAppearance({ fontLigatures: v })}
        />

        {/* Chat font size slider */}
        <SettingsRow label="Chat font size">
          <span
            style={{
              'font-family': 'Geist Mono, monospace',
              'font-size': '13px',
              color: '#F5F5F7',
            }}
          >
            {settings().appearance.chatFontSize}px
          </span>
        </SettingsRow>
        <div class="flex items-center gap-2">
          <span
            style={{
              'font-family': 'Geist Mono, monospace',
              'font-size': '12px',
              color: '#48484A',
              width: '24px',
            }}
          >
            11
          </span>
          <input
            type="range"
            min="11"
            max="20"
            step="1"
            value={settings().appearance.chatFontSize}
            onInput={(e) =>
              updateAppearance({ chatFontSize: Number.parseInt(e.currentTarget.value, 10) })
            }
            class="flex-1 h-1 appearance-none rounded-full cursor-pointer"
            style={{ background: 'var(--surface-overlay)', 'accent-color': 'var(--accent)' }}
          />
          <span
            style={{
              'font-family': 'Geist Mono, monospace',
              'font-size': '12px',
              color: '#48484A',
              width: '24px',
              'text-align': 'right',
            }}
          >
            20
          </span>
        </div>
      </SettingsCard>

      {/* ================================================================ */}
      {/* Layout Card                                                      */}
      {/* ================================================================ */}
      <SettingsCard
        icon={LayoutGrid}
        title="Layout"
        description="Interface scale, density, and border radius"
      >
        {/* Interface Scale */}
        <SettingsRow label="Interface scale">
          <span
            style={{
              'font-family': 'Geist Mono, monospace',
              'font-size': '13px',
              color: '#F5F5F7',
            }}
          >
            {Math.round(settings().appearance.uiScale * 100)}%
          </span>
        </SettingsRow>
        <div class="flex items-center gap-2">
          <span
            style={{
              'font-family': 'Geist Mono, monospace',
              'font-size': '12px',
              color: '#48484A',
              width: '32px',
            }}
          >
            85%
          </span>
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
            class="flex-1 h-1 appearance-none rounded-full cursor-pointer"
            style={{ background: 'var(--surface-overlay)', 'accent-color': 'var(--accent)' }}
          />
          <span
            style={{
              'font-family': 'Geist Mono, monospace',
              'font-size': '12px',
              color: '#48484A',
              width: '32px',
              'text-align': 'right',
            }}
          >
            120%
          </span>
        </div>
        <div class="flex gap-1">
          <For each={SCALE_STEPS}>
            {(step) => (
              <button
                type="button"
                onClick={() => updateAppearance({ uiScale: step })}
                style={{
                  padding: '4px 8px',
                  'font-family': 'Geist Mono, monospace',
                  'font-size': '11px',
                  'border-radius': '6px',
                  border: 'none',
                  cursor: 'pointer',
                  transition: 'background 150ms, color 150ms',
                  background:
                    Math.abs(settings().appearance.uiScale - step) < 0.01
                      ? 'var(--accent)'
                      : 'var(--alpha-white-8)',
                  color:
                    Math.abs(settings().appearance.uiScale - step) < 0.01
                      ? 'var(--text-on-accent)'
                      : 'var(--text-muted)',
                }}
              >
                {Math.round(step * 100)}
              </button>
            )}
          </For>
        </div>

        {/* UI Density */}
        <SettingsRow label="UI density" description="Spacing between interface elements">
          <div class="flex gap-1">
            <For each={DENSITY_OPTIONS}>
              {(opt) => (
                <button
                  type="button"
                  onClick={() => updateAppearance({ density: opt.id })}
                  onMouseEnter={() => previewAppearance({ density: opt.id })}
                  onMouseLeave={restoreAppearance}
                  class={segmentedBtnClass(settings().appearance.density === opt.id)}
                >
                  {opt.label}
                </button>
              )}
            </For>
          </div>
        </SettingsRow>

        {/* Border Radius */}
        <SettingsRow label="Border radius" description="Corner rounding for UI elements">
          <div class="flex gap-1">
            <For each={RADIUS_OPTIONS}>
              {(opt) => (
                <button
                  type="button"
                  onClick={() => updateAppearance({ borderRadius: opt.id })}
                  onMouseEnter={() => previewAppearance({ borderRadius: opt.id })}
                  onMouseLeave={restoreAppearance}
                  class={segmentedBtnClass(settings().appearance.borderRadius === opt.id)}
                >
                  {opt.label}
                </button>
              )}
            </For>
          </div>
        </SettingsRow>
        {/* Radius preview */}
        <div class="flex gap-2">
          <div
            style={{
              width: '32px',
              height: '32px',
              background: 'var(--accent-subtle)',
              border: '1px solid var(--accent-border)',
              'border-radius': '4px',
            }}
          />
          <div
            style={{
              width: '32px',
              height: '32px',
              background: 'var(--accent-subtle)',
              border: '1px solid var(--accent-border)',
              'border-radius': '8px',
            }}
          />
          <div
            style={{
              width: '32px',
              height: '32px',
              background: 'var(--accent-subtle)',
              border: '1px solid var(--accent-border)',
              'border-radius': '12px',
            }}
          />
          <div
            style={{
              width: '32px',
              height: '32px',
              background: 'var(--accent-subtle)',
              border: '1px solid var(--accent-border)',
              'border-radius': '16px',
            }}
          />
        </div>

        {/* Sidebar Order */}
        <SidebarOrderInline />
      </SettingsCard>

      {/* ================================================================ */}
      {/* Display Card                                                     */}
      {/* ================================================================ */}
      <SettingsCard
        icon={Eye}
        title="Display"
        description="Thinking, activity display, and code theme"
      >
        {/* Thinking toggle */}
        <ToggleRow
          label="Show thinking"
          description="Display model reasoning in chat"
          checked={settings().appearance.thinkingDisplay !== 'hidden'}
          onChange={(v) => updateAppearance({ thinkingDisplay: v ? 'bubble' : 'hidden' })}
        />

        {/* Auto-compact toggle */}
        <ToggleRow
          label="Auto-compact"
          description="Automatically condense context when approaching limits (managed in generation settings)"
          checked={false}
          onChange={() => {}}
          disabled
        />

        {/* Activity display dropdown */}
        <SettingsRow
          label="Activity display"
          description="How tool calls appear in the chat stream"
        >
          <label
            style={{
              display: 'flex',
              'align-items': 'center',
              gap: '8px',
              'min-width': '160px',
              padding: '6px 12px',
              'border-radius': '8px',
              background: '#ffffff06',
              border: '1px solid #ffffff08',
            }}
          >
            <select
              value={settings().appearance.activityDisplay}
              onChange={(e) =>
                updateAppearance({ activityDisplay: e.currentTarget.value as ActivityDisplay })
              }
              style={{
                width: '100%',
                appearance: 'none',
                border: 'none',
                background: 'transparent',
                'padding-right': '16px',
                'font-family': 'Geist Mono, monospace',
                'font-size': '12px',
                color: '#F5F5F7',
                outline: 'none',
              }}
              aria-label="Activity display"
            >
              <For each={ACTIVITY_OPTIONS}>
                {(opt) => <option value={opt.value}>{opt.label}</option>}
              </For>
            </select>
          </label>
        </SettingsRow>

        {/* Code Theme */}
        <SettingsRow label="Code theme" description="Syntax highlighting style for code blocks">
          <div class="flex flex-wrap gap-1">
            <For each={CODE_THEME_OPTIONS}>
              {(theme) => (
                <button
                  type="button"
                  onClick={() => updateAppearance({ codeTheme: theme.id })}
                  onMouseEnter={() => previewAppearance({ codeTheme: theme.id })}
                  onMouseLeave={restoreAppearance}
                  class={segmentedBtnClass(settings().appearance.codeTheme === theme.id)}
                >
                  {theme.label}
                </button>
              )}
            </For>
          </div>
        </SettingsRow>
        {/* Code preview */}
        <pre
          style={{
            'margin-top': '0',
            padding: '12px',
            'border-radius': '8px',
            border: '1px solid var(--border-default)',
            'font-size': '12px',
            'line-height': '1.6',
            overflow: 'hidden',
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
      </SettingsCard>

      {/* ================================================================ */}
      {/* Accessibility Card                                               */}
      {/* ================================================================ */}
      <SettingsCard
        icon={Accessibility}
        title="Accessibility"
        description="High contrast, reduced motion, and visual aids"
      >
        <ToggleRow
          label="High contrast"
          description="Stronger text and borders for better visibility"
          checked={settings().appearance.highContrast}
          onChange={(v) => updateAppearance({ highContrast: v })}
        />
        <ToggleRow
          label="Reduce motion"
          description="Disables all animations and transitions"
          checked={settings().appearance.reduceMotion}
          onChange={(v) => updateAppearance({ reduceMotion: v })}
        />
      </SettingsCard>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Sidebar Order (inline version for the Layout card)
// ---------------------------------------------------------------------------

import { ChevronDown, ChevronUp } from 'lucide-solid'

const SIDEBAR_LABELS: Record<string, string> = {
  sessions: 'Sessions',
  explorer: 'Explorer',
}

const SidebarOrderInline: Component = () => {
  const { settings, updateUI } = useSettings()

  const order = (): string[] => {
    const saved = settings().ui.sidebarOrder
    return saved?.length ? saved : ['sessions', 'explorer']
  }

  const moveItem = (index: number, direction: -1 | 1): void => {
    const current = [...order()]
    const target = index + direction
    if (target < 0 || target >= current.length) return
    ;[current[index], current[target]] = [current[target], current[index]]
    updateUI({ sidebarOrder: current })
  }

  return (
    <>
      <SettingsRow label="Sidebar order" description="Drag to reorder sidebar sections">
        <span />
      </SettingsRow>
      <div style={{ display: 'flex', 'flex-direction': 'column', gap: '4px' }}>
        <For each={order()}>
          {(id, index) => (
            <div
              class="flex items-center justify-between"
              style={{
                padding: '6px 8px',
                'border-radius': '8px',
                background: 'var(--alpha-white-8)',
              }}
            >
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '13px',
                  color: '#C8C8CC',
                }}
              >
                {SIDEBAR_LABELS[id] ?? id}
              </span>
              <div class="flex" style={{ gap: '2px' }}>
                <button
                  type="button"
                  onClick={() => moveItem(index(), -1)}
                  disabled={index() === 0}
                  style={{
                    padding: '2px',
                    'border-radius': '4px',
                    background: 'transparent',
                    border: 'none',
                    cursor: 'pointer',
                    color: 'var(--text-muted)',
                    opacity: index() === 0 ? '0.3' : '1',
                  }}
                  title="Move up"
                >
                  <ChevronUp style={{ width: '14px', height: '14px' }} />
                </button>
                <button
                  type="button"
                  onClick={() => moveItem(index(), 1)}
                  disabled={index() === order().length - 1}
                  style={{
                    padding: '2px',
                    'border-radius': '4px',
                    background: 'transparent',
                    border: 'none',
                    cursor: 'pointer',
                    color: 'var(--text-muted)',
                    opacity: index() === order().length - 1 ? '0.3' : '1',
                  }}
                  title="Move down"
                >
                  <ChevronDown style={{ width: '14px', height: '14px' }} />
                </button>
              </div>
            </div>
          )}
        </For>
      </div>
    </>
  )
}
