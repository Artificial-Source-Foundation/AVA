/**
 * Dialog Component
 *
 * A flexible, accessible dialog/modal built with Kobalte.
 * Supports various sizes, animations, and content types.
 */

import { Dialog as KobalteDialog } from '@kobalte/core/dialog'
import { X } from 'lucide-solid'
import { type Component, type JSX, Show, splitProps } from 'solid-js'

export interface DialogProps {
  /** Whether the dialog is open */
  open: boolean
  /** Callback when dialog open state changes */
  onOpenChange: (open: boolean) => void
  /** Dialog title */
  title?: string
  /** Dialog description */
  description?: string
  /** Dialog content */
  children?: JSX.Element
  /** Footer content (buttons, etc.) */
  footer?: JSX.Element
  /** Dialog size */
  size?: 'sm' | 'md' | 'lg' | 'xl' | '2xl' | 'full'
  /** Show close button */
  showCloseButton?: boolean
  /** Close on overlay click */
  closeOnOverlayClick?: boolean
  /** Close on escape key */
  closeOnEscape?: boolean
  /** Additional CSS classes for content */
  class?: string
}

const sizeStyles: Record<string, string> = {
  sm: 'max-w-sm',
  md: 'max-w-md',
  lg: 'max-w-lg',
  xl: 'max-w-xl',
  '2xl': 'max-w-[56rem]',
  full: 'max-w-[90vw] max-h-[90vh]',
}

export const Dialog: Component<DialogProps> = (props) => {
  const [local, others] = splitProps(props, [
    'open',
    'onOpenChange',
    'title',
    'description',
    'children',
    'footer',
    'size',
    'showCloseButton',
    'closeOnOverlayClick',
    'closeOnEscape',
    'class',
  ])

  const size = () => local.size ?? 'md'
  const showClose = () => local.showCloseButton ?? true
  const closeOnOverlay = () => local.closeOnOverlayClick ?? true
  const closeOnEsc = () => local.closeOnEscape ?? true

  return (
    <KobalteDialog open={local.open} onOpenChange={local.onOpenChange} {...others}>
      <KobalteDialog.Portal>
        {/* Overlay */}
        <KobalteDialog.Overlay
          class="
            fixed inset-0 z-50
            data-[expanded]:animate-in data-[expanded]:fade-in-0
            data-[closed]:animate-out data-[closed]:fade-out-0
          "
          style={{ background: 'var(--modal-overlay)' }}
        />

        {/* Content */}
        <KobalteDialog.Content
          role="dialog"
          onInteractOutside={(e) => {
            if (!closeOnOverlay()) e.preventDefault()
          }}
          onEscapeKeyDown={(e) => {
            if (!closeOnEsc()) e.preventDefault()
          }}
          onKeyDown={(e: KeyboardEvent) => e.stopPropagation()}
          class={`
            fixed left-1/2 top-1/2 z-50
            -translate-x-1/2 -translate-y-1/2
            w-full ${sizeStyles[size()]}
            data-[expanded]:animate-in data-[expanded]:fade-in-0 data-[expanded]:zoom-in-95 data-[expanded]:slide-in-from-left-1/2 data-[expanded]:slide-in-from-top-[48%]
            data-[closed]:animate-out data-[closed]:fade-out-0 data-[closed]:zoom-out-95 data-[closed]:slide-out-to-left-1/2 data-[closed]:slide-out-to-top-[48%]
            duration-200
            ${local.class ?? ''}
          `}
          style={{
            background: 'var(--modal-surface)',
            border: '1px solid var(--modal-border)',
            'border-radius': 'var(--modal-radius-lg)',
            'box-shadow': 'var(--modal-shadow)',
          }}
        >
          {/* Header */}
          <Show when={local.title || showClose()}>
            <div
              class="flex items-start justify-between gap-4 p-4 border-b"
              style={{ 'border-color': 'var(--modal-border)' }}
            >
              <div class="flex-1">
                <Show when={local.title}>
                  <KobalteDialog.Title class="text-lg font-semibold text-[var(--text-primary)]">
                    {local.title}
                  </KobalteDialog.Title>
                </Show>
                <Show when={local.description}>
                  <KobalteDialog.Description class="mt-1 text-sm text-[var(--text-secondary)]">
                    {local.description}
                  </KobalteDialog.Description>
                </Show>
              </div>
              <Show when={showClose()}>
                <KobalteDialog.CloseButton
                  class="
                    dialog-close-button
                    p-1.5 rounded-[var(--radius-md)]
                    hover:bg-[var(--alpha-white-5)]
                    transition-colors duration-[var(--duration-fast)]
                  "
                  aria-label="Close dialog"
                >
                  <X class="w-4 h-4" />
                </KobalteDialog.CloseButton>
              </Show>
            </div>
          </Show>

          {/* Body */}
          <div
            class="p-4 overflow-y-auto max-h-[70vh]"
            style={{ 'will-change': 'scroll-position', '-webkit-overflow-scrolling': 'touch' }}
          >
            {local.children}
          </div>

          {/* Footer */}
          <Show when={local.footer}>
            <div
              class="flex items-center justify-end gap-2 p-4 border-t"
              style={{ 'border-color': 'var(--modal-border)' }}
            >
              {local.footer}
            </div>
          </Show>
        </KobalteDialog.Content>
      </KobalteDialog.Portal>
    </KobalteDialog>
  )
}

/**
 * Dialog Trigger - Wraps the element that opens the dialog
 */
export const DialogTrigger = KobalteDialog.Trigger

/**
 * Dialog Close - Wraps the element that closes the dialog
 */
export const DialogClose = KobalteDialog.CloseButton
