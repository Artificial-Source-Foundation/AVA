/**
 * Delta9 Configuration Schema
 *
 * Zod schemas for validating configuration files.
 */

import { z } from 'zod'
import { DEFAULT_CONFIG } from '../types/config.js'

// =============================================================================
// Base Schemas
// =============================================================================

const modelSchema = z.string().min(1, 'Model name required')
const fallbacksSchema = z.array(z.string()).default([])
const temperatureSchema = z.number().min(0).max(2).default(0.7)

// =============================================================================
// Commander Schema
// =============================================================================

export const commanderConfigSchema = z.object({
  model: modelSchema.default(DEFAULT_CONFIG.commander.model),
  fallbacks: fallbacksSchema.default(DEFAULT_CONFIG.commander.fallbacks),
  temperature: temperatureSchema.default(DEFAULT_CONFIG.commander.temperature),
  dispatchModel: modelSchema.default(DEFAULT_CONFIG.commander.dispatchModel),
})

// =============================================================================
// Council Schema
// =============================================================================

export const councilModeSchema = z.enum(['none', 'quick', 'standard', 'xhigh'])

export const oracleSpecialtySchema = z.enum([
  'architecture',
  'logic',
  'ui',
  'performance',
  'security',
  'simplification',
  'innovation',
  'general',
])

// Thinking/reasoning configuration (ARCH-5)
export const thinkingConfigSchema = z
  .object({
    reasoningMode: z.enum(['standard', 'high', 'xhigh']).optional(),
    thinkingBudget: z.number().int().min(1000).max(128000).optional(),
    deepThink: z.boolean().optional(),
    triggerThinking: z.boolean().optional(),
  })
  .optional()

export const oracleConfigSchema = z.object({
  name: z.string().min(1),
  model: modelSchema,
  fallbacks: fallbacksSchema.default([]),
  enabled: z.boolean().default(true),
  specialty: oracleSpecialtySchema.default('general'),
  temperature: z.number().min(0).max(2).optional(),
  thinking: thinkingConfigSchema,
})

export const councilConfigSchema = z.object({
  enabled: z.boolean().default(DEFAULT_CONFIG.council.enabled),
  defaultMode: councilModeSchema.default(DEFAULT_CONFIG.council.defaultMode),
  autoDetectComplexity: z.boolean().default(DEFAULT_CONFIG.council.autoDetectComplexity),
  members: z.array(oracleConfigSchema).default(DEFAULT_CONFIG.council.members),
  parallel: z.boolean().default(DEFAULT_CONFIG.council.parallel),
  requireConsensus: z.boolean().default(DEFAULT_CONFIG.council.requireConsensus),
  minResponses: z.number().int().min(1).max(10).default(DEFAULT_CONFIG.council.minResponses),
  timeoutSeconds: z.number().int().min(10).max(600).default(DEFAULT_CONFIG.council.timeoutSeconds),
})

// =============================================================================
// Operator Schema (3-Tier Marine System)
// =============================================================================

export const operatorConfigSchema = z.object({
  // Tier 1: Marine Private (simple tasks)
  tier1Model: modelSchema.default(DEFAULT_CONFIG.operators.tier1Model),
  tier1Fallbacks: fallbacksSchema.default(DEFAULT_CONFIG.operators.tier1Fallbacks),
  tier1Thinking: thinkingConfigSchema,

  // Tier 2: Marine Sergeant (moderate tasks)
  tier2Model: modelSchema.default(DEFAULT_CONFIG.operators.tier2Model),
  tier2Fallbacks: fallbacksSchema.default(DEFAULT_CONFIG.operators.tier2Fallbacks),
  tier2Thinking: thinkingConfigSchema,

  // Tier 3: Delta Force (critical tasks)
  tier3Model: modelSchema.default(DEFAULT_CONFIG.operators.tier3Model),
  tier3Fallbacks: fallbacksSchema.default(DEFAULT_CONFIG.operators.tier3Fallbacks),
  tier3Thinking: thinkingConfigSchema,

  maxParallel: z.number().int().min(1).max(10).default(DEFAULT_CONFIG.operators.maxParallel),
  retryLimit: z.number().int().min(0).max(5).default(DEFAULT_CONFIG.operators.retryLimit),
  canInvokeSupport: z.boolean().default(DEFAULT_CONFIG.operators.canInvokeSupport),
})

// =============================================================================
// Validator Schema
// =============================================================================

export const validatorConfigSchema = z.object({
  model: modelSchema.default(DEFAULT_CONFIG.validator.model),
  fallbacks: fallbacksSchema.default(DEFAULT_CONFIG.validator.fallbacks),
  strictMode: z.boolean().default(DEFAULT_CONFIG.validator.strictMode),
  runTests: z.boolean().default(DEFAULT_CONFIG.validator.runTests),
  checkLinting: z.boolean().default(DEFAULT_CONFIG.validator.checkLinting),
})

