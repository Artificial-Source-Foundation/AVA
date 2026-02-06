/**
 * Toggle/Switch Component
 *
 * A toggle switch for boolean settings.
 * Built with Kobalte for accessibility.
 */

import { Switch } from '@kobalte/core/switch'
import { type Component, Show, splitProps } from 'solid-js'

export interface ToggleProps {
  /** Toggle label */
  label?: string
  /** Description text */
  description?: string
  /** Checked state */
  checked?: boolean
  /** Default checked state (uncontrolled) */
  defaultChecked?: boolean
  /** Change handler */
  onChange?: (checked: boolean) => void
  /** Disabled state */
  disabled?: boolean
  /** Toggle size */
  size?: 'sm' | 'md' | 'lg'
  /** Additional CSS classes */
  class?: string
  /** Label position */
  labelPosition?: 'left' | 'right'
}

export const Toggle: Component<ToggleProps> = (props) => {
  const [local, others] = splitProps(props, [
    'label',
    'description',
    'checked',
    'defaultChecked',
    'onChange',
    'disabled',
    'size',
    'class',
    'labelPosition',
  ])

  const size = () => local.size ?? 'md'
  const labelPosition = () => local.labelPosition ?? 'right'

  const trackSizes = {
    sm: 'h-4 w-7',
    md: 'h-5 w-9',
    lg: 'h-6 w-11',
  }

  const thumbSizes = {
    sm: 'h-3 w-3',
    md: 'h-4 w-4',
    lg: 'h-5 w-5',
  }

  const thumbTranslate = {
    sm: 'data-[checked]:translate-x-3',
    md: 'data-[checked]:translate-x-4',
    lg: 'data-[checked]:translate-x-5',
  }

  return (
    <Switch
      checked={local.checked}
      defaultChecked={local.defaultChecked}
      onChange={local.onChange}
      disabled={local.disabled}
      class={`
        inline-flex items-center gap-3
        ${labelPosition() === 'left' ? 'flex-row-reverse' : 'flex-row'}
        ${local.disabled ? 'opacity-50 cursor-not-allowed' : 'cursor-pointer'}
        ${local.class ?? ''}
      `}
      {...others}
    >
      <Switch.Input class="sr-only" />

      <Switch.Control
        class={`
          relative inline-flex shrink-0
          items-center
          rounded-full
          border-2 border-transparent
          bg-[var(--border-default)]
          transition-colors duration-[var(--duration-fast)] ease-[var(--ease-out)]
          focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-2 focus-visible:ring-offset-[var(--background)]
          data-[checked]:bg-[var(--accent)]
          data-[disabled]:cursor-not-allowed
          ${trackSizes[size()]}
        `}
      >
        <Switch.Thumb
          class={`
            pointer-events-none
            block
            rounded-full
            bg-white
            shadow-sm
            ring-0
            transition-transform duration-[var(--duration-normal)] ease-[var(--ease-spring)]
            translate-x-0.5
            ${thumbSizes[size()]}
            ${thumbTranslate[size()]}
          `}
        />
      </Switch.Control>

      <Show when={local.label || local.description}>
        <div class="flex flex-col">
          <Show when={local.label}>
            <Switch.Label class="text-sm font-medium text-[var(--text-primary)]">
              {local.label}
            </Switch.Label>
          </Show>
          <Show when={local.description}>
            <Switch.Description class="text-xs text-[var(--text-tertiary)]">
              {local.description}
            </Switch.Description>
          </Show>
        </div>
      </Show>
    </Switch>
  )
}
