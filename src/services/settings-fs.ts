/**
 * Settings Filesystem Persistence
 *
 * Reads/writes settings to $APPDATA/settings.json via Tauri FS plugin.
 * localStorage is kept as a fast sync layer for flash prevention;
 * Tauri FS is the reliable backend that survives cache clears.
 */

import { isTauri } from '@tauri-apps/api/core'
import { logWarn } from './logger'

const SETTINGS_FILE = 'settings.json'
let fsAvailable = false

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
  if (!isTauri()) return
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

/** Read settings JSON from Tauri FS. Returns null if unavailable. */
export async function readSettingsFromFS(): Promise<Record<string, unknown> | null> {
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

/** Write settings JSON to Tauri FS (fire-and-forget from caller). */
export async function writeSettingsToFS(data: Record<string, unknown>): Promise<void> {
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