// =============================================================================
// Patcher Schema
// =============================================================================

export const patcherConfigSchema = z.object({
  model: modelSchema.default(DEFAULT_CONFIG.patcher.model),
  fallbacks: fallbacksSchema.default(DEFAULT_CONFIG.patcher.fallbacks),
  maxLines: z.number().int().min(1).max(500).default(DEFAULT_CONFIG.patcher.maxLines),
})

// =============================================================================
// Support Agents Schema
// =============================================================================

export const scoutConfigSchema = z.object({
  model: modelSchema.default(DEFAULT_CONFIG.support.scout.model),
  fallbacks: fallbacksSchema.default(DEFAULT_CONFIG.support.scout.fallbacks),
  timeoutSeconds: z
    .number()
    .int()
    .min(5)
    .max(120)
    .default(DEFAULT_CONFIG.support.scout.timeoutSeconds),
})

export const intelSourceSchema = z.enum(['docs', 'github', 'web'])

export const intelConfigSchema = z.object({
  model: modelSchema.default(DEFAULT_CONFIG.support.intel.model),
  fallbacks: fallbacksSchema.default(DEFAULT_CONFIG.support.intel.fallbacks),
  sources: z.array(intelSourceSchema).default(DEFAULT_CONFIG.support.intel.sources),
})

export const invokeThresholdSchema = z.enum(['simple', 'moderate', 'complex'])

export const strategistConfigSchema = z.object({
  model: modelSchema.default(DEFAULT_CONFIG.support.strategist.model),
  fallbacks: fallbacksSchema.default(DEFAULT_CONFIG.support.strategist.fallbacks),
  invokeThreshold: invokeThresholdSchema.default(DEFAULT_CONFIG.support.strategist.invokeThreshold),
})

export const styleSystemSchema = z.enum(['tailwind', 'css', 'scss', 'styled-components'])

export const uiOpsConfigSchema = z.object({
  model: modelSchema.default(DEFAULT_CONFIG.support.uiOps.model),
  fallbacks: fallbacksSchema.default(DEFAULT_CONFIG.support.uiOps.fallbacks),
  styleSystem: styleSystemSchema.default(DEFAULT_CONFIG.support.uiOps.styleSystem),
})

export const docFormatSchema = z.enum(['markdown', 'jsdoc', 'tsdoc'])

export const scribeConfigSchema = z.object({
  model: modelSchema.default(DEFAULT_CONFIG.support.scribe.model),
  fallbacks: fallbacksSchema.default(DEFAULT_CONFIG.support.scribe.fallbacks),
  format: docFormatSchema.default(DEFAULT_CONFIG.support.scribe.format),
})

export const qaConfigSchema = z.object({
  model: modelSchema.default(DEFAULT_CONFIG.support.qa.model),
  fallbacks: fallbacksSchema.default(DEFAULT_CONFIG.support.qa.fallbacks),
  frameworkDetect: z.boolean().default(DEFAULT_CONFIG.support.qa.frameworkDetect),
})

// Note: SPECTRE (optics) removed - redundant with FACADE
export const supportConfigSchema = z.object({
  scout: scoutConfigSchema.default(DEFAULT_CONFIG.support.scout),
  intel: intelConfigSchema.default(DEFAULT_CONFIG.support.intel),
  strategist: strategistConfigSchema.default(DEFAULT_CONFIG.support.strategist),
  uiOps: uiOpsConfigSchema.default(DEFAULT_CONFIG.support.uiOps),
  scribe: scribeConfigSchema.default(DEFAULT_CONFIG.support.scribe),
  qa: qaConfigSchema.default(DEFAULT_CONFIG.support.qa),
})

// =============================================================================
// Mission Settings Schema
// =============================================================================

export const checkpointOnSchema = z.enum(['objective_complete', 'task_complete', 'never'])

export const missionSettingsSchema = z.object({
  autoCheckpoint: z.boolean().default(DEFAULT_CONFIG.mission.autoCheckpoint),
  checkpointOn: checkpointOnSchema.default(DEFAULT_CONFIG.mission.checkpointOn),
  stateDir: z.string().min(1).default(DEFAULT_CONFIG.mission.stateDir),
  historyEnabled: z.boolean().default(DEFAULT_CONFIG.mission.historyEnabled),
})

// =============================================================================
// Memory Schema
// =============================================================================

