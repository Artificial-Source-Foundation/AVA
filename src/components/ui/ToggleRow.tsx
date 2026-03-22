/**
 * ToggleRow Component
 *
 * Flex row with label + optional description on the left, Toggle on the right.
 * Used extensively in Settings panels.
 */

import type { Component } from 'solid-js'
import { Show } from 'solid-js'
import { Toggle } from './Toggle'

export interface ToggleRowProps {
  /** Row label */
  label: string
  /** Optional description text */
  description?: string
  /** Checked state */
  checked: boolean
  /** Change handler */
  onChange: (checked: boolean) => void
  /** Disabled state */
  disabled?: boolean
  /** Additional CSS classes */
  class?: string
}

export const ToggleRow: Component<ToggleRowProps> = (props) => {
  return (
    <div
      class={`
        flex items-center justify-between gap-4
        ${props.disabled ? 'opacity-50' : ''}
        ${props.class ?? ''}
      `}
    >
      <div class="flex flex-col min-w-0">
        <span class="text-[var(--settings-text-label)] text-[var(--gray-10)] leading-tight">
          {props.label}
        </span>
        <Show when={props.description}>
          <span class="text-[var(--settings-text-description)] text-[var(--gray-7)] leading-tight mt-0.5">
            {props.description}
          </span>
        </Show>
      </div>
      <Toggle checked={props.checked} onChange={props.onChange} disabled={props.disabled} />
    </div>
  )
}
