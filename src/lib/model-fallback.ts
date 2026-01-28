/**
 * Delta9 Model Fallback Chains
 *
 * Intelligent model fallback system with:
 * - Provider health tracking
 * - Circuit breaker pattern
 * - Quality-tier preservation
 * - Cost-aware selection
 * - Automatic recovery
 */

import { loadConfig } from './config.js'

// =============================================================================
// Types
// =============================================================================

/** Model quality tier (higher = better) */
export type QualityTier = 'premium' | 'standard' | 'economy'

/** Provider status */
export type ProviderStatus = 'healthy' | 'degraded' | 'unavailable'

/** Model definition */
export interface ModelDefinition {
  /** Model ID (e.g., "anthropic/claude-opus-4-5") */
  id: string
  /** Provider name */
  provider: string
  /** Quality tier */
  tier: QualityTier
  /** Cost per 1M tokens (approximate) */
  costPer1M: number
  /** Context window size */
  contextWindow: number
  /** Capabilities */
  capabilities: ('coding' | 'reasoning' | 'vision' | 'function_calling')[]
}

/** Provider health record */
export interface ProviderHealth {
  /** Provider name */
  provider: string
  /** Current status */
  status: ProviderStatus
  /** Consecutive failures */
  failures: number
  /** Last failure timestamp */
  lastFailure?: Date
  /** Last success timestamp */
  lastSuccess?: Date
  /** Circuit breaker open until */
  circuitOpenUntil?: Date
  /** Total requests */
  totalRequests: number
  /** Successful requests */
  successfulRequests: number
}

/** Fallback chain configuration */
export interface FallbackChainConfig {
  /** Maximum retries before giving up */
  maxRetries: number
  /** Circuit breaker threshold (failures before opening) */
  circuitBreakerThreshold: number
  /** Circuit breaker reset time (ms) */
  circuitBreakerResetMs: number
  /** Preserve quality tier (only fallback to same or better tier) */
  preserveQualityTier: boolean
  /** Prefer lower cost fallbacks */
  preferLowerCost: boolean
  /** Required capabilities (filter fallbacks) */
  requiredCapabilities?: ('coding' | 'reasoning' | 'vision' | 'function_calling')[]
  /** Logger */
  log?: (level: string, message: string, data?: Record<string, unknown>) => void
}

/** Fallback selection result */
export interface FallbackSelection {
  /** Selected model */
  model: ModelDefinition
  /** Why this model was selected */
  reason: string
  /** Position in fallback chain */
  position: number
  /** Alternatives that were skipped */
  skipped: { model: string; reason: string }[]
}

/** Execution result */
export interface FallbackExecutionResult<T> {
  /** Whether execution succeeded */
  success: boolean
  /** Result if successful */
  result?: T
  /** Error if failed */
  error?: Error
  /** Model used for successful execution */
  usedModel?: string
  /** Models attempted */
  attemptedModels: string[]
  /** Total attempts */
  totalAttempts: number
  /** Total delay (ms) */
  totalDelayMs: number
  /** Chain exhausted */
  chainExhausted: boolean
}

// =============================================================================
// Model Registry
// =============================================================================

