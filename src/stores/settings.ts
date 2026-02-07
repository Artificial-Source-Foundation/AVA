/**
 * Settings Store
 * Persistent settings with localStorage backup.
 * Follows the same pattern as layout.ts — module-level signals + export hook.
 */

import { createSignal } from 'solid-js'
import type { AgentPreset } from '../components/settings/tabs/AgentsTab'
import { defaultAgentPresets } from '../components/settings/tabs/AgentsTab'
import type { LLMProviderConfig } from '../components/settings/tabs/ProvidersTab'
import { defaultProviders } from '../components/settings/tabs/ProvidersTab'
import { STORAGE_KEYS } from '../config/constants'

// ============================================================================
// Credential Sync — bridges Settings UI → Core credential store
// ============================================================================

const CREDENTIAL_PREFIX = 'estela_cred_'

/** Maps provider IDs to the credential key names that core reads via TauriCredentialStore */
const PROVIDER_KEY_MAP: Record<string, string> = {
  anthropic: 'anthropic-api-key',
  openrouter: 'openrouter-api-key',
  openai: 'openai-api-key',
  google: 'google-api-key',
  copilot: 'copilot-api-key',
  glm: 'glm-api-key',
  kimi: 'kimi-api-key',
  mistral: 'mistral-api-key',
  groq: 'groq-api-key',
  deepseek: 'deepseek-api-key',
  xai: 'xai-api-key',
  cohere: 'cohere-api-key',
  together: 'together-api-key',
  ollama: 'ollama-api-key',
}

/** Write a single provider's API key to the core credential store */
function syncProviderCredentials(providerId: string, apiKey: string | undefined) {
  const credKey = PROVIDER_KEY_MAP[providerId]
  if (!credKey) return
  const storageKey = CREDENTIAL_PREFIX + credKey
  if (apiKey) {
    localStorage.setItem(storageKey, apiKey)
  } else {
    localStorage.removeItem(storageKey)
  }
}

/** Hydrate all provider API keys from settings into the core credential store (call at startup) */
export function syncAllApiKeys() {
  const current = loadSettings()
  for (const provider of current.providers) {
    if (provider.apiKey) {
      syncProviderCredentials(provider.id, provider.apiKey)
    }
  }
}

// ============================================================================
// Types
// ============================================================================

export interface AppSettings {
  onboardingComplete: boolean
  theme: string
  mode: 'light' | 'dark'
  providers: LLMProviderConfig[]
  agents: AgentPreset[]
  autoApprovedTools: string[]
}

const DEFAULT_SETTINGS: AppSettings = {
  onboardingComplete: false,
  theme: 'glass',
  mode: 'dark',
  providers: defaultProviders,
  agents: defaultAgentPresets,
  autoApprovedTools: [],
}

// ============================================================================
// Persistence
// ============================================================================

function loadSettings(): AppSettings {
  try {
    const raw = localStorage.getItem(STORAGE_KEYS.SETTINGS)
    if (raw) {
      const parsed = JSON.parse(raw) as Partial<AppSettings>
      // Merge with defaults so new keys are always present
      return { ...DEFAULT_SETTINGS, ...parsed }
    }
  } catch {
    /* corrupted data — use defaults */
  }
  return { ...DEFAULT_SETTINGS }
}

function saveSettings(settings: AppSettings) {
  try {
    // Strip icon references before serializing (they're functions)
    const serializable = {
      ...settings,
      providers: settings.providers.map((p) => ({
        id: p.id,
        name: p.name,
        description: p.description,
        enabled: p.enabled,
        apiKey: p.apiKey,
        baseUrl: p.baseUrl,
        defaultModel: p.defaultModel,
        models: p.models,
        status: p.status,
        error: p.error,
      })),
      agents: settings.agents.map((a) => ({
        id: a.id,
        name: a.name,
        description: a.description,
        enabled: a.enabled,
        systemPrompt: a.systemPrompt,
        capabilities: a.capabilities,
        model: a.model,
        isCustom: a.isCustom,
        type: a.type,
      })),
    }
    localStorage.setItem(STORAGE_KEYS.SETTINGS, JSON.stringify(serializable))
  } catch {
    /* storage full or unavailable */
  }
}

// ============================================================================
// Hydrate providers/agents — restore icon references from defaults
// ============================================================================

function hydrateProviders(saved: LLMProviderConfig[]): LLMProviderConfig[] {
  return saved.map((sp) => {
    const def = defaultProviders.find((d) => d.id === sp.id)
    return def
      ? { ...def, ...sp, icon: def.icon, models: sp.models.length > 0 ? sp.models : def.models }
      : sp
  })
}

function hydrateAgents(saved: AgentPreset[]): AgentPreset[] {
  return saved.map((sa) => {
    const def = defaultAgentPresets.find((d) => d.id === sa.id)
    return def ? { ...def, ...sa, icon: def.icon } : sa
  })
}

// ============================================================================
// Module-level Signals
// ============================================================================

const initial = loadSettings()
initial.providers = hydrateProviders(initial.providers)
initial.agents = hydrateAgents(initial.agents)

const [settings, setSettingsRaw] = createSignal<AppSettings>(initial)

// ============================================================================
// Mutators — update + persist in one call
// ============================================================================

function updateSettings(patch: Partial<AppSettings>) {
  setSettingsRaw((prev) => {
    const next = { ...prev, ...patch }
    saveSettings(next)
    return next
  })
}

function updateProvider(id: string, patch: Partial<LLMProviderConfig>) {
  setSettingsRaw((prev) => {
    const next = {
      ...prev,
      providers: prev.providers.map((p) => (p.id === id ? { ...p, ...patch } : p)),
    }
    saveSettings(next)
    return next
  })
  // Sync API key to core credential store so LLM providers can read it
  if (patch.apiKey !== undefined) {
    syncProviderCredentials(id, patch.apiKey)
  }
}

function updateAgent(id: string, patch: Partial<AgentPreset>) {
  setSettingsRaw((prev) => {
    const next = {
      ...prev,
      agents: prev.agents.map((a) => (a.id === id ? { ...a, ...patch } : a)),
    }
    saveSettings(next)
    return next
  })
}

function addAgent(agent: AgentPreset) {
  setSettingsRaw((prev) => {
    const next = { ...prev, agents: [...prev.agents, agent] }
    saveSettings(next)
    return next
  })
}

function removeAgent(id: string) {
  setSettingsRaw((prev) => {
    const next = { ...prev, agents: prev.agents.filter((a) => a.id !== id) }
    saveSettings(next)
    return next
  })
}

function addAutoApprovedTool(toolName: string) {
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

function resetSettings() {
  const fresh = { ...DEFAULT_SETTINGS }
  setSettingsRaw(fresh)
  saveSettings(fresh)
}

// ============================================================================
// Export Hook
// ============================================================================

export function useSettings() {
  return {
    settings,
    updateSettings,
    updateProvider,
    updateAgent,
    addAgent,
    removeAgent,
    addAutoApprovedTool,
    isToolAutoApproved,
    resetSettings,
  }
}
