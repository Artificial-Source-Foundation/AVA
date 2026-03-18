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
        <span class="text-[14px] text-[var(--text-secondary)]">Scale</span>
        <span class="text-[14px] font-mono text-[var(--text-primary)]">
          {Math.round(settings().appearance.uiScale * 100)}%
        </span>
      </div>
      <div class="flex items-center gap-2 py-1">
        <span class="text-[13px] text-[var(--text-muted)] w-8">85%</span>
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
        <span class="text-[13px] text-[var(--text-muted)] w-8 text-right">120%</span>
      </div>
      <div class="flex gap-1 mt-1.5">
        <For each={SCALE_STEPS}>
          {(step) => (
            <button
              type="button"
              onClick={() => updateAppearance({ uiScale: step })}
              class={`
                px-2 py-1 text-[12px] rounded-[var(--radius-sm)] transition-colors
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
        <span class="text-[14px] text-[var(--text-secondary)]">Corners</span>
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
      <span class="text-[14px] text-[var(--text-secondary)]">Spacing</span>
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
      <div class="space-y-1">
        <For each={order()}>
          {(id, index) => (
            <div class="flex items-center justify-between py-1 px-2 rounded-[var(--radius-md)] bg-[var(--surface-raised)]">
              <span class="text-[14px] text-[var(--text-secondary)]">
                {SIDEBAR_LABELS[id] ?? id}
              </span>
              <div class="flex gap-0.5">
                <button
                  type="button"
                  onClick={() => moveItem(index(), -1)}
                  disabled={index() === 0}
                  class="p-0.5 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--alpha-white-5)] disabled:opacity-30 disabled:pointer-events-none transition-colors"
                  title="Move up"
                >
                  <ChevronUp class="w-3.5 h-3.5" />
                </button>
                <button
                  type="button"
                  onClick={() => moveItem(index(), 1)}
                  disabled={index() === order().length - 1}
                  class="p-0.5 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--alpha-white-5)] disabled:opacity-30 disabled:pointer-events-none transition-colors"
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

// ---------------------------------------------------------------------------
// Accessibility Section
// ---------------------------------------------------------------------------

export const AccessibilitySection: Component = () => {
  const { settings, updateAppearance } = useSettings()

  return (
    <div>
      <SectionHeader title="Accessibility" />
      {/* High contrast */}
      <div class="flex items-center justify-between py-2">
        <div>
          <span class="text-[14px] text-[var(--text-secondary)]">High contrast</span>
          <p class="text-[13px] text-[var(--text-muted)] mt-0.5">Stronger text and borders</p>
        </div>
        <Toggle
          checked={settings().appearance.highContrast}
          onChange={(v) => updateAppearance({ highContrast: v })}
        />
      </div>
      {/* Reduce motion */}
      <div class="flex items-center justify-between py-2">
        <div>
          <span class="text-[14px] text-[var(--text-secondary)]">Reduce motion</span>
          <p class="text-[13px] text-[var(--text-muted)] mt-0.5">
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
