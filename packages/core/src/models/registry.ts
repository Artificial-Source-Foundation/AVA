/**
 * Model Registry
 *
 * Centralized registry of all supported LLM models with their
 * configurations, capabilities, and pricing information.
 *
 * Usage:
 * ```ts
 * import { getModel, getContextLimit, findModels } from '@estela/core/models'
 *
 * const model = getModel('claude-sonnet-4')
 * const limit = getContextLimit('gpt-4o') // 128000
 *
 * // Find all models with tools support
 * const toolModels = findModels({ capability: 'tools' })
 * ```
 */

import type { LLMProvider } from '../types/llm.js'
import type { ModelConfig, ModelFilter } from './types.js'

// ============================================================================
// Model Registry
// ============================================================================

/**
 * Registry of all supported models
 * Keys are short identifiers, values contain full API model IDs
 */
export const MODEL_REGISTRY: Record<string, ModelConfig> = {
  // ==========================================================================
  // Anthropic Claude Models
  // ==========================================================================

  'claude-opus-4': {
    id: 'claude-opus-4-20250514',
    provider: 'anthropic',
    displayName: 'Claude Opus 4',
    contextWindow: 200000,
    maxOutputTokens: 32000,
    capabilities: {
      tools: true,
      vision: true,
      streaming: true,
      json: true,
      structuredOutput: true,
      thinking: true,
      computerUse: true,
    },
    pricing: {
      inputPer1k: 0.015,
      outputPer1k: 0.075,
      cacheReadPer1k: 0.0015,
      cacheWritePer1k: 0.01875,
    },
    releaseDate: '2025-05-14',
    description: 'Most capable Claude model with extended thinking',
  },

  'claude-sonnet-4': {
    id: 'claude-sonnet-4-20250514',
    provider: 'anthropic',
    displayName: 'Claude Sonnet 4',
    contextWindow: 200000,
    maxOutputTokens: 16384,
    capabilities: {
      tools: true,
      vision: true,
      streaming: true,
      json: true,
      structuredOutput: true,
      thinking: true,
      computerUse: true,
    },
    pricing: {
      inputPer1k: 0.003,
      outputPer1k: 0.015,
      cacheReadPer1k: 0.0003,
      cacheWritePer1k: 0.00375,
    },
    releaseDate: '2025-05-14',
    description: 'Balanced performance and cost',
  },

  'claude-haiku-3.5': {
    id: 'claude-3-5-haiku-20241022',
    provider: 'anthropic',
    displayName: 'Claude 3.5 Haiku',
    contextWindow: 200000,
    maxOutputTokens: 8192,
    capabilities: {
      tools: true,
      vision: true,
      streaming: true,
      json: true,
    },
    pricing: {
      inputPer1k: 0.0008,
      outputPer1k: 0.004,
      cacheReadPer1k: 0.00008,
      cacheWritePer1k: 0.001,
    },
    releaseDate: '2024-10-22',
    description: 'Fastest Claude model',
  },

  // ==========================================================================
  // OpenAI Models
  // ==========================================================================

  'gpt-4o': {
    id: 'gpt-4o',
    provider: 'openai',
    displayName: 'GPT-4o',
    contextWindow: 128000,
    maxOutputTokens: 16384,
    capabilities: {
      tools: true,
      vision: true,
      streaming: true,
      json: true,
      structuredOutput: true,
    },
    pricing: {
      inputPer1k: 0.005,
      outputPer1k: 0.015,
    },
    releaseDate: '2024-05-13',
    description: 'Flagship GPT model with vision',
  },

  'gpt-4o-mini': {
    id: 'gpt-4o-mini',
    provider: 'openai',
    displayName: 'GPT-4o Mini',
    contextWindow: 128000,
    maxOutputTokens: 16384,
    capabilities: {
      tools: true,
      vision: true,
      streaming: true,
      json: true,
      structuredOutput: true,
    },
    pricing: {
      inputPer1k: 0.00015,
      outputPer1k: 0.0006,
    },
    releaseDate: '2024-07-18',
    description: 'Fast and affordable GPT model',
  },

  o1: {
    id: 'o1',
    provider: 'openai',
    displayName: 'OpenAI o1',
    contextWindow: 200000,
    maxOutputTokens: 100000,
    capabilities: {
      tools: true,
      vision: true,
      streaming: true,
      json: true,
      thinking: true,
    },
    pricing: {
      inputPer1k: 0.015,
      outputPer1k: 0.06,
    },
    releaseDate: '2024-12-17',
    description: 'Reasoning model with extended thinking',
  },

  'o3-mini': {
    id: 'o3-mini',
    provider: 'openai',
    displayName: 'OpenAI o3-mini',
    contextWindow: 200000,
    maxOutputTokens: 100000,
    capabilities: {
      tools: true,
      vision: false,
      streaming: true,
      json: true,
      thinking: true,
    },
    pricing: {
      inputPer1k: 0.0011,
      outputPer1k: 0.0044,
    },
    releaseDate: '2025-01-31',
    description: 'Fast reasoning model',
  },

  // ==========================================================================
  // Google Models
  // ==========================================================================

  'gemini-2.0-flash': {
    id: 'gemini-2.0-flash',
    provider: 'google',
    displayName: 'Gemini 2.0 Flash',
    contextWindow: 1048576,
    maxOutputTokens: 8192,
    capabilities: {
      tools: true,
      vision: true,
      streaming: true,
      json: true,
      codeExecution: true,
    },
    pricing: {
      inputPer1k: 0.0001,
      outputPer1k: 0.0004,
    },
    releaseDate: '2024-12-11',
    description: 'Fast Gemini with 1M context',
  },

  'gemini-2.0-flash-thinking': {
    id: 'gemini-2.0-flash-thinking-exp',
    provider: 'google',
    displayName: 'Gemini 2.0 Flash Thinking',
    contextWindow: 1048576,
    maxOutputTokens: 65536,
    capabilities: {
      tools: true,
      vision: true,
      streaming: true,
      json: true,
      thinking: true,
    },
    releaseDate: '2025-01-21',
    description: 'Extended thinking variant',
  },

  'gemini-1.5-pro': {
    id: 'gemini-1.5-pro',
    provider: 'google',
    displayName: 'Gemini 1.5 Pro',
    contextWindow: 2097152,
    maxOutputTokens: 8192,
    capabilities: {
      tools: true,
      vision: true,
      streaming: true,
      json: true,
    },
    pricing: {
      inputPer1k: 0.00125,
      outputPer1k: 0.005,
    },
    releaseDate: '2024-02-15',
    description: '2M context window',
  },

  // ==========================================================================
  // DeepSeek Models (via OpenRouter)
  // ==========================================================================

  'deepseek-chat': {
    id: 'deepseek/deepseek-chat',
    provider: 'openrouter',
    displayName: 'DeepSeek Chat',
    contextWindow: 64000,
    maxOutputTokens: 8192,
    capabilities: {
      tools: true,
      vision: false,
      streaming: true,
      json: true,
    },
    pricing: {
      inputPer1k: 0.00014,
      outputPer1k: 0.00028,
    },
    description: 'Affordable coding model',
  },

  'deepseek-reasoner': {
    id: 'deepseek/deepseek-reasoner',
    provider: 'openrouter',
    displayName: 'DeepSeek R1',
    contextWindow: 64000,
    maxOutputTokens: 8192,
    capabilities: {
      tools: true,
      vision: false,
      streaming: true,
      json: true,
      thinking: true,
    },
    pricing: {
      inputPer1k: 0.00055,
      outputPer1k: 0.00219,
    },
    description: 'Reasoning model with thinking',
  },

  // ==========================================================================
  // GLM Models (Zhipu AI)
  // ==========================================================================

  'glm-4-plus': {
    id: 'glm-4-plus',
    provider: 'glm',
    displayName: 'GLM-4 Plus',
    contextWindow: 128000,
    maxOutputTokens: 4096,
    capabilities: {
      tools: true,
      vision: false,
      streaming: true,
      json: true,
    },
    description: 'Advanced Chinese language model',
  },

  // ==========================================================================
  // Kimi Models (Moonshot AI)
  // ==========================================================================

  'kimi-k1.5': {
    id: 'moonshot-v1-auto',
    provider: 'kimi',
    displayName: 'Kimi K1.5',
    contextWindow: 128000,
    maxOutputTokens: 8192,
    capabilities: {
      tools: true,
      vision: true,
      streaming: true,
      json: true,
      thinking: true,
    },
    description: 'Kimi with extended context',
  },

  // ==========================================================================
  // GitHub Copilot (via OAuth)
  // ==========================================================================

  'copilot-gpt-4o': {
    id: 'gpt-4o',
    provider: 'copilot',
    displayName: 'Copilot GPT-4o',
    contextWindow: 128000,
    maxOutputTokens: 16384,
    capabilities: {
      tools: true,
      vision: true,
      streaming: true,
      json: true,
    },
    description: 'GPT-4o via GitHub Copilot',
  },

  'copilot-claude-sonnet': {
    id: 'claude-3.5-sonnet',
    provider: 'copilot',
    displayName: 'Copilot Claude Sonnet',
    contextWindow: 200000,
    maxOutputTokens: 8192,
    capabilities: {
      tools: true,
      vision: true,
      streaming: true,
      json: true,
    },
    description: 'Claude 3.5 Sonnet via GitHub Copilot',
  },
}

