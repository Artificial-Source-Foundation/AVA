/**
 * Settings Store
 * Module-level SolidJS signals, mutators, and the useSettings() hook.
 * Re-exports all public types and functions so existing imports keep working.
 */

import { createRoot, createSignal } from 'solid-js'
import { applyAppearanceToDOM, isDarkMode as isDarkModeImpl } from './settings-appearance'
import { DEFAULT_SETTINGS } from './settings-defaults'
import { exportSettingsToFile, hydrateFromFS, importSettingsFromFile } from './settings-io'
import {
  addAgent,
  addAutoApprovedTool,
  addMcpServer,
  cyclePermissionMode,
  exportAgents,
  importAgents,
  isToolAutoApproved,
  removeAgent,
  removeAutoApprovedTool,
  removeMcpServer,
  updateAgent,
  updateAgentBackend,
  updateAgentLimits,
  updateAppearance,
  updateBehavior,
  updateGeneration,
  updateGit,
  updateMcpServer,
  updateNotifications,
  updateProvider,
  updateTeam,
  updateUI,
} from './settings-mutators'
import {
  detectEnvApiKeys as detectEnvApiKeysImpl,
  type EnvKeyDetectionResult,
  loadSharedSettingsFromCore as loadSharedFromCoreImpl,
  pushSettingsToCore as pushSettingsToCoreImpl,
  saveSettings,
  syncAllApiKeys as syncAllApiKeysImpl,
} from './settings-persistence'
import { setSettingsRaw, settings, updateSettings } from './settings-signal'
import type { AppSettings } from './settings-types'

export { resolveMode } from './settings-appearance'
export { populateModelsFromCatalog, refreshAllProviderModels } from './settings-mutators'
export { syncProviderCredentials } from './settings-persistence'
// Re-exports — keep existing import paths stable
export type {
  AccentColor,
  AgentBackend,
  AgentLimitSettings,
  AppearanceSettings,
  AppLogLevel,
  AppSettings,
  BehaviorSettings,
  BorderRadius,
  CodeTheme,
  CustomMicroagent,
  CustomSkill,
  DarkStyle,
  FontSize,
  GenerationSettings,
  GitSettings,
  LeadConfig,
  MCPServerConfig,
  MonoFont,
  NotificationSettings,
  PermissionMode,
  SansFont,
  SendKey,
  TeamConfig,
  ToolApprovalRule,
  ToolResponseStyle,
  UIDensity,
  UISettings,
} from './settings-types'

// Thin wrappers — adapt pure functions to read/write the module signal

export function isDarkMode(): boolean {
  return isDarkModeImpl(settings())
}

export function applyAppearance(): void {
  applyAppearanceToDOM(settings())
}

/** Preview appearance changes without persisting. Call restoreAppearance() to revert. */
export function previewAppearance(patch: Partial<AppSettings['appearance']>): void {
  const preview = { ...settings(), appearance: { ...settings().appearance, ...patch } }
  applyAppearanceToDOM(preview)
}

/** Restore persisted appearance after a preview. */
export function restoreAppearance(): void {
  applyAppearanceToDOM(settings())
}

export function pushSettingsToCore(): void {
  pushSettingsToCoreImpl(settings())
}

export function syncAllApiKeys(): void {
  syncAllApiKeysImpl(settings())
}

/**
 * Bulk-sync all localStorage credentials to ~/.ava/credentials.json.
 * Call after platform is initialized to share credentials with the CLI.
 */
/**
 * Bulk-sync all localStorage credentials to ~/.ava/credentials.json.
 * Now handled by the Rust backend — this is a no-op stub.
 */
export function syncCredentialsToDisk(): void {
  // No-op — credential sync is now handled by the Rust backend
}

// Signal to track env key detection results for the toast notification
const { envKeysDetected, setEnvKeysDetected } = createRoot(() => {
  const [envKeysDetected, setEnvKeysDetected] = createSignal<EnvKeyDetectionResult | null>(null)
  return { envKeysDetected, setEnvKeysDetected }
})
export { envKeysDetected }

export async function detectEnvApiKeys(): Promise<EnvKeyDetectionResult> {
  const result = await detectEnvApiKeysImpl(settings().providers, (id, patch) =>
    updateProvider(id, patch)
  )
  if (result.count > 0) setEnvKeysDetected(result)
  return result
}

export function setupSystemThemeListener(): () => void {
  const mq = window.matchMedia('(prefers-color-scheme: dark)')
  const handler = () => {
    if (settings().mode === 'system') applyAppearance()
  }
  mq.addEventListener('change', handler)
  return () => mq.removeEventListener('change', handler)
}

export async function hydrateSettingsFromFS(): Promise<void> {
  await hydrateFromFS(settings(), (merged) => {
    setSettingsRaw(merged)
    applyAppearance()
  })

  // After FS hydration, also pull shared settings from config.yaml so the
  // Desktop reflects the same provider/model/features the TUI last used.
  const patch = await loadSharedFromCoreImpl()
  if (!patch) return

  setSettingsRaw((prev) => {
    const next = { ...prev }
    // Deep-merge generation sub-object so we don't clobber other generation fields
    if (patch.generation) {
      next.generation = { ...prev.generation, ...patch.generation }
    }
    // Deep-merge git sub-object
    if (patch.git) {
      next.git = { ...prev.git, ...patch.git }
    }
    saveSettings(next)
    return next
  })
}

function resetSettings(): void {
  const fresh = { ...DEFAULT_SETTINGS }
  setSettingsRaw(fresh)
  saveSettings(fresh)
}

// Export Hook

export function useSettings() {
  return {
    settings,
    updateSettings,
    updateProvider,
    updateAgent,
    addAgent,
    removeAgent,
    addAutoApprovedTool,
    removeAutoApprovedTool,
    isToolAutoApproved,
    updateUI,
    updateAppearance,
    previewAppearance,
    restoreAppearance,
    updateGeneration,
    updateAgentLimits,
    updateBehavior,
    updateNotifications,
    updateGit,
    cyclePermissionMode,
    resetSettings,
    exportSettings: () => exportSettingsToFile(settings()),
    importSettings: () =>
      importSettingsFromFile((merged) => {
        setSettingsRaw(merged)
        saveSettings(merged)
        applyAppearance()
      }),
    exportAgents,
    importAgents,
    addMcpServer,
    removeMcpServer,
    updateMcpServer,
    updateAgentBackend,
    updateTeam,
  }
}
