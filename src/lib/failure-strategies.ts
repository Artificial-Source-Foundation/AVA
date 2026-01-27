/**
 * Delta9 Enhanced Failure Strategies
 *
 * Provides sophisticated failure handling beyond basic retry:
 * - Intelligent retry with context adjustment
 * - Partial success handling
 * - Dependency-aware failure propagation
 * - Recovery state management
 * - Failure pattern detection
 *
 * Strategies:
 * - retry: Simple retry with backoff
 * - retry_with_context: Retry with adjusted context/prompt
 * - delegate: Hand off to different agent type
 * - decompose: Break into smaller tasks
 * - skip: Mark as skipped and continue
 * - escalate: Escalate to human review
 * - rollback: Trigger checkpoint rollback
 */

import { getNamedLogger } from './logger.js'

const log = getNamedLogger('failure-strategies')

// =============================================================================
// Types
// =============================================================================

/** Available failure strategies */
export type FailureStrategy =
  | 'retry'
  | 'retry_with_context'
  | 'delegate'
  | 'decompose'
  | 'skip'
  | 'escalate'
  | 'rollback'
  | 'abort'

/** Failure category for strategy selection */
export type FailureCategory =
  | 'timeout'
  | 'rate_limit'
  | 'context_overflow'
  | 'validation_error'
  | 'dependency_error'
  | 'auth_error'
  | 'resource_error'
  | 'unknown'

/** Severity level */
export type FailureSeverity = 'low' | 'medium' | 'high' | 'critical'

/** Failure context for strategy selection */
export interface FailureContext {
  /** Task ID */
  taskId: string
  /** Agent type that failed */
  agentType: string
  /** Error message */
  error: string
  /** Error category */
  category: FailureCategory
  /** Number of previous attempts */
  attempts: number
  /** Time spent so far (ms) */
  elapsedMs: number
  /** Task dependencies */
  dependencies?: string[]
  /** Tasks that depend on this one */
  dependents?: string[]
  /** Previous strategies attempted */
  previousStrategies?: FailureStrategy[]
  /** Additional metadata */
  metadata?: Record<string, unknown>
}

/** Strategy recommendation */
export interface StrategyRecommendation {
  /** Recommended strategy */
  strategy: FailureStrategy
  /** Confidence in this recommendation (0-1) */
  confidence: number
  /** Reasoning for this recommendation */
  reasoning: string
  /** Parameters for the strategy */
  params: StrategyParams
  /** Alternative strategies if primary fails */
  alternatives: FailureStrategy[]
}

/** Parameters for strategy execution */
export interface StrategyParams {
  /** For retry: delay before retry (ms) */
  retryDelayMs?: number
  /** For retry: max additional attempts */
  maxRetries?: number
  /** For retry_with_context: context adjustments */
  contextAdjustments?: {
    /** Reduce context size */
    reduceContext?: boolean
    /** Add clarifying instructions */
    addClarification?: string
    /** Simplify the task description */
    simplifyTask?: boolean
  }
  /** For delegate: target agent type */
  delegateTarget?: string
  /** For decompose: suggested subtasks */
  subtasks?: string[]
  /** For skip: reason for skipping */
  skipReason?: string
  /** For escalate: escalation message */
  escalationMessage?: string
  /** For rollback: checkpoint name */
  checkpointName?: string
}

/** Execution result for a strategy */
export interface StrategyExecutionResult {
  /** Strategy that was executed */
  strategy: FailureStrategy
  /** Whether the strategy succeeded */
  success: boolean
  /** Result data (varies by strategy) */
  result?: unknown
  /** Error if strategy failed */
  error?: string
  /** Next action to take */
  nextAction: 'continue' | 'retry' | 'abort' | 'escalate'
  /** Duration of strategy execution (ms) */
  durationMs: number
}

/** Strategy rule for the rule engine */
export interface StrategyRule {
  /** Rule ID */
  id: string
  /** Rule priority (higher = checked first) */
  priority: number
  /** Condition function */
  condition: (context: FailureContext) => boolean
  /** Strategy to use if condition matches */
  strategy: FailureStrategy
  /** Default params for this rule */
  defaultParams: StrategyParams
  /** Confidence when this rule matches */
  confidence: number
  /** Description of the rule */
  description: string
}

