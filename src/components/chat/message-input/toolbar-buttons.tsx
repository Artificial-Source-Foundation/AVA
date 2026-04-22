/**
 * Toolbar Buttons
 *
 * Strip sub-components:
 * - ReasoningDropdown — brain icon + effort level, cycles Off→Low→Med→High
 * - PlanActSlider — Cline-style animated two-segment slider
 * - PermissionBadge — styled pill cycling through permission modes
 */

import { Brain, Shield, ShieldAlert } from 'lucide-solid'
import type { Accessor, Component } from 'solid-js'
import type { PermissionMode } from '../../../stores/settings'
import type { ReasoningEffort } from '../../../stores/settings/settings-types'
import type { PermissionConfigEntry } from './types'

// ---------------------------------------------------------------------------
// Permission configuration (constant map)
// ---------------------------------------------------------------------------

export const PERMISSION_CONFIG: Record<PermissionMode, PermissionConfigEntry> = {
  ask: { icon: Shield, color: 'var(--text-muted)', label: 'Ask' },
  'auto-approve': { icon: ShieldAlert, color: 'var(--warning)', label: 'Auto' },
}

// ---------------------------------------------------------------------------
// ReasoningDropdown (replaces ThinkingToggle)
// ---------------------------------------------------------------------------

const EFFORT_LABELS: Record<ReasoningEffort, string> = {
  off: '',
  none: 'None',
  minimal: 'Min',
  low: 'Low',
  medium: 'Med',
  high: 'High',
  xhigh: 'XHigh',
  max: 'Max',
}

/** Per-provider supported effort levels (excluding 'off' which is always available). */
const PROVIDER_EFFORTS: Record<string, ReasoningEffort[]> = {
  anthropic: ['low', 'medium', 'high', 'max'],
  openai: ['low', 'medium', 'high', 'xhigh'],
  openrouter: ['low', 'medium', 'high'],
  google: ['low', 'medium', 'high'],
  copilot: ['low', 'medium', 'high', 'xhigh'],
  together: ['low', 'medium', 'high'],
  cohere: ['low', 'medium', 'high'],
  ollama: ['low', 'medium', 'high'],
  glm: ['low', 'medium', 'high'],
  kimi: ['low', 'medium', 'high'],
}

const DEFAULT_EFFORTS: ReasoningEffort[] = ['low', 'medium', 'high']

/** Get the supported effort levels for a provider. */
export function getProviderEffortLevels(providerId: string): ReasoningEffort[] {
  return PROVIDER_EFFORTS[providerId] ?? DEFAULT_EFFORTS
}

export interface ReasoningDropdownProps {
  effort: Accessor<ReasoningEffort>
  onCycle: () => void
  available: Accessor<boolean>
}

/** Intensity config for each reasoning effort level. */
const INACTIVE_STYLE = { bg: 'var(--alpha-white-5)', text: 'var(--text-muted)', weight: '500' }
const EFFORT_INTENSITY: Record<string, { bg: string; text: string; weight: string }> = {
  off: INACTIVE_STYLE,
  none: INACTIVE_STYLE,
  minimal: { bg: 'rgba(255,255,255,0.08)', text: 'rgba(255,255,255,0.55)', weight: '500' },
  low: { bg: 'rgba(255,255,255,0.12)', text: 'rgba(255,255,255,0.7)', weight: '500' },
  medium: { bg: 'rgba(255,255,255,0.22)', text: 'rgba(255,255,255,0.85)', weight: '600' },
  high: { bg: 'rgba(255,255,255,0.38)', text: 'rgba(255,255,255,0.95)', weight: '700' },
  xhigh: { bg: 'rgba(255,255,255,0.55)', text: '#000', weight: '700' },
  max: { bg: 'rgba(255,255,255,0.55)', text: '#000', weight: '700' },
}

export const ReasoningDropdown: Component<ReasoningDropdownProps> = (props) => {
  const isActive = () => props.effort() !== 'off' && props.effort() !== 'none'
  const intensity = () => EFFORT_INTENSITY[props.effort()] ?? INACTIVE_STYLE

  return (
    <button
      type="button"
      onClick={() => props.onCycle()}
      class="flex items-center rounded-[6px] transition-all duration-200"
      style={{
        gap: '4px',
        padding: '4px 8px',
        background: intensity().bg,
      }}
      title={isActive() ? `Reasoning: ${EFFORT_LABELS[props.effort()]}` : 'Enable reasoning'}
      aria-label={isActive() ? `Reasoning: ${EFFORT_LABELS[props.effort()]}` : 'Enable reasoning'}
    >
      <Brain
        class="transition-colors duration-200"
        style={{
          width: '10px',
          height: '10px',
          color: intensity().text,
        }}
      />
      <span
        style={{
          'font-size': '10px',
          'font-weight': intensity().weight,
          'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)",
          color: intensity().text,
        }}
      >
        {isActive() ? `Think ${EFFORT_LABELS[props.effort()]}` : 'Think'}
      </span>
    </button>
  )
}