/** Known models with their definitions */
export const MODEL_REGISTRY: Record<string, ModelDefinition> = {
  // Anthropic
  'anthropic/claude-opus-4-5': {
    id: 'anthropic/claude-opus-4-5',
    provider: 'anthropic',
    tier: 'premium',
    costPer1M: 15.0,
    contextWindow: 200000,
    capabilities: ['coding', 'reasoning', 'vision', 'function_calling'],
  },
  'anthropic/claude-sonnet-4-5': {
    id: 'anthropic/claude-sonnet-4-5',
    provider: 'anthropic',
    tier: 'standard',
    costPer1M: 3.0,
    contextWindow: 200000,
    capabilities: ['coding', 'reasoning', 'vision', 'function_calling'],
  },
  'anthropic/claude-haiku-4': {
    id: 'anthropic/claude-haiku-4',
    provider: 'anthropic',
    tier: 'economy',
    costPer1M: 0.25,
    contextWindow: 200000,
    capabilities: ['coding', 'reasoning', 'function_calling'],
  },

  // OpenAI
  'openai/gpt-4o': {
    id: 'openai/gpt-4o',
    provider: 'openai',
    tier: 'standard',
    costPer1M: 5.0,
    contextWindow: 128000,
    capabilities: ['coding', 'reasoning', 'vision', 'function_calling'],
  },
  'openai/gpt-4o-mini': {
    id: 'openai/gpt-4o-mini',
    provider: 'openai',
    tier: 'economy',
    costPer1M: 0.15,
    contextWindow: 128000,
    capabilities: ['coding', 'reasoning', 'vision', 'function_calling'],
  },
  'openai/o1': {
    id: 'openai/o1',
    provider: 'openai',
    tier: 'premium',
    costPer1M: 15.0,
    contextWindow: 128000,
    capabilities: ['coding', 'reasoning'],
  },

  // Google
  'google/gemini-2.0-pro': {
    id: 'google/gemini-2.0-pro',
    provider: 'google',
    tier: 'standard',
    costPer1M: 3.5,
    contextWindow: 1000000,
    capabilities: ['coding', 'reasoning', 'vision', 'function_calling'],
  },
  'google/gemini-2.0-flash': {
    id: 'google/gemini-2.0-flash',
    provider: 'google',
    tier: 'economy',
    costPer1M: 0.1,
    contextWindow: 1000000,
    capabilities: ['coding', 'reasoning', 'vision', 'function_calling'],
  },

  // DeepSeek
  'deepseek/deepseek-chat': {
    id: 'deepseek/deepseek-chat',
    provider: 'deepseek',
    tier: 'economy',
    costPer1M: 0.14,
    contextWindow: 64000,
    capabilities: ['coding', 'reasoning', 'function_calling'],
  },
}

/** Fallback chains by model */
export const FALLBACK_CHAINS: Record<string, string[]> = {
  // Premium tier chains
  'anthropic/claude-opus-4-5': [
    'openai/o1',
    'anthropic/claude-sonnet-4-5',
    'openai/gpt-4o',
    'google/gemini-2.0-pro',
  ],
  'openai/o1': ['anthropic/claude-opus-4-5', 'anthropic/claude-sonnet-4-5', 'openai/gpt-4o'],

  // Standard tier chains
  'anthropic/claude-sonnet-4-5': [
    'openai/gpt-4o',
    'google/gemini-2.0-pro',
    'anthropic/claude-haiku-4',
    'deepseek/deepseek-chat',
  ],
  'openai/gpt-4o': [
    'anthropic/claude-sonnet-4-5',
    'google/gemini-2.0-pro',
    'openai/gpt-4o-mini',
    'anthropic/claude-haiku-4',
  ],
  'google/gemini-2.0-pro': [
    'anthropic/claude-sonnet-4-5',
    'openai/gpt-4o',
    'google/gemini-2.0-flash',
    'anthropic/claude-haiku-4',
  ],

  // Economy tier chains
  'anthropic/claude-haiku-4': [
    'openai/gpt-4o-mini',
    'google/gemini-2.0-flash',
    'deepseek/deepseek-chat',
  ],
  'openai/gpt-4o-mini': [
    'anthropic/claude-haiku-4',
    'google/gemini-2.0-flash',
    'deepseek/deepseek-chat',
  ],
  'google/gemini-2.0-flash': [
    'openai/gpt-4o-mini',
    'anthropic/claude-haiku-4',
    'deepseek/deepseek-chat',
  ],
  'deepseek/deepseek-chat': [
    'anthropic/claude-haiku-4',
    'google/gemini-2.0-flash',
    'openai/gpt-4o-mini',
  ],
}

// =============================================================================
// Oracle & Agent Fallback Chains
// =============================================================================

// Oracle and Agent fallbacks are loaded dynamically from config
// See getOracleFallbackChain() and getAgentFallbackChain() below

/**
 * Get fallback chain for an Oracle
 *
 * Loads fallback configuration from the project config file.
 *
 * @param oracleName - Oracle codename (CIPHER, VECTOR, PRISM, APEX)
 * @param cwd - Working directory to load config from (defaults to process.cwd())
 * @returns Fallback model chain or empty array if unknown/not configured
 */
export function getOracleFallbackChain(oracleName: string, cwd?: string): string[] {
  try {
    const config = loadConfig(cwd || process.cwd())
    const oracle = config.council.members.find(
      (m) => m.name.toUpperCase() === oracleName.toUpperCase()
    )
    return oracle?.fallbacks || []
  } catch {
    return []
  }
}