/** Configuration for the strategy manager */
export interface FailureStrategyConfig {
  /** Custom rules (merged with defaults) */
  rules?: StrategyRule[]
  /** Whether to use default rules */
  useDefaults?: boolean
  /** Max attempts before escalating */
  maxAttempts?: number
  /** Max time before escalating (ms) */
  maxTimeMs?: number
  /** Strategy execution handlers */
  handlers?: Partial<Record<FailureStrategy, StrategyHandler>>
}

/** Handler function for executing a strategy */
export type StrategyHandler = (
  context: FailureContext,
  params: StrategyParams
) => Promise<StrategyExecutionResult>

// =============================================================================
// Constants
// =============================================================================

/** Default strategy rules in priority order */
export const DEFAULT_STRATEGY_RULES: StrategyRule[] = [
  // Critical: Auth errors should abort immediately
  {
    id: 'auth_abort',
    priority: 100,
    condition: (ctx) => ctx.category === 'auth_error',
    strategy: 'abort',
    defaultParams: {},
    confidence: 0.95,
    description: 'Abort on authentication errors',
  },

  // High priority: Rate limits should wait and retry
  {
    id: 'rate_limit_retry',
    priority: 90,
    condition: (ctx) => ctx.category === 'rate_limit' && ctx.attempts < 3,
    strategy: 'retry',
    defaultParams: {
      retryDelayMs: 30000, // Wait 30s
      maxRetries: 3,
    },
    confidence: 0.9,
    description: 'Retry rate limit errors with delay',
  },

  // Context overflow: reduce and retry
  {
    id: 'context_overflow_reduce',
    priority: 85,
    condition: (ctx) => ctx.category === 'context_overflow' && ctx.attempts < 2,
    strategy: 'retry_with_context',
    defaultParams: {
      contextAdjustments: {
        reduceContext: true,
        simplifyTask: true,
      },
    },
    confidence: 0.85,
    description: 'Reduce context on overflow errors',
  },

  // Context overflow: decompose if reduction failed
  {
    id: 'context_overflow_decompose',
    priority: 84,
    condition: (ctx) =>
      ctx.category === 'context_overflow' &&
      (ctx.previousStrategies?.includes('retry_with_context') ?? false),
    strategy: 'decompose',
    defaultParams: {},
    confidence: 0.8,
    description: 'Decompose task if context reduction failed',
  },

  // Timeout: try faster model first
  {
    id: 'timeout_delegate',
    priority: 80,
    condition: (ctx) =>
      ctx.category === 'timeout' &&
      ctx.attempts < 2 &&
      !(ctx.previousStrategies?.includes('delegate') ?? false),
    strategy: 'delegate',
    defaultParams: {
      delegateTarget: 'haiku', // Faster model
    },
    confidence: 0.75,
    description: 'Delegate to faster model on timeout',
  },

  // Timeout: retry with extended time
  {
    id: 'timeout_retry',
    priority: 79,
    condition: (ctx) =>
      ctx.category === 'timeout' && (ctx.previousStrategies?.includes('delegate') ?? false),
    strategy: 'retry',
    defaultParams: {
      retryDelayMs: 5000,
      maxRetries: 1,
    },
    confidence: 0.6,
    description: 'Retry timeout with original model',
  },

  // Validation errors: retry with clarification
  {
    id: 'validation_clarify',
    priority: 70,
    condition: (ctx) => ctx.category === 'validation_error' && ctx.attempts < 3,
    strategy: 'retry_with_context',
    defaultParams: {
      contextAdjustments: {
        addClarification: 'Please ensure output matches the required format exactly.',
      },
    },
    confidence: 0.8,
    description: 'Add clarification for validation errors',
  },

  // Dependency errors: check if can skip
  {
    id: 'dependency_skip',
    priority: 65,
    condition: (ctx) =>
      ctx.category === 'dependency_error' && (!ctx.dependents || ctx.dependents.length === 0),
    strategy: 'skip',
    defaultParams: {
      skipReason: 'Dependency unavailable, no downstream impact',
    },
    confidence: 0.7,
    description: 'Skip tasks with failed dependencies if safe',
  },

  // Dependency errors with dependents: escalate
  {
    id: 'dependency_escalate',
    priority: 64,
    condition: (ctx) =>
      ctx.category === 'dependency_error' && (ctx.dependents?.length ?? 0) > 0,
    strategy: 'escalate',
    defaultParams: {
      escalationMessage: 'Critical dependency failure affecting downstream tasks',
    },
    confidence: 0.85,
    description: 'Escalate dependency errors with downstream impact',
  },

  // Too many attempts: escalate
  {
    id: 'max_attempts_escalate',
    priority: 50,
    condition: (ctx) => ctx.attempts >= 5,
    strategy: 'escalate',
    defaultParams: {
      escalationMessage: 'Max retry attempts exceeded',
    },
    confidence: 0.9,
    description: 'Escalate after max attempts',
  },

  // Taking too long: escalate
  {
    id: 'max_time_escalate',
    priority: 49,
    condition: (ctx) => ctx.elapsedMs >= 600000, // 10 minutes
    strategy: 'escalate',
    defaultParams: {
      escalationMessage: 'Task exceeded maximum time limit',
    },
    confidence: 0.85,
    description: 'Escalate after max time',
  },

  // Default: retry
  {
    id: 'default_retry',
    priority: 10,
    condition: (ctx) => ctx.attempts < 3,
    strategy: 'retry',
    defaultParams: {
      retryDelayMs: 5000,
      maxRetries: 3,
    },
    confidence: 0.5,
    description: 'Default retry strategy',
  },

  // Final fallback: escalate
  {
    id: 'fallback_escalate',
    priority: 1,
    condition: () => true,
    strategy: 'escalate',
    defaultParams: {
      escalationMessage: 'No suitable recovery strategy found',
    },
    confidence: 0.3,
    description: 'Fallback escalation',
  },
]

