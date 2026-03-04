/**
 * Settings Store
 * Module-level SolidJS signals, mutators, and the useSettings() hook.
 * Re-exports all public types and functions so existing imports keep working.
 */

import { createRoot, createSignal } from 'solid-js'
import { type AgentPreset, resolveAgentIcon } from '../../config/defaults/agent-defaults'
import {
  defaultProviders,
  type LLMProviderConfig,
  type ProviderModel,
} from '../../config/defaults/provider-defaults'
import { logWarn } from '../../services/logger'
import { enrichWithCatalog, fetchModels } from '../../services/providers/model-fetcher'
import type { LLMProvider } from '../../types/llm'
import { applyAppearanceToDOM, isDarkMode as isDarkModeImpl } from './settings-appearance'
import { DEFAULT_SETTINGS } from './settings-defaults'
import { hydrateAgents, hydrateProviders } from './settings-hydration'
import { exportSettingsToFile, hydrateFromFS, importSettingsFromFile } from './settings-io'
import {
  detectEnvApiKeys as detectEnvApiKeysImpl,
  type EnvKeyDetectionResult,
  loadSettings,
  pushSettingsToCore as pushSettingsToCoreImpl,
  saveSettings,
  syncAllApiKeys as syncAllApiKeysImpl,
  syncProviderCredentials,
} from './settings-persistence'
import type { AppSettings, MCPServerConfig } from './settings-types'

export { resolveMode } from './settings-appearance'
export { syncProviderCredentials } from './settings-persistence'
// Re-exports — keep existing import paths stable
export type {
  AccentColor,
  AgentBackend,
  AgentLimitSettings,
  AppearanceSettings,
  AppSettings,
  BehaviorSettings,
  BorderRadius,
  CodeTheme,
  CustomMicroagent,
  CustomSkill,
  DarkStyle,
  GenerationSettings,
  GitSettings,
  MCPServerConfig,
  MonoFont,
  NotificationSettings,
  PermissionMode,
  SansFont,
  SendKey,
  ToolApprovalRule,
  UIDensity,
  UISettings,
} from './settings-types'

// Module-level signal — wrapped in createRoot to avoid "cleanups outside createRoot" warnings
const { settings, setSettingsRaw } = createRoot(() => {
  const initial = loadSettings()
  initial.providers = hydrateProviders(initial.providers)
  initial.agents = hydrateAgents(initial.agents)
  const [settings, setSettingsRaw] = createSignal<AppSettings>(initial)

  // Listen for settings changes from core-v2 extensions (bidirectional sync)
  window.addEventListener('ava:core-settings-changed', ((e: CustomEvent) => {
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
  }) as EventListener)

  return { settings, setSettingsRaw }
})

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
export function syncCredentialsToDisk(): void {
  import('@ava/platform-tauri')
    .then(({ TauriCredentialStore }) => {
      const store = new TauriCredentialStore()
      store.syncAllToDisk().catch(() => {})
    })
    .catch(() => {})
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
  if (patch.apiKey !== undefined) {
    syncProviderCredentials(id, patch.apiKey)
    // Auto-fetch models when an API key is set
    if (patch.apiKey) autoFetchModels(id)
  }
}

/** Get the effective API key for a provider (from config or OAuth storage) */
function getProviderCredential(id: string): string | undefined {
  const provider = settings().providers.find((p) => p.id === id)
  if (provider?.apiKey) return provider.apiKey

  // Check OAuth token in localStorage (same logic as providers-tab-helpers)
  try {
    const raw =
      localStorage.getItem('ava_credentials') || localStorage.getItem('estela_credentials')
    if (raw) {
      const all = JSON.parse(raw) as Record<string, { type?: string; value?: string }>
      if (all[id]?.type === 'oauth-token' && all[id]?.value) return all[id].value
    }
  } catch {
    // ignore
  }
  return undefined
}

