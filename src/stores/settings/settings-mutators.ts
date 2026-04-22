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
import { log } from '../../lib/logger'
import { isOAuthSupported } from '../../services/auth/oauth-config'
import { logInfo, logWarn } from '../../services/logger'
import { getModelsDevModels } from '../../services/providers/curated-model-catalog'
import { enrichWithCatalog, fetchModels } from '../../services/providers/model-fetcher'
import type { Credentials, LLMProvider } from '../../types/llm'
import { applyAppearanceToDOM } from './settings-appearance'
import { syncProviderCredentials } from './settings-persistence'
import { commitSettings, settings, updateSettings, updateSubKey } from './settings-signal'
import type { AppSettings, MCPServerConfig } from './settings-types'

// ── Provider ─────────────────────────────────────────────────────────────────

const AVA_CREDENTIALS_KEY = 'ava_credentials'
const LEGACY_CREDENTIALS_KEY = 'estela_credentials'
const AVA_CREDENTIAL_PREFIX = 'ava_cred_'
const LEGACY_CREDENTIAL_PREFIX = 'estela_cred_'

function clearStoredOAuthCredentials(id: string): void {
  try {
    const raw =
      localStorage.getItem(AVA_CREDENTIALS_KEY) || localStorage.getItem(LEGACY_CREDENTIALS_KEY)
    if (raw) {
      const all = JSON.parse(raw) as Record<string, { type?: string }>
      if (all[id]?.type === 'oauth-token') {
        delete all[id]
        const serialized = JSON.stringify(all)
        localStorage.setItem(AVA_CREDENTIALS_KEY, serialized)
        localStorage.setItem(LEGACY_CREDENTIALS_KEY, serialized)
      }
    }
  } catch {
    // Ignore malformed credential cache
  }

  localStorage.removeItem(`${AVA_CREDENTIAL_PREFIX}ava:${id}:oauth_token`)
  localStorage.removeItem(`${AVA_CREDENTIAL_PREFIX}ava:${id}:account_id`)
  localStorage.removeItem(`${AVA_CREDENTIAL_PREFIX}auth-${id}`)
  localStorage.removeItem(`${LEGACY_CREDENTIAL_PREFIX}auth-${id}`)
}

export function updateProvider(id: string, patch: Partial<LLMProviderConfig>): void {
  if (patch.status)
    log.info(
      'settings',
      `Provider ${patch.status === 'connected' ? 'connected' : 'disconnected'}`,
      { provider: id }
    )
  commitSettings((prev) => {
    const next = {
      ...prev,
      providers: prev.providers.map((p) => (p.id === id ? { ...p, ...patch } : p)),
    }
    return next
  })
  if ('apiKey' in patch) {
    if (patch.apiKey) {
      clearStoredOAuthCredentials(id)
    }
    syncProviderCredentials(id, patch.apiKey)
    // Auto-fetch models when an API key is set
    if (patch.apiKey) autoFetchModels(id)
  }
}

/** Get the effective credential metadata for a provider (from config or OAuth storage). */
export interface ProviderCredentialInfo {
  value: string
  type: Credentials['type']
}

function getStoredOAuthCredentialInfo(id: string): ProviderCredentialInfo | undefined {
  const cachedOAuthToken = localStorage.getItem(`ava_cred_ava:${id}:oauth_token`)
  if (cachedOAuthToken) {
    return {
      value: cachedOAuthToken,
      type: 'oauth-token',
    }
  }

  // Check OAuth token in localStorage (same logic as providers-tab-helpers)
  try {
    const raw =
      localStorage.getItem('ava_credentials') || localStorage.getItem('estela_credentials')
    if (raw) {
      const all = JSON.parse(raw) as Record<string, { type?: string; value?: string }>
      if (all[id]?.type === 'oauth-token' && all[id]?.value) {
        return {
          value: all[id].value,
          type: 'oauth-token',
        }
      }
    }
  } catch {
    // ignore
  }

  // Legacy key format (best-effort fallback for migration paths)
  try {
    const legacyCoreAuth =
      localStorage.getItem(`${AVA_CREDENTIAL_PREFIX}auth-${id}`) ||
      localStorage.getItem(`${LEGACY_CREDENTIAL_PREFIX}auth-${id}`)
    if (legacyCoreAuth) {
      const parsed = JSON.parse(legacyCoreAuth) as {
        type?: string
        accessToken?: string
        value?: string
      }
      if (parsed.type === 'oauth' && (parsed.accessToken || parsed.value)) {
        return {
          value: parsed.accessToken || parsed.value || '',
          type: 'oauth-token',
        }
      }
    }
  } catch {
    // Ignore malformed legacy credentials
  }

  return undefined
}

