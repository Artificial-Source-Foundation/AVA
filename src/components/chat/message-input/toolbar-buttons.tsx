/**
 * Toolbar Buttons
 *
 * Strip sub-components:
 * - ReasoningDropdown — brain icon + effort level, cycles Off→Low→Med→High
 * - DelegationToggle — users icon, toggles team delegation on/off
 * - PlanActSlider — Cline-style animated two-segment slider
 * - PermissionBadge — styled pill cycling through permission modes
 */

import { Brain, Shield, ShieldAlert, ShieldOff, Users } from 'lucide-solid'
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
  bypass: { icon: ShieldOff, color: 'var(--error)', label: 'Bypass' },
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
  groq: ['none', 'low', 'medium', 'high'],
  xai: ['low', 'medium', 'high'],
  deepseek: ['low', 'medium', 'high'],
  together: ['low', 'medium', 'high'],
  mistral: ['low', 'medium', 'high'],
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

export const ReasoningDropdown: Component<ReasoningDropdownProps> = (props) => {
  const isActive = () => props.effort() !== 'off'

  return (
    <button
      type="button"
      onClick={() => props.onCycle()}
      class="flex items-center rounded-[6px] bg-[var(--alpha-white-5)] transition-colors duration-200 hover:bg-[var(--alpha-white-8)]"
      style={{
        gap: '4px',
        padding: '4px 8px',
      }}
      title={isActive() ? `Reasoning: ${EFFORT_LABELS[props.effort()]}` : 'Enable reasoning'}
      aria-label={isActive() ? `Reasoning: ${EFFORT_LABELS[props.effort()]}` : 'Enable reasoning'}
    >
      <Brain
        class="transition-colors duration-200"
        style={{
          width: '10px',
          height: '10px',
          color: isActive() ? 'var(--text-tertiary)' : 'var(--text-muted)',
        }}
      />
      <span
        style={{
          'font-size': '10px',
          'font-weight': '500',
          'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)",
          color: isActive() ? 'var(--text-tertiary)' : 'var(--text-muted)',
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
// DelegationToggle
// ---------------------------------------------------------------------------

export interface DelegationToggleProps {
  enabled: Accessor<boolean>
  onToggle: () => void
}

export const DelegationToggle: Component<DelegationToggleProps> = (props) => (
  <button
    type="button"
    onClick={() => props.onToggle()}
    class={`
      flex items-center gap-1 px-1.5 py-1
      text-[var(--text-xs)] font-medium rounded-[var(--radius-md)]
      transition-colors duration-200
      ${
        props.enabled()
          ? 'text-[var(--accent)] bg-[var(--accent-subtle)]'
          : 'text-[var(--text-muted)] hover:text-[var(--text-secondary)] bg-transparent hover:bg-[var(--surface-raised)]'
      }
    `}
    title={props.enabled() ? 'Team delegation on' : 'Enable team delegation'}
    aria-label={props.enabled() ? 'Team delegation on' : 'Enable team delegation'}
  >
    <Users class="w-3.5 h-3.5" />
    <span>{props.enabled() ? 'Team' : 'Solo'}</span>
  </button>
)

// ---------------------------------------------------------------------------
// PlanActSlider
// ---------------------------------------------------------------------------

export interface PlanActSliderProps {
  isPlanMode: Accessor<boolean>
  togglePlanMode: () => void
  isProcessing: Accessor<boolean>
}

export const PlanActSlider: Component<PlanActSliderProps> = (props) => (
  <div
    class="
      relative flex items-center
      rounded-[6px]
      bg-[var(--alpha-white-5)] p-[2px]
      select-none
    "
  >
    {/* Plan tab */}
    <button
      type="button"
      onClick={() => {
        if (!props.isPlanMode()) props.togglePlanMode()
      }}
      disabled={props.isProcessing()}
      class="relative z-10 flex items-center justify-center rounded-[4px] transition-colors duration-200 disabled:opacity-50 disabled:cursor-not-allowed"
      style={{
        padding: '4px 10px',
        'font-size': '11px',
        'font-family': "var(--font-sans, 'Geist', system-ui, sans-serif)",
        color: props.isPlanMode() ? 'var(--text-primary)' : 'var(--text-muted)',
        'font-weight': props.isPlanMode() ? '500' : undefined,
        'background-color': props.isPlanMode() ? 'var(--alpha-white-10)' : undefined,
      }}
      title="Plan mode — read-only exploration"
      aria-label="Switch to plan mode"
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
      class="relative z-10 flex items-center justify-center rounded-[4px] transition-colors duration-200 disabled:opacity-50 disabled:cursor-not-allowed"
      style={{
        padding: '4px 10px',
        'font-size': '11px',
        'font-family': "var(--font-sans, 'Geist', system-ui, sans-serif)",
        color: props.isPlanMode() ? 'var(--text-muted)' : 'var(--text-primary)',
        'font-weight': props.isPlanMode() ? undefined : '500',
        'background-color': props.isPlanMode() ? undefined : 'var(--alpha-white-10)',
      }}
      title="Act mode — full execution"
      aria-label="Switch to act mode"
    >
      Act
    </button>
  </div>
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
