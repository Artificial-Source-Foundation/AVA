/**
 * Platform Initialization for Tauri
 * Wires up @ava/platform-tauri with @ava/core
 */

import { setPlatform } from '@ava/core'
import { createTauriPlatform } from '@ava/platform-tauri'
import { logInfo } from './logger'

let initialized = false

/**
 * Initialize the platform provider for Tauri
 * Must be called before using any @ava/core features
 */
export function initializePlatform(): void {
  if (initialized) return

  // Create Tauri platform with default database path
  // Note: The actual database is managed separately by src/services/database.ts
  // This is for the credential store and other platform services
  const platform = createTauriPlatform('ava.db')

  setPlatform(platform)
  initialized = true

  logInfo('platform', 'Tauri platform initialized')
}

/**
 * Check if platform is initialized
 */
export function isPlatformInitialized(): boolean {
  return initialized
}
