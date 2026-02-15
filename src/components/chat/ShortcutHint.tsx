/**
 * Shortcut Hint Component
 *
 * Dismissible "Shift+Enter for newline" hint below the textarea.
 * Auto-dismisses after 3 message sends OR 8 seconds, whichever comes first.
 * Stores dismissal in localStorage so it doesn't reappear.
 */

import { type Component, createEffect, createSignal, onCleanup, Show } from 'solid-js'
import { useSettings } from '../../stores/settings'

const STORAGE_KEY = 'ava-shortcut-hint-dismissed'
const AUTO_DISMISS_MS = 8000
const DISMISS_AFTER_SENDS = 3

export const ShortcutHint: Component<{ sendCount: number }> = (props) => {
  const { settings } = useSettings()
  const alreadyDismissed = localStorage.getItem(STORAGE_KEY) === '1'
  const [visible, setVisible] = createSignal(!alreadyDismissed)
  const [fading, setFading] = createSignal(false)

  const dismiss = () => {
    setFading(true)
    setTimeout(() => {
      setVisible(false)
      localStorage.setItem(STORAGE_KEY, '1')
    }, 300)
  }

  // Auto-dismiss after timeout
  if (!alreadyDismissed) {
    const timer = setTimeout(dismiss, AUTO_DISMISS_MS)
    onCleanup(() => clearTimeout(timer))
  }

  // Dismiss after N sends
  createEffect(() => {
    if (props.sendCount >= DISMISS_AFTER_SENDS && visible()) {
      dismiss()
    }
  })

  return (
    <Show when={visible()}>
      <div
        class="text-[10px] text-[var(--text-muted)] transition-opacity duration-300"
        style={{ opacity: fading() ? '0' : '0.7' }}
      >
        <kbd class="px-1 py-0.5 bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded text-[9px] font-mono">
          {settings().behavior.sendKey === 'enter' ? 'Shift+Enter' : 'Enter'}
        </kbd>{' '}
        for newline
      </div>
    </Show>
  )
}
