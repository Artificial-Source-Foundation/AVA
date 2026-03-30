/**
 * Settings Persistence
 * Load/save settings, credential sync, env-var detection, and core bridge.
 * Contains side effects (localStorage, IPC, logging) but no SolidJS signals.
 */

import { invoke } from '@tauri-apps/api/core'

import { STORAGE_KEYS } from '../../config/constants'
import type { LLMProviderConfig } from '../../config/defaults/provider-defaults'
import { logDebug, logInfo, logWarn } from '../../services/logger'
import { writeSettingsToFS } from '../../services/settings-fs'
import { setPermissionMode } from '../../services/tool-approval-bridge'
import { DEFAULT_SETTINGS } from './settings-defaults'
import { mergeWithDefaults } from './settings-hydration'
import type { AppSettings } from './settings-types'

interface HqAgentOverridePayload {
  id: string
  enabled: boolean
  modelSpec: string
  systemPrompt: string
}

function buildHqAgentOverrides(settings: AppSettings): HqAgentOverridePayload[] {
  const providerDefaultModel = new Map(
    settings.providers.map((provider) => [provider.id, provider.defaultModel])
  )
  return settings.agents
    .filter((agent) => agent.tier != null)
    .map((agent) => {
      const modelSpec = agent.model
        ? agent.provider
          ? `${agent.provider}/${agent.model}`
          : agent.model
        : agent.provider
          ? providerDefaultModel.get(agent.provider)
            ? `${agent.provider}/${providerDefaultModel.get(agent.provider)}`
            : ''
          : ''

      return {
        id: agent.id,
        enabled: agent.enabled,
        modelSpec,
        systemPrompt: agent.systemPrompt ?? '',
      }
    })
}

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

/**
 * Push current frontend settings to the Rust AgentStack via Tauri IPC.
 *
 * Syncs:
 * - permissionMode → desktop approval middleware + Rust set_permission_level
 * - LLM config (provider, model, temperature, maxTokens) → config.yaml
 * - Feature flags (git, lsp, mcp, audit, session logging, etc.) → config.yaml
 *
 * Additional settings (reasoningEffort → thinkingLevel, maxTurns) are
 * passed per-run via SubmitGoalArgs in submit_goal.
 */
export function pushSettingsToCore(s: AppSettings): void {
  // Sync permission mode to the desktop approval middleware (+ Rust backend)
  setPermissionMode(s.permissionMode)

  logDebug('settings', 'Core settings synced', {
    permissionMode: s.permissionMode,
    reasoningEffort: s.generation.reasoningEffort,
    maxTurns: s.agentLimits.agentMaxTurns,
    temperature: s.generation.temperature,
  })

  invoke('sync_hq_agent_overrides', { overrides: buildHqAgentOverrides(s) }).catch((err) => {
    logWarn('settings', 'HQ agent override sync failed', err)
  })

  // Sync shared LLM config to config.yaml (fire-and-forget)
  syncLlmConfigToYaml(s)

  // Sync shared feature flags to config.yaml (fire-and-forget)
  syncFeatureFlagsToYaml(s)
}

/**
 * Resolve the active provider ID and model from the Desktop settings.
 * The active provider is the first enabled provider with an API key.
 * The model is that provider's defaultModel.
 */
function resolveActiveProviderModel(s: AppSettings): { provider?: string; model?: string } {
  const active = s.providers.find((p) => p.enabled && p.apiKey)
  if (!active) return {}
  return {
    provider: active.id,
    model: active.defaultModel || undefined,
  }
}

/**
 * Sync LLM settings (provider, model, temperature, maxTokens) to config.yaml.
 * Fire-and-forget — never blocks the UI.
 */
function syncLlmConfigToYaml(s: AppSettings): void {
  const { provider, model } = resolveActiveProviderModel(s)
  invoke('update_llm_config', {
    provider: provider ?? null,
    model: model ?? null,
    temperature: s.generation.temperature,
    maxTokens: s.generation.maxTokens,
  }).catch((err) => {
    logWarn('settings', 'LLM config sync to config.yaml failed', err)
  })
}

/**
 * Sync feature flags to config.yaml.
 * Fire-and-forget — never blocks the UI.
 */
