/**
 * ProviderCard Component
 *
 * Card displaying an LLM provider's connection status, models, and controls.
 * Used in the Providers settings tab.
 */

import { type Component, For, type JSX, Show } from 'solid-js'
import { StatusDot, type StatusDotStatus } from './StatusDot'
import { Toggle } from './Toggle'

export interface ProviderCardProps {
  /** Provider display name */
  name: string
  /** Connection status */
  status: StatusDotStatus
  /** Available model names */
  models?: string[]
  /** Whether the provider is enabled */
  enabled: boolean
  /** Toggle enable/disable */
  onToggle: (enabled: boolean) => void
  /** Optional test connection handler */
  onTest?: () => void
  /** Optional badge elements (e.g., "OAuth", "Free") */
  badges?: JSX.Element
  /** Additional CSS classes */
  class?: string
}

const statusLabels: Record<StatusDotStatus, string> = {
  connected: 'Connected',
  disconnected: 'Not connected',
  error: 'Connection error',
}

export const ProviderCard: Component<ProviderCardProps> = (props) => {
  return (
    <div
      class={`
        flex flex-col gap-3
        rounded-[var(--radius-lg)]
        border border-[var(--card-border)]
        bg-[var(--card-background)]
        p-4
        ${props.class ?? ''}
      `}
    >
      {/* Header row: status dot + name + badges + toggle */}
      <div class="flex items-center justify-between gap-3">
        <div class="flex items-center gap-2.5 min-w-0">
          <StatusDot status={props.status} />
          <span class="text-[13px] font-medium text-[var(--text-primary)] truncate">
            {props.name}
          </span>
          <Show when={props.badges}>
            <div class="flex items-center gap-1">{props.badges}</div>
          </Show>
        </div>
        <Toggle checked={props.enabled} onChange={props.onToggle} />
      </div>

      {/* Status text */}
      <span class="text-[11px] text-[var(--text-tertiary)]">{statusLabels[props.status]}</span>

      {/* Model list */}
      <Show when={props.models && props.models.length > 0}>
        <div class="flex flex-wrap gap-1.5">
          <For each={props.models}>
            {(model) => (
              <span
                class="
                  px-1.5 py-px
                  rounded-[var(--radius-sm)]
                  text-[10px] font-medium
                  bg-[var(--surface-raised)] text-[var(--text-secondary)]
                "
              >
                {model}
              </span>
            )}
          </For>
        </div>
      </Show>

      {/* Action buttons */}
      <Show when={props.onTest}>
        <div class="flex items-center gap-2 pt-1">
          <button
            type="button"
            onClick={() => props.onTest?.()}
            class="
              px-2.5 py-1
              text-[11px] font-medium
              rounded-[var(--radius-md)]
              bg-[var(--surface-raised)] text-[var(--text-secondary)]
              border border-[var(--border-default)]
              hover:bg-[var(--alpha-white-8)]
              transition-colors duration-[var(--duration-fast)]
              cursor-pointer select-none
            "
          >
            Test Connection
          </button>
        </div>
      </Show>
    </div>
  )
}
