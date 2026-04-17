/**
 * Theme Selector Components
 *
 * ColorModeSection, AccentSection, and compact ThemeSelector with browse modal.
 */

import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { Portal } from 'solid-js/web'
import { THEME_PRESETS, type ThemePreset } from '../../../../config/theme-presets'
import type { AccentColor, DarkStyle } from '../../../../stores/settings'
import { isDarkMode, useSettings } from '../../../../stores/settings'
import { useSettingsDialogEscape } from '../../settings-dialog-utils'
import { SectionHeader, segmentedBtn } from './appearance-utils'

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

const ACCENT_PRESETS: { id: AccentColor; label: string; color: string }[] = [
  { id: 'blue', label: 'Blue', color: '#0A84FF' },
  { id: 'violet', label: 'Violet', color: '#8b5cf6' },
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
        <span class="settings-label">Theme</span>
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
          <span class="settings-label">Dark style</span>
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
    return ACCENT_PRESETS.find((p) => p.id === settings().appearance.accentColor)?.label ?? 'Blue'
  })

  return (
    <div>
      <SectionHeader title="Accent Color" />
      <div class="flex items-center justify-between py-2">
        <span class="settings-label">Color</span>
        <div class="flex gap-2 items-center">
          <For each={ACCENT_PRESETS}>
            {(preset) => (
              <button
                type="button"
                onClick={() => updateAppearance({ accentColor: preset.id })}
                onMouseEnter={() => previewAppearance({ accentColor: preset.id })}
                onMouseLeave={restoreAppearance}
                title={preset.label}
                aria-label={`Use ${preset.label} accent`}
                style={{
                  width: '24px',
                  height: '24px',
                  'border-radius': '50%',
                  border:
                    settings().appearance.accentColor === preset.id
                      ? `2px solid ${preset.color}`
                      : '2px solid transparent',
                  'box-shadow':
                    settings().appearance.accentColor === preset.id
                      ? `0 0 0 2px var(--background), 0 0 0 4px ${preset.color}`
                      : 'none',
                  background: preset.color,
                  cursor: 'pointer',
                  transition: 'transform 150ms, opacity 150ms',
                  transform:
                    settings().appearance.accentColor === preset.id ? 'scale(1.1)' : undefined,
                  opacity: settings().appearance.accentColor === preset.id ? '1' : '0.7',
                  'flex-shrink': '0',
                }}
              />
            )}
          </For>
          {/* Custom accent swatch */}
          <button
            type="button"
            onClick={() => updateAppearance({ accentColor: 'custom' })}
            title="Custom color"
            aria-label="Use custom accent color"
            style={{
              width: '24px',
              height: '24px',
              'border-radius': '50%',
              background:
                'conic-gradient(#f43f5e, #f59e0b, #22c55e, #06b6d4, #3b82f6, #8b5cf6, #f43f5e)',
              border: isCustom()
                ? `2px solid ${settings().appearance.customAccentColor}`
                : '2px solid transparent',
              'box-shadow': isCustom()
                ? `0 0 0 2px var(--background), 0 0 0 4px ${settings().appearance.customAccentColor}`
                : 'none',
              cursor: 'pointer',
              transition: 'transform 150ms, opacity 150ms',
              transform: isCustom() ? 'scale(1.1)' : undefined,
              opacity: isCustom() ? '1' : '0.7',
              'flex-shrink': '0',
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
            style={{
              width: '28px',
              height: '28px',
              'border-radius': '6px',
              border: '1px solid var(--border-default)',
              cursor: 'pointer',
              background: 'transparent',
              padding: '0',
            }}
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
            style={{
              width: '96px',
              padding: '6px 12px',
              'font-family': 'Geist Mono, monospace',
              'font-size': '12px',
              color: 'var(--text-secondary)',
              background: 'var(--alpha-white-8)',
              border: '1px solid var(--border-default)',
              'border-radius': '8px',
              outline: 'none',
            }}
            placeholder="#8b5cf6"
          />
        </div>
      </Show>
      <span class="mt-1.5 block text-[12px] text-[var(--text-muted)]">{accentLabel()}</span>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Theme Browser Modal
// ---------------------------------------------------------------------------

interface ThemeBrowserModalProps {
  open: boolean
  onClose: () => void
  onApply: (preset: ThemePreset) => void
  activeThemeName: string | undefined
}

const ThemeBrowserModal: Component<ThemeBrowserModalProps> = (props) => {
  const { previewAppearance, restoreAppearance } = useSettings()
  const [search, setSearch] = createSignal('')
  let dialogRef: HTMLDivElement | undefined

  const handleClose = () => {
    restoreAppearance()
    props.onClose()
  }

  useSettingsDialogEscape({
    onEscape: handleClose,
    isOpen: () => props.open,
    getDialogElement: () => dialogRef,
  })

  const builtInThemes = createMemo(() => {
    const q = search().toLowerCase().trim()
    if (!q) return THEME_PRESETS
    return THEME_PRESETS.filter(
      (t) => t.name.toLowerCase().includes(q) || t.mode.toLowerCase().includes(q)
    )
  })

  const handleApply = (preset: ThemePreset): void => {
    restoreAppearance()
    props.onApply(preset)
    props.onClose()
  }

  const handlePreview = (preset: ThemePreset): void => {
    previewAppearance({
      accentColor: preset.accentColor,
      codeTheme: preset.codeTheme,
      borderRadius: preset.borderRadius,
      ...(preset.customAccentColor ? { customAccentColor: preset.customAccentColor } : {}),
      ...(preset.darkStyle ? { darkStyle: preset.darkStyle } : {}),
    })
  }

  const handleMouseLeave = (): void => {
    restoreAppearance()
  }

  return (
    <Show when={props.open}>
      <Portal>
        {/* biome-ignore lint/a11y/noStaticElementInteractions: modal wrapper needs Escape handling */}
        <div
          ref={dialogRef}
          data-settings-nested-dialog="true"
          class="fixed inset-0 z-50 flex items-center justify-center outline-none"
          style={{ background: 'rgba(0, 0, 0, 0.6)' }}
          tabindex="-1"
          role="dialog"
          aria-modal="true"
          aria-label="Theme Browser"
        >
          <button
            type="button"
            class="absolute inset-0 h-full w-full border-none bg-transparent p-0"
            onClick={handleClose}
            aria-label="Close theme browser"
          />
          <div
            style={{
              'max-width': '480px',
              'max-height': '520px',
              width: '100%',
              'border-radius': '16px',
              background: 'var(--surface)',
              border: '1px solid var(--border-subtle)',
              'box-shadow': '0 24px 48px rgba(0, 0, 0, 0.4)',
              display: 'flex',
              'flex-direction': 'column',
              overflow: 'hidden',
            }}
          >
            {/* Header */}
            <div
              style={{
                display: 'flex',
                'align-items': 'center',
                'justify-content': 'space-between',
                padding: '16px 20px 12px',
              }}
            >
              <span class="text-base font-semibold text-[var(--text-primary)]">Theme Browser</span>
              <button
                type="button"
                onClick={handleClose}
                style={{
                  background: 'none',
                  border: 'none',
                  cursor: 'pointer',
                  color: 'var(--text-tertiary)',
                  padding: '4px',
                  display: 'flex',
                  'align-items': 'center',
                  'justify-content': 'center',
                  'border-radius': '6px',
                }}
                class="hover:bg-[var(--alpha-white-8)]"
                title="Close"
                aria-label="Close theme browser"
              >
                <svg width="14" height="14" viewBox="0 0 14 14" fill="none" aria-hidden="true">
                  <path
                    d="M1 1L13 13M13 1L1 13"
                    stroke="currentColor"
                    stroke-width="1.5"
                    stroke-linecap="round"
                  />
                </svg>
              </button>
            </div>

            {/* Search */}
            <div style={{ padding: '0 20px 12px' }}>
              <input
                type="text"
                placeholder="Search themes..."
                value={search()}
                onInput={(e) => setSearch(e.currentTarget.value)}
                style={{
                  width: '100%',
                  'font-family': 'Geist, sans-serif',
                  'font-size': '13px',
                  color: 'var(--text-primary)',
                  background: 'var(--alpha-white-8)',
                  border: '1px solid var(--border-default)',
                  'border-radius': '8px',
                  padding: '8px 12px',
                  outline: 'none',
                  'box-sizing': 'border-box',
                }}
                autofocus
              />
            </div>

            {/* Theme list */}
            <div
              style={{
                flex: '1',
                'overflow-y': 'auto',
                padding: '0 8px 12px',
              }}
            >
              {/* Built-in themes */}
              <div
                style={{
                  padding: '4px 12px 6px',
                  'font-family': 'Geist, sans-serif',
                  'font-size': '10px',
                  'font-weight': '600',
                  'text-transform': 'uppercase',
                  'letter-spacing': '0.06em',
                  color: 'var(--text-muted)',
                }}
              >
                Built-in Themes
              </div>
              <For each={builtInThemes()}>
                {(preset) => {
                  const isActive = (): boolean => preset.name === props.activeThemeName
                  return (
                    <button
                      type="button"
                      onClick={() => handleApply(preset)}
                      onMouseEnter={() => handlePreview(preset)}
                      onMouseLeave={handleMouseLeave}
                      style={{
                        display: 'flex',
                        'align-items': 'center',
                        gap: '10px',
                        width: '100%',
                        padding: '8px 12px',
                        'border-radius': '8px',
                        border: 'none',
                        background: 'transparent',
                        cursor: 'pointer',
                        'text-align': 'left',
                        transition: 'background 120ms ease',
                      }}
                      class="hover:!bg-[var(--alpha-white-8)]"
                    >
                      {/* Color swatch */}
                      <div
                        style={{
                          width: '12px',
                          height: '12px',
                          'border-radius': '50%',
                          'flex-shrink': '0',
                          background: preset.swatch,
                        }}
                      />
                      {/* Theme name */}
                      <span
                        style={{
                          flex: '1',
                          'font-family': 'Geist, sans-serif',
                          'font-size': '13px',
                          color: isActive() ? 'var(--text-primary)' : 'var(--text-secondary)',
                          'font-weight': isActive() ? '500' : '400',
                        }}
                      >
                        {preset.name}
                      </span>
                      {/* Mode badge */}
                      <span
                        style={{
                          'font-family': 'Geist Mono, monospace',
                          'font-size': '9px',
                          color: 'var(--text-muted)',
                          background: 'var(--alpha-white-5)',
                          padding: '2px 6px',
                          'border-radius': '4px',
                          'flex-shrink': '0',
                        }}
                      >
                        {preset.mode}
                      </span>
                      {/* Checkmark for active */}
                      <Show when={isActive()}>
                        <svg
                          width="14"
                          height="14"
                          viewBox="0 0 14 14"
                          fill="none"
                          style={{ 'flex-shrink': '0' }}
                          aria-hidden="true"
                        >
                          <path
                            d="M2.5 7.5L5.5 10.5L11.5 3.5"
                            stroke="var(--accent)"
                            stroke-width="1.5"
                            stroke-linecap="round"
                            stroke-linejoin="round"
                          />
                        </svg>
                      </Show>
                    </button>
                  )
                }}
              </For>

              {/* Empty state */}
              <Show when={builtInThemes().length === 0}>
                <div
                  style={{
                    padding: '24px 12px',
                    'text-align': 'center',
                    'font-family': 'Geist, sans-serif',
                    'font-size': '13px',
                    color: 'var(--text-muted)',
                  }}
                >
                  No themes match "{search()}"
                </div>
              </Show>
            </div>
          </div>
        </div>
      </Portal>
    </Show>
  )
}

// ---------------------------------------------------------------------------
// Compact Theme Selector (dropdown + browse)
// ---------------------------------------------------------------------------

export interface ThemeSelectorCompactProps {
  onApply: (preset: ThemePreset) => void
  activeThemeName: string | undefined
}

export const ThemeSelectorCompact: Component<ThemeSelectorCompactProps> = (props) => {
  const [browserOpen, setBrowserOpen] = createSignal(false)

  const activePreset = createMemo(() => THEME_PRESETS.find((p) => p.name === props.activeThemeName))

  const displayName = createMemo(() => props.activeThemeName ?? 'Default')

  return (
    <>
      <div
        style={{
          display: 'flex',
          'align-items': 'center',
          'justify-content': 'space-between',
          width: '100%',
        }}
      >
        {/* Label */}
        <span class="settings-label">Theme</span>

        {/* Controls */}
        <div style={{ display: 'flex', 'align-items': 'center', gap: '8px' }}>
          {/* Dropdown-style display */}
          <button
            type="button"
            onClick={() => setBrowserOpen(true)}
            style={{
              display: 'flex',
              'align-items': 'center',
              gap: '8px',
              'border-radius': '8px',
              background: 'var(--alpha-white-8)',
              border: '1px solid var(--border-default)',
              padding: '6px 12px',
              cursor: 'pointer',
            }}
          >
            <Show when={activePreset()}>
              <div
                style={{
                  width: '10px',
                  height: '10px',
                  'border-radius': '50%',
                  'flex-shrink': '0',
                  background: activePreset()!.swatch,
                }}
              />
            </Show>
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '13px',
                color: 'var(--text-primary)',
              }}
            >
              {displayName()}
            </span>
            {/* Chevron down */}
            <svg
              width="12"
              height="12"
              viewBox="0 0 12 12"
              fill="none"
              style={{ 'flex-shrink': '0' }}
              aria-hidden="true"
            >
              <path
                d="M3 4.5L6 7.5L9 4.5"
                stroke="var(--text-tertiary)"
                stroke-width="1.2"
                stroke-linecap="round"
                stroke-linejoin="round"
              />
            </svg>
          </button>

          {/* Browse button */}
          <button
            type="button"
            onClick={() => setBrowserOpen(true)}
            style={{
              'border-radius': '8px',
              background: 'var(--alpha-white-8)',
              border: '1px solid var(--border-default)',
              padding: '6px 12px',
              cursor: 'pointer',
              'font-family': 'Geist, sans-serif',
              'font-size': '12px',
              color: 'var(--text-tertiary)',
            }}
            class="hover:!bg-[var(--alpha-white-10)]"
          >
            Browse
          </button>
        </div>
      </div>

      <ThemeBrowserModal
        open={browserOpen()}
        onClose={() => setBrowserOpen(false)}
        onApply={props.onApply}
        activeThemeName={props.activeThemeName}
      />
    </>
  )
}