export const memoryConfigSchema = z.object({
  enabled: z.boolean().default(DEFAULT_CONFIG.memory.enabled),
  learnFromFailures: z.boolean().default(DEFAULT_CONFIG.memory.learnFromFailures),
  learnFromSuccesses: z.boolean().default(DEFAULT_CONFIG.memory.learnFromSuccesses),
  maxEntries: z.number().int().min(10).max(10000).default(DEFAULT_CONFIG.memory.maxEntries),
})

// =============================================================================
// Budget Schema
// =============================================================================

export const budgetConfigSchema = z.object({
  enabled: z.boolean().default(DEFAULT_CONFIG.budget.enabled),
  defaultLimit: z.number().min(0.01).max(1000).default(DEFAULT_CONFIG.budget.defaultLimit),
  warnAt: z.number().min(0).max(1).default(DEFAULT_CONFIG.budget.warnAt),
  pauseAt: z.number().min(0).max(1).default(DEFAULT_CONFIG.budget.pauseAt),
  hardLimitAt: z.number().min(0).max(2).default(DEFAULT_CONFIG.budget.hardLimitAt),
  trackByAgent: z.boolean().default(DEFAULT_CONFIG.budget.trackByAgent),
})

// =============================================================================
// Notification Schema
// =============================================================================

export const notificationEventSchema = z.enum([
  'mission_complete',
  'validation_failed',
  'budget_warning',
  'needs_input',
])

export const notificationConfigSchema = z.object({
  enabled: z.boolean().default(DEFAULT_CONFIG.notifications.enabled),
  discordWebhook: z.string().url().nullable().default(DEFAULT_CONFIG.notifications.discordWebhook),
  slackWebhook: z.string().url().nullable().default(DEFAULT_CONFIG.notifications.slackWebhook),
  onEvents: z.array(notificationEventSchema).default(DEFAULT_CONFIG.notifications.onEvents),
})

// =============================================================================
// UI Schema
// =============================================================================

export const uiConfigSchema = z.object({
  showProgress: z.boolean().default(DEFAULT_CONFIG.ui.showProgress),
  showCost: z.boolean().default(DEFAULT_CONFIG.ui.showCost),
  verboseLogs: z.boolean().default(DEFAULT_CONFIG.ui.verboseLogs),
})

// =============================================================================
// Seamless Schema
// =============================================================================

export const keywordsSchema = z.object({
  councilXhigh: z.array(z.string()).default(DEFAULT_CONFIG.seamless.keywords.councilXhigh),
  councilNone: z.array(z.string()).default(DEFAULT_CONFIG.seamless.keywords.councilNone),
  forcePlan: z.array(z.string()).default(DEFAULT_CONFIG.seamless.keywords.forcePlan),
})

export const seamlessConfigSchema = z.object({
  replaceBuild: z.boolean().default(DEFAULT_CONFIG.seamless.replaceBuild),
  replacePlan: z.boolean().default(DEFAULT_CONFIG.seamless.replacePlan),
  keywordDetection: z.boolean().default(DEFAULT_CONFIG.seamless.keywordDetection),
  keywords: keywordsSchema.default(DEFAULT_CONFIG.seamless.keywords),
})

// =============================================================================
// Full Config Schema
// =============================================================================

export const delta9ConfigSchema = z.object({
  commander: commanderConfigSchema.default(DEFAULT_CONFIG.commander),
  council: councilConfigSchema.default(DEFAULT_CONFIG.council),
  operators: operatorConfigSchema.default(DEFAULT_CONFIG.operators),
  validator: validatorConfigSchema.default(DEFAULT_CONFIG.validator),
  patcher: patcherConfigSchema.default(DEFAULT_CONFIG.patcher),
  support: supportConfigSchema.default(DEFAULT_CONFIG.support),
  mission: missionSettingsSchema.default(DEFAULT_CONFIG.mission),
  memory: memoryConfigSchema.default(DEFAULT_CONFIG.memory),
  budget: budgetConfigSchema.default(DEFAULT_CONFIG.budget),
  notifications: notificationConfigSchema.default(DEFAULT_CONFIG.notifications),
  ui: uiConfigSchema.default(DEFAULT_CONFIG.ui),
  seamless: seamlessConfigSchema.default(DEFAULT_CONFIG.seamless),
})

// =============================================================================
// Type Exports
// =============================================================================

export type ConfigSchema = z.infer<typeof delta9ConfigSchema>

// =============================================================================
// Validation Helper
// =============================================================================

export function validateConfig(config: unknown): ConfigSchema {
  return delta9ConfigSchema.parse(config)
}

export function validateConfigSafe(
  config: unknown
): { success: true; data: ConfigSchema } | { success: false; error: z.ZodError } {
  const result = delta9ConfigSchema.safeParse(config)
  return result
}
