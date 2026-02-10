/**
 * LLM Client Interface
 * Provider-agnostic interface for streaming chat completions
 */

import { getAccountId, getStoredAuth, getValidAccessToken } from '../auth/index.js'
import { getSettingsManager } from '../config/manager.js'
import { getPlatform } from '../platform.js'
import type { ChatMessage, LLMProvider, ProviderConfig, StreamDelta } from '../types/llm.js'

// ============================================================================
// Client Interface
// ============================================================================

/**
 * Interface all provider clients must implement
 */
export interface LLMClient {
  /**
   * Stream chat completion tokens
   * @param messages - Conversation history
   * @param config - Provider configuration
   * @param signal - AbortSignal for cancellation
   * @yields StreamDelta objects with content chunks
   */
  stream(
    messages: ChatMessage[],
    config: ProviderConfig,
    signal?: AbortSignal
  ): AsyncGenerator<StreamDelta, void, unknown>
}

// ============================================================================
// Client Registry (lazy loaded)
// ============================================================================

// Type for client constructor
type ClientConstructor = new () => LLMClient

// Registry populated on first use
const clientRegistry: Partial<Record<LLMProvider, ClientConstructor>> = {}

/**
 * Register a client for a provider
 * Called by provider modules when imported
 */
export function registerClient(provider: LLMProvider, clientClass: ClientConstructor): void {
  clientRegistry[provider] = clientClass
}

// ============================================================================
// Factory Function
// ============================================================================

/**
 * Create a client instance for a provider
 * Dynamically imports provider module if not already loaded
 */
export async function createClient(provider: LLMProvider): Promise<LLMClient> {
  // Check if already registered
  let ClientClass = clientRegistry[provider]

  // Lazy load provider module
  if (!ClientClass) {
    switch (provider) {
      case 'anthropic':
        await import('./providers/anthropic.js')
        break
      case 'openrouter':
        await import('./providers/openrouter.js')
        break
      case 'openai':
        await import('./providers/openai.js')
        break
      case 'google':
        await import('./providers/google.js')
        break
      case 'glm':
        await import('./providers/glm.js')
        break
      case 'kimi':
        await import('./providers/kimi.js')
        break
      case 'mistral':
        await import('./providers/mistral.js')
        break
      case 'groq':
        await import('./providers/groq.js')
        break
      case 'deepseek':
        await import('./providers/deepseek.js')
        break
      case 'xai':
        await import('./providers/xai.js')
        break
      case 'cohere':
        await import('./providers/cohere.js')
        break
      case 'together':
        await import('./providers/together.js')
        break
      case 'ollama':
        await import('./providers/ollama.js')
        break
      case 'copilot':
        throw new Error('Copilot provider not yet implemented (requires OAuth)')
      default:
        throw new Error(`Unknown provider: ${provider}`)
    }

    ClientClass = clientRegistry[provider]
    if (!ClientClass) {
      throw new Error(`Provider ${provider} failed to register`)
    }
  }

  return new ClientClass()
}

// ============================================================================
// Weak Model Resolution
// ============================================================================

/** Provider inference from model name prefix */
const MODEL_PREFIX_PROVIDERS: [string, LLMProvider][] = [
  ['claude', 'anthropic'],
  ['gpt', 'openai'],
  ['o1', 'openai'],
  ['o3', 'openai'],
  ['gemini', 'google'],
  ['glm', 'glm'],
  ['kimi', 'kimi'],
  ['moonshot', 'kimi'],
  ['mistral', 'mistral'],
  ['deepseek', 'deepseek'],
  ['llama', 'groq'],
  ['mixtral', 'groq'],
]

/** Infer provider from a model name string */
function inferProvider(model: string): LLMProvider {
  for (const [prefix, provider] of MODEL_PREFIX_PROVIDERS) {
    if (model.startsWith(prefix)) return provider
  }
  return 'openrouter' // fallback for unknown models
}

/**
 * Get model + provider config for secondary tasks (summaries, reviews, planning).
 * Returns the weak model if configured, otherwise falls back to default model.
 */
export function getWeakModelConfig(): { model: string; provider: LLMProvider } {
  try {
    const providerSettings = getSettingsManager().get('provider')
    if (providerSettings.weakModel) {
      return {
        model: providerSettings.weakModel,
        provider: providerSettings.weakModelProvider ?? inferProvider(providerSettings.weakModel),
      }
    }
    return { model: providerSettings.defaultModel, provider: providerSettings.defaultProvider }
  } catch {
    // Settings manager not initialized — use defaults
    return { model: 'claude-sonnet-4-20250514', provider: 'anthropic' }
  }
}

/**
 * Get model + provider config for editor tasks (file edits by Junior Devs).
 * Returns the editor model if configured, otherwise falls back to default model.
 * Part of the architect/editor split: Team Lead uses primary (architect) model,
 * Junior Devs use cheaper editor model for actual file changes.
 */
export function getEditorModelConfig(): { model: string; provider: LLMProvider } {
  try {
    const providerSettings = getSettingsManager().get('provider')
    if (providerSettings.editorModel) {
      return {
        model: providerSettings.editorModel,
        provider:
          providerSettings.editorModelProvider ?? inferProvider(providerSettings.editorModel),
      }
    }
    return { model: providerSettings.defaultModel, provider: providerSettings.defaultProvider }
  } catch {
    // Settings manager not initialized — use defaults
    return { model: 'claude-sonnet-4-20250514', provider: 'anthropic' }
  }
}

// ============================================================================
// Credential Resolution
// ============================================================================

/**
 * Get API key for a provider from platform credentials
 */
export async function getApiKey(provider: LLMProvider): Promise<string | null> {
  const platform = getPlatform()

  // Map provider to credential key
  const keyMap: Record<LLMProvider, string> = {
    anthropic: 'anthropic-api-key',
    openrouter: 'openrouter-api-key',
    openai: 'openai-api-key',
    google: 'google-api-key',
    copilot: 'copilot-api-key', // Note: Copilot typically uses OAuth, not API keys
    glm: 'glm-api-key',
    kimi: 'kimi-api-key',
    // New providers
    mistral: 'mistral-api-key',
    groq: 'groq-api-key',
    deepseek: 'deepseek-api-key',
    xai: 'xai-api-key',
    cohere: 'cohere-api-key',
    together: 'together-api-key',
    ollama: 'ollama-api-key', // Ollama doesn't need API key, but included for consistency
  }

  return platform.credentials.get(keyMap[provider])
}

// ============================================================================
// Auth Resolution (API Key or OAuth)
// ============================================================================

/** Authentication info returned from getAuth */
export interface AuthInfo {
  type: 'api-key' | 'oauth'
  /** API key or OAuth access token */
  token: string
  /** For OpenAI Codex - the account ID */
  accountId?: string
}

/**
 * Get authentication for a provider
 * Checks OAuth first, falls back to API key
 */
export async function getAuth(provider: LLMProvider): Promise<AuthInfo | null> {
  // Check for OAuth auth first
  const storedAuth = await getStoredAuth(provider)
  if (storedAuth?.type === 'oauth') {
    const accessToken = await getValidAccessToken(provider)
    if (accessToken) {
      const accountId = await getAccountId(provider)
      return {
        type: 'oauth',
        token: accessToken,
        accountId: accountId ?? undefined,
      }
    }
  }

  // Fall back to API key
  const apiKey = await getApiKey(provider)
  if (apiKey) {
    return {
      type: 'api-key',
      token: apiKey,
    }
  }

  return null
}
