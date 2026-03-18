/**
 * Toggle/Switch Component
 *
 * A toggle switch for boolean settings.
 * Built with Kobalte for accessibility.
 *
 * Soft Zinc design: 40x22px track, 18px white knob, accent when ON.
 */

import { Switch } from '@kobalte/core/switch'
import { type Component, splitProps } from 'solid-js'

export interface ToggleProps {
  /** Checked state */
  checked?: boolean
  /** Default checked state (uncontrolled) */
  defaultChecked?: boolean
  /** Change handler */
  onChange?: (checked: boolean) => void
  /** Disabled state */
  disabled?: boolean
  /** Additional CSS classes */
  class?: string
}

export const Toggle: Component<ToggleProps> = (props) => {
  const [local, others] = splitProps(props, [
    'checked',
    'defaultChecked',
    'onChange',
    'disabled',
    'class',
  ])

  return (
    <Switch
      checked={local.checked}
      defaultChecked={local.defaultChecked}
      onChange={local.onChange}
      disabled={local.disabled}
      class={`
        inline-flex items-center
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
          w-[44px] h-[24px]
          bg-[var(--gray-5)]
          transition-colors duration-[var(--duration-fast)] ease-[var(--ease-out)]
          focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-2 focus-visible:ring-offset-[var(--background)]
          data-[checked]:bg-[var(--accent)]
          data-[disabled]:cursor-not-allowed
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
            w-[20px] h-[20px]
            transition-transform duration-[var(--duration-normal)] ease-[var(--ease-spring)]
            translate-x-[2px]
            data-[checked]:translate-x-[22px]
          `}
        />
      </Switch.Control>
    </Switch>
  )
}
