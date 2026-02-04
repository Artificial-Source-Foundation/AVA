/**
 * Alert Dialog Component
 *
 * A simple alert dialog for displaying information, warnings, or errors.
 * Single action button to dismiss.
 */

import { AlertTriangle, CheckCircle, Info, XCircle } from 'lucide-solid'
import type { Component, JSX } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import { Button } from './Button'
import { Dialog } from './Dialog'

export interface AlertDialogProps {
  /** Whether the dialog is open */
  open: boolean
  /** Callback when dialog open state changes */
  onOpenChange: (open: boolean) => void
  /** Dialog title */
  title: string
  /** Dialog message/description */
  message: string
  /** Button text */
  buttonText?: string
  /** Callback when dismissed */
  onDismiss?: () => void
  /** Alert variant */
  variant?: 'info' | 'success' | 'warning' | 'error'
}

type IconComponent = (props: { class?: string; style?: JSX.CSSProperties }) => JSX.Element

const iconConfig: Record<string, { icon: IconComponent; color: string; bg: string }> = {
  info: { icon: Info, color: 'var(--info)', bg: 'var(--info-subtle)' },
  success: { icon: CheckCircle, color: 'var(--success)', bg: 'var(--success-subtle)' },
  warning: { icon: AlertTriangle, color: 'var(--warning)', bg: 'var(--warning-subtle)' },
  error: { icon: XCircle, color: 'var(--error)', bg: 'var(--error-subtle)' },
}

export const AlertDialog: Component<AlertDialogProps> = (props) => {
  const variant = () => props.variant ?? 'info'

  const handleDismiss = () => {
    props.onDismiss?.()
    props.onOpenChange(false)
  }

  const config = () => iconConfig[variant()]

  return (
    <Dialog
      open={props.open}
      onOpenChange={props.onOpenChange}
      size="sm"
      showCloseButton={false}
      closeOnOverlayClick={false}
    >
      <div class="flex flex-col items-center text-center">
        {/* Icon */}
        <div class="p-3 rounded-full mb-4" style={{ background: config().bg }}>
          <Dynamic component={config().icon} class="w-6 h-6" style={{ color: config().color }} />
        </div>

        {/* Title */}
        <h3 class="text-lg font-semibold text-[var(--text-primary)] mb-2">{props.title}</h3>

        {/* Message */}
        <p class="text-sm text-[var(--text-secondary)] mb-6 whitespace-pre-wrap">{props.message}</p>

        {/* Action */}
        <Button variant="primary" fullWidth onClick={handleDismiss}>
          {props.buttonText ?? 'OK'}
        </Button>
      </div>
    </Dialog>
  )
}
