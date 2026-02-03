/**
 * Avatar Component
 *
 * User avatars with image, initials, or icon fallback.
 */

import { User } from 'lucide-solid'
import { type Component, createSignal, type JSX, Show, splitProps } from 'solid-js'

export interface AvatarProps {
  /** Image source URL */
  src?: string
  /** Alt text for image */
  alt?: string
  /** Fallback initials (e.g., "JD" for John Doe) */
  initials?: string
  /** Avatar size */
  size?: 'xs' | 'sm' | 'md' | 'lg' | 'xl'
  /** Shape */
  shape?: 'circle' | 'square'
  /** Status indicator */
  status?: 'online' | 'offline' | 'away' | 'busy'
  /** Additional CSS classes */
  class?: string
}

export const Avatar: Component<AvatarProps> = (props) => {
  const [local, others] = splitProps(props, [
    'src',
    'alt',
    'initials',
    'size',
    'shape',
    'status',
    'class',
  ])

  const [imageError, setImageError] = createSignal(false)

  const size = () => local.size ?? 'md'
  const shape = () => local.shape ?? 'circle'

  const sizeStyles = {
    xs: 'h-6 w-6 text-2xs',
    sm: 'h-8 w-8 text-xs',
    md: 'h-10 w-10 text-sm',
    lg: 'h-12 w-12 text-base',
    xl: 'h-16 w-16 text-lg',
  }

  const iconSizes = {
    xs: 'h-3 w-3',
    sm: 'h-4 w-4',
    md: 'h-5 w-5',
    lg: 'h-6 w-6',
    xl: 'h-8 w-8',
  }

  const statusColors = {
    online: 'bg-[var(--success)]',
    offline: 'bg-[var(--text-muted)]',
    away: 'bg-[var(--warning)]',
    busy: 'bg-[var(--error)]',
  }

  const statusSizes = {
    xs: 'h-1.5 w-1.5 border',
    sm: 'h-2 w-2 border',
    md: 'h-2.5 w-2.5 border-2',
    lg: 'h-3 w-3 border-2',
    xl: 'h-4 w-4 border-2',
  }

  const showImage = () => local.src && !imageError()

  return (
    <div
      class={`
        relative inline-flex items-center justify-center
        overflow-hidden
        bg-[var(--surface-raised)]
        border border-[var(--border-subtle)]
        ${sizeStyles[size()]}
        ${shape() === 'circle' ? 'rounded-full' : 'rounded-[var(--radius-lg)]'}
        ${local.class ?? ''}
      `}
      {...others}
    >
      {/* Image */}
      <Show when={showImage()}>
        <img
          src={local.src}
          alt={local.alt ?? ''}
          onError={() => setImageError(true)}
          class="h-full w-full object-cover"
        />
      </Show>

      {/* Initials fallback */}
      <Show when={!showImage() && local.initials}>
        <span class="font-medium text-[var(--text-secondary)] uppercase">{local.initials}</span>
      </Show>

      {/* Icon fallback */}
      <Show when={!showImage() && !local.initials}>
        <User class={`${iconSizes[size()]} text-[var(--text-tertiary)]`} />
      </Show>

      {/* Status indicator */}
      <Show when={local.status}>
        <span
          class={`
            absolute bottom-0 right-0
            rounded-full
            border-[var(--surface)]
            ${statusColors[local.status!]}
            ${statusSizes[size()]}
          `}
        />
      </Show>
    </div>
  )
}

/**
 * AvatarGroup - Stack multiple avatars
 */
export interface AvatarGroupProps {
  /** Maximum avatars to show before +N */
  max?: number
  /** Avatar size */
  size?: 'xs' | 'sm' | 'md' | 'lg' | 'xl'
  /** Children (Avatar components) */
  children: JSX.Element
  /** Additional CSS classes */
  class?: string
}

export const AvatarGroup: Component<AvatarGroupProps> = (props) => {
  const [local, others] = splitProps(props, ['max', 'size', 'children', 'class'])

  const overlapStyles = {
    xs: '-ml-1.5',
    sm: '-ml-2',
    md: '-ml-2.5',
    lg: '-ml-3',
    xl: '-ml-4',
  }

  const size = () => local.size ?? 'md'

  return (
    <div class={`flex items-center ${local.class ?? ''}`} {...others}>
      <div class={`flex [&>*:not(:first-child)]:${overlapStyles[size()]}`}>{local.children}</div>
    </div>
  )
}