// ============================================================================
// Lookup Functions
// ============================================================================

/**
 * Get a model by its short identifier
 */
export function getModel(shortId: string): ModelConfig | undefined {
  return MODEL_REGISTRY[shortId]
}

/**
 * Get a model by its full API ID
 */
export function getModelByApiId(apiId: string): ModelConfig | undefined {
  return Object.values(MODEL_REGISTRY).find((m) => m.id === apiId)
}

/**
 * Get context window limit for a model
 * @param shortId - Model short identifier
 * @returns Context window size, or default 128000 if not found
 */
export function getContextLimit(shortId: string): number {
  return MODEL_REGISTRY[shortId]?.contextWindow ?? 128000
}

/**
 * Get max output tokens for a model
 * @param shortId - Model short identifier
 * @returns Max output tokens, or default 4096 if not found
 */
export function getMaxOutputTokens(shortId: string): number {
  return MODEL_REGISTRY[shortId]?.maxOutputTokens ?? 4096
}

/**
 * Check if a model has a specific capability
 */
export function hasCapability(
  shortId: string,
  capability: keyof ModelConfig['capabilities']
): boolean {
  return MODEL_REGISTRY[shortId]?.capabilities[capability] === true
}

// ============================================================================
// Query Functions
// ============================================================================

/**
 * Find models matching filter criteria
 */
