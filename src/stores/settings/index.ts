/**
 * Settings Store
 * Module-level SolidJS signals, mutators, and the useSettings() hook.
 * Re-exports all public types and functions so existing imports keep working.
 */

import type { MCPServerConfig } from '@ava/core'
import { createSignal } from 'solid-js'
import type { AgentPreset } from '../../config/defaults/agent-defaults'
import type { LLMProviderConfig } from '../../config/defaults/provider-defaults'
import { applyAppearanceToDOM, isDarkMode as isDarkModeImpl } from './settings-appearance'
import { DEFAULT_SETTINGS } from './settings-defaults'
import { hydrateAgents, hydrateProviders } from './settings-hydration'
import { exportSettingsToFile, hydrateFromFS, importSettingsFromFile } from './settings-io'
import {
  detectEnvApiKeys as detectEnvApiKeysImpl,
  loadSettings,
  pushSettingsToCore as pushSettingsToCoreImpl,
  saveSettings,
  syncAllApiKeys as syncAllApiKeysImpl,
  syncProviderCredentials,
} from './settings-persistence'
import type { AppSettings } from './settings-types'

export { resolveMode } from './settings-appearance'
export { syncProviderCredentials } from './settings-persistence'
// Re-exports — keep existing import paths stable
export type {
  AccentColor,
  AgentLimitSettings,
  AppearanceSettings,
  AppSettings,
  BehaviorSettings,
  BorderRadius,
  CodeTheme,
  DarkStyle,
  GenerationSettings,
  GitSettings,
  MonoFont,
  NotificationSettings,
  PermissionMode,
  SansFont,
  SendKey,
  UIDensity,
  UISettings,
} from './settings-types'

// Module-level signal
const initial = loadSettings()
initial.providers = hydrateProviders(initial.providers)
initial.agents = hydrateAgents(initial.agents)
const [settings, setSettingsRaw] = createSignal<AppSettings>(initial)

// Thin wrappers — adapt pure functions to read/write the module signal

export function isDarkMode(): boolean {
  return isDarkModeImpl(settings())
}

export function applyAppearance(): void {
  applyAppearanceToDOM(settings())
}

export function pushSettingsToCore(): void {
  pushSettingsToCoreImpl(settings())
}

export function syncAllApiKeys(): void {
  syncAllApiKeysImpl(settings())
}

export async function detectEnvApiKeys(): Promise<number> {
  return detectEnvApiKeysImpl(settings().providers, (id, patch) => updateProvider(id, patch))
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
}

// Mutators — update + persist in one call

/** Keys of AppSettings whose values are plain objects (not arrays/primitives) */
type SubObjectKey =
  | 'ui'
  | 'appearance'
  | 'generation'
  | 'agentLimits'
  | 'behavior'
  | 'notifications'
  | 'git'

/** Generic sub-object updater: patches a nested key then persists */
function updateSubKey<K extends SubObjectKey>(key: K, patch: Partial<AppSettings[K]>): void {
  setSettingsRaw((prev) => {
    const next = { ...prev, [key]: { ...prev[key], ...patch } }
    saveSettings(next)
    return next
  })
}

function updateSettings(patch: Partial<AppSettings>): void {
  setSettingsRaw((prev) => {
    const next = { ...prev, ...patch }
    saveSettings(next)
    return next
  })
  if (patch.mode !== undefined) applyAppearance()
}

function updateProvider(id: string, patch: Partial<LLMProviderConfig>): void {
  setSettingsRaw((prev) => {
    const next = {
      ...prev,
      providers: prev.providers.map((p) => (p.id === id ? { ...p, ...patch } : p)),
    }
    saveSettings(next)
    return next
  })
  if (patch.apiKey !== undefined) syncProviderCredentials(id, patch.apiKey)
}

function updateAgent(id: string, patch: Partial<AgentPreset>): void {
  setSettingsRaw((prev) => {
    const next = {
      ...prev,
      agents: prev.agents.map((a) => (a.id === id ? { ...a, ...patch } : a)),
    }
    saveSettings(next)
    return next
  })
}

function addAgent(agent: AgentPreset): void {
  setSettingsRaw((prev) => {
    const next = { ...prev, agents: [...prev.agents, agent] }
    saveSettings(next)
    return next
  })
}

function removeAgent(id: string): void {
  setSettingsRaw((prev) => {
    const next = { ...prev, agents: prev.agents.filter((a) => a.id !== id) }
    saveSettings(next)
    return next
  })
}

function addAutoApprovedTool(toolName: string): void {
  setSettingsRaw((prev) => {
    if (prev.autoApprovedTools.includes(toolName)) return prev
    const next = { ...prev, autoApprovedTools: [...prev.autoApprovedTools, toolName] }
    saveSettings(next)
    return next
  })
}

function isToolAutoApproved(toolName: string): boolean {
  return settings().autoApprovedTools.includes(toolName)
}

function removeAutoApprovedTool(toolName: string): void {
  setSettingsRaw((prev) => {
    const next = {
      ...prev,
      autoApprovedTools: prev.autoApprovedTools.filter((t) => t !== toolName),
    }
    saveSettings(next)
    return next
  })
}

function cyclePermissionMode(): void {
  const modes: Array<'ask' | 'auto-approve' | 'bypass'> = ['ask', 'auto-approve', 'bypass']
  setSettingsRaw((prev) => {
    const idx = modes.indexOf(prev.permissionMode)
    const next = { ...prev, permissionMode: modes[(idx + 1) % modes.length] }
    saveSettings(next)
    return next
  })
}

function updateUI(patch: Partial<AppSettings['ui']>): void {
  updateSubKey('ui', patch)
}

function updateAppearance(patch: Partial<AppSettings['appearance']>): void {
  updateSubKey('appearance', patch)
  applyAppearance()
}

function updateGeneration(patch: Partial<AppSettings['generation']>): void {
  updateSubKey('generation', patch)
}

function updateAgentLimits(patch: Partial<AppSettings['agentLimits']>): void {
  updateSubKey('agentLimits', patch)
}

function updateBehavior(patch: Partial<AppSettings['behavior']>): void {
  updateSubKey('behavior', patch)
}

function updateNotifications(patch: Partial<AppSettings['notifications']>): void {
  updateSubKey('notifications', patch)
}

function updateGit(patch: Partial<AppSettings['git']>): void {
  updateSubKey('git', patch)
}

function resetSettings(): void {
  const fresh = { ...DEFAULT_SETTINGS }
  setSettingsRaw(fresh)
  saveSettings(fresh)
}

// MCP Server CRUD

function addMcpServer(config: MCPServerConfig): void {
  const current = settings()
  if (current.mcpServers.find((s) => s.name === config.name)) return
  updateSettings({ mcpServers: [...current.mcpServers, config] })
}

function removeMcpServer(name: string): void {
  updateSettings({ mcpServers: settings().mcpServers.filter((s) => s.name !== name) })
}

function updateMcpServer(name: string, updates: Partial<MCPServerConfig>): void {
  updateSettings({
    mcpServers: settings().mcpServers.map((s) => (s.name === name ? { ...s, ...updates } : s)),
  })
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
    addMcpServer,
    removeMcpServer,
    updateMcpServer,
  }
}
