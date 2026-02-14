/**
 * LLM Bridge for Tauri
 * Bridges local model resolution with @ava/core LLM client
 */

import { createClient as coreCreateClient, type LLMClient } from '@ava/core'

// Re-export LLMClient type for consumers
export type { LLMClient }

import type { LLMProvider } from '../../types/llm'

// ============================================================================
// Model to Provider Mapping
// ============================================================================

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
 * Find the native provider for a model ID
 */
export function findProviderForModel(model: string): LLMProvider | null {
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
  if (model.startsWith('gemini')) return 'google' as LLMProvider
  if (model.startsWith('moonshot') || model.startsWith('kimi')) return 'kimi' as LLMProvider

  return null
}

/**
 * Resolve provider for a model, with OpenRouter fallback
 */
export function resolveProvider(model: string): LLMProvider {
  const native = findProviderForModel(model)
  // For now, always use the native provider or default to openrouter
  return native || 'openrouter'
}

// ============================================================================
// Client Factory
// ============================================================================

/**
 * Create LLM client for a model
 * Uses @ava/core client with Tauri platform
 */
export async function createClient(model: string): Promise<LLMClient> {
  const provider = resolveProvider(model)
  return coreCreateClient(provider)
}

/**
 * Get the provider that will be used for a model
 */
export function getProviderForModel(model: string): LLMProvider {
  return resolveProvider(model)
}
