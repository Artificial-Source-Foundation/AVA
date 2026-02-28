/**
 * Toolbar Buttons
 *
 * Three strip sub-components:
 * - ThinkingToggle — brain icon, only for models with 'thinking' capability
 * - PlanActSlider — Cline-style animated two-segment slider
 * - PermissionBadge — styled pill cycling through permission modes
 */

import { Brain, FileSearch, Play, Shield, ShieldAlert, ShieldOff } from 'lucide-solid'
import { type Accessor, type Component, Show } from 'solid-js'
import type { PermissionMode } from '../../../stores/settings'
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
// ThinkingToggle
// ---------------------------------------------------------------------------

export interface ThinkingToggleProps {
  enabled: Accessor<boolean>
  onToggle: () => void
  available: Accessor<boolean>
}

export const ThinkingToggle: Component<ThinkingToggleProps> = (props) => (
  <Show when={props.available()}>
    <button
      type="button"
      onClick={props.onToggle}
      class={`
        flex items-center gap-1 px-1.5 py-1
        text-[11px] font-medium rounded-[var(--radius-md)]
        transition-all duration-200
        ${
          props.enabled()
            ? 'text-[var(--accent)] bg-[var(--accent-subtle)]'
            : 'text-[var(--text-muted)] hover:text-[var(--text-secondary)] bg-transparent hover:bg-[var(--surface-raised)]'
        }
      `}
      title={props.enabled() ? 'Thinking mode on' : 'Enable thinking mode'}
    >
      <Brain
        class="w-3.5 h-3.5 transition-all duration-200"
        style={{
          filter: props.enabled() ? 'drop-shadow(0 0 4px var(--accent))' : 'none',
        }}
      />
    </button>
  </Show>
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
  <button
    type="button"
    onClick={props.togglePlanMode}
    disabled={props.isProcessing()}
    class="
      relative flex items-center
      h-[22px] w-[88px] rounded-[var(--radius-md)]
      bg-[var(--surface-raised)] border border-[var(--border-subtle)]
      text-[10px] font-semibold
      disabled:opacity-50 disabled:cursor-not-allowed
      overflow-hidden select-none
      transition-colors
    "
    title={props.isPlanMode() ? 'Plan mode — read-only exploration' : 'Act mode — full execution'}
  >
    {/* Sliding highlight */}
    <div
      class="
        absolute top-[1px] bottom-[1px] w-[44px]
        rounded-[calc(var(--radius-md)-2px)]
        transition-all duration-300 ease-[cubic-bezier(0.4,0,0.2,1)]
      "
      style={{
        left: props.isPlanMode() ? '1px' : '42px',
        'background-color': props.isPlanMode() ? 'var(--accent)' : 'var(--accent)',
      }}
    />
    {/* Labels */}
    <span
      class="relative z-10 flex-1 text-center transition-colors duration-200"
      style={{
        color: props.isPlanMode() ? 'white' : 'var(--text-muted)',
      }}
    >
      <span class="flex items-center justify-center gap-0.5">
        <FileSearch class="w-2.5 h-2.5" />
        Plan
      </span>
    </span>
    <span
      class="relative z-10 flex-1 text-center transition-colors duration-200"
      style={{
        color: props.isPlanMode() ? 'var(--text-muted)' : 'white',
      }}
    >
      <span class="flex items-center justify-center gap-0.5">
        <Play class="w-2.5 h-2.5" />
        Act
      </span>
    </span>
  </button>
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
      onClick={props.onCyclePermission}
      class="
        flex items-center gap-1 px-2 py-1
        text-[11px] font-medium rounded-[var(--radius-md)]
        transition-all duration-200
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
    >
      {(() => {
        const Icon = cfg().icon
        return <Icon class="w-3 h-3" />
      })()}
      <span>{cfg().label}</span>
    </button>
  )
}
