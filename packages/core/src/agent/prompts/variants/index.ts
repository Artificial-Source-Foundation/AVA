/**
 * Prompt Variants
 * Model-specific prompt customizations
 */

import { claudeVariant } from './claude.js'
import { geminiVariant } from './gemini.js'
import { genericVariant } from './generic.js'
import { gptVariant } from './gpt.js'
import { detectPromptModelFamily, type PromptModelFamily, type PromptVariant } from './types.js'

// Re-export variants
export { claudeVariant } from './claude.js'
export { geminiVariant } from './gemini.js'
export { genericVariant } from './generic.js'
export { gptVariant } from './gpt.js'
// Re-export types
export {
  detectPromptModelFamily,
  getRecommendedVerbosity,
  type PromptModelFamily,
  type PromptVariant,
  type PromptVerbosity,
} from './types.js'

// ============================================================================
// Variant Registry
// ============================================================================

/**
 * Registry of prompt variants by model family
 */
const variantRegistry: Record<PromptModelFamily, PromptVariant> = {
  claude: claudeVariant,
  gpt: gptVariant,
  gemini: geminiVariant,
  llama: genericVariant, // Llama uses generic for now
  mistral: genericVariant, // Mistral uses generic for now
  generic: genericVariant,
}

/**
 * Get the prompt variant for a model family
 */
export function getVariant(family: PromptModelFamily): PromptVariant {
  return variantRegistry[family] ?? genericVariant
}

/**
 * Get the prompt variant for a model ID
 */
export function getVariantForModel(modelId: string): PromptVariant {
  const family = detectPromptModelFamily(modelId)
  return getVariant(family)
}

/**
 * Build a system prompt for a specific model
 */
export function buildSystemPromptForModel(
  modelId: string,
  context: import('../system.js').SystemPromptContext
): string {
  const variant = getVariantForModel(modelId)
  return variant.buildSystemPrompt(context)
}

/**
 * Build a worker prompt for a specific model
 */
export function buildWorkerPromptForModel(
  modelId: string,
  context: import('../system.js').SystemPromptContext
): string {
  const variant = getVariantForModel(modelId)
  return variant.buildWorkerPrompt(context)
}
