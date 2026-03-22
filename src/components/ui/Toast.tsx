/**
 * Toast Component
 *
 * Individual toast notification with variants, icons, and dismiss functionality.
 */

import { AlertTriangle, CheckCircle, Info, X, XCircle } from 'lucide-solid'
import { type Component, type JSX, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import { Motion } from 'solid-motionone'

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
    <Motion.div
      initial={{ opacity: 0, y: -8, scale: 0.96 }}
      animate={{ opacity: 1, y: 0, scale: 1 }}
      exit={{ opacity: 0, y: -8, scale: 0.96 }}
      transition={{ duration: 0.2, easing: [0.2, 0, 0, 1] }}
      class="
        pointer-events-auto
        max-w-[calc(100vw-2rem)]
        px-3 py-2
        bg-[var(--surface-overlay)]
        border border-[var(--border-subtle)]
        rounded-full
        shadow-lg
      "
      role="alert"
    >
      <div class="flex items-center gap-2">
        {/* Icon */}
        <Dynamic
          component={config().icon}
          class="w-3.5 h-3.5 flex-shrink-0"
          style={{ color: config().color }}
        />

        {/* Content */}
        <span class="text-xs font-medium text-[var(--text-primary)] whitespace-nowrap">
          {props.title}
        </span>
        <Show when={props.message}>
          <span class="text-xs text-[var(--text-secondary)] whitespace-nowrap">
            {props.message}
          </span>
        </Show>
        <Show when={props.action}>
          <button
            type="button"
            onClick={() => props.action?.onClick()}
            class="
              text-xs font-medium
              transition-colors duration-[var(--duration-fast)]
              hover:underline
              whitespace-nowrap
            "
            style={{ color: config().color }}
          >
            {props.action?.label}
          </button>
        </Show>

        {/* Dismiss */}
        <Show when={props.onDismiss}>
          <button
            type="button"
            onClick={() => props.onDismiss?.()}
            class="
              flex-shrink-0
              p-0.5 rounded-full
              text-[var(--text-tertiary)]
              hover:text-[var(--text-primary)]
              transition-colors duration-[var(--duration-fast)]
            "
          >
            <X class="w-3 h-3" />
          </button>
        </Show>
      </div>
    </Motion.div>
  )
}
