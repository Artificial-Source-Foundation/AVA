/**
 * Settings Storage
 *
 * File-based storage for settings using platform abstraction.
 * Settings are stored in ~/.ava/settings.json
 */

import { getPlatform } from '../platform.js'
import type { Settings } from './types.js'

// ============================================================================
// Constants
// ============================================================================

/** Settings directory path */
const SETTINGS_DIR = '~/.ava'

/** Settings file name */
const SETTINGS_FILE = 'settings.json'

// ============================================================================
// Path Resolution
// ============================================================================

/**
 * Expand ~ to home directory
 */
function expandPath(path: string): string {
  if (path.startsWith('~/')) {
    const home = process.env.HOME || process.env.USERPROFILE || ''
    return path.replace('~', home)
  }
  return path
}

/**
 * Get full path to settings file
 */
export function getSettingsPath(): string {
  return expandPath(`${SETTINGS_DIR}/${SETTINGS_FILE}`)
}

/**
 * Get settings directory path
 */
export function getSettingsDir(): string {
  return expandPath(SETTINGS_DIR)
}

// ============================================================================
// Storage Operations
// ============================================================================

/**
 * Ensure settings directory exists
 */
export async function ensureSettingsDir(): Promise<void> {
  const fs = getPlatform().fs
  const dir = getSettingsDir()

  if (!(await fs.exists(dir))) {
    await fs.mkdir(dir)
  }
}

/**
 * Load settings from file
 * Returns null if file doesn't exist
 */
export async function loadSettingsFromFile(): Promise<Partial<Settings> | null> {
  const fs = getPlatform().fs
  const path = getSettingsPath()

  try {
    if (!(await fs.exists(path))) {
      return null
    }

    const content = await fs.readFile(path)
    return JSON.parse(content) as Partial<Settings>
  } catch (err) {
    console.warn('Failed to load settings:', err)
    return null
  }
}

/**
 * Save settings to file
 */
export async function saveSettingsToFile(settings: Settings): Promise<void> {
  const fs = getPlatform().fs
  const path = getSettingsPath()

  await ensureSettingsDir()

  const content = JSON.stringify(settings, null, 2)
  await fs.writeFile(path, content)
}

/**
 * Delete settings file
 */
export async function deleteSettingsFile(): Promise<void> {
  const fs = getPlatform().fs
  const path = getSettingsPath()

  try {
    if (await fs.exists(path)) {
      await fs.remove(path)
    }
  } catch (err) {
    console.warn('Failed to delete settings file:', err)
  }
}