/** Category detection patterns */
const CATEGORY_PATTERNS: Array<{ pattern: RegExp; category: FailureCategory }> = [
  { pattern: /timeout|timed out|deadline exceeded/i, category: 'timeout' },
  { pattern: /rate limit|too many requests|429/i, category: 'rate_limit' },
  { pattern: /context.*(length|overflow|too long)|token limit/i, category: 'context_overflow' },
  { pattern: /validation|format|schema|invalid/i, category: 'validation_error' },
  { pattern: /dependency|depends on|waiting for/i, category: 'dependency_error' },
  { pattern: /auth|unauthorized|forbidden|401|403/i, category: 'auth_error' },
  { pattern: /resource|not found|404|unavailable/i, category: 'resource_error' },
]

// =============================================================================
// Failure Strategy Manager
// =============================================================================

export class FailureStrategyManager {
  private rules: StrategyRule[] = []
  private handlers: Partial<Record<FailureStrategy, StrategyHandler>> = {}
  private executionHistory: Array<{
    taskId: string
    strategy: FailureStrategy
    success: boolean
    timestamp: number
  }> = []

  constructor(config: FailureStrategyConfig = {}) {
    const { rules = [], useDefaults = true } = config

    // Load default rules
    if (useDefaults) {
      this.rules = [...DEFAULT_STRATEGY_RULES]
    }

    // Add custom rules
    for (const rule of rules) {
      this.addRule(rule)
    }

    // Sort by priority (descending)
    this.rules.sort((a, b) => b.priority - a.priority)

    // Load handlers
    if (config.handlers) {
      this.handlers = { ...config.handlers }
    }
  }

  // ===========================================================================
  // Rule Management
  // ===========================================================================

  /**
   * Add a rule
   */
  addRule(rule: StrategyRule): void {
    // Remove existing rule with same ID
    this.rules = this.rules.filter((r) => r.id !== rule.id)
    this.rules.push(rule)
    // Re-sort
    this.rules.sort((a, b) => b.priority - a.priority)
    log.debug(`Added rule: ${rule.id} (priority: ${rule.priority})`)
  }

