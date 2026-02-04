/**
 * Toast Component
 *
 * Individual toast notification with variants, icons, and dismiss functionality.
 */

import { AlertTriangle, CheckCircle, Info, X, XCircle } from 'lucide-solid'
import { type Component, type JSX, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'

export type ToastVariant = 'info' | 'success' | 'warning' | 'error'

export interface ToastProps {
  /** Toast variant */
  variant?: ToastVariant
  /** Toast title */
  title: string
  /** Toast message/description */
  message?: string
  /** Action button */
  action?: {
    label: string
    onClick: () => void
  }
  /** Dismiss callback */
  onDismiss?: () => void
}

type IconComponent = (props: { class?: string; style?: JSX.CSSProperties }) => JSX.Element

const variantConfig: Record<
  ToastVariant,
  { icon: IconComponent; color: string; bg: string; border: string }
> = {
  info: {
    icon: Info,
    color: 'var(--info)',
    bg: 'var(--info-subtle)',
    border: 'var(--info-muted)',
  },
  success: {
    icon: CheckCircle,
    color: 'var(--success)',
    bg: 'var(--success-subtle)',
    border: 'var(--success-muted)',
  },
  warning: {
    icon: AlertTriangle,
    color: 'var(--warning)',
    bg: 'var(--warning-subtle)',
    border: 'var(--warning-muted)',
  },
  error: {
    icon: XCircle,
    color: 'var(--error)',
    bg: 'var(--error-subtle)',
    border: 'var(--error-muted)',
  },
}

export const Toast: Component<ToastProps> = (props) => {
  const variant = () => props.variant ?? 'info'
  const config = () => variantConfig[variant()]

  return (
    <div
      class={`
        pointer-events-auto
        w-80 max-w-[calc(100vw-2rem)]
        p-4
        bg-[var(--surface-overlay)]
        border rounded-[var(--radius-lg)]
        shadow-lg
        animate-in slide-in-from-right-full fade-in-0
        duration-300
      `}
      style={{ 'border-color': config().border }}
      role="alert"
    >
      <div class="flex gap-3">
        {/* Icon */}
        <div class="flex-shrink-0 p-1.5 rounded-full" style={{ background: config().bg }}>
          <Dynamic component={config().icon} class="w-4 h-4" style={{ color: config().color }} />
        </div>

        {/* Content */}
        <div class="flex-1 min-w-0">
          <p class="text-sm font-medium text-[var(--text-primary)]">{props.title}</p>
          <Show when={props.message}>
            <p class="mt-1 text-sm text-[var(--text-secondary)]">{props.message}</p>
          </Show>
          <Show when={props.action}>
            <button
              type="button"
              onClick={props.action?.onClick}
              class="
                mt-2
                text-sm font-medium
                transition-colors duration-[var(--duration-fast)]
                hover:underline
              "
              style={{ color: config().color }}
            >
              {props.action?.label}
            </button>
          </Show>
        </div>

        {/* Dismiss */}
        <Show when={props.onDismiss}>
          <button
            type="button"
            onClick={props.onDismiss}
            class="
              flex-shrink-0
              p-1 rounded-[var(--radius-md)]
              text-[var(--text-tertiary)]
              hover:text-[var(--text-primary)]
              hover:bg-[var(--surface-raised)]
              transition-colors duration-[var(--duration-fast)]
            "
          >
            <X class="w-4 h-4" />
          </button>
        </Show>
      </div>
    </div>
  )
}
