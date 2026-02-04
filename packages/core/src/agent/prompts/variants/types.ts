/**
 * Prompt Variant Types
 * Type definitions for model-specific prompt variants
 */

import type { SystemPromptContext } from '../system.js'

// ============================================================================
// Model Family Detection
// ============================================================================

/**
 * Supported model families for prompt customization
 */
export type PromptModelFamily = 'claude' | 'gpt' | 'gemini' | 'llama' | 'mistral' | 'generic'

/**
 * Detect model family from model ID
 */
export function detectPromptModelFamily(modelId: string): PromptModelFamily {
  const lower = modelId.toLowerCase()

  // Claude models
  if (lower.includes('claude') || lower.includes('anthropic')) {
    return 'claude'
  }

  // GPT/OpenAI models
  if (lower.includes('gpt') || lower.includes('o1') || lower.includes('openai')) {
    return 'gpt'
  }

  // Gemini/Google models
  if (lower.includes('gemini') || lower.includes('palm') || lower.includes('google')) {
    return 'gemini'
  }

  // Llama models (Meta, various providers)
  if (lower.includes('llama') || lower.includes('meta')) {
    return 'llama'
  }

  // Mistral models
  if (lower.includes('mistral') || lower.includes('mixtral')) {
    return 'mistral'
  }

  return 'generic'
}

// ============================================================================
// Prompt Variant Interface
// ============================================================================

/**
 * A prompt variant provides model-specific customizations
 */
export interface PromptVariant {
  /** Model family this variant is for */
  family: PromptModelFamily

  /**
   * Get the rules section for this model family
   * @param context - System prompt context
   * @returns Rules section as string
   */
  getRules(context: SystemPromptContext): string

  /**
   * Get the capabilities section for this model family
   * @param context - System prompt context
   * @returns Capabilities section as string
   */
  getCapabilities(context: SystemPromptContext): string

  /**
   * Get the full system prompt for this model family
   * @param context - System prompt context
   * @returns Complete system prompt
   */
  buildSystemPrompt(context: SystemPromptContext): string

  /**
   * Get a minimal worker prompt for subagents
   * @param context - System prompt context
   * @returns Worker system prompt
   */
  buildWorkerPrompt(context: SystemPromptContext): string

  /**
   * Get any model-specific notes or adjustments
   * @returns Model-specific notes
   */
  getModelNotes(): string
}

// ============================================================================
// Prompt Verbosity Levels
// ============================================================================

/**
 * How verbose should the prompt be
 */
export type PromptVerbosity = 'minimal' | 'standard' | 'full'

/**
 * Get recommended verbosity for a model family
 */
export function getRecommendedVerbosity(family: PromptModelFamily): PromptVerbosity {
  switch (family) {
    case 'claude':
      return 'full' // Claude handles detailed prompts well
    case 'gpt':
      return 'standard' // GPT prefers concise but complete
    case 'gemini':
      return 'standard' // Gemini works well with moderate detail
    case 'llama':
      return 'standard' // Llama needs clear structure
    case 'mistral':
      return 'standard' // Mistral handles moderate detail
    default:
      return 'minimal' // Generic and unknown models get minimal to avoid issues
  }
}