function getConfiguredApiKeyCredentialInfo(
  id: string,
  provider: LLMProviderConfig | undefined
): ProviderCredentialInfo | undefined {
  if (!provider?.apiKey) return undefined

  const cachedApiKey = localStorage.getItem(`ava_cred_ava:${id}:api_key`)
  const isPersistedApiKey = cachedApiKey === provider.apiKey
  const isHydratedConfiguredProvider = provider.enabled || provider.status === 'connected'

  if (!isPersistedApiKey && !isHydratedConfiguredProvider) return undefined

  return {
    value: provider.apiKey,
    type: 'api-key',
  }
}

/** Get the effective credential for a provider (from config or OAuth storage). */
export function getProviderCredentialInfo(id: string): ProviderCredentialInfo | undefined {
  const provider = settings().providers.find((p) => p.id === id)
  const configuredApiKey = getConfiguredApiKeyCredentialInfo(id, provider)
  const storedOAuthCredential = getStoredOAuthCredentialInfo(id)

  if (configuredApiKey) {
    return configuredApiKey
  }

  if (isOAuthSupported(id as LLMProvider) && storedOAuthCredential) {
    return storedOAuthCredential
  }

  if (provider?.apiKey) {
    return {
      value: provider.apiKey,
      type: 'api-key',
    }
  }

  return storedOAuthCredential
}

/** Get the effective credential value for a provider (from config or OAuth storage). */
export function getProviderCredential(id: string): string | undefined {
  return getProviderCredentialInfo(id)?.value
}

