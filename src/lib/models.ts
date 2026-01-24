/**
 * Delta9 Model Resolution
 *
 * Config-driven model selection with fallback chains.
 * Zero hardcoded model IDs - all models come from config.
 */

import type { Delta9Config, OracleConfig } from '../types/config.js'
import { loadConfig } from './config.js'

// =============================================================================
// Types
// =============================================================================

export type ModelRole =
  | 'commander'
  | 'operator'
  | 'validator'
  | 'patcher'
  | 'scout'
  | 'intel'
  | 'strategist'
  | 'ui_ops'
  | 'scribe'
  | 'optics'
  | 'qa'

export type SupportAgentType = 'scout' | 'intel' | 'strategist' | 'uiOps' | 'scribe' | 'optics' | 'qa'

export type TaskComplexity = 'simple' | 'standard' | 'complex'

// =============================================================================
// Model Resolution
// =============================================================================

/**
 * Get model for a specific role
 *
 * Uses config to determine the appropriate model based on role and complexity.
 */
export function getModelForRole(
  cwd: string,
  role: ModelRole,
  complexity: TaskComplexity = 'standard'
): string {
  const config = loadConfig(cwd)

  switch (role) {
    case 'commander':
      return config.commander.model

    case 'operator':
      return complexity === 'complex'
        ? config.operators.complexModel
        : config.operators.defaultModel

    case 'validator':
      return config.validator.model

    case 'patcher':
      return config.patcher.model

    case 'scout':
      return config.support.scout.model

    case 'intel':
      return config.support.intel.model

    case 'strategist':
      return config.support.strategist.model

    case 'ui_ops':
      return config.support.uiOps.model

    case 'scribe':
      return config.support.scribe.model

    case 'optics':
      return config.support.optics.model

    case 'qa':
      return config.support.qa.model

    default:
      return config.operators.defaultModel
  }
}

/**
 * Get oracle configurations for enabled oracles
 */
export function getEnabledOracleConfigs(cwd: string): OracleConfig[] {
  const config = loadConfig(cwd)

  if (!config.council.enabled) {
    return []
  }

  return config.council.members.filter((m) => m.enabled)
}

/**
 * Get support agent model
 *
 * Returns the specific model for a support agent, or falls back to operator model.
 */
export function getSupportAgentModel(cwd: string, agentType: SupportAgentType): string {
  const config = loadConfig(cwd)

  const modelMap: Record<SupportAgentType, string> = {
    scout: config.support.scout.model,
    intel: config.support.intel.model,
    strategist: config.support.strategist.model,
    uiOps: config.support.uiOps.model,
    scribe: config.support.scribe.model,
    optics: config.support.optics.model,
    qa: config.support.qa.model,
  }

  return modelMap[agentType] ?? config.operators.defaultModel
}

/**
 * Parse model ID into provider and model
 *
 * Format: "provider/model" (e.g., "anthropic/claude-sonnet-4")
 */
export function parseModelId(modelId: string): { provider: string; model: string } {
  const parts = modelId.split('/')
  if (parts.length !== 2) {
    throw new Error(`Invalid model ID format: ${modelId}. Expected "provider/model"`)
  }
  return {
    provider: parts[0],
    model: parts[1],
  }
}

/**
 * Build model ID from provider and model
 */
export function buildModelId(provider: string, model: string): string {
  return `${provider}/${model}`
}

// =============================================================================
// Model Fallback Resolution
// =============================================================================

/**
 * Define fallback chains for models
 *
 * When a preferred model is unavailable, try fallbacks in order.
 */
const DEFAULT_FALLBACKS: Record<string, string[]> = {
  // Claude models
  'anthropic/claude-opus-4-5': [
    'anthropic/claude-sonnet-4',
    'openai/gpt-4o',
    'google/gemini-2.0-pro',
  ],
  'anthropic/claude-sonnet-4': [
    'anthropic/claude-haiku-4',
    'openai/gpt-4o-mini',
    'google/gemini-2.0-flash',
  ],
  'anthropic/claude-haiku-4': [
    'google/gemini-2.0-flash',
    'openai/gpt-4o-mini',
  ],

  // OpenAI models
  'openai/gpt-4o': [
    'anthropic/claude-sonnet-4',
    'google/gemini-2.0-pro',
  ],
  'openai/gpt-4o-mini': [
    'anthropic/claude-haiku-4',
    'google/gemini-2.0-flash',
  ],

  // Google models
  'google/gemini-2.0-pro': [
    'anthropic/claude-sonnet-4',
    'openai/gpt-4o',
  ],
  'google/gemini-2.0-flash': [
    'anthropic/claude-haiku-4',
    'openai/gpt-4o-mini',
  ],
}

/**
 * Get fallback chain for a model
 */
export function getFallbackChain(modelId: string): string[] {
  return DEFAULT_FALLBACKS[modelId] ?? []
}

/**
 * Resolve model with fallback
 *
 * Checks if preferred model is available, otherwise tries fallbacks.
 * Returns the first available model.
 *
 * @param preferredModel - Preferred model ID
 * @param availableModels - List of available model IDs
 * @param fallbacks - Custom fallback chain (uses defaults if not provided)
 */
export function resolveModelWithFallback(
  preferredModel: string,
  availableModels: string[],
  fallbacks?: string[]
): string {
  // Check if preferred model is available
  if (availableModels.includes(preferredModel)) {
    return preferredModel
  }

  // Try fallbacks in order
  const fallbackChain = fallbacks ?? getFallbackChain(preferredModel)
  for (const fallback of fallbackChain) {
    if (availableModels.includes(fallback)) {
      return fallback
    }
  }

  // Last resort: return preferred model (let OpenCode handle the error)
  return preferredModel
}

// =============================================================================
// Council Mode Model Selection
// =============================================================================

/**
 * Get models for council based on mode
 */
export function getCouncilModels(
  cwd: string,
  mode: 'none' | 'quick' | 'standard' | 'xhigh'
): OracleConfig[] {
  const config = loadConfig(cwd)
  const enabledOracles = config.council.members.filter((m) => m.enabled)

  switch (mode) {
    case 'none':
      return []

    case 'quick':
      // Return first enabled oracle only
      return enabledOracles.slice(0, 1)

    case 'standard':
      // Return all enabled oracles
      return enabledOracles

    case 'xhigh':
      // Return all enabled oracles (could add more rigorous checks here)
      return enabledOracles

    default:
      return enabledOracles
  }
}

/**
 * Determine council mode based on task complexity
 */
export function autoDetectCouncilMode(
  complexity: TaskComplexity,
  config: Delta9Config
): 'none' | 'quick' | 'standard' | 'xhigh' {
  if (!config.council.enabled) {
    return 'none'
  }

  if (!config.council.autoDetectComplexity) {
    return config.council.defaultMode
  }

  switch (complexity) {
    case 'simple':
      return 'none'
    case 'standard':
      return 'quick'
    case 'complex':
      return 'standard'
    default:
      return config.council.defaultMode
  }
}
