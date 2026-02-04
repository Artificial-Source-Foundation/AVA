/**
 * Notification Context
 *
 * Global notification/toast system for displaying alerts, success messages,
 * errors, and other feedback to users.
 */

import { type Component, createContext, createSignal, For, type JSX, useContext } from 'solid-js'
import { Portal } from 'solid-js/web'
import { Toast } from '../components/ui/Toast'

// ============================================================================
// Types
// ============================================================================

export type ToastVariant = 'info' | 'success' | 'warning' | 'error'
export type ToastPosition =
  | 'top-right'
  | 'top-left'
  | 'bottom-right'
  | 'bottom-left'
  | 'top-center'
  | 'bottom-center'

export interface ToastOptions {
  /** Toast variant */
  variant?: ToastVariant
  /** Toast title */
  title: string
  /** Toast message/description */
  message?: string
  /** Duration in ms (0 for persistent) */
  duration?: number
  /** Action button */
  action?: {
    label: string
    onClick: () => void
  }
}

interface ToastItem extends ToastOptions {
  id: string
}

interface NotificationContextValue {
  /** Show a toast notification */
  toast: (options: ToastOptions) => string
  /** Show info toast */
  info: (title: string, message?: string) => string
  /** Show success toast */
  success: (title: string, message?: string) => string
  /** Show warning toast */
  warning: (title: string, message?: string) => string
  /** Show error toast */
  error: (title: string, message?: string) => string
  /** Dismiss a specific toast */
  dismiss: (id: string) => void
  /** Dismiss all toasts */
  dismissAll: () => void
}

// ============================================================================
// Context
// ============================================================================

const NotificationContext = createContext<NotificationContextValue>()

export const useNotification = () => {
  const context = useContext(NotificationContext)
  if (!context) {
    throw new Error('useNotification must be used within a NotificationProvider')
  }
  return context
}

// ============================================================================
// Provider
// ============================================================================

interface NotificationProviderProps {
  children: JSX.Element
  /** Toast position */
  position?: ToastPosition
  /** Default duration in ms */
  defaultDuration?: number
  /** Maximum toasts to show at once */
  maxToasts?: number
}

export const NotificationProvider: Component<NotificationProviderProps> = (props) => {
  const [toasts, setToasts] = createSignal<ToastItem[]>([])

  const position = () => props.position ?? 'top-right'
  const defaultDuration = () => props.defaultDuration ?? 5000
  const maxToasts = () => props.maxToasts ?? 5

  let toastCounter = 0

  const generateId = () => {
    toastCounter += 1
    return `toast-${toastCounter}-${Date.now()}`
  }

  const addToast = (options: ToastOptions): string => {
    const id = generateId()
    const toast: ToastItem = {
      id,
      variant: options.variant ?? 'info',
      ...options,
    }

    setToasts((prev) => {
      const updated = [toast, ...prev]
      // Limit number of toasts
      if (updated.length > maxToasts()) {
        return updated.slice(0, maxToasts())
      }
      return updated
    })

    // Auto dismiss
    const duration = options.duration ?? defaultDuration()
    if (duration > 0) {
      setTimeout(() => {
        dismiss(id)
      }, duration)
    }

    return id
  }

  const dismiss = (id: string) => {
    setToasts((prev) => prev.filter((t) => t.id !== id))
  }

  const dismissAll = () => {
    setToasts([])
  }

  const contextValue: NotificationContextValue = {
    toast: addToast,
    info: (title, message) => addToast({ variant: 'info', title, message }),
    success: (title, message) => addToast({ variant: 'success', title, message }),
    warning: (title, message) => addToast({ variant: 'warning', title, message }),
    error: (title, message) => addToast({ variant: 'error', title, message }),
    dismiss,
    dismissAll,
  }

  const positionStyles: Record<ToastPosition, string> = {
    'top-right': 'top-4 right-4',
    'top-left': 'top-4 left-4',
    'bottom-right': 'bottom-4 right-4',
    'bottom-left': 'bottom-4 left-4',
    'top-center': 'top-4 left-1/2 -translate-x-1/2',
    'bottom-center': 'bottom-4 left-1/2 -translate-x-1/2',
  }

  return (
    <NotificationContext.Provider value={contextValue}>
      {props.children}
      <Portal>
        <div
          class={`
            fixed z-[100]
            flex flex-col gap-2
            pointer-events-none
            ${positionStyles[position()]}
          `}
        >
          <For each={toasts()}>
            {(toast) => (
              <Toast
                variant={toast.variant}
                title={toast.title}
                message={toast.message}
                action={toast.action}
                onDismiss={() => dismiss(toast.id)}
              />
            )}
          </For>
        </div>
      </Portal>
    </NotificationContext.Provider>
  )
}