/** Fetch models from provider API and merge with hardcoded defaults */
export function autoFetchModels(id: string): void {
  const credential = getProviderCredentialInfo(id)
  if (!credential) return

  const provider = settings().providers.find((p) => p.id === id)
  fetchModels(id as LLMProvider, {
    apiKey: credential.value,
    authType: credential.type,
    baseUrl: provider?.baseUrl,
  })
    .then((rawFetched) => {
      // Enrich with the backend-owned curated catalog (fills pricing, context windows, capabilities)
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
        const capabilities = [...new Set([...(m.capabilities ?? []), ...(def?.capabilities ?? [])])]
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
      const preferredDefaultModelId = keepDefault ? current.defaultModel : defaults?.defaultModel
      const defaultModelId =
        preferredDefaultModelId && models.some((m) => m.id === preferredDefaultModelId)
          ? preferredDefaultModelId
          : models[0]?.id
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
  // CLI agents are discovered at startup via AgentStack for subagent delegation.
  // They are not shown in the provider/model selector UI.
}

/**
 * Discover installed CLI agents from the backend and update the cli-agents provider.
 * CLI agents don't need API keys — they're detected by checking if their binary is on PATH.
 */
export async function refreshCLIAgents(): Promise<void> {
  try {
    const { apiInvoke } = await import('../../lib/api-client')
    const agents =
      await apiInvoke<Array<{ name: string; binary: string; version: string; installed: boolean }>>(
        'discover_cli_agents'
      )

    if (!agents || agents.length === 0) {
      updateProvider('cli-agents', { status: 'disconnected', models: [] })
      return
    }

    const models: ProviderModel[] = agents.map(
      (a: { name: string; binary: string; version: string; installed: boolean }) => ({
        id: a.name,
        name: `${a.name
          .split('-')
          .map((w: string) => w.charAt(0).toUpperCase() + w.slice(1))
          .join(' ')} (CLI)`,
        contextWindow: 200000,
        capabilities: ['tools'],
      })
    )

    updateProvider('cli-agents', {
      status: 'connected',
      models,
    })
    log.info(
      'settings',
      `Discovered ${agents.length} CLI agents: ${agents.map((a: { name: string; version: string }) => `${a.name} v${a.version}`).join(', ')}`
    )
  } catch {
    // Backend may not support this endpoint yet — keep static fallbacks
  }
}

/**
 * Populate provider model lists from the backend-owned curated catalog.
 * Called after syncModelsCatalog() on startup. For each provider, merges
 * catalog models into the existing list (adds missing, doesn't remove existing).
 * Returns the number of providers that received new models.
 */
export function populateModelsFromCatalog(): number {
  const currentProviders = settings().providers
  let enrichedCount = 0

  const updatedProviders = currentProviders.map((p) => {
    const catalogModels = getModelsDevModels(p.id as LLMProvider)
    if (catalogModels.length === 0) return p

    // Build set of existing model IDs
    const existingIds = new Set(p.models.map((m) => m.id))
    const newModels = catalogModels.filter((m) => !existingIds.has(m.id))
    let enrichedExistingChanged = false

    // Also enrich existing models that lack capabilities or pricing
    const enrichedExisting = p.models.map((existing) => {
      const catalogMatch = catalogModels.find((cm) => cm.id === existing.id)
      if (!catalogMatch) return existing
      const patched = { ...existing }
      if (catalogMatch.capabilities?.length) {
        const merged = new Set([...(patched.capabilities ?? []), ...catalogMatch.capabilities])
        const mergedCapabilities = [...merged]
        if ((patched.capabilities ?? []).join('|') !== mergedCapabilities.join('|')) {
          patched.capabilities = mergedCapabilities
          enrichedExistingChanged = true
        }
      }
      if (!patched.pricing && catalogMatch.pricing) {
        patched.pricing = catalogMatch.pricing
        enrichedExistingChanged = true
      }
      if (patched.contextWindow <= 4096 && catalogMatch.contextWindow > 4096) {
        patched.contextWindow = catalogMatch.contextWindow
        enrichedExistingChanged = true
      }
      return patched
    })

    if (newModels.length === 0 && !enrichedExistingChanged) return p

    enrichedCount++
    const mergedModels = [...enrichedExisting, ...newModels]

    // Preserve default model selection
    const keepDefault = p.defaultModel && mergedModels.some((m) => m.id === p.defaultModel)
    const defaults = defaultProviders.find((dp) => dp.id === p.id)
    const defaultModelId = keepDefault
      ? p.defaultModel
      : (defaults?.defaultModel ?? mergedModels[0]?.id)
    for (const m of mergedModels) m.isDefault = m.id === defaultModelId

    return { ...p, models: mergedModels, defaultModel: defaultModelId }
  })

  if (enrichedCount > 0) {
    updateSettings({ providers: updatedProviders })
    logInfo('settings', `Populated models from curated catalog for ${enrichedCount} providers`)
  }

  return enrichedCount
}

// ── Agents ───────────────────────────────────────────────────────────────────

export function updateAgent(id: string, patch: Partial<AgentPreset>): void {
  commitSettings((prev) => {
    const next = {
      ...prev,
      agents: prev.agents.map((a) => (a.id === id ? { ...a, ...patch } : a)),
    }
    return next
  })
}

export function addAgent(agent: AgentPreset): void {
  commitSettings((prev) => {
    const next = { ...prev, agents: [...prev.agents, agent] }
    return next
  })
}

export function removeAgent(id: string): void {
  commitSettings((prev) => {
    const next = { ...prev, agents: prev.agents.filter((a) => a.id !== id) }
    return next
  })
}

// ── Permissions ──────────────────────────────────────────────────────────────

export function addAutoApprovedTool(toolName: string): void {
  commitSettings((prev) => {
    if (prev.autoApprovedTools.includes(toolName)) return prev
    const next = { ...prev, autoApprovedTools: [...prev.autoApprovedTools, toolName] }
    return next
  })
}

export function isToolAutoApproved(toolName: string): boolean {
  return settings().autoApprovedTools.includes(toolName)
}

export function removeAutoApprovedTool(toolName: string): void {
  commitSettings((prev) => {
    const next = {
      ...prev,
      autoApprovedTools: prev.autoApprovedTools.filter((t) => t !== toolName),
    }
    return next
  })
}

export function cyclePermissionMode(): void {
  const modes: Array<'ask' | 'auto-approve'> = ['ask', 'auto-approve']
  commitSettings((prev) => {
    const idx = modes.indexOf(prev.permissionMode)
    const nextMode = idx >= 0 ? modes[(idx + 1) % modes.length] : modes[0]
    const next = { ...prev, permissionMode: nextMode }
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
  return JSON.stringify({ agents: serializable, version: 2 }, null, 2)
}

export function importAgents(json: string): { imported: number; skipped: number } {
  const data = JSON.parse(json) as {
    agents?: Array<Omit<AgentPreset, 'icon'>>
    hq_agents?: Array<Omit<AgentPreset, 'icon'>>
    version?: number
  }
  const importedAgents = data.agents ?? data.hq_agents
  if (!importedAgents || !Array.isArray(importedAgents)) {
    throw new Error('Invalid agent export format')
  }

  const existingIds = new Set(settings().agents.map((a) => a.id))
  let imported = 0
  let skipped = 0

  for (const raw of importedAgents) {
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
  if (patch.darkStyle) log.info('settings', 'Appearance changed', { darkStyle: patch.darkStyle })
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