/**
 * Get fallback chain for a Delta Team agent
 *
 * Loads fallback configuration from the project config file.
 * Maps agent codenames to their config locations:
 * - RECON -> support.scout
 * - SIGINT -> support.intel
 * - TACCOM -> support.strategist
 * - SURGEON -> patcher
 * - SENTINEL -> support.qa
 * - SCRIBE -> support.scribe
 * - FACADE -> support.uiOps
 * - SPECTRE -> support.optics
 *
 * @param agentName - Agent codename (RECON, SIGINT, TACCOM, etc.)
 * @param cwd - Working directory to load config from (defaults to process.cwd())
 * @returns Fallback model chain or empty array if unknown/not configured
 */
export function getAgentFallbackChain(agentName: string, cwd?: string): string[] {
  try {
    const config = loadConfig(cwd || process.cwd())
    const agentMap: Record<string, string[] | undefined> = {
      RECON: (config.support.scout as { fallbacks?: string[] }).fallbacks,
      SIGINT: (config.support.intel as { fallbacks?: string[] }).fallbacks,
      TACCOM: (config.support.strategist as { fallbacks?: string[] }).fallbacks,
      SURGEON: (config.patcher as { fallbacks?: string[] }).fallbacks,
      SENTINEL: (config.support.qa as { fallbacks?: string[] }).fallbacks,
      SCRIBE: (config.support.scribe as { fallbacks?: string[] }).fallbacks,
      FACADE: (config.support.uiOps as { fallbacks?: string[] }).fallbacks,
    }
    return agentMap[agentName.toUpperCase()] || []
  } catch {
    return []
  }
}

// =============================================================================
// Fallback Chain Manager
// =============================================================================

export class FallbackChainManager {
  private config: FallbackChainConfig
  private providerHealth: Map<string, ProviderHealth> = new Map()

  constructor(config?: Partial<FallbackChainConfig>) {
    this.config = {
      maxRetries: config?.maxRetries ?? 3,
      circuitBreakerThreshold: config?.circuitBreakerThreshold ?? 3,
      circuitBreakerResetMs: config?.circuitBreakerResetMs ?? 60000,
      preserveQualityTier: config?.preserveQualityTier ?? false,
      preferLowerCost: config?.preferLowerCost ?? true,
      requiredCapabilities: config?.requiredCapabilities,
      log: config?.log,
    }
  }

  // ===========================================================================
  // Provider Health
  // ===========================================================================

  /**
   * Get or create provider health record
   */
  private getProviderHealth(provider: string): ProviderHealth {
    let health = this.providerHealth.get(provider)
    if (!health) {
      health = {
        provider,
        status: 'healthy',
        failures: 0,
        totalRequests: 0,
        successfulRequests: 0,
      }
      this.providerHealth.set(provider, health)
    }
    return health
  }

  /**
   * Record success for a provider
   */
  recordSuccess(provider: string): void {
    const health = this.getProviderHealth(provider)
    health.failures = 0
    health.lastSuccess = new Date()
    health.totalRequests++
    health.successfulRequests++
    health.status = 'healthy'
    health.circuitOpenUntil = undefined

    if (this.config.log) {
      this.config.log('debug', `Provider ${provider} success recorded`, {
        status: health.status,
        successRate: this.getSuccessRate(provider),
      })
    }
  }

  /**
   * Record failure for a provider
   */
  recordFailure(provider: string): void {
    const health = this.getProviderHealth(provider)
    health.failures++
    health.lastFailure = new Date()
    health.totalRequests++

    // Update status based on failure count
    if (health.failures >= this.config.circuitBreakerThreshold) {
      health.status = 'unavailable'
      health.circuitOpenUntil = new Date(Date.now() + this.config.circuitBreakerResetMs)

      if (this.config.log) {
        this.config.log('warn', `Circuit breaker opened for ${provider}`, {
          failures: health.failures,
          resetAt: health.circuitOpenUntil,
        })
      }
    } else if (health.failures >= Math.floor(this.config.circuitBreakerThreshold / 2)) {
      health.status = 'degraded'
    }
  }

  /**
   * Check if provider is available
   */
  isProviderAvailable(provider: string): boolean {
    const health = this.getProviderHealth(provider)

    // Check circuit breaker
    if (health.circuitOpenUntil) {
      if (new Date() < health.circuitOpenUntil) {
        return false
      }
      // Circuit breaker has reset - try again
      health.circuitOpenUntil = undefined
      health.failures = 0
      health.status = 'healthy'
    }

    return health.status !== 'unavailable'
  }

