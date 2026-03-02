/**
 * Settings Sync
 * Bidirectional bridge between core-v2 SettingsManager and the frontend.
 *
 * - When extensions modify settings via SettingsManager.set(), dispatches
 *   a CustomEvent so the frontend settings store can react.
 * - Uses a _pushing flag to prevent feedback loops when the frontend
 *   pushes settings to core via pushSettingsToCore().
 */

import type { SettingsEvent } from '@ava/core-v2/config'
import { getCoreSettings } from './core-bridge'

// ─── Loop Prevention ────────────────────────────────────────────────────────

let _pushing = false

/**
 * Call before pushSettingsToCore() to suppress the echo event.
 * The flag auto-resets on the next microtask.
 */
export function markPushing(): void {
  _pushing = true
  queueMicrotask(() => {
    _pushing = false
  })
}

// ─── Sync Lifecycle ─────────────────────────────────────────────────────────

/**
 * Start listening for SettingsManager events and forward them to the window.
 * Returns a cleanup function.
 */
export function startSettingsSync(): () => void {
  const sm = getCoreSettings()
  if (!sm) return () => {}

  const handler = (event: SettingsEvent) => {
    if (_pushing) return

    if (event.type === 'category_changed' || event.type === 'category_registered') {
      const value = event.type === 'category_changed' ? sm.get(event.category) : undefined
      window.dispatchEvent(
        new CustomEvent('ava:core-settings-changed', {
          detail: { type: event.type, category: event.category, value },
        })
      )
    }
  }

  const unsub = sm.on(handler)
  return unsub
}