  /**
   * Remove a rule
   */
  removeRule(id: string): boolean {
    const initialLength = this.rules.length
    this.rules = this.rules.filter((r) => r.id !== id)
    return this.rules.length < initialLength
  }

  /**
   * Get all rules
   */
  getRules(): StrategyRule[] {
    return [...this.rules]
  }

  // ===========================================================================
  // Strategy Selection
  // ===========================================================================

  /**
   * Get strategy recommendation for a failure
   */
  recommend(context: FailureContext): StrategyRecommendation {
    // Find first matching rule
    for (const rule of this.rules) {
      if (rule.condition(context)) {
        // Find alternatives (other matching rules)
        const alternatives = this.rules
          .filter((r) => r.id !== rule.id && r.condition(context))
          .map((r) => r.strategy)
          .slice(0, 3) // Max 3 alternatives

        return {
          strategy: rule.strategy,
          confidence: rule.confidence,
          reasoning: rule.description,
          params: { ...rule.defaultParams },
          alternatives,
        }
      }
    }

    // Should never reach here due to fallback rule
    return {
      strategy: 'escalate',
      confidence: 0.1,
      reasoning: 'No matching rule found',
      params: { escalationMessage: 'Strategy selection failed' },
      alternatives: [],
    }
  }

  /**
   * Categorize an error
   */
  categorizeError(error: string): FailureCategory {
    for (const { pattern, category } of CATEGORY_PATTERNS) {
      if (pattern.test(error)) {
        return category
      }
    }
    return 'unknown'
  }

  /**
   * Determine failure severity
   */
  determineSeverity(context: FailureContext): FailureSeverity {
    // Critical: auth errors or errors with many dependents
    if (
      context.category === 'auth_error' ||
      (context.dependents && context.dependents.length > 3)
    ) {
      return 'critical'
    }

    // High: many attempts or long elapsed time
    if (context.attempts >= 3 || context.elapsedMs >= 300000) {
      return 'high'
    }

    // Medium: some attempts or moderate time
    if (context.attempts >= 2 || context.elapsedMs >= 60000) {
      return 'medium'
    }

    return 'low'
  }

  // ===========================================================================
  // Strategy Execution
  // ===========================================================================

  /**
   * Execute a strategy
   */
  async execute(
    context: FailureContext,
    recommendation: StrategyRecommendation
  ): Promise<StrategyExecutionResult> {
    const handler = this.handlers[recommendation.strategy]
    const startTime = Date.now()

    if (!handler) {
      // No handler - return a default result
      log.warn(`No handler for strategy: ${recommendation.strategy}`)
      return {
        strategy: recommendation.strategy,
        success: false,
        error: `No handler registered for strategy: ${recommendation.strategy}`,
        nextAction: 'escalate',
        durationMs: Date.now() - startTime,
      }
    }

    try {
      const result = await handler(context, recommendation.params)

      // Record history
      this.executionHistory.push({
        taskId: context.taskId,
        strategy: recommendation.strategy,
        success: result.success,
        timestamp: Date.now(),
      })

      // Trim history
      if (this.executionHistory.length > 1000) {
        this.executionHistory = this.executionHistory.slice(-1000)
      }

      return result
    } catch (error) {
      const result: StrategyExecutionResult = {
        strategy: recommendation.strategy,
        success: false,
        error: error instanceof Error ? error.message : String(error),
        nextAction: 'escalate',
        durationMs: Date.now() - startTime,
      }

      this.executionHistory.push({
        taskId: context.taskId,
        strategy: recommendation.strategy,
        success: false,
        timestamp: Date.now(),
      })

      return result
    }
  }

  /**
   * Register a strategy handler
   */
  registerHandler(strategy: FailureStrategy, handler: StrategyHandler): void {
    this.handlers[strategy] = handler
    log.debug(`Registered handler for: ${strategy}`)
  }