  /**
   * Get provider status
   */
  getProviderStatus(provider: string): ProviderStatus {
    const health = this.getProviderHealth(provider)

    // Check if circuit breaker has reset
    if (health.circuitOpenUntil && new Date() >= health.circuitOpenUntil) {
      health.circuitOpenUntil = undefined
      health.failures = 0
      health.status = 'healthy'
    }

    return health.status
  }

  /**
   * Get success rate for a provider
   */
  getSuccessRate(provider: string): number {
    const health = this.getProviderHealth(provider)
    if (health.totalRequests === 0) return 1.0
    return health.successfulRequests / health.totalRequests
  }

  /**
   * Get all provider health statuses
   */
  getAllProviderHealth(): ProviderHealth[] {
    return Array.from(this.providerHealth.values())
  }

  /**
   * Reset provider health (for testing)
   */
  resetProviderHealth(provider?: string): void {
    if (provider) {
      this.providerHealth.delete(provider)
    } else {
      this.providerHealth.clear()
    }
  }

  // ===========================================================================
  // Fallback Selection
  // ===========================================================================

  /**
   * Get model definition
   */
  getModelDefinition(modelId: string): ModelDefinition | undefined {
    return MODEL_REGISTRY[modelId]
  }

  /**
   * Get fallback chain for a model
   */
  getFallbackChain(modelId: string): string[] {
    return FALLBACK_CHAINS[modelId] || []
  }

  /**
   * Select next fallback model
   */
  selectFallback(
    primaryModel: string,
    attemptedModels: Set<string> = new Set()
  ): FallbackSelection | null {
    const primary = this.getModelDefinition(primaryModel)
    const chain = this.getFallbackChain(primaryModel)
    const skipped: { model: string; reason: string }[] = []

    // Quality tier ordering
    const tierOrder: Record<QualityTier, number> = {
      premium: 3,
      standard: 2,
      economy: 1,
    }

    // Filter and sort candidates
    const candidates = chain
      .filter((modelId) => {
        const model = this.getModelDefinition(modelId)
        if (!model) {
          skipped.push({ model: modelId, reason: 'Unknown model' })
          return false
        }

        // Skip already attempted
        if (attemptedModels.has(modelId)) {
          skipped.push({ model: modelId, reason: 'Already attempted' })
          return false
        }

        // Check provider availability
        if (!this.isProviderAvailable(model.provider)) {
          skipped.push({ model: modelId, reason: 'Provider unavailable' })
          return false
        }

        // Check quality tier preservation
        if (this.config.preserveQualityTier && primary) {
          if (tierOrder[model.tier] < tierOrder[primary.tier]) {
            skipped.push({ model: modelId, reason: 'Lower quality tier' })
            return false
          }
        }

        // Check required capabilities
        if (this.config.requiredCapabilities) {
          const hasAllCapabilities = this.config.requiredCapabilities.every((cap) =>
            model.capabilities.includes(cap)
          )
          if (!hasAllCapabilities) {
            skipped.push({ model: modelId, reason: 'Missing required capabilities' })
            return false
          }
        }

        return true
      })
      .map((modelId) => this.getModelDefinition(modelId)!)
      .sort((a, b) => {
        // Prefer higher quality tier
        const tierDiff = tierOrder[b.tier] - tierOrder[a.tier]
        if (tierDiff !== 0) return tierDiff

        // If same tier and preferLowerCost, prefer cheaper
        if (this.config.preferLowerCost) {
          return a.costPer1M - b.costPer1M
        }

        // Otherwise prefer higher success rate
        return this.getSuccessRate(b.provider) - this.getSuccessRate(a.provider)
      })

    if (candidates.length === 0) {
      return null
    }

    const selected = candidates[0]
    const position = chain.indexOf(selected.id) + 1

    return {
      model: selected,
      reason: this.generateSelectionReason(selected, primary),
      position,
      skipped,
    }
  }

  /**
   * Generate selection reason
   */
  private generateSelectionReason(selected: ModelDefinition, primary?: ModelDefinition): string {
    const reasons: string[] = []

    if (primary && selected.tier !== primary.tier) {
      reasons.push(`Quality tier: ${selected.tier}`)
    }

    reasons.push(`Provider: ${selected.provider}`)
    reasons.push(`Cost: $${selected.costPer1M}/1M tokens`)

    const successRate = this.getSuccessRate(selected.provider)
    if (successRate < 1.0) {
      reasons.push(`Success rate: ${(successRate * 100).toFixed(0)}%`)
    }

    return reasons.join(', ')
  }

