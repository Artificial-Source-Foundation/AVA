/**
 * Cost Optimizer Types
 *
 * Auto-select models based on budget and task complexity.
 */

import { z } from 'zod'

// =============================================================================
// Model Cost Profile
// =============================================================================

export const modelCostProfileSchema = z.object({
  modelId: z.string(),
  provider: z.string(),
  inputCostPer1k: z.number().min(0), // $ per 1k input tokens
  outputCostPer1k: z.number().min(0), // $ per 1k output tokens
  averageLatency: z.number().min(0), // ms
  qualityScore: z.number().min(0).max(100), // 0-100 quality rating
  capabilities: z.array(z.string()), // e.g., ['coding', 'reasoning', 'vision']
  maxContextTokens: z.number().int().positive(),
  tier: z.enum(['budget', 'standard', 'premium', 'flagship']),
})

export type ModelCostProfile = z.infer<typeof modelCostProfileSchema>

// =============================================================================
// Cost Optimizer Configuration
// =============================================================================

export const optimizerConfigSchema = z.object({
  /** Enable cost optimization */
  enabled: z.boolean().default(true),
  /** Budget mode: 'strict' (never exceed), 'soft' (warn but allow), 'none' */
  budgetMode: z.enum(['strict', 'soft', 'none']).default('soft'),
  /** Quality threshold (0-100) - minimum acceptable quality */
  minQualityThreshold: z.number().min(0).max(100).default(60),
  /** Latency threshold in ms - switch to faster model if exceeded */
  maxLatencyThreshold: z.number().min(100).max(60000).default(10000),
  /** Prefer cached/local models when available */
  preferCached: z.boolean().default(true),
  /** Auto-downgrade model if budget is low */
  autoDowngrade: z.boolean().default(true),
  /** Downgrade threshold (0-1) - budget percentage to trigger downgrade */
  downgradeThreshold: z.number().min(0).max(1).default(0.8),
})

export type OptimizerConfig = z.infer<typeof optimizerConfigSchema>

// =============================================================================
// Task Requirements
// =============================================================================

export const taskRequirementsSchema = z.object({
  /** Task complexity */
  complexity: z.enum(['trivial', 'low', 'medium', 'high', 'critical']),
  /** Required capabilities */
  requiredCapabilities: z.array(z.string()).default([]),
  /** Estimated input tokens */
  estimatedInputTokens: z.number().int().positive().default(1000),
  /** Estimated output tokens */
  estimatedOutputTokens: z.number().int().positive().default(500),
  /** Priority (affects model selection) */
  priority: z.enum(['low', 'normal', 'high', 'critical']).default('normal'),
  /** Maximum acceptable latency */
  maxLatency: z.number().optional(),
  /** Minimum quality required */
  minQuality: z.number().min(0).max(100).optional(),
})

export type TaskRequirements = z.infer<typeof taskRequirementsSchema>

// =============================================================================
// Optimization Result
// =============================================================================

export interface OptimizationResult {
  selectedModel: string
  estimatedCost: number
  estimatedLatency: number
  qualityScore: number
  reason: string
  alternatives: Array<{
    model: string
    cost: number
    quality: number
    reason: string
  }>
  warnings: string[]
  isDowngraded: boolean
}

// =============================================================================
// Budget Status
// =============================================================================

export interface BudgetStatus {
  totalBudget: number
  spent: number
  remaining: number
  percentUsed: number
  projectedTotal: number
  isOverBudget: boolean
  shouldDowngrade: boolean
  recommendedTier: 'budget' | 'standard' | 'premium' | 'flagship'
}

// =============================================================================
// Default Model Profiles
// =============================================================================

export const DEFAULT_MODEL_PROFILES: ModelCostProfile[] = [
  // Anthropic
  {
    modelId: 'anthropic/claude-opus-4-5',
    provider: 'anthropic',
    inputCostPer1k: 0.015,
    outputCostPer1k: 0.075,
    averageLatency: 8000,
    qualityScore: 98,
    capabilities: ['coding', 'reasoning', 'analysis', 'creative'],
    maxContextTokens: 200000,
    tier: 'flagship',
  },
  {
    modelId: 'anthropic/claude-sonnet-4-5',
    provider: 'anthropic',
    inputCostPer1k: 0.003,
    outputCostPer1k: 0.015,
    averageLatency: 3000,
    qualityScore: 90,
    capabilities: ['coding', 'reasoning', 'analysis'],
    maxContextTokens: 200000,
    tier: 'premium',
  },
  {
    modelId: 'anthropic/claude-haiku-4',
    provider: 'anthropic',
    inputCostPer1k: 0.00025,
    outputCostPer1k: 0.00125,
    averageLatency: 1000,
    qualityScore: 75,
    capabilities: ['coding', 'reasoning'],
    maxContextTokens: 200000,
    tier: 'budget',
  },
  // OpenAI
  {
    modelId: 'openai/gpt-4o',
    provider: 'openai',
    inputCostPer1k: 0.005,
    outputCostPer1k: 0.015,
    averageLatency: 4000,
    qualityScore: 92,
    capabilities: ['coding', 'reasoning', 'vision', 'analysis'],
    maxContextTokens: 128000,
    tier: 'premium',
  },
  {
    modelId: 'openai/gpt-4o-mini',
    provider: 'openai',
    inputCostPer1k: 0.00015,
    outputCostPer1k: 0.0006,
    averageLatency: 1500,
    qualityScore: 70,
    capabilities: ['coding', 'reasoning'],
    maxContextTokens: 128000,
    tier: 'budget',
  },
  // Google
  {
    modelId: 'google/gemini-2.0-flash',
    provider: 'google',
    inputCostPer1k: 0.0001,
    outputCostPer1k: 0.0004,
    averageLatency: 800,
    qualityScore: 80,
    capabilities: ['coding', 'reasoning', 'vision'],
    maxContextTokens: 1000000,
    tier: 'standard',
  },
  // DeepSeek
  {
    modelId: 'deepseek/deepseek-chat',
    provider: 'deepseek',
    inputCostPer1k: 0.00014,
    outputCostPer1k: 0.00028,
    averageLatency: 2000,
    qualityScore: 82,
    capabilities: ['coding', 'reasoning', 'analysis'],
    maxContextTokens: 64000,
    tier: 'budget',
  },
]

// =============================================================================
// Default Configuration
// =============================================================================

export const DEFAULT_OPTIMIZER_CONFIG: OptimizerConfig = {
  enabled: true,
  budgetMode: 'soft',
  minQualityThreshold: 60,
  maxLatencyThreshold: 10000,
  preferCached: true,
  autoDowngrade: true,
  downgradeThreshold: 0.8,
}