  // ===========================================================================
  // Analytics
  // ===========================================================================

  /**
   * Get execution statistics
   */
  getStats(): {
    totalExecutions: number
    successRate: number
    byStrategy: Record<FailureStrategy, { count: number; successRate: number }>
  } {
    const total = this.executionHistory.length
    const successful = this.executionHistory.filter((h) => h.success).length

    const byStrategy: Record<FailureStrategy, { count: number; successRate: number }> =
      {} as Record<FailureStrategy, { count: number; successRate: number }>

    for (const entry of this.executionHistory) {
      if (!byStrategy[entry.strategy]) {
        byStrategy[entry.strategy] = { count: 0, successRate: 0 }
      }
      byStrategy[entry.strategy].count++
    }

    // Calculate success rates per strategy
    for (const strategy of Object.keys(byStrategy) as FailureStrategy[]) {
      const strategyEntries = this.executionHistory.filter((h) => h.strategy === strategy)
      const strategySuccessful = strategyEntries.filter((h) => h.success).length
      byStrategy[strategy].successRate =
        strategyEntries.length > 0 ? strategySuccessful / strategyEntries.length : 0
    }

    return {
      totalExecutions: total,
      successRate: total > 0 ? successful / total : 0,
      byStrategy,
    }
  }

  /**
   * Get recent failures for a task
   */
  getTaskHistory(taskId: string): typeof this.executionHistory {
    return this.executionHistory.filter((h) => h.taskId === taskId)
  }

  /**
   * Clear execution history
   */
  clearHistory(): void {
    this.executionHistory = []
  }
}

// =============================================================================
// Factory Functions
// =============================================================================

/** Singleton instance */
let defaultManager: FailureStrategyManager | null = null

/**
 * Get or create the default failure strategy manager
 */
export function getFailureStrategyManager(config?: FailureStrategyConfig): FailureStrategyManager {
  if (!defaultManager) {
    defaultManager = new FailureStrategyManager(config)
  }
  return defaultManager
}

/**
 * Reset the default manager (for testing)
 */
export function resetFailureStrategyManager(): void {
  defaultManager = null
}

/**
 * Create a new failure strategy manager
 */
export function createFailureStrategyManager(config?: FailureStrategyConfig): FailureStrategyManager {
  return new FailureStrategyManager(config)
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Create a failure context from minimal info
 */
export function createFailureContext(
  taskId: string,
  agentType: string,
  error: string,
  options: Partial<FailureContext> = {}
): FailureContext {
  const manager = getFailureStrategyManager()

  return {
    taskId,
    agentType,
    error,
    category: options.category ?? manager.categorizeError(error),
    attempts: options.attempts ?? 1,
    elapsedMs: options.elapsedMs ?? 0,
    dependencies: options.dependencies,
    dependents: options.dependents,
    previousStrategies: options.previousStrategies,
    metadata: options.metadata,
  }
}

/**
 * Describe a strategy recommendation
 */
export function describeRecommendation(rec: StrategyRecommendation): string {
  const lines: string[] = [
    `Strategy: ${rec.strategy}`,
    `Confidence: ${(rec.confidence * 100).toFixed(0)}%`,
    `Reasoning: ${rec.reasoning}`,
  ]

  if (rec.alternatives.length > 0) {
    lines.push(`Alternatives: ${rec.alternatives.join(', ')}`)
  }

  if (rec.params.retryDelayMs) {
    lines.push(`Retry delay: ${rec.params.retryDelayMs}ms`)
  }

  if (rec.params.delegateTarget) {
    lines.push(`Delegate to: ${rec.params.delegateTarget}`)
  }

  return lines.join('\n')
}

/**
 * Check if a strategy is terminal (ends the task)
 */
export function isTerminalStrategy(strategy: FailureStrategy): boolean {
  return ['abort', 'skip', 'escalate', 'rollback'].includes(strategy)
}

/**
 * Check if a strategy requires human intervention
 */
export function requiresHumanIntervention(strategy: FailureStrategy): boolean {
  return ['escalate', 'rollback'].includes(strategy)
}