  // ===========================================================================
  // Execution with Fallback
  // ===========================================================================

  /**
   * Execute with fallback chain
   */
  async executeWithFallback<T>(
    fn: (modelId: string) => Promise<T>,
    primaryModel: string,
    options: {
      onAttempt?: (modelId: string, attempt: number) => void
      onFallback?: (fromModel: string, toModel: string) => void
      retryDelay?: (attempt: number) => number
    } = {}
  ): Promise<FallbackExecutionResult<T>> {
    const attemptedModels: string[] = []
    const attemptedSet = new Set<string>()
    let totalAttempts = 0
    let totalDelayMs = 0

    // Try primary model first
    let currentModel = primaryModel
    attemptedModels.push(currentModel)
    attemptedSet.add(currentModel)

    while (totalAttempts < this.config.maxRetries) {
      totalAttempts++

      if (options.onAttempt) {
        options.onAttempt(currentModel, totalAttempts)
      }

      try {
        const result = await fn(currentModel)
        const model = this.getModelDefinition(currentModel)
        if (model) {
          this.recordSuccess(model.provider)
        }

        return {
          success: true,
          result,
          usedModel: currentModel,
          attemptedModels,
          totalAttempts,
          totalDelayMs,
          chainExhausted: false,
        }
      } catch (error) {
        const model = this.getModelDefinition(currentModel)
        if (model) {
          this.recordFailure(model.provider)
        }

        if (this.config.log) {
          this.config.log('warn', `Model ${currentModel} failed`, {
            attempt: totalAttempts,
            error: error instanceof Error ? error.message : String(error),
          })
        }

        // Try to find fallback
        const fallback = this.selectFallback(primaryModel, attemptedSet)

        if (!fallback) {
          // No more fallbacks available
          return {
            success: false,
            error: error instanceof Error ? error : new Error(String(error)),
            attemptedModels,
            totalAttempts,
            totalDelayMs,
            chainExhausted: true,
          }
        }

        // Switch to fallback
        const previousModel = currentModel
        currentModel = fallback.model.id
        attemptedModels.push(currentModel)
        attemptedSet.add(currentModel)

        if (options.onFallback) {
          options.onFallback(previousModel, currentModel)
        }

        // Apply retry delay
        if (options.retryDelay) {
          const delay = options.retryDelay(totalAttempts)
          await new Promise((resolve) => setTimeout(resolve, delay))
          totalDelayMs += delay
        }
      }
    }

    // Max retries exceeded
    return {
      success: false,
      error: new Error(`Max retries (${this.config.maxRetries}) exceeded`),
      attemptedModels,
      totalAttempts,
      totalDelayMs,
      chainExhausted: attemptedSet.size >= this.getFallbackChain(primaryModel).length + 1,
    }
  }
}

// =============================================================================
// Singleton & Utilities
// =============================================================================

let defaultManager: FallbackChainManager | null = null

/**
 * Get the default fallback chain manager
 */
export function getFallbackManager(config?: Partial<FallbackChainConfig>): FallbackChainManager {
  if (!defaultManager) {
    defaultManager = new FallbackChainManager(config)
  }
  return defaultManager
}

/**
 * Reset the default manager (for testing)
 */
export function resetFallbackManager(): void {
  defaultManager = null
}

/**
 * Get quality tier for a model
 */
export function getModelTier(modelId: string): QualityTier | undefined {
  return MODEL_REGISTRY[modelId]?.tier
}

/**
 * Get models by tier
 */
export function getModelsByTier(tier: QualityTier): ModelDefinition[] {
  return Object.values(MODEL_REGISTRY).filter((m) => m.tier === tier)
}

/**
 * Get models by provider
 */
export function getModelsByProvider(provider: string): ModelDefinition[] {
  return Object.values(MODEL_REGISTRY).filter((m) => m.provider === provider)
}

/**
 * Describe fallback execution result
 */
