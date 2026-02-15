/**
 * Settings I/O
 * Import, export, and FS hydration logic.
 * Accepts signal accessors/setters as parameters to avoid owning the signal.
 */

import { STORAGE_KEYS } from '../../config/constants'
import type { AgentPreset } from '../../config/defaults/agent-defaults'
import { defaultAgentPresets } from '../../config/defaults/agent-defaults'
import type { LLMProviderConfig } from '../../config/defaults/provider-defaults'
import { defaultProviders } from '../../config/defaults/provider-defaults'
import { logWarn } from '../../services/logger'
import { readSettingsFromFS } from '../../services/settings-fs'
import { hydrateAgents, hydrateProviders, mergeWithDefaults } from './settings-hydration'
import { serializeSettings } from './settings-persistence'
import type { AppSettings } from './settings-types'

/** Export current settings as a JSON file (triggers download) */
export function exportSettingsToFile(current: AppSettings): void {
  const data = serializeSettings(current)
  const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `ava-settings-${new Date().toISOString().slice(0, 10)}.json`
  a.click()
  URL.revokeObjectURL(url)
}

/** Import settings from a JSON file.
 *  Calls `onImported` with the merged settings so the caller can set the signal. */
export async function importSettingsFromFile(
  onImported: (merged: AppSettings) => void
): Promise<void> {
  return new Promise((resolve) => {
    const input = document.createElement('input')
    input.type = 'file'
    input.accept = '.json'
    input.onchange = async () => {
      const file = input.files?.[0]
      if (!file) {
        resolve()
        return
      }
      try {
        const text = await file.text()
        const parsed = JSON.parse(text) as Partial<AppSettings>
        const merged = mergeWithDefaults(parsed)
        merged.providers = hydrateProviders(
          (merged.providers ?? defaultProviders) as LLMProviderConfig[]
        )
        merged.agents = hydrateAgents((merged.agents ?? defaultAgentPresets) as AgentPreset[])
        onImported(merged)
      } catch (err) {
        logWarn('settings', 'Import failed', err)
      }
      resolve()
    }
    input.click()
  })
}

/** Load settings from Tauri FS and merge with current state.
 *  Calls `onHydrated` with the merged settings so the caller can set the signal. */
export async function hydrateFromFS(
  current: AppSettings,
  onHydrated: (merged: AppSettings) => void
): Promise<void> {
  try {
    const fsData = await readSettingsFromFS()
    if (!fsData) return

    const parsed = fsData as Partial<AppSettings>
    const merged = mergeWithDefaults(parsed)
    merged.providers = hydrateProviders(
      (merged.providers ?? defaultProviders) as LLMProviderConfig[]
    )
    merged.agents = hydrateAgents((merged.agents ?? defaultAgentPresets) as AgentPreset[])

    const currentJson = JSON.stringify(serializeSettings(current))
    const fsJson = JSON.stringify(serializeSettings(merged))
    if (currentJson !== fsJson) {
      try {
        localStorage.setItem(STORAGE_KEYS.SETTINGS, fsJson)
        localStorage.setItem('ava-mode', merged.mode)
      } catch {
        /* ignore */
      }
      onHydrated(merged)
    }
  } catch (err) {
    logWarn('settings', 'FS hydration failed', err)
  }
}