export function findModels(filter: ModelFilter = {}): ModelConfig[] {
  let models = Object.values(MODEL_REGISTRY)

  if (filter.provider) {
    models = models.filter((m) => m.provider === filter.provider)
  }

  if (filter.capability) {
    models = models.filter((m) => m.capabilities[filter.capability!] === true)
  }

  if (filter.minContext) {
    models = models.filter((m) => m.contextWindow >= filter.minContext!)
  }

  if (filter.maxOutputPrice && filter.maxOutputPrice > 0) {
    models = models.filter((m) => (m.pricing?.outputPer1k ?? 0) <= filter.maxOutputPrice!)
  }

  if (filter.excludeDeprecated) {
    models = models.filter((m) => !m.deprecated)
  }

  return models
}

/**
 * Get all models for a provider
 */
export function getModelsForProvider(provider: LLMProvider): ModelConfig[] {
  return Object.values(MODEL_REGISTRY).filter((m) => m.provider === provider)
}

/**
 * Get all available model short IDs
 */
export function getModelIds(): string[] {
  return Object.keys(MODEL_REGISTRY)
}

// ============================================================================
// Pricing Functions
// ============================================================================

/**
 * Estimate cost for a given token usage
 */
export function estimateCost(
  shortId: string,
  inputTokens: number,
  outputTokens: number
): number | null {
  const model = MODEL_REGISTRY[shortId]
  if (!model?.pricing) return null

  const inputCost = (inputTokens / 1000) * model.pricing.inputPer1k
  const outputCost = (outputTokens / 1000) * model.pricing.outputPer1k

  return inputCost + outputCost
}

/**
 * Format cost as currency string
 */
export function formatCost(cost: number): string {
  if (cost < 0.01) {
    return `$${(cost * 1000).toFixed(2)}m` // millicents
  }
  return `$${cost.toFixed(4)}`
}

// ============================================================================
// Validation
// ============================================================================

/**
 * Check if a model ID is valid
 */
export function isValidModel(shortId: string): boolean {
  return shortId in MODEL_REGISTRY
}

/**
 * Get suggested model for a provider
 * Returns the most capable non-deprecated model
 */
export function getSuggestedModel(provider: LLMProvider): string | undefined {
  const models = getModelsForProvider(provider).filter((m) => !m.deprecated)

  // Sort by context window (larger is better)
  models.sort((a, b) => b.contextWindow - a.contextWindow)

  return models[0]
    ? Object.keys(MODEL_REGISTRY).find((k) => MODEL_REGISTRY[k] === models[0])
    : undefined
}
