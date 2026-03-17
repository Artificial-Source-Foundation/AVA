/**
 * Settings Filesystem Persistence
 *
 * Reads/writes settings to $APPDATA/settings.json via Tauri FS plugin.
 * In web mode (non-Tauri), falls back to a dedicated localStorage key
 * so that settings survive page reloads without Tauri FS.
 *
 * localStorage is also kept as a fast sync layer for flash prevention;
 * Tauri FS is the reliable backend that survives cache clears.
 */

import { isTauri } from '@tauri-apps/api/core'
import { logWarn } from './logger'

const SETTINGS_FILE = 'settings.json'
/** localStorage key used as the FS-layer fallback in web mode */
const WEB_FS_KEY = 'ava-settings-fs-v2'
let fsAvailable = false
let webMode = false

/** Lazy-load Tauri FS to avoid top-level import issues in non-Tauri env */
async function getFsModule() {
  if (!isTauri()) return null
  try {
    const fs = await import('@tauri-apps/plugin-fs')
    return fs
  } catch {
    return null
  }
}

/** Initialize FS backend — call once at startup */
export async function initSettingsFS(): Promise<void> {
  if (!isTauri()) {
    webMode = true
    return
  }
  try {
    const fs = await getFsModule()
    if (!fs) return
    // $APPDATA dir is guaranteed to exist by Tauri — no need to check/create it.
    // Just verify we can access the FS plugin.
    fsAvailable = true
  } catch (err) {
    logWarn('settings-fs', 'Failed to initialize', err)
  }
}

/** Read settings JSON from Tauri FS. Returns null if unavailable.
 *  In web mode, reads from a dedicated localStorage key. */
export async function readSettingsFromFS(): Promise<Record<string, unknown> | null> {
  // Web mode: use localStorage as the FS layer
  if (webMode) {
    try {
      const raw = localStorage.getItem(WEB_FS_KEY)
      if (!raw) return null
      return JSON.parse(raw) as Record<string, unknown>
    } catch {
      return null
    }
  }

  if (!fsAvailable) return null
  try {
    const fs = await getFsModule()
    if (!fs) return null
    const fileExists = await fs.exists(SETTINGS_FILE, { baseDir: fs.BaseDirectory.AppData })
    if (!fileExists) return null
    const text = await fs.readTextFile(SETTINGS_FILE, { baseDir: fs.BaseDirectory.AppData })
    return JSON.parse(text)
  } catch (err) {
    logWarn('settings-fs', 'Failed to read settings', err)
    return null
  }
}

/** Write settings JSON to Tauri FS (fire-and-forget from caller).
 *  In web mode, writes to a dedicated localStorage key. */
export async function writeSettingsToFS(data: Record<string, unknown>): Promise<void> {
  // Web mode: persist to localStorage
  if (webMode) {
    try {
      localStorage.setItem(WEB_FS_KEY, JSON.stringify(data))
    } catch {
      // localStorage full or unavailable — ignore
    }
    return
  }

  if (!fsAvailable) return
  try {
    const fs = await getFsModule()
    if (!fs) return
    await fs.writeTextFile(SETTINGS_FILE, JSON.stringify(data, null, 2), {
      baseDir: fs.BaseDirectory.AppData,
    })
  } catch (err) {
    logWarn('settings-fs', 'Failed to write settings', err)
  }
}
