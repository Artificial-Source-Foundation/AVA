/**
 * Settings Sync
 * Bidirectional bridge between core-v2 SettingsManager and the frontend.
 *
 * - When extensions modify settings via SettingsManager.set(), dispatches
 *   a CustomEvent so the frontend settings store can react.
 * - Uses a _pushing flag to prevent feedback loops when the frontend
 *   pushes settings to core via pushSettingsToCore().
 */

// ─── Loop Prevention ────────────────────────────────────────────────────────

/**
 * Call before pushSettingsToCore() to suppress the echo event.
 * Retained as a no-op stub for call-site compatibility.
 */
export function markPushing(): void {
  // No-op — core-v2 SettingsManager has been removed
}

// ─── Sync Lifecycle ─────────────────────────────────────────────────────────

/**
 * Start listening for SettingsManager events and forward them to the window.
 * Returns a cleanup function.
 *
 * Note: With core-v2 removed, getCoreSettings() always returns null.
 * This is now a no-op stub retained for call-site compatibility.
 */
export function startSettingsSync(): () => void {
  return () => {}
}
