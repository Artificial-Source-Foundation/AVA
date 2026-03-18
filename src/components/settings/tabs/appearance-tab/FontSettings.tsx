/**
 * Font & Code Theme Settings
 *
 * FontSection (sans + mono font, ligatures, chat font size) and CodeThemeSection.
 */

import { type Component, For } from 'solid-js'
import type { CodeTheme, MonoFont, SansFont } from '../../../../stores/settings'
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
        <span class="text-[14px] text-[var(--text-secondary)]">UI Font</span>
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
        <span class="text-[14px] text-[var(--text-secondary)]">Monospace</span>
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
      <div class="flex items-center justify-between py-2 mt-1">
        <div>
          <span class="text-[14px] text-[var(--text-secondary)]">Ligatures</span>
          <p class="text-[13px] text-[var(--text-muted)] mt-0.5">
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
        <span class="text-[14px] text-[var(--text-secondary)]">Chat font size</span>
        <span class="text-[14px] font-mono text-[var(--text-primary)]">
          {settings().appearance.chatFontSize}px
        </span>
      </div>
      <div class="flex items-center gap-2 py-1">
        <span class="text-[13px] text-[var(--text-muted)] w-6">11</span>
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
        <span class="text-[13px] text-[var(--text-muted)] w-6 text-right">20</span>
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
