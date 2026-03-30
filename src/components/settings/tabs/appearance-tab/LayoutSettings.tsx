/**
 * Layout Settings
 *
 * Interface Scale, Border Radius, UI Density, Sidebar Order, and Accessibility.
 */

import { ChevronDown, ChevronUp } from 'lucide-solid'
import { type Component, For } from 'solid-js'
import type { BorderRadius, UIDensity } from '../../../../stores/settings'
import { useSettings } from '../../../../stores/settings'
import { SectionHeader, segmentedBtn, Toggle } from './appearance-utils'

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

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

const SIDEBAR_LABELS: Record<string, string> = {
  sessions: 'Sessions',
  explorer: 'Explorer',
}

// ---------------------------------------------------------------------------
// Interface Scale Section
// ---------------------------------------------------------------------------

export const InterfaceScaleSection: Component = () => {
  const { settings, updateAppearance } = useSettings()

  return (
    <div>
      <SectionHeader title="Scale" />
      <div class="flex items-center justify-between py-2">
        <span class="settings-label">Scale</span>
        <span class="font-ui-mono text-[13px] text-[var(--text-primary)]">
          {Math.round(settings().appearance.uiScale * 100)}%
        </span>
      </div>
      <div class="flex items-center gap-2 py-1">
        <span class="font-ui-mono w-8 text-[12px] text-[var(--text-muted)]">85%</span>
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
        <span class="font-ui-mono w-8 text-right text-[12px] text-[var(--text-muted)]">120%</span>
      </div>
      <div class="flex gap-1" style={{ 'margin-top': '6px' }}>
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
    </div>
  )
}

// ---------------------------------------------------------------------------
// Border Radius Section
// ---------------------------------------------------------------------------

export const BorderRadiusSection: Component = () => {
  const { settings, updateAppearance, previewAppearance, restoreAppearance } = useSettings()

  return (
    <>
      <div class="flex items-center justify-between py-2">
        <span class="settings-label">Corners</span>
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
      <div class="flex gap-2" style={{ 'margin-top': '8px' }}>
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
    </>
  )
}

// ---------------------------------------------------------------------------
// UI Density Section
// ---------------------------------------------------------------------------

export const DensitySection: Component = () => {
  const { settings, updateAppearance, previewAppearance, restoreAppearance } = useSettings()

  return (
    <div class="flex items-center justify-between py-2">
      <span class="settings-label">Spacing</span>
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
  )
}

// ---------------------------------------------------------------------------
// Sidebar Order Section
// ---------------------------------------------------------------------------

export const SidebarOrderSection: Component = () => {
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
    <div>
      <SectionHeader title="Sidebar Order" />
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
              <span class="settings-label">{SIDEBAR_LABELS[id] ?? id}</span>
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
    </div>
  )
}

// ---------------------------------------------------------------------------
// Accessibility Section
// ---------------------------------------------------------------------------

export const AccessibilitySection: Component = () => {
  const { settings, updateAppearance } = useSettings()

  return (
    <div>
      <SectionHeader title="Accessibility" />
      {/* High contrast */}
      <div class="flex items-center justify-between" style={{ width: '100%' }}>
        <div class="flex flex-col gap-0.5">
          <span class="settings-label">High contrast</span>
          <span class="settings-description">Stronger text and borders</span>
        </div>
        <Toggle
          checked={settings().appearance.highContrast}
          onChange={(v) => updateAppearance({ highContrast: v })}
        />
      </div>
      {/* Reduce motion */}
      <div class="flex items-center justify-between" style={{ width: '100%' }}>
        <div class="flex flex-col gap-0.5">
          <span class="settings-label">Reduce motion</span>
          <span class="settings-description">Disables all animations and transitions</span>
        </div>
        <Toggle
          checked={settings().appearance.reduceMotion}
          onChange={(v) => updateAppearance({ reduceMotion: v })}
        />
      </div>
    </div>
  )
}