/** Fetch models from provider API and merge with hardcoded defaults */
function autoFetchModels(id: string): void {
  const credential = getProviderCredential(id)
  if (!credential) return

  const provider = settings().providers.find((p) => p.id === id)
  fetchModels(id as LLMProvider, { apiKey: credential, baseUrl: provider?.baseUrl })
    .then((rawFetched) => {
      // Enrich with models.dev catalog (fills pricing, context windows, capabilities)
      const fetched = enrichWithCatalog(id as LLMProvider, rawFetched)
      if (fetched.length === 0) return
      const current = settings().providers.find((p) => p.id === id)

      // Build a map of hardcoded defaults for enrichment
      const defaults = defaultProviders.find((p) => p.id === id)
      const defaultMap = new Map<string, ProviderModel>()
      for (const m of defaults?.models ?? []) defaultMap.set(m.id, m)

      // Merge: fetched models enriched with hardcoded pricing/capabilities
      const fetchedMap = new Map<string, ProviderModel>()
      for (const m of fetched) {
        const def = defaultMap.get(m.id)
        const pricing = m.pricing
          ? { input: m.pricing.prompt, output: m.pricing.completion }
          : def?.pricing
        const capabilities = m.capabilities?.length ? m.capabilities : def?.capabilities
        fetchedMap.set(m.id, {
          id: m.id,
          name: m.name,
          contextWindow: m.contextWindow,
          ...(pricing && { pricing }),
          ...(capabilities?.length && { capabilities }),
        })
      }

      // Add hardcoded models not returned by the API (they may still be valid)
      for (const [defId, def] of defaultMap) {
        if (!fetchedMap.has(defId)) {
          fetchedMap.set(defId, def)
        }
      }

      // Convert to array, mark the first model or current default
      const models: ProviderModel[] = [...fetchedMap.values()]
      const keepDefault = current?.defaultModel && models.some((m) => m.id === current.defaultModel)
      const defaultModelId = keepDefault
        ? current.defaultModel
        : (defaults?.defaultModel ?? models[0]?.id)
      for (const m of models) m.isDefault = m.id === defaultModelId

      updateProvider(id, {
        models,
        defaultModel: defaultModelId,
        status: 'connected',
      })
    })
    .catch(() => {
      logWarn('settings', `Auto-fetch models failed for ${id}`)
    })
}

/**
 * Refresh models for all providers that have credentials (API key or OAuth).
 * Called on app startup so existing keys get fresh model lists + correct status.
 */
export function refreshAllProviderModels(): void {
  for (const provider of settings().providers) {
    if (getProviderCredential(provider.id)) {
      autoFetchModels(provider.id)
    }
  }
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

function updateAgentBackend(backend: AppSettings['agentBackend']): void {
  updateSettings({ agentBackend: backend })
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

// ─── Agent Import/Export ─────────────────────────────────────────────────────

function exportAgents(agentIds?: string[]): string {
  const agents = agentIds
    ? settings().agents.filter((a) => agentIds.includes(a.id))
    : settings().agents.filter((a) => a.isCustom)
  // Strip icon (Component) — not serializable
  const serializable = agents.map(({ icon: _icon, ...rest }) => rest)
  return JSON.stringify({ praxis_agents: serializable, version: 1 }, null, 2)
}

function importAgents(json: string): { imported: number; skipped: number } {
  const data = JSON.parse(json) as {
    praxis_agents?: Array<Omit<AgentPreset, 'icon'>>
    version?: number
  }
  if (!data.praxis_agents || !Array.isArray(data.praxis_agents)) {
    throw new Error('Invalid agent export format')
  }

  const existingIds = new Set(settings().agents.map((a) => a.id))
  let imported = 0
  let skipped = 0

  for (const raw of data.praxis_agents) {
    if (existingIds.has(raw.id)) {
      skipped++
      continue
    }
    const agent: AgentPreset = {
      ...raw,
      icon: resolveAgentIcon(undefined),
      isCustom: true,
      enabled: raw.enabled ?? true,
      capabilities: raw.capabilities ?? [],
    }
    addAgent(agent)
    imported++
  }

  return { imported, skipped }
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
  }
}
