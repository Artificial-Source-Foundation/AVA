/**
 * Settings Persistence
 * Load/save settings, credential sync, env-var detection, and core bridge.
 * Contains side effects (localStorage, IPC, logging) but no SolidJS signals.
 */

import type { LLMProvider } from '@ava/core-v2/llm'
import { invoke } from '@tauri-apps/api/core'
import { STORAGE_KEYS } from '../../config/constants'
import type { LLMProviderConfig } from '../../config/defaults/provider-defaults'
import { getCoreSettings } from '../../services/core-bridge'
import { logDebug, logInfo, logWarn } from '../../services/logger'
import { writeSettingsToFS } from '../../services/settings-fs'
import { DEFAULT_SETTINGS } from './settings-defaults'
import { mergeWithDefaults } from './settings-hydration'
import type { AppSettings } from './settings-types'

// ============================================================================
// Credential Sync — bridges Settings UI → Core credential store
// ============================================================================

/**
 * Write a single provider's API key to the credential store.
 * Key format matches core-v2 getApiKey(): "ava:{provider}:api_key"
 * TauriCredentialStore adds "ava_cred_" prefix, so final localStorage
 * key is "ava_cred_ava:{provider}:api_key".
 */
export function syncProviderCredentials(providerId: string, apiKey: string | undefined): void {
  const storageKey = `ava_cred_ava:${providerId}:api_key`
  if (apiKey) {
    localStorage.setItem(storageKey, apiKey)
  } else {
    localStorage.removeItem(storageKey)
  }
}

// ============================================================================
// Environment Variable Auto-Detection
// ============================================================================

/** Map of provider IDs to their standard environment variable names */
const ENV_VAR_MAP: Record<string, string> = {
  anthropic: 'ANTHROPIC_API_KEY',
  openai: 'OPENAI_API_KEY',
  google: 'GOOGLE_API_KEY',
  groq: 'GROQ_API_KEY',
  mistral: 'MISTRAL_API_KEY',
  deepseek: 'DEEPSEEK_API_KEY',
  xai: 'XAI_API_KEY',
  cohere: 'COHERE_API_KEY',
  together: 'TOGETHER_API_KEY',
  openrouter: 'OPENROUTER_API_KEY',
}

export interface EnvKeyDetectionResult {
  count: number
  providers: string[]
}

/**
 * Auto-detect API keys from environment variables via Rust IPC.
 * Calls `onDetected` for each key found so the caller can update the signal.
 * Returns the count and names of detected providers.
 */
export async function detectEnvApiKeys(
  currentProviders: LLMProviderConfig[],
  onDetected: (providerId: string, patch: Partial<LLMProviderConfig>) => void
): Promise<EnvKeyDetectionResult> {
  const detectedProviders: string[] = []

  for (const [providerId, envVar] of Object.entries(ENV_VAR_MAP)) {
    const provider = currentProviders.find((p) => p.id === providerId)
    if (provider?.apiKey) continue

    try {
      const value = await invoke<string | null>('get_env_var', { name: envVar })
      if (value) {
        onDetected(providerId, { apiKey: value, status: 'connected', enabled: true })
        detectedProviders.push(providerId)
      }
    } catch {
      // Rust command not available (e.g., running in browser) — skip silently
    }
  }

  if (detectedProviders.length > 0) {
    logInfo('settings', 'Auto-detected API keys', { count: detectedProviders.length })
  }

  return { count: detectedProviders.length, providers: detectedProviders }
}

/** Hydrate all provider API keys from settings into the core credential store */
export function syncAllApiKeys(current: AppSettings): void {
  for (const provider of current.providers) {
    if (provider.apiKey) {
      syncProviderCredentials(provider.id, provider.apiKey)
    }
  }
}

// ============================================================================
// Core Settings Sync — pushes frontend AppSettings → core SettingsManager
// ============================================================================

/** Push current frontend settings to the core SettingsManager */
export function pushSettingsToCore(s: AppSettings): void {
  const sm = getCoreSettings()
  if (!sm) return

  // Ensure categories exist (core-v2 only ships 'provider' and 'agent' by default;
  // extensions may register more, but on first save they might not be loaded yet)
  const registered = sm.getRegisteredCategories()
  if (!registered.includes('permissions')) {
    sm.registerCategory('permissions', { allowBashExecution: false, autoApprovePatterns: [] })
  }
  if (!registered.includes('context')) {
    sm.registerCategory('context', { maxTokens: 4096, compactionThreshold: 80 })
  }
  if (!registered.includes('git')) {
    sm.registerCategory('git', {
      enabled: true,
      autoCommit: false,
      branchPrefix: 'ava/',
      messagePrefix: '[ava]',
    })
  }

  const activeProvider = s.providers.find(
    (p) => p.enabled && (p.apiKey || p.status === 'connected')
  )
  const providerUpdate: Record<string, unknown> = {
    defaultProvider: (activeProvider?.id ?? 'openai') as LLMProvider,
    defaultModel: activeProvider?.defaultModel ?? 'gpt-5.2',
  }
  if (s.generation.weakModel) {
    providerUpdate.weakModel = s.generation.weakModel
  }
  if (s.generation.editorModel) {
    providerUpdate.editorModel = s.generation.editorModel
  }
  sm.set('provider', providerUpdate)
  logDebug('settings', 'Provider settings synced', {
    defaultProvider: providerUpdate.defaultProvider,
    defaultModel: providerUpdate.defaultModel,
  })

  sm.set('permissions', {
    allowBashExecution: s.permissionMode !== 'ask',
    autoApprovePatterns: s.autoApprovedTools,
    toolRules: s.toolRules,
    autoApproveReads: true,
    autoApproveWrites: s.permissionMode === 'auto-approve' || s.permissionMode === 'bypass',
    autoApproveCommands: s.permissionMode === 'bypass',
    smartApprove: s.permissionMode === 'auto-approve',
  })

  sm.set('context', {
    maxTokens: s.generation.maxTokens,
    compactionThreshold: s.generation.compactionThreshold,
  })
  sm.set('git', {
    enabled: s.git.enabled,
    autoCommit: s.git.autoCommit,
    branchPrefix: 'ava/',
    messagePrefix: s.git.commitPrefix,
  })
  logDebug('settings', 'Core settings synced')
}

// ============================================================================
// Persistence Helpers
// ============================================================================

/** Serialize settings for storage (strip non-serializable fields like icon functions) */
export function serializeSettings(s: AppSettings): Record<string, unknown> {
  return {
    ...s,
    providers: s.providers.map((p) => ({
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
    agents: s.agents.map((a) => ({
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
}

export function loadSettings(): AppSettings {
  try {
    const raw = localStorage.getItem(STORAGE_KEYS.SETTINGS)
    if (raw) {
      const parsed = JSON.parse(raw) as Partial<AppSettings>
      return mergeWithDefaults(parsed)
    }
  } catch {
    /* corrupted data — use defaults */
  }
  return { ...DEFAULT_SETTINGS }
}

export function saveSettings(s: AppSettings): void {
  const serializable = serializeSettings(s)
  try {
    localStorage.setItem(STORAGE_KEYS.SETTINGS, JSON.stringify(serializable))
    // Bridge ava-mode for index.html flash-prevention script
    localStorage.setItem('ava-mode', s.mode)
  } catch (err) {
    logWarn('settings', 'localStorage write failed', err)
  }
  // Write to Tauri FS backend (async, fire-and-forget)
  writeSettingsToFS(serializable).catch(() => {})
  // Keep core SettingsManager in sync
  pushSettingsToCore(s)
}
