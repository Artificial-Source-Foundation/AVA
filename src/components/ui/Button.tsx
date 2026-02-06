/**
 * Button Component
 *
 * A versatile button with multiple variants, sizes, and states.
 * Built with Kobalte for accessibility.
 */

import { Button as KobalteButton } from '@kobalte/core/button'
import { Loader2 } from 'lucide-solid'
import { type Component, type JSX, Show, splitProps } from 'solid-js'
import { Motion } from 'solid-motionone'

export interface ButtonProps {
  /** Button variant */
  variant?: 'primary' | 'secondary' | 'ghost' | 'danger' | 'success' | 'warning'
  /** Button size */
  size?: 'sm' | 'md' | 'lg' | 'icon'
  /** Full width button */
  fullWidth?: boolean
  /** Loading state */
  loading?: boolean
  /** Disabled state */
  disabled?: boolean
  /** Button type */
  type?: 'button' | 'submit' | 'reset'
  /** Click handler */
  onClick?: (e: MouseEvent) => void
  /** Button content */
  children?: JSX.Element
  /** Additional CSS classes */
  class?: string
  /** Icon to show before text */
  icon?: JSX.Element
  /** Icon to show after text */
  iconRight?: JSX.Element
}

export const Button: Component<ButtonProps> = (props) => {
  const [local, others] = splitProps(props, [
    'variant',
    'size',
    'fullWidth',
    'loading',
    'disabled',
    'type',
    'onClick',
    'children',
    'class',
    'icon',
    'iconRight',
  ])

  const variant = () => local.variant ?? 'primary'
  const size = () => local.size ?? 'md'

  const baseStyles = `
    inline-flex items-center justify-center gap-2
    font-medium
    transition-colors duration-[var(--duration-fast)] ease-[var(--ease-out)]
    focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-2 focus-visible:ring-offset-[var(--background)]
    disabled:pointer-events-none disabled:opacity-50
    select-none
  `

  const variantStyles = {
    primary: `
      bg-[var(--button-primary-bg)]
      text-[var(--button-primary-text)]
      hover:bg-[var(--button-primary-hover)]
      active:bg-[var(--button-primary-active)]
    `,
    secondary: `
      bg-[var(--button-secondary-bg)]
      text-[var(--button-secondary-text)]
      border border-[var(--button-secondary-border)]
      hover:bg-[var(--button-secondary-hover)]
      active:bg-[var(--button-secondary-active)]
    `,
    ghost: `
      bg-[var(--button-ghost-bg)]
      text-[var(--button-ghost-text)]
      hover:bg-[var(--button-ghost-hover)]
      active:bg-[var(--button-ghost-active)]
    `,
    danger: `
      bg-[var(--error)]
      text-white
      hover:brightness-110
      active:brightness-90
    `,
    success: `
      bg-[var(--success)]
      text-white
      hover:brightness-110
      active:brightness-90
    `,
    warning: `
      bg-[var(--warning)]
      text-white
      hover:brightness-110
      active:brightness-90
    `,
  }

  const sizeStyles = {
    sm: 'h-8 px-3 text-sm rounded-[var(--radius-md)]',
    md: 'h-10 px-4 text-sm rounded-[var(--radius-lg)]',
    lg: 'h-12 px-6 text-base rounded-[var(--radius-lg)]',
    icon: 'h-10 w-10 rounded-[var(--radius-lg)]',
  }

  return (
    <Motion.div
      press={{ scale: 0.97 }}
      transition={{ duration: 0.15 }}
      style={{ display: local.fullWidth ? 'block' : 'inline-block' }}
    >
      <KobalteButton
        type={local.type ?? 'button'}
        disabled={local.disabled || local.loading}
        onClick={local.onClick}
        class={`
          ${baseStyles}
          ${variantStyles[variant()]}
          ${sizeStyles[size()]}
          ${local.fullWidth ? 'w-full' : ''}
          ${local.class ?? ''}
        `}
        {...others}
      >
        <Show when={local.loading}>
          <Loader2 class="h-4 w-4 animate-spin" />
        </Show>
        <Show when={!local.loading && local.icon}>{local.icon}</Show>
        <Show when={local.children}>{local.children}</Show>
        <Show when={!local.loading && local.iconRight}>{local.iconRight}</Show>
      </KobalteButton>
    </Motion.div>
  )
}
