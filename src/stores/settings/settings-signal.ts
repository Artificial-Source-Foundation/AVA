/**
 * Settings Signal
 * Module-level SolidJS signal for AppSettings, plus base update helpers.
 * Shared by index.ts and settings-mutators.ts to avoid circular dependencies.
 */

import { createRoot, createSignal } from 'solid-js'
import { debugLog } from '../../lib/debug-log'
import { log } from '../../lib/logger'
import { installReplaceableWindowListener } from '../../lib/replaceable-window-listener'
import { applyAppearanceToDOM } from './settings-appearance'
import { hydrateAgents, hydrateProviders } from './settings-hydration'
import { loadSettings, saveSettings } from './settings-persistence'
import type { AppSettings } from './settings-types'

// Module-level signal — wrapped in createRoot to avoid "cleanups outside createRoot" warnings
const { settings, setSettingsRaw } = createRoot(() => {
  const initial = loadSettings()
  initial.providers = hydrateProviders(initial.providers)
  initial.agents = hydrateAgents(initial.agents)
  const [settings, setSettingsRaw] = createSignal<AppSettings>(initial)

  // Listen for settings changes from core-v2 extensions (bidirectional sync)
  installReplaceableWindowListener('settings-signal:core-settings', (target) => {
    const listener = ((e: CustomEvent) => {
      const { category, value } = e.detail as { category: string; value: unknown }
      if (!value || typeof value !== 'object') return

      const patch = value as Record<string, unknown>
      setSettingsRaw((prev) => {
        // Map known core categories back to AppSettings fields
        if (category === 'permissions') {
          return {
            ...prev,
            autoApprovedTools: (patch.autoApprovePatterns as string[]) ?? prev.autoApprovedTools,
          }
        }
        if (category === 'context') {
          return {
            ...prev,
            generation: {
              ...prev.generation,
              maxTokens: (patch.maxTokens as number) ?? prev.generation.maxTokens,
              autoCompact: (patch.autoCompact as boolean) ?? prev.generation.autoCompact,
              compactionThreshold:
                (patch.compactionThreshold as number) ?? prev.generation.compactionThreshold,
            },
          }
        }
        if (category === 'git') {
          return {
            ...prev,
            git: {
              ...prev.git,
              enabled: (patch.enabled as boolean) ?? prev.git.enabled,
              autoCommit: (patch.autoCommit as boolean) ?? prev.git.autoCommit,
              commitPrefix: (patch.messagePrefix as string) ?? prev.git.commitPrefix,
            },
          }
        }
        // Unknown categories (extension-specific) — ignore
        return prev
      })
    }) as EventListener

    target.addEventListener('ava:core-settings-changed', listener)
    return () => target.removeEventListener('ava:core-settings-changed', listener)
  })

  return { settings, setSettingsRaw }
})

export { settings, setSettingsRaw }

/** Keys of AppSettings whose values are plain objects (not arrays/primitives) */
export type SubObjectKey =
  | 'ui'
  | 'appearance'
  | 'generation'
  | 'agentLimits'
  | 'behavior'
  | 'notifications'
  | 'git'
  | 'team'

/** Generic sub-object updater: patches a nested key then persists */
export function updateSubKey<K extends SubObjectKey>(key: K, patch: Partial<AppSettings[K]>): void {
  debugLog('settings', `updateSubKey(${key})`, patch)
  log.info('settings', `Setting changed: ${key}`, { keys: Object.keys(patch) })
  setSettingsRaw((prev) => {
    const next = { ...prev, [key]: { ...prev[key], ...patch } }
    saveSettings(next)
    return next
  })
}

export function updateSettings(patch: Partial<AppSettings>): void {
  debugLog('settings', 'updateSettings', Object.keys(patch))
  log.info('settings', 'Settings updated', { keys: Object.keys(patch) })
  if (patch.theme !== undefined) log.info('settings', 'Theme changed', { theme: patch.theme })
  setSettingsRaw((prev) => {
    const next = { ...prev, ...patch }
    saveSettings(next)
    return next
  })
  if (patch.mode !== undefined) applyAppearanceToDOM(settings())
}
