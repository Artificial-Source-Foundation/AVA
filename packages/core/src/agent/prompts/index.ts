/**
 * Agent Prompts
 * System prompts and prompt builders for the agent
 */

export {
  BEST_PRACTICES,
  buildScenarioPrompt,
  buildSystemPrompt,
  buildWorkerPrompt,
  CAPABILITIES,
  getModelAdjustments,
  RULES,
  type SystemPromptContext,
} from './system.js'

// Model-specific variants
export {
  buildSystemPromptForModel,
  buildWorkerPromptForModel,
  claudeVariant,
  detectPromptModelFamily,
  geminiVariant,
  genericVariant,
  getRecommendedVerbosity,
  getVariant,
  getVariantForModel,
  gptVariant,
  type PromptModelFamily,
  type PromptVariant,
  type PromptVerbosity,
} from './variants/index.js'
