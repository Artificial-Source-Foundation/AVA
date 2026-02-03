/**
 * Model Types
 * Type definitions for model configuration and capabilities
 */

import type { LLMProvider } from '../types/llm.js'

// ============================================================================
// Capability Types
// ============================================================================

/**
 * Model capability flags
 */
export interface ModelCapabilities {
  /** Supports tool/function calling */
  tools: boolean
  /** Supports image/vision input */
  vision: boolean
  /** Supports streaming responses */
  streaming: boolean
  /** Supports JSON mode output */
  json: boolean
  /** Supports structured outputs (beyond JSON mode) */
  structuredOutput?: boolean
  /** Supports extended thinking/reasoning */
  thinking?: boolean
  /** Supports computer use (screen interaction) */
  computerUse?: boolean
  /** Supports code execution */
  codeExecution?: boolean
}

/**
 * Model pricing information
 */
export interface ModelPricing {
  /** Cost per 1k input tokens (USD) */
  inputPer1k: number
  /** Cost per 1k output tokens (USD) */
  outputPer1k: number
  /** Cost per 1k cache read tokens (USD) */
  cacheReadPer1k?: number
  /** Cost per 1k cache write tokens (USD) */
  cacheWritePer1k?: number
}

// ============================================================================
// Model Configuration
// ============================================================================

/**
 * Complete model configuration
 */
export interface ModelConfig {
  /** Model ID used in API calls */
  id: string
  /** Provider for this model */
  provider: LLMProvider
  /** Human-readable display name */
  displayName: string
  /** Context window size (input tokens) */
  contextWindow: number
  /** Maximum output tokens */
  maxOutputTokens: number
  /** Model capabilities */
  capabilities: ModelCapabilities
  /** Pricing (if known) */
  pricing?: ModelPricing
  /** Whether this model is deprecated */
  deprecated?: boolean
  /** Suggested replacement model */
  replacement?: string
  /** Release date */
  releaseDate?: string
  /** Short description */
  description?: string
}

/**
 * Model family for grouping related models
 */
export type ModelFamily =
  | 'claude'
  | 'gpt'
  | 'gemini'
  | 'llama'
  | 'mistral'
  | 'deepseek'
  | 'kimi'
  | 'glm'

/**
 * Model tier for capability grouping
 */
export type ModelTier = 'flagship' | 'standard' | 'fast' | 'vision'

// ============================================================================
// Query Types
// ============================================================================

/**
 * Filter criteria for finding models
 */
export interface ModelFilter {
  /** Filter by provider */
  provider?: LLMProvider
  /** Filter by capability */
  capability?: keyof ModelCapabilities
  /** Minimum context window */
  minContext?: number
  /** Maximum price per 1k output tokens */
  maxOutputPrice?: number
  /** Exclude deprecated models */
  excludeDeprecated?: boolean
}

/**
 * Sort order for model lists
 */
export type ModelSort = 'name' | 'context' | 'price' | 'release' | 'capability'