export function describeFallbackResult<T>(result: FallbackExecutionResult<T>): string {
  const lines: string[] = []

  if (result.success) {
    lines.push(`Success with ${result.usedModel}`)
  } else {
    lines.push('Failed')
    if (result.error) {
      lines.push(`Error: ${result.error.message}`)
    }
  }

  lines.push(`Attempts: ${result.totalAttempts}`)
  lines.push(`Models tried: ${result.attemptedModels.join(' -> ')}`)

  if (result.totalDelayMs > 0) {
    lines.push(`Total delay: ${(result.totalDelayMs / 1000).toFixed(1)}s`)
  }

  if (result.chainExhausted) {
    lines.push('Fallback chain exhausted')
  }

  return lines.join('\n')
}

// =============================================================================
// Fallback Chain Activity Logging
// =============================================================================

/** Fallback chain activity record */
export interface FallbackActivity {
  timestamp: string
  primaryModel: string
  usedModel: string
  attemptedModels: string[]
  success: boolean
  errorMessage?: string
  durationMs: number
}

/** Activity history buffer */
const fallbackActivityHistory: FallbackActivity[] = []
const MAX_ACTIVITY_HISTORY = 100

/**
 * Record fallback chain activity
 *
 * Call this after executeWithFallback to track fallback patterns.
 */
export function recordFallbackActivity<T>(
  primaryModel: string,
  result: FallbackExecutionResult<T>,
  durationMs: number
): void {
  const activity: FallbackActivity = {
    timestamp: new Date().toISOString(),
    primaryModel,
    usedModel: result.usedModel || 'none',
    attemptedModels: result.attemptedModels,
    success: result.success,
    errorMessage: result.error?.message,
    durationMs,
  }

  fallbackActivityHistory.push(activity)

  // Keep history bounded
  if (fallbackActivityHistory.length > MAX_ACTIVITY_HISTORY) {
    fallbackActivityHistory.shift()
  }
}

/**
 * Get recent fallback activity
 */
export function getRecentFallbackActivity(limit = 10): FallbackActivity[] {
  return fallbackActivityHistory.slice(-limit)
}

/**
 * Get fallback activity summary
 *
 * Returns statistics about recent fallback behavior.
 */
export function getFallbackActivitySummary(): {
  total: number
  successRate: number
  avgAttempts: number
  fallbackRate: number
  mostUsedFallbacks: Array<{ model: string; count: number }>
  failuresByPrimary: Array<{ model: string; failures: number }>
} {
  if (fallbackActivityHistory.length === 0) {
    return {
      total: 0,
      successRate: 0,
      avgAttempts: 0,
      fallbackRate: 0,
      mostUsedFallbacks: [],
      failuresByPrimary: [],
    }
  }

  const total = fallbackActivityHistory.length
  const successes = fallbackActivityHistory.filter((a) => a.success).length
  const fallbacks = fallbackActivityHistory.filter((a) => a.attemptedModels.length > 1).length
  const totalAttempts = fallbackActivityHistory.reduce(
    (sum, a) => sum + a.attemptedModels.length,
    0
  )

  // Count fallback model usage
  const fallbackUsage = new Map<string, number>()
  for (const activity of fallbackActivityHistory) {
    if (activity.attemptedModels.length > 1 && activity.usedModel !== activity.primaryModel) {
      fallbackUsage.set(activity.usedModel, (fallbackUsage.get(activity.usedModel) || 0) + 1)
    }
  }

  // Count failures by primary model
  const failuresByPrimaryMap = new Map<string, number>()
  for (const activity of fallbackActivityHistory) {
    if (!activity.success) {
      failuresByPrimaryMap.set(
        activity.primaryModel,
        (failuresByPrimaryMap.get(activity.primaryModel) || 0) + 1
      )
    }
  }

  return {
    total,
    successRate: Math.round((successes / total) * 100) / 100,
    avgAttempts: Math.round((totalAttempts / total) * 100) / 100,
    fallbackRate: Math.round((fallbacks / total) * 100) / 100,
    mostUsedFallbacks: Array.from(fallbackUsage.entries())
      .map(([model, count]) => ({ model, count }))
      .sort((a, b) => b.count - a.count)
      .slice(0, 5),
    failuresByPrimary: Array.from(failuresByPrimaryMap.entries())
      .map(([model, failures]) => ({ model, failures }))
      .sort((a, b) => b.failures - a.failures)
      .slice(0, 5),
  }
}

/**
 * Clear fallback activity history (for testing)
 */
export function clearFallbackActivityHistory(): void {
  fallbackActivityHistory.length = 0
}
