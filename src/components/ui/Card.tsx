/**
 * Card Component
 *
 * A flexible card container with optional header, footer, and glass effect.
 */

import { type Component, type JSX, Show, splitProps } from 'solid-js'

export interface CardProps {
  /** Card content */
  children?: JSX.Element
  /** Card title */
  title?: string
  /** Card description */
  description?: string
  /** Header content (replaces title/description) */
  header?: JSX.Element
  /** Footer content */
  footer?: JSX.Element
  /** Use glass effect (theme-dependent) */
  glass?: boolean
  /** Interactive card (hover effects) */
  interactive?: boolean
  /** Padding size */
  padding?: 'none' | 'sm' | 'md' | 'lg'
  /** Additional CSS classes */
  class?: string
  /** Click handler (makes card interactive) */
  onClick?: (e: MouseEvent) => void
}

export const Card: Component<CardProps> = (props) => {
  const [local, others] = splitProps(props, [
    'children',
    'title',
    'description',
    'header',
    'footer',
    'glass',
    'interactive',
    'padding',
    'class',
    'onClick',
  ])

  const padding = () => local.padding ?? 'md'

  const paddingStyles = {
    none: '',
    sm: 'p-3',
    md: 'p-4',
    lg: 'p-6',
  }

  const isInteractive = () => local.interactive || !!local.onClick

  const handleKeyDown = (e: KeyboardEvent) => {
    if (local.onClick && (e.key === 'Enter' || e.key === ' ')) {
      e.preventDefault()
      local.onClick(e as unknown as MouseEvent)
    }
  }

  return (
    // biome-ignore lint/a11y/noStaticElementInteractions: role is conditionally set via onClick prop
    <div
      class={`
        rounded-[var(--radius-lg)]
        overflow-hidden
        ${local.glass ? 'glass' : 'border border-[var(--card-border)] bg-[var(--card-background)]'}
        ${isInteractive() ? 'interactive cursor-pointer hover:border-[var(--card-hover-border)] active:bg-[var(--surface-raised)]' : ''}
        ${local.class ?? ''}
      `}
      onClick={(e) => local.onClick?.(e)}
      // eslint-disable-next-line solid/reactivity -- conditional handler binding is intentional
      onKeyDown={local.onClick ? (e: KeyboardEvent) => handleKeyDown(e) : undefined}
      role={local.onClick ? 'button' : undefined}
      tabIndex={local.onClick ? 0 : undefined}
      {...others}
    >
      {/* Header */}
      <Show when={local.header || local.title}>
        <div class={`${paddingStyles[padding()]} ${local.children ? 'pb-0' : ''}`}>
          <Show
            when={local.header}
            fallback={
              <div>
                <Show when={local.title}>
                  <h3 class="text-base font-semibold text-[var(--text-primary)]">{local.title}</h3>
                </Show>
                <Show when={local.description}>
                  <p class="mt-1 text-sm text-[var(--text-secondary)]">{local.description}</p>
                </Show>
              </div>
            }
          >
            {local.header}
          </Show>
        </div>
      </Show>

      {/* Content */}
      <Show when={local.children}>
        <div class={paddingStyles[padding()]}>{local.children}</div>
      </Show>

      {/* Footer */}
      <Show when={local.footer}>
        <div
          class={`
          ${paddingStyles[padding()]}
          ${local.children ? 'pt-0' : ''}
          border-t border-[var(--border-subtle)]
          bg-[var(--surface-sunken)]
        `}
        >
          {local.footer}
        </div>
      </Show>
    </div>
  )
}

/**
 * CardHeader - Standalone header for custom card layouts
 */
export const CardHeader: Component<{ children: JSX.Element; class?: string }> = (props) => (
  <div class={`p-4 pb-0 ${props.class ?? ''}`}>{props.children}</div>
)

/**
 * CardContent - Standalone content area for custom card layouts
 */
export const CardContent: Component<{ children: JSX.Element; class?: string }> = (props) => (
  <div class={`p-4 ${props.class ?? ''}`}>{props.children}</div>
)

/**
 * CardFooter - Standalone footer for custom card layouts
 */
export const CardFooter: Component<{ children: JSX.Element; class?: string }> = (props) => (
  <div
    class={`p-4 pt-0 border-t border-[var(--border-subtle)] bg-[var(--surface-sunken)] ${props.class ?? ''}`}
  >
    {props.children}
  </div>
)
