/**
 * ToggleRow Component
 *
 * Flex row with label + optional description on the left, Toggle on the right.
 * Used extensively in Settings panels.
 */

import type { Component } from 'solid-js'
import { createUniqueId, Show } from 'solid-js'
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
  const generatedId = createUniqueId()
  const labelId = () => `toggle-row-label-${generatedId}`
  const descriptionId = () => `toggle-row-description-${generatedId}`

  return (
    <div
      class={`
        flex items-center justify-between gap-4
        ${props.disabled ? 'opacity-50' : ''}
        ${props.class ?? ''}
      `}
    >
      <div class="flex flex-col min-w-0 gap-0.5">
        <span id={labelId()} class="settings-label">
          {props.label}
        </span>
        <Show when={props.description}>
          <span id={descriptionId()} class="settings-description">
            {props.description}
          </span>
        </Show>
      </div>
      <Toggle
        checked={props.checked}
        onChange={props.onChange}
        disabled={props.disabled}
        aria-labelledby={labelId()}
        aria-describedby={props.description ? descriptionId() : undefined}
      />
    </div>
  )
}
