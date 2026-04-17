/**
 * Settings Dialog Utils
 *
 * Shared utilities for reliable nested dialog Escape handling.
 * Provides focus management and Escape key handling for settings dialogs.
 */

import type { Accessor, Component, JSX } from 'solid-js'
import { createEffect, onCleanup, onMount } from 'solid-js'

interface DialogEscapeConfig {
  /** Called when Escape is pressed and dialog should close */
  onEscape: () => void
  /** Whether the dialog is currently open */
  isOpen: Accessor<boolean>
  /** Returns the dialog element that owns Escape while open */
  getDialogElement: () => HTMLElement | undefined
}

const settingsDialogStack: HTMLElement[] = []

function cleanupSettingsDialogStack() {
  for (let i = settingsDialogStack.length - 1; i >= 0; i -= 1) {
    if (!settingsDialogStack[i]?.isConnected) {
      settingsDialogStack.splice(i, 1)
    }
  }
}

function registerSettingsDialog(dialogElement: HTMLElement) {
  cleanupSettingsDialogStack()

  const existingIndex = settingsDialogStack.indexOf(dialogElement)
  if (existingIndex !== -1) {
    settingsDialogStack.splice(existingIndex, 1)
  }

  settingsDialogStack.push(dialogElement)

  return () => {
    const index = settingsDialogStack.indexOf(dialogElement)
    if (index !== -1) {
      settingsDialogStack.splice(index, 1)
    }
  }
}

function getTopmostSettingsDialog() {
  cleanupSettingsDialogStack()
  return settingsDialogStack.at(-1)
}

function focusSettingsDialog(dialogElement: HTMLElement | undefined) {
  if (!dialogElement) return

  queueMicrotask(() => {
    if (!dialogElement.isConnected) return

    const activeElement = document.activeElement
    if (activeElement instanceof Node && dialogElement.contains(activeElement)) {
      return
    }

    dialogElement.focus()
  })
}

/**
 * Hook for reliable Escape handling in nested settings dialogs.
 * - Prevents event from bubbling to parent dialogs
 * - Lets only the topmost open settings child dialog own Escape
 * - Cleans up on unmount
 */
export function useSettingsDialogEscape(config: DialogEscapeConfig) {
  let unregisterDialog: (() => void) | undefined

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key !== 'Escape') return
    if (!config.isOpen()) return

    const dialogElement = config.getDialogElement()
    if (!dialogElement) return
    if (getTopmostSettingsDialog() !== dialogElement) return

    e.preventDefault()
    e.stopImmediatePropagation()
    e.stopPropagation()
    config.onEscape()
  }

  onMount(() => {
    // Capture phase to intercept before window listeners
    window.addEventListener('keydown', handleKeyDown, true)
  })

  createEffect(() => {
    if (!config.isOpen()) {
      unregisterDialog?.()
      unregisterDialog = undefined
      return
    }

    queueMicrotask(() => {
      if (!config.isOpen()) return

      const dialogElement = config.getDialogElement()
      if (!dialogElement) return

      unregisterDialog?.()
      unregisterDialog = registerSettingsDialog(dialogElement)
      focusSettingsDialog(dialogElement)
    })
  })

  onCleanup(() => {
    unregisterDialog?.()
    window.removeEventListener('keydown', handleKeyDown, true)
  })
}

interface SettingsDialogContainerProps {
  children: JSX.Element
  /** Called when clicking backdrop or pressing Escape */
  onClose: () => void
  /** Whether the dialog is open */
  open: boolean
  /** Additional CSS classes for the content container */
  contentClass?: string
  /** ARIA label for the dialog */
  ariaLabel?: string
}

/**
 * Shared container for settings nested dialogs.
 * Provides consistent Escape handling, backdrop click, and focus management.
 */
export const SettingsDialogContainer: Component<SettingsDialogContainerProps> = (props) => {
  let dialogRef: HTMLDivElement | undefined

  useSettingsDialogEscape({
    onEscape: props.onClose,
    isOpen: () => props.open,
    getDialogElement: () => dialogRef,
  })

  const handleBackdropClick = (e: MouseEvent) => {
    if (e.target === dialogRef) {
      props.onClose()
    }
  }

  return (
    <div
      ref={dialogRef}
      role="dialog"
      aria-modal="true"
      aria-label={props.ariaLabel}
      data-settings-nested-dialog="true"
      class="fixed inset-0 z-[var(--z-modal)] flex items-center justify-center outline-none"
      style={{ background: 'var(--modal-overlay)' }}
      tabindex="-1"
      onClick={handleBackdropClick}
      onKeyDown={(e) => {
        if (e.key !== 'Escape') return
        e.preventDefault()
        e.stopPropagation()
        props.onClose()
      }}
    >
      <div class={props.contentClass}>{props.children}</div>
    </div>
  )
}