function syncFeatureFlagsToYaml(s: AppSettings): void {
  invoke('update_feature_flags', {
    enableGit: s.git.enabled,
    enableLsp: null,
    enableMcp: s.mcpServers.length > 0,
    auditLogging: null,
    sessionLogging: null,
    autoReview: null,
    enableCodebaseIndex: null,
  }).catch((err) => {
    logWarn('settings', 'Feature flags sync to config.yaml failed', err)
  })
}

// ============================================================================
// Config.yaml Hydration — load shared settings from Rust on startup
// ============================================================================

/** Shape of the `llm` section returned by `get_config` */
interface CoreLlmConfig {
  provider?: string
  model?: string
  temperature?: number
  max_tokens?: number
}

/** Shape of the `features` section returned by `get_feature_flags` */
interface CoreFeatureFlags {
  enable_git?: boolean
  enable_mcp?: boolean
}

/**
 * Load shared settings from config.yaml via Tauri IPC and merge into Desktop settings.
 *
 * Called once at startup. Only overwrites fields that differ from defaults so
 * user's localStorage settings take precedence for Desktop-only knobs.
 *
 * Returns a partial patch — the caller is responsible for merging it into the
 * signal and persisting.
 */
export async function loadSharedSettingsFromCore(): Promise<Partial<AppSettings> | null> {
  try {
    const [configResult, flagsResult] = await Promise.allSettled([
      invoke<Record<string, unknown>>('get_config'),
      invoke<CoreFeatureFlags>('get_feature_flags'),
    ])

    const patch: Partial<AppSettings> = {}
    let changed = false

    // --- LLM provider/model from config.yaml ---
    if (configResult.status === 'fulfilled' && configResult.value) {
      const llm = (configResult.value as Record<string, unknown>).llm as CoreLlmConfig | undefined
      if (llm) {
        if (llm.temperature != null) {
          patch.generation = { temperature: llm.temperature } as AppSettings['generation']
          changed = true
        }
        if (llm.max_tokens != null) {
          patch.generation = {
            ...patch.generation,
            maxTokens: llm.max_tokens,
          } as AppSettings['generation']
          changed = true
        }
        // provider/model are informational — log them so Desktop can show
        // the same model the TUI last used, but only if they're non-empty
        if (llm.provider && llm.model) {
          logInfo('settings', 'Core config.yaml LLM', {
            provider: llm.provider,
            model: llm.model,
          })
        }
      }
    }

    // --- Feature flags from config.yaml ---
    if (flagsResult.status === 'fulfilled' && flagsResult.value) {
      const flags = flagsResult.value
      if (flags.enable_git != null) {
        patch.git = { enabled: flags.enable_git } as AppSettings['git']
        changed = true
      }
    }

    return changed ? patch : null
  } catch (err) {
    logWarn('settings', 'Failed to load shared settings from config.yaml', err)
    return null
  }
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
      tier: a.tier,
      tools: a.tools,
      delegates: a.delegates,
      domain: a.domain,
      provider: a.provider,
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

const SETTINGS_PERSIST_DELAY_MS = 180

let pendingSettings: AppSettings | null = null
let pendingPersistTimer: ReturnType<typeof setTimeout> | undefined
let persistenceListenersInstalled = false

function persistSettingsNow(s: AppSettings): void {
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

function flushPendingSettings(): void {
  if (pendingPersistTimer) {
    clearTimeout(pendingPersistTimer)
    pendingPersistTimer = undefined
  }

  if (!pendingSettings) return

  const next = pendingSettings
  pendingSettings = null
  persistSettingsNow(next)
}

function ensurePersistenceListeners(): void {
  if (persistenceListenersInstalled || typeof window === 'undefined') return

  persistenceListenersInstalled = true

  const flush = () => flushPendingSettings()
  window.addEventListener('beforeunload', flush)
  window.addEventListener('pagehide', flush)

  if (typeof document !== 'undefined') {
    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'hidden') flushPendingSettings()
    })
  }
}

export function saveSettings(s: AppSettings): void {
  ensurePersistenceListeners()
  pendingSettings = s

  if (pendingPersistTimer) clearTimeout(pendingPersistTimer)
  pendingPersistTimer = setTimeout(() => {
    pendingPersistTimer = undefined
    flushPendingSettings()
  }, SETTINGS_PERSIST_DELAY_MS)
}
