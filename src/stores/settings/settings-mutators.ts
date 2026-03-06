/**
 * Settings Mutators
 * Provider, agent, permission, and MCP mutation functions.
 * Reads/writes the shared settings signal from settings-signal.ts.
 */

import { type AgentPreset, resolveAgentIcon } from '../../config/defaults/agent-defaults'
import {
  defaultProviders,
  type LLMProviderConfig,
  type ProviderModel,
} from '../../config/defaults/provider-defaults'
import { logWarn } from '../../services/logger'
import { enrichWithCatalog, fetchModels } from '../../services/providers/model-fetcher'
import type { LLMProvider } from '../../types/llm'
import { applyAppearanceToDOM } from './settings-appearance'
import { saveSettings, syncProviderCredentials } from './settings-persistence'
import { setSettingsRaw, settings, updateSettings, updateSubKey } from './settings-signal'
import type { AppSettings, MCPServerConfig } from './settings-types'

// ── Provider ─────────────────────────────────────────────────────────────────

export function updateProvider(id: string, patch: Partial<LLMProviderConfig>): void {
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
export function getProviderCredential(id: string): string | undefined {
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
export function autoFetchModels(id: string): void {
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

// ── Agents ───────────────────────────────────────────────────────────────────

export function updateAgent(id: string, patch: Partial<AgentPreset>): void {
  setSettingsRaw((prev) => {
    const next = {
      ...prev,
      agents: prev.agents.map((a) => (a.id === id ? { ...a, ...patch } : a)),
    }
    saveSettings(next)
    return next
  })
}

export function addAgent(agent: AgentPreset): void {
  setSettingsRaw((prev) => {
    const next = { ...prev, agents: [...prev.agents, agent] }
    saveSettings(next)
    return next
  })
}

export function removeAgent(id: string): void {
  setSettingsRaw((prev) => {
    const next = { ...prev, agents: prev.agents.filter((a) => a.id !== id) }
    saveSettings(next)
    return next
  })
}

// ── Permissions ──────────────────────────────────────────────────────────────

export function addAutoApprovedTool(toolName: string): void {
  setSettingsRaw((prev) => {
    if (prev.autoApprovedTools.includes(toolName)) return prev
    const next = { ...prev, autoApprovedTools: [...prev.autoApprovedTools, toolName] }
    saveSettings(next)
    return next
  })
}

export function isToolAutoApproved(toolName: string): boolean {
  return settings().autoApprovedTools.includes(toolName)
}

export function removeAutoApprovedTool(toolName: string): void {
  setSettingsRaw((prev) => {
    const next = {
      ...prev,
      autoApprovedTools: prev.autoApprovedTools.filter((t) => t !== toolName),
    }
    saveSettings(next)
    return next
  })
}

export function cyclePermissionMode(): void {
  const modes: Array<'ask' | 'auto-approve' | 'bypass'> = ['ask', 'auto-approve', 'bypass']
  setSettingsRaw((prev) => {
    const idx = modes.indexOf(prev.permissionMode)
    const next = { ...prev, permissionMode: modes[(idx + 1) % modes.length] }
    saveSettings(next)
    return next
  })
}

// ── MCP Server CRUD ──────────────────────────────────────────────────────────

export function addMcpServer(config: MCPServerConfig): void {
  const current = settings()
  if (current.mcpServers.find((s) => s.name === config.name)) return
  updateSettings({ mcpServers: [...current.mcpServers, config] })
}

export function removeMcpServer(name: string): void {
  updateSettings({ mcpServers: settings().mcpServers.filter((s) => s.name !== name) })
}

export function updateMcpServer(name: string, updates: Partial<MCPServerConfig>): void {
  updateSettings({
    mcpServers: settings().mcpServers.map((s) => (s.name === name ? { ...s, ...updates } : s)),
  })
}

// ── Agent Import/Export ──────────────────────────────────────────────────────

export function exportAgents(agentIds?: string[]): string {
  const agents = agentIds
    ? settings().agents.filter((a) => agentIds.includes(a.id))
    : settings().agents.filter((a) => a.isCustom)
  // Strip icon (Component) — not serializable
  const serializable = agents.map(({ icon: _icon, ...rest }) => rest)
  return JSON.stringify({ praxis_agents: serializable, version: 1 }, null, 2)
}

export function importAgents(json: string): { imported: number; skipped: number } {
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

// ── Sub-key Updaters ─────────────────────────────────────────────────────────

export function updateUI(patch: Partial<AppSettings['ui']>): void {
  updateSubKey('ui', patch)
}

export function updateAppearance(patch: Partial<AppSettings['appearance']>): void {
  updateSubKey('appearance', patch)
  applyAppearanceToDOM(settings())
}

export function updateGeneration(patch: Partial<AppSettings['generation']>): void {
  updateSubKey('generation', patch)
}

export function updateAgentLimits(patch: Partial<AppSettings['agentLimits']>): void {
  updateSubKey('agentLimits', patch)
}

export function updateBehavior(patch: Partial<AppSettings['behavior']>): void {
  updateSubKey('behavior', patch)
}

export function updateNotifications(patch: Partial<AppSettings['notifications']>): void {
  updateSubKey('notifications', patch)
}

export function updateGit(patch: Partial<AppSettings['git']>): void {
  updateSubKey('git', patch)
}

export function updateAgentBackend(backend: AppSettings['agentBackend']): void {
  updateSettings({ agentBackend: backend })
}