/** Cycle to the next reasoning effort level based on provider-specific levels. */
export function cycleReasoningEffort(
  current: ReasoningEffort,
  providerId?: string
): ReasoningEffort {
  const levels = getProviderEffortLevels(providerId ?? '')
  const cycle: ReasoningEffort[] = ['off', ...levels]
  const idx = cycle.indexOf(current)
  // If current level isn't in this provider's cycle, reset to off
  if (idx === -1) return cycle[1] ?? 'low'
  return cycle[(idx + 1) % cycle.length]!
}

// ---------------------------------------------------------------------------
// PlanActSlider
// ---------------------------------------------------------------------------

export interface PlanActSliderProps {
  isPlanMode: Accessor<boolean>
  togglePlanMode: () => void
  isProcessing: Accessor<boolean>
}

export const PlanActSlider: Component<PlanActSliderProps> = (props) => (
  <fieldset
    class="
      relative flex items-center
      rounded-[6px]
      bg-[var(--alpha-white-5)] p-[2px]
      select-none
    "
    aria-label="Mode selector"
  >
    {/* Plan tab */}
    <button
      type="button"
      onClick={() => {
        if (!props.isPlanMode()) props.togglePlanMode()
      }}
      disabled={props.isProcessing()}
      class="relative z-10 flex items-center justify-center rounded-[4px] transition-colors duration-200 disabled:opacity-50 disabled:cursor-not-allowed focus:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-1"
      style={{
        padding: '4px 10px',
        'font-size': '11px',
        'font-family': "var(--font-sans, 'Geist', system-ui, sans-serif)",
        color: props.isPlanMode() ? 'var(--text-primary)' : 'var(--text-muted)',
        'font-weight': props.isPlanMode() ? '500' : undefined,
        'background-color': props.isPlanMode() ? 'var(--alpha-white-10)' : undefined,
      }}
      title={
        props.isPlanMode()
          ? 'Plan mode active — read-only exploration'
          : 'Switch to plan mode — read-only exploration'
      }
      aria-label="Plan mode"
      aria-pressed={props.isPlanMode()}
    >
      Plan
    </button>
    {/* Act tab */}
    <button
      type="button"
      onClick={() => {
        if (props.isPlanMode()) props.togglePlanMode()
      }}
      disabled={props.isProcessing()}
      class="relative z-10 flex items-center justify-center rounded-[4px] transition-colors duration-200 disabled:opacity-50 disabled:cursor-not-allowed focus:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-1"
      style={{
        padding: '4px 10px',
        'font-size': '11px',
        'font-family': "var(--font-sans, 'Geist', system-ui, sans-serif)",
        color: props.isPlanMode() ? 'var(--text-muted)' : 'var(--text-primary)',
        'font-weight': props.isPlanMode() ? undefined : '500',
        'background-color': props.isPlanMode() ? undefined : 'var(--alpha-white-10)',
      }}
      title={
        props.isPlanMode()
          ? 'Switch to act mode — full execution'
          : 'Act mode active — full execution'
      }
      aria-label="Act mode"
      aria-pressed={!props.isPlanMode()}
    >
      Act
    </button>
  </fieldset>
)

// ---------------------------------------------------------------------------
// PermissionBadge
// ---------------------------------------------------------------------------

export interface PermissionBadgeProps {
  permissionMode: Accessor<PermissionMode>
  onCyclePermission: () => void
}

export const PermissionBadge: Component<PermissionBadgeProps> = (props) => {
  const cfg = () => PERMISSION_CONFIG[props.permissionMode()]

  return (
    <button
      type="button"
      onClick={() => props.onCyclePermission()}
      class="
        flex items-center gap-1 px-2 py-1
        text-[var(--text-xs)] font-medium rounded-[var(--radius-md)]
        transition-[background-color,border-color,color] duration-200
        border
      "
      style={{
        color: cfg().color,
        'border-color': props.permissionMode() === 'ask' ? 'var(--border-subtle)' : cfg().color,
        'background-color':
          props.permissionMode() === 'ask'
            ? 'var(--surface-raised)'
            : props.permissionMode() === 'auto-approve'
              ? 'color-mix(in srgb, var(--warning) 10%, transparent)'
              : 'color-mix(in srgb, var(--error) 10%, transparent)',
      }}
      title={`Permissions: ${cfg().label} (click to cycle)`}
      aria-label={`Permissions: ${cfg().label}`}
    >
      {(() => {
        const Icon = cfg().icon
        return <Icon class="w-3 h-3" />
      })()}
      <span>{cfg().label}</span>
    </button>
  )
}
