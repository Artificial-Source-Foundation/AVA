/**
 * Confirm Dialog Component
 *
 * A confirmation dialog for yes/no decisions.
 * Supports danger mode for destructive actions.
 */

import { AlertTriangle, HelpCircle, Info } from 'lucide-solid'
import type { Component, JSX } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import { Button } from './Button'
import { Dialog } from './Dialog'

export interface ConfirmDialogProps {
  /** Whether the dialog is open */
  open: boolean
  /** Callback when dialog open state changes */
  onOpenChange: (open: boolean) => void
  /** Dialog title */
  title: string
  /** Dialog message/description */
  message: string
  /** Confirm button text */
  confirmText?: string
  /** Cancel button text */
  cancelText?: string
  /** Callback when confirmed */
  onConfirm: () => void
  /** Callback when cancelled */
  onCancel?: () => void
  /** Variant for different contexts */
  variant?: 'default' | 'danger' | 'warning' | 'info'
  /** Loading state for confirm button */
  loading?: boolean
}

type IconComponent = (props: { class?: string; style?: JSX.CSSProperties }) => JSX.Element

const iconConfig: Record<string, { icon: IconComponent; color: string; bg: string }> = {
  default: { icon: HelpCircle, color: 'var(--accent)', bg: 'var(--accent-subtle)' },
  danger: { icon: AlertTriangle, color: 'var(--error)', bg: 'var(--error-subtle)' },
  warning: { icon: AlertTriangle, color: 'var(--warning)', bg: 'var(--warning-subtle)' },
  info: { icon: Info, color: 'var(--info)', bg: 'var(--info-subtle)' },
}

export const ConfirmDialog: Component<ConfirmDialogProps> = (props) => {
  const variant = () => props.variant ?? 'default'

  const handleConfirm = () => {
    props.onConfirm()
  }

  const handleCancel = () => {
    props.onCancel?.()
    props.onOpenChange(false)
  }

  const config = () => iconConfig[variant()]

  return (
    <Dialog open={props.open} onOpenChange={props.onOpenChange} size="sm" showCloseButton={false}>
      <div class="flex flex-col items-center text-center">
        {/* Icon */}
        <div class="p-3 rounded-full mb-4" style={{ background: config().bg }}>
          <Dynamic component={config().icon} class="w-6 h-6" style={{ color: config().color }} />
        </div>

        {/* Title */}
        <h3 class="text-lg font-semibold text-[var(--text-primary)] mb-2">{props.title}</h3>

        {/* Message */}
        <p class="text-sm text-[var(--text-secondary)] mb-6">{props.message}</p>

        {/* Actions */}
        <div class="flex items-center gap-3 w-full">
          <Button variant="secondary" fullWidth onClick={handleCancel} disabled={props.loading}>
            {props.cancelText ?? 'Cancel'}
          </Button>
          <Button
            variant={variant() === 'danger' ? 'danger' : 'primary'}
            fullWidth
            onClick={handleConfirm}
            loading={props.loading}
          >
            {props.confirmText ?? 'Confirm'}
          </Button>
        </div>
      </div>
    </Dialog>
  )
}
