/**
 * Badge Component
 *
 * Small labels for status, tags, and counts.
 */

import { type Component, type JSX, Show, splitProps } from 'solid-js'

export interface BadgeProps {
  /** Badge variant */
  variant?: 'default' | 'secondary' | 'success' | 'warning' | 'error' | 'info' | 'outline'
  /** Badge size */
  size?: 'sm' | 'md' | 'lg'
  /** Badge content */
  children?: JSX.Element
  /** Additional CSS classes */
  class?: string
  /** Dot indicator instead of text */
  dot?: boolean
  /** Pulse animation (for active/new indicators) */
  pulse?: boolean
}

export const Badge: Component<BadgeProps> = (props) => {
  const [local, others] = splitProps(props, [
    'variant',
    'size',
    'children',
    'class',
    'dot',
    'pulse',
  ])

  const variant = () => local.variant ?? 'default'
  const size = () => local.size ?? 'md'

  const variantStyles = {
    default: 'bg-[var(--accent)] text-white',
    secondary:
      'bg-[var(--surface-raised)] text-[var(--text-secondary)] border border-[var(--border-subtle)]',
    success: 'bg-[var(--success-subtle)] text-[var(--success)]',
    warning: 'bg-[var(--warning-subtle)] text-[var(--warning)]',
    error: 'bg-[var(--error-subtle)] text-[var(--error)]',
    info: 'bg-[var(--info-subtle)] text-[var(--info)]',
    outline: 'bg-transparent border border-[var(--border-default)] text-[var(--text-secondary)]',
  }

  const sizeStyles = {
    sm: 'text-[10px] px-1.5 py-px',
    md: 'text-[11px] px-2 py-0.5',
    lg: 'text-xs px-2.5 py-0.5',
  }

  const dotColors = {
    default: 'bg-[var(--accent)]',
    secondary: 'bg-[var(--text-tertiary)]',
    success: 'bg-[var(--success)]',
    warning: 'bg-[var(--warning)]',
    error: 'bg-[var(--error)]',
    info: 'bg-[var(--info)]',
    outline: 'bg-[var(--text-tertiary)]',
  }

  return (
    <Show
      when={!local.dot}
      fallback={
        <span
          class={`
            inline-block
            rounded-full
            ${dotColors[variant()]}
            ${size() === 'sm' ? 'h-1.5 w-1.5' : size() === 'md' ? 'h-2 w-2' : 'h-2.5 w-2.5'}
            ${local.pulse ? 'animate-pulse-subtle' : ''}
            ${local.class ?? ''}
          `}
          {...others}
        />
      }
    >
      <span
        class={`
          inline-flex items-center justify-center
          font-[var(--font-ui-mono)]
          font-medium tracking-wide
          rounded-[var(--radius-md)]
          whitespace-nowrap
          ${variantStyles[variant()]}
          ${sizeStyles[size()]}
          ${local.pulse ? 'animate-pulse-subtle' : ''}
          ${local.class ?? ''}
        `}
        {...others}
      >
        {local.children}
      </span>
    </Show>
  )
}
