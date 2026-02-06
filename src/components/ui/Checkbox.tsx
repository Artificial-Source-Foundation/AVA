/**
 * Checkbox Component
 *
 * A simple checkbox for boolean inputs.
 * Built with Kobalte for accessibility.
 */

import { Checkbox as KCheckbox } from '@kobalte/core/checkbox'
import { Check } from 'lucide-solid'
import { type Component, Show, splitProps } from 'solid-js'

export interface CheckboxProps {
  /** Checkbox ID for label association */
  id?: string
  /** Checkbox label */
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
  /** Checkbox size */
  size?: 'sm' | 'md' | 'lg'
  /** Additional CSS classes */
  class?: string
}

export const Checkbox: Component<CheckboxProps> = (props) => {
  const [local, others] = splitProps(props, [
    'id',
    'label',
    'description',
    'checked',
    'defaultChecked',
    'onChange',
    'disabled',
    'size',
    'class',
  ])

  const size = () => local.size ?? 'md'

  const boxSizes = {
    sm: 'h-4 w-4',
    md: 'h-5 w-5',
    lg: 'h-6 w-6',
  }

  const iconSizes = {
    sm: 'w-3 h-3',
    md: 'w-3.5 h-3.5',
    lg: 'w-4 h-4',
  }

  return (
    <KCheckbox
      checked={local.checked}
      defaultChecked={local.defaultChecked}
      onChange={local.onChange}
      disabled={local.disabled}
      class={`
        inline-flex items-center gap-2
        ${local.disabled ? 'opacity-50 cursor-not-allowed' : 'cursor-pointer'}
        ${local.class ?? ''}
      `}
      {...others}
    >
      <KCheckbox.Input id={local.id} class="sr-only" />

      <KCheckbox.Control
        class={`
          relative inline-flex shrink-0
          items-center justify-center
          rounded-[var(--radius-sm)]
          border-2 border-[var(--border-default)]
          bg-[var(--surface)]
          transition-colors duration-[var(--duration-fast)] ease-[var(--ease-out)]
          focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-2 focus-visible:ring-offset-[var(--background)]
          data-[checked]:bg-[var(--accent)] data-[checked]:border-[var(--accent)]
          data-[disabled]:cursor-not-allowed
          hover:border-[var(--accent)]
          ${boxSizes[size()]}
        `}
      >
        <KCheckbox.Indicator class="flex items-center justify-center">
          <Check class={`text-white ${iconSizes[size()]}`} />
        </KCheckbox.Indicator>
      </KCheckbox.Control>

      <Show when={local.label || local.description}>
        <div class="flex flex-col">
          <Show when={local.label}>
            <KCheckbox.Label class="text-sm font-medium text-[var(--text-primary)]">
              {local.label}
            </KCheckbox.Label>
          </Show>
          <Show when={local.description}>
            <KCheckbox.Description class="text-xs text-[var(--text-tertiary)]">
              {local.description}
            </KCheckbox.Description>
          </Show>
        </div>
      </Show>
    </KCheckbox>
  )
}
