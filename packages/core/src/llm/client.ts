/**
 * LLM Client Interface
 * Provider-agnostic interface for streaming chat completions
 */

import { getAccountId, getStoredAuth, getValidAccessToken } from '../auth/index.js'
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
        // TODO: Port openrouter provider
        throw new Error('OpenRouter provider not yet ported to core')
      case 'openai':
        throw new Error('OpenAI provider not yet implemented')
      case 'glm':
        throw new Error('GLM provider not yet implemented')
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
