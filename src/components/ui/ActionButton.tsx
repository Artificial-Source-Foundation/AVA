/**
 * ActionButton Component
 *
 * A compact action button with primary, secondary, and danger variants.
 * Lighter-weight than the full Button component — no Kobalte/MotionOne deps.
 */

import { type Component, type JSX, Show } from 'solid-js'

export type ActionButtonVariant = 'primary' | 'secondary' | 'danger'

export interface ActionButtonProps {
  /** Button label */
  label: string
  /** Optional icon (rendered before label) */
  icon?: JSX.Element
  /** Visual variant */
  variant: ActionButtonVariant
  /** Click handler */
  onClick: () => void
  /** Disabled state */
  disabled?: boolean
  /** Additional CSS classes */
  class?: string
}

const variantStyles: Record<ActionButtonVariant, string> = {
  primary: `
    bg-[var(--accent)] text-white
    hover:bg-[var(--accent-hover)]
    active:bg-[var(--accent-active)]
  `,
  secondary: `
    bg-[var(--gray-5)] text-[var(--text-secondary)]
    border border-[var(--border-default)]
    hover:bg-[var(--alpha-white-8)]
    active:bg-[var(--alpha-white-10)]
  `,
  danger: `
    bg-[var(--surface-raised)] text-[var(--error)]
    border border-[var(--border-default)]
    hover:bg-[var(--error-subtle)]
    active:bg-[var(--error-subtle)]
  `,
}

export const ActionButton: Component<ActionButtonProps> = (props) => {
  return (
    <button
      type="button"
      onClick={() => props.onClick()}
      disabled={props.disabled}
      class={`
        inline-flex items-center justify-center gap-1.5
        px-3 py-1.5
        text-[12px] font-medium
        rounded-[var(--radius-md)]
        transition-colors duration-[var(--duration-fast)] ease-[var(--ease-out)]
        select-none cursor-pointer
        disabled:opacity-50 disabled:pointer-events-none
        focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-2 focus-visible:ring-offset-[var(--background)]
        ${variantStyles[props.variant]}
        ${props.class ?? ''}
      `}
    >
      <Show when={props.icon}>
        <span class="flex-shrink-0">{props.icon}</span>
      </Show>
      {props.label}
    </button>
  )
}
