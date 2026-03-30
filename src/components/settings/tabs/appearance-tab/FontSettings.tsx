/**
 * Font & Code Theme Settings
 *
 * FontSection (sans + mono font, ligatures, chat font size) and CodeThemeSection.
 */

import { type Component, For } from 'solid-js'
import type { CodeTheme, FontSize, MonoFont, SansFont } from '../../../../stores/settings'
import { useSettings } from '../../../../stores/settings'
import { SectionHeader, segmentedBtn, Toggle } from './appearance-utils'

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

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

const CODE_THEME_OPTIONS: { id: CodeTheme; label: string }[] = [
  { id: 'default', label: 'Default' },
  { id: 'github-dark', label: 'GitHub Dark' },
  { id: 'monokai', label: 'Monokai' },
  { id: 'nord', label: 'Nord' },
  { id: 'solarized-dark', label: 'Solarized' },
  { id: 'catppuccin', label: 'Catppuccin' },
]

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

// ---------------------------------------------------------------------------
// Font Size Section
// ---------------------------------------------------------------------------

export const FontSizeSection: Component = () => {
  const { settings, updateAppearance } = useSettings()

  return (
    <div>
      <SectionHeader title="Font Size" />
      <div class="flex items-center justify-between py-2">
        <span class="settings-label">Size</span>
        <div class="flex gap-1">
          <For each={FONT_SIZE_OPTIONS}>
            {(opt) => (
              <button
                type="button"
                onClick={() => updateAppearance({ fontSize: opt.id })}
                class={segmentedBtn(settings().appearance.fontSize === opt.id)}
              >
                {opt.label}
              </button>
            )}
          </For>
        </div>
      </div>
      <p class="settings-description">
        {FONT_SIZE_DESCRIPTIONS[settings().appearance.fontSize ?? 'medium']}
      </p>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Font Section
// ---------------------------------------------------------------------------

export const FontSection: Component = () => {
  const { settings, updateAppearance } = useSettings()

  return (
    <div>
      <SectionHeader title="Font" />
      {/* Sans / UI font */}
      <div class="flex items-center justify-between py-2">
        <span class="settings-label">UI Font</span>
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
      <div class="flex items-center justify-between py-2">
        <span class="settings-label">Monospace</span>
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
        style={{
          'margin-top': '4px',
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
      <div class="flex items-center justify-between py-2 mt-1">
        <div>
          <span class="settings-label">Ligatures</span>
          <p class="settings-description" style={{ 'margin-top': '2px' }}>
            {'Enables => and !== style ligatures (Fira Code, JetBrains Mono)'}
          </p>
        </div>
        <Toggle
          checked={settings().appearance.fontLigatures}
          onChange={(v) => updateAppearance({ fontLigatures: v })}
        />
      </div>
      {/* Chat font size */}
      <div class="flex items-center justify-between py-2 mt-1">
        <span class="settings-label">Chat font size</span>
        <span class="font-ui-mono text-[13px] text-[var(--text-primary)]">
          {settings().appearance.chatFontSize}px
        </span>
      </div>
      <div class="flex items-center gap-2 py-1">
        <span class="font-ui-mono w-6 text-[12px] text-[var(--text-muted)]">11</span>
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
        <span class="font-ui-mono w-6 text-right text-[12px] text-[var(--text-muted)]">20</span>
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Code Theme Section
// ---------------------------------------------------------------------------

export const CodeThemeSection: Component = () => {
  const { settings, updateAppearance, previewAppearance, restoreAppearance } = useSettings()

  return (
    <div>
      <SectionHeader title="Code Theme" />
      <div class="flex flex-wrap gap-1 py-2">
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
        style={{
          'margin-top': '8px',
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
    </div>
  )
}
