/**
 * Settings Persistence
 * Load/save settings, credential sync, env-var detection, and core bridge.
 * Contains side effects (localStorage, IPC, logging) but no SolidJS signals.
 */

import type { LLMProvider } from '@ava/core'
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

const CREDENTIAL_PREFIX = 'ava_cred_'

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
export function syncProviderCredentials(providerId: string, apiKey: string | undefined): void {
  const credKey = PROVIDER_KEY_MAP[providerId]
  if (!credKey) return
  const storageKey = CREDENTIAL_PREFIX + credKey
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

/**
 * Auto-detect API keys from environment variables via Rust IPC.
 * Calls `onDetected` for each key found so the caller can update the signal.
 * Returns the number of keys detected.
 */
export async function detectEnvApiKeys(
  currentProviders: LLMProviderConfig[],
  onDetected: (providerId: string, patch: Partial<LLMProviderConfig>) => void
): Promise<number> {
  let detected = 0

  for (const [providerId, envVar] of Object.entries(ENV_VAR_MAP)) {
    const provider = currentProviders.find((p) => p.id === providerId)
    if (provider?.apiKey) continue

    try {
      const value = await invoke<string | null>('get_env_var', { name: envVar })
      if (value) {
        onDetected(providerId, { apiKey: value, status: 'connected', enabled: true })
        detected++
      }
    } catch {
      // Rust command not available (e.g., running in browser) — skip silently
    }
  }

  if (detected > 0) {
    logInfo('settings', 'Auto-detected API keys', { count: detected })
  }

  return detected
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

  const activeProvider = s.providers.find((p) => p.enabled && p.apiKey)
  const providerUpdate: Record<string, unknown> = {
    defaultProvider: (activeProvider?.id ?? 'anthropic') as LLMProvider,
    defaultModel: activeProvider?.defaultModel ?? 'claude-sonnet-4-20250514',
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
  })

  sm.set('context', { maxTokens: s.generation.maxTokens, compactionThreshold: 80 })
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
