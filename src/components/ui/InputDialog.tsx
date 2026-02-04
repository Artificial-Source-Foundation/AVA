/**
 * Input Dialog Component
 *
 * A dialog that prompts the user for text input.
 * Useful for rename operations, creating new items, etc.
 */

import { type Component, createEffect, createSignal } from 'solid-js'
import { Button } from './Button'
import { Dialog } from './Dialog'
import { Input } from './Input'

export interface InputDialogProps {
  /** Whether the dialog is open */
  open: boolean
  /** Callback when dialog open state changes */
  onOpenChange: (open: boolean) => void
  /** Dialog title */
  title: string
  /** Dialog description */
  description?: string
  /** Input label */
  label?: string
  /** Input placeholder */
  placeholder?: string
  /** Initial value */
  defaultValue?: string
  /** Confirm button text */
  confirmText?: string
  /** Cancel button text */
  cancelText?: string
  /** Callback when confirmed with value */
  onConfirm: (value: string) => void
  /** Callback when cancelled */
  onCancel?: () => void
  /** Validation function - return error message or undefined */
  validate?: (value: string) => string | undefined
  /** Loading state */
  loading?: boolean
  /** Input type */
  type?: 'text' | 'password' | 'email' | 'url'
}

export const InputDialog: Component<InputDialogProps> = (props) => {
  const [value, setValue] = createSignal(props.defaultValue ?? '')
  const [error, setError] = createSignal<string | undefined>()

  // Reset value when dialog opens
  createEffect(() => {
    if (props.open) {
      setValue(props.defaultValue ?? '')
      setError(undefined)
    }
  })

  const handleConfirm = () => {
    const currentValue = value().trim()

    if (props.validate) {
      const validationError = props.validate(currentValue)
      if (validationError) {
        setError(validationError)
        return
      }
    }

    props.onConfirm(currentValue)
  }

  const handleCancel = () => {
    props.onCancel?.()
    props.onOpenChange(false)
  }

  return (
    <Dialog
      open={props.open}
      onOpenChange={props.onOpenChange}
      title={props.title}
      description={props.description}
      size="sm"
      footer={
        <>
          <Button variant="secondary" onClick={handleCancel} disabled={props.loading}>
            {props.cancelText ?? 'Cancel'}
          </Button>
          <Button
            variant="primary"
            onClick={handleConfirm}
            loading={props.loading}
            disabled={!value().trim()}
          >
            {props.confirmText ?? 'Confirm'}
          </Button>
        </>
      }
    >
      <Input
        type={props.type ?? 'text'}
        label={props.label}
        placeholder={props.placeholder}
        value={value()}
        onValueChange={(newValue) => {
          setValue(newValue)
          setError(undefined)
        }}
        error={error()}
        autofocus
      />
    </Dialog>
  )
}
