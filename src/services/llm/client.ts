/**
 * LLM Client Interface
 * Provider-agnostic interface for streaming chat completions
 */

import type {
  ChatMessage,
  Credentials,
  LLMProvider,
  ProviderConfig,
  StreamDelta,
} from '../../types/llm'
import { getApiKeyWithFallback, getCredentials } from '../auth/credentials'

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
      case 'openrouter':
        await import('./providers/openrouter')
        break
      case 'anthropic':
        await import('./providers/anthropic')
        break
      case 'openai':
        // TODO: Implement OpenAI provider
        throw new Error('OpenAI provider not yet implemented')
      case 'glm':
        // TODO: Implement GLM provider
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
// Auth Resolution
// ============================================================================

/**
 * Result of resolving authentication for a model
 */
export interface ResolvedAuth {
  provider: LLMProvider
  credentials: Credentials
  useGateway: boolean
}

/**
 * Model to provider mapping
 * Maps model IDs to their native providers
 */
const MODEL_PROVIDERS: Record<string, LLMProvider> = {
  // Anthropic models
  'claude-opus-4': 'anthropic',
  'claude-sonnet-4': 'anthropic',
  'claude-haiku-4': 'anthropic',
  'claude-3-opus': 'anthropic',
  'claude-3-sonnet': 'anthropic',
  'claude-3-haiku': 'anthropic',

  // OpenAI models
  'gpt-4': 'openai',
  'gpt-4-turbo': 'openai',
  'gpt-4o': 'openai',
  'gpt-3.5-turbo': 'openai',
  o1: 'openai',
  'o1-mini': 'openai',

  // GLM models
  'glm-4': 'glm',
  'glm-4-plus': 'glm',
}

/**
 * Resolve the best provider and credentials for a model
 *
 * Priority:
 * 1. OAuth token for native provider (if available)
 * 2. Direct API key for native provider
 * 3. OpenRouter gateway (if configured)
 *
 * @param model - Model ID to resolve
 * @returns Provider and credentials, or null if no auth available
 */
export function resolveAuth(model: string): ResolvedAuth | null {
  // Determine native provider for this model
  const nativeProvider = findNativeProvider(model)

  // 1. Check OAuth for native provider
  if (nativeProvider) {
    const oauthCred = getCredentials(nativeProvider)
    if (oauthCred?.type === 'oauth-token') {
      return { provider: nativeProvider, credentials: oauthCred, useGateway: false }
    }

    // 2. Check direct API key for native provider
    const apiKey = getApiKeyWithFallback(nativeProvider)
    if (apiKey) {
      return {
        provider: nativeProvider,
        credentials: {
          provider: nativeProvider,
          type: 'api-key',
          value: apiKey,
        },
        useGateway: false,
      }
    }
  }

  // 3. Fallback to OpenRouter gateway
  const openrouterKey = getApiKeyWithFallback('openrouter')
  if (openrouterKey) {
    return {
      provider: 'openrouter',
      credentials: {
        provider: 'openrouter',
        type: 'api-key',
        value: openrouterKey,
      },
      useGateway: true,
    }
  }

  // No credentials available
  return null
}

/**
 * Find the native provider for a model ID
 */
function findNativeProvider(model: string): LLMProvider | null {
  // Check exact match
  if (model in MODEL_PROVIDERS) {
    return MODEL_PROVIDERS[model]
  }

  // Check prefix match (e.g., 'claude-sonnet-4-20250514' -> 'claude-sonnet-4')
  for (const [prefix, provider] of Object.entries(MODEL_PROVIDERS)) {
    if (model.startsWith(prefix)) {
      return provider
    }
  }

  // Check by provider prefix patterns
  if (model.startsWith('claude')) return 'anthropic'
  if (model.startsWith('gpt') || model.startsWith('o1')) return 'openai'
  if (model.startsWith('glm')) return 'glm'

  return null
}

/**
 * Get the OpenRouter model ID for a model
 * OpenRouter uses format: provider/model-name
 */
export function getOpenRouterModelId(model: string): string {
  const provider = findNativeProvider(model)

  // Map providers to OpenRouter prefixes
  const prefixMap: Record<LLMProvider, string> = {
    anthropic: 'anthropic',
    openai: 'openai',
    glm: 'z-ai',
    openrouter: '',
  }

  if (provider && prefixMap[provider]) {
    return `${prefixMap[provider]}/${model}`
  }

  // Return as-is if already in OpenRouter format or unknown
  return model
}
