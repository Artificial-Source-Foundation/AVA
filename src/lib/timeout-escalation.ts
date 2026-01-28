/**
 * Delta9 Timeout Escalation Chains
 *
 * Implements progressive timeout handling with escalation:
 * 1. Initial timeout: Extend and retry
 * 2. Second timeout: Switch to faster model
 * 3. Third timeout: Notify and use cached result
 * 4. Final timeout: Fail with detailed diagnostics
 *
 * Each escalation level has configurable actions and thresholds.
 */

import { getNamedLogger } from './logger.js'
import { quickEstimate } from './timeout-estimator.js'
import { loadConfig } from './config.js'

const log = getNamedLogger('timeout-escalation')

// =============================================================================
// Types
// =============================================================================

/** Escalation level */
export type EscalationLevel = 1 | 2 | 3 | 4

/** Action to take at each escalation level */
export type EscalationAction =
  | 'extend' // Extend timeout and retry
  | 'switch_model' // Switch to faster model
  | 'use_cache' // Use cached/partial result
  | 'notify' // Notify but continue
  | 'fail' // Fail the operation
  | 'skip' // Skip this task

/** Escalation step configuration */
export interface EscalationStep {
  /** Escalation level (1-4) */
  level: EscalationLevel
  /** Action to take */
  action: EscalationAction
  /** Timeout multiplier for this level (e.g., 1.5 = 50% more time) */
  timeoutMultiplier: number
  /** Optional fallback model for 'switch_model' action */
  fallbackModel?: string
  /** Description of this step */
  description: string
}

/** Escalation chain configuration */
export interface EscalationChain {
  /** Chain identifier */
  id: string
  /** Steps in order of escalation */
  steps: EscalationStep[]
  /** Maximum total time allowed (ms) */
  maxTotalTimeMs: number
  /** Whether to record metrics */
  recordMetrics: boolean
}

/** Escalation state for a single operation */
export interface EscalationState {
  /** Operation identifier */
  operationId: string
  /** Current escalation level */
  currentLevel: EscalationLevel
  /** Time elapsed so far (ms) */
  elapsedMs: number
  /** Number of timeouts encountered */
  timeoutCount: number
  /** Models attempted */
  modelsAttempted: string[]
  /** Start time */
  startedAt: number
  /** Current timeout value (ms) */
  currentTimeoutMs: number
  /** Original timeout value (ms) */
  originalTimeoutMs: number
}

/** Result of an escalation decision */
export interface EscalationDecision {
  /** Action to take */
  action: EscalationAction
  /** New timeout value (if extending) */
  newTimeoutMs?: number
  /** Model to switch to (if switching) */
  newModel?: string
  /** Current escalation level */
  level: EscalationLevel
  /** Whether max time has been exceeded */
  maxTimeExceeded: boolean
  /** Recommended message for logging/display */
  message: string
  /** Whether this is the final escalation */
  isFinal: boolean
}

/** Metrics for timeout escalations */
export interface EscalationMetrics {
  /** Total escalations triggered */
  totalEscalations: number
  /** Escalations by level */
  byLevel: Record<EscalationLevel, number>
  /** Escalations by action */
  byAction: Record<EscalationAction, number>
  /** Operations that recovered */
  recoveredCount: number
  /** Operations that failed */
  failedCount: number
  /** Average escalation level reached */
  avgLevelReached: number
}

/** Configuration for the escalation manager */
export interface EscalationConfig {
  /** Default chain to use */
  defaultChain?: EscalationChain
  /** Custom chains by ID */
  chains?: Record<string, EscalationChain>
  /** Global max time override */
  globalMaxTimeMs?: number
  /** Callback on escalation */
  onEscalate?: (state: EscalationState, decision: EscalationDecision) => void
  /** Working directory for config-driven model resolution */
  cwd?: string
}

// =============================================================================
// Constants
// =============================================================================

/** Default escalation chain for standard operations */
export const DEFAULT_ESCALATION_CHAIN: EscalationChain = {
  id: 'default',
  steps: [
    {
      level: 1,
      action: 'extend',
      timeoutMultiplier: 1.5,
      description: 'Extend timeout by 50% and retry',
    },
    {
      level: 2,
      action: 'switch_model',
      timeoutMultiplier: 1.0,
      fallbackModel: 'anthropic/claude-haiku-4',
      description: 'Switch to faster model',
    },
    {
      level: 3,
      action: 'notify',
      timeoutMultiplier: 2.0,
      description: 'Notify and continue with extended timeout',
    },
    {
      level: 4,
      action: 'fail',
      timeoutMultiplier: 0,
      description: 'Fail after exhausting escalation options',
    },
  ],
  maxTotalTimeMs: 600000, // 10 minutes max
  recordMetrics: true,
}

/** Aggressive chain for time-sensitive operations */
export const AGGRESSIVE_ESCALATION_CHAIN: EscalationChain = {
  id: 'aggressive',
  steps: [
    {
      level: 1,
      action: 'switch_model',
      timeoutMultiplier: 0.5,
      fallbackModel: 'anthropic/claude-haiku-4',
      description: 'Immediately switch to faster model',
    },
    {
      level: 2,
      action: 'extend',
      timeoutMultiplier: 1.5,
      description: 'Extend timeout and continue',
    },
    {
      level: 3,
      action: 'skip',
      timeoutMultiplier: 0,
      description: 'Skip this task',
    },
    {
      level: 4,
      action: 'fail',
      timeoutMultiplier: 0,
      description: 'Fail operation',
    },
  ],
  maxTotalTimeMs: 180000, // 3 minutes max
  recordMetrics: true,
}

/** Patient chain for complex operations */
export const PATIENT_ESCALATION_CHAIN: EscalationChain = {
  id: 'patient',
  steps: [
    {
      level: 1,
      action: 'extend',
      timeoutMultiplier: 2.0,
      description: 'Double timeout and retry',
    },
    {
      level: 2,
      action: 'extend',
      timeoutMultiplier: 1.5,
      description: 'Extend by 50% more',
    },
    {
      level: 3,
      action: 'notify',
      timeoutMultiplier: 2.0,
      description: 'Notify and continue',
    },
    {
      level: 4,
      action: 'use_cache',
      timeoutMultiplier: 0,
      description: 'Use cached/partial result if available',
    },
  ],
  maxTotalTimeMs: 1200000, // 20 minutes max
  recordMetrics: true,
}

/** Pre-defined chains */
export const ESCALATION_CHAINS: Record<string, EscalationChain> = {
  default: DEFAULT_ESCALATION_CHAIN,
  aggressive: AGGRESSIVE_ESCALATION_CHAIN,
  patient: PATIENT_ESCALATION_CHAIN,
}

/**
 * Create escalation chains with config-driven fallback models
 *
 * Uses the validator model from config as the fallback for escalations.
 */
export function createConfigDrivenChains(cwd: string): Record<string, EscalationChain> {
  const config = loadConfig(cwd)
  const fallbackModel = config.validator.model // Fast model for escalation fallback

  return {
    default: {
      ...DEFAULT_ESCALATION_CHAIN,
      steps: DEFAULT_ESCALATION_CHAIN.steps.map((step) =>
        step.action === 'switch_model' ? { ...step, fallbackModel } : step
      ),
    },
    aggressive: {
      ...AGGRESSIVE_ESCALATION_CHAIN,
      steps: AGGRESSIVE_ESCALATION_CHAIN.steps.map((step) =>
        step.action === 'switch_model' ? { ...step, fallbackModel } : step
      ),
    },
    patient: PATIENT_ESCALATION_CHAIN, // No switch_model in patient chain
  }
}

// =============================================================================
// Timeout Escalation Manager
// =============================================================================

export class TimeoutEscalationManager {
  private activeOperations: Map<string, EscalationState> = new Map()
  private chains: Map<string, EscalationChain> = new Map()
  private metrics: EscalationMetrics = {
    totalEscalations: 0,
    byLevel: { 1: 0, 2: 0, 3: 0, 4: 0 },
    byAction: {
      extend: 0,
      switch_model: 0,
      use_cache: 0,
      notify: 0,
      fail: 0,
      skip: 0,
    },
    recoveredCount: 0,
    failedCount: 0,
    avgLevelReached: 0,
  }
  private completedLevels: number[] = []
  private globalMaxTimeMs?: number
  private onEscalate?: (state: EscalationState, decision: EscalationDecision) => void

  constructor(config: EscalationConfig = {}) {
    // Load default chains - use config-driven models if cwd is provided
    const defaultChains = config.cwd ? createConfigDrivenChains(config.cwd) : ESCALATION_CHAINS

    for (const [id, chain] of Object.entries(defaultChains)) {
      this.chains.set(id, chain)
    }

    // Load custom chains
    if (config.chains) {
      for (const [id, chain] of Object.entries(config.chains)) {
        this.chains.set(id, chain)
      }
    }

    // Override default if provided
    if (config.defaultChain) {
      this.chains.set('default', config.defaultChain)
    }

    this.globalMaxTimeMs = config.globalMaxTimeMs
    this.onEscalate = config.onEscalate
  }

  // ===========================================================================
  // Operation Management
  // ===========================================================================

  /**
   * Start tracking an operation
   */
  startOperation(
    operationId: string,
    options: {
      /** Initial model */
      model?: string
      /** Initial timeout (ms) */
      timeoutMs?: number
      /** Agent type for timeout estimation */
      agentType?: string
      /** Prompt for timeout estimation */
      prompt?: string
    } = {}
  ): EscalationState {
    // Estimate timeout if not provided
    let timeoutMs = options.timeoutMs
    if (!timeoutMs && options.agentType) {
      timeoutMs = quickEstimate(options.agentType, options.prompt ?? '')
    }
    timeoutMs = timeoutMs ?? 60000 // Default 1 minute

    const state: EscalationState = {
      operationId,
      currentLevel: 1,
      elapsedMs: 0,
      timeoutCount: 0,
      modelsAttempted: options.model ? [options.model] : [],
      startedAt: Date.now(),
      currentTimeoutMs: timeoutMs,
      originalTimeoutMs: timeoutMs,
    }

    this.activeOperations.set(operationId, state)
    log.debug(`Started tracking operation ${operationId} with ${timeoutMs}ms timeout`)

    return state
  }

  /**
   * Record a timeout and get escalation decision
   */
  recordTimeout(operationId: string, chainId = 'default'): EscalationDecision {
    const state = this.activeOperations.get(operationId)
    if (!state) {
      return {
        action: 'fail',
        level: 4,
        maxTimeExceeded: true,
        message: `Unknown operation: ${operationId}`,
        isFinal: true,
      }
    }

    const chain = this.chains.get(chainId) ?? this.chains.get('default')!
    state.timeoutCount++
    state.elapsedMs = Date.now() - state.startedAt

    // Check max time
    const maxTime = this.globalMaxTimeMs ?? chain.maxTotalTimeMs
    if (state.elapsedMs >= maxTime) {
      const decision: EscalationDecision = {
        action: 'fail',
        level: 4,
        maxTimeExceeded: true,
        message: `Max time exceeded (${state.elapsedMs}ms >= ${maxTime}ms)`,
        isFinal: true,
      }
      this.recordMetric(state, decision)
      return decision
    }

    // Get current step
    const stepIndex = Math.min(state.currentLevel - 1, chain.steps.length - 1)
    const step = chain.steps[stepIndex]

    // Build decision
    const decision: EscalationDecision = {
      action: step.action,
      level: state.currentLevel as EscalationLevel,
      maxTimeExceeded: false,
      message: step.description,
      isFinal: step.action === 'fail' || step.action === 'skip',
    }

    // Calculate new timeout
    if (step.timeoutMultiplier > 0) {
      decision.newTimeoutMs = Math.round(state.currentTimeoutMs * step.timeoutMultiplier)
      state.currentTimeoutMs = decision.newTimeoutMs
    }

    // Add fallback model if switching
    if (step.action === 'switch_model' && step.fallbackModel) {
      decision.newModel = step.fallbackModel
      state.modelsAttempted.push(step.fallbackModel)
    }

    // Advance level for next timeout
    if (state.currentLevel < 4) {
      state.currentLevel = (state.currentLevel + 1) as EscalationLevel
    }

    // Record metrics
    this.recordMetric(state, decision)

    // Callback
    if (this.onEscalate) {
      this.onEscalate(state, decision)
    }

    log.info(
      `Operation ${operationId} escalated to level ${decision.level}: ${decision.action} - ${decision.message}`
    )

    return decision
  }

  /**
   * Mark operation as completed (success or failure)
   */
  completeOperation(operationId: string, success: boolean): void {
    const state = this.activeOperations.get(operationId)
    if (!state) return

    // Record final level for avg calculation
    this.completedLevels.push(state.currentLevel)

    if (success && state.timeoutCount > 0) {
      this.metrics.recoveredCount++
    } else if (!success) {
      this.metrics.failedCount++
    }

    // Update average
    if (this.completedLevels.length > 0) {
      this.metrics.avgLevelReached =
        this.completedLevels.reduce((a, b) => a + b, 0) / this.completedLevels.length
    }

    this.activeOperations.delete(operationId)
    log.debug(`Completed operation ${operationId}, success: ${success}`)
  }

  /**
   * Get state for an operation
   */
  getOperationState(operationId: string): EscalationState | undefined {
    return this.activeOperations.get(operationId)
  }

  // ===========================================================================
  // Chain Management
  // ===========================================================================

  /**
   * Add or update a chain
   */
  addChain(chain: EscalationChain): void {
    this.chains.set(chain.id, chain)
  }

  /**
   * Get a chain by ID
   */
  getChain(id: string): EscalationChain | undefined {
    return this.chains.get(id)
  }

  /**
   * Get all chains
   */
  getChains(): EscalationChain[] {
    return Array.from(this.chains.values())
  }

  // ===========================================================================
  // Metrics
  // ===========================================================================

  /**
   * Record a metric
   */
  private recordMetric(_state: EscalationState, decision: EscalationDecision): void {
    this.metrics.totalEscalations++
    this.metrics.byLevel[decision.level]++
    this.metrics.byAction[decision.action]++
  }

  /**
   * Get current metrics
   */
  getMetrics(): EscalationMetrics {
    return { ...this.metrics }
  }

  /**
   * Reset metrics
   */
  resetMetrics(): void {
    this.metrics = {
      totalEscalations: 0,
      byLevel: { 1: 0, 2: 0, 3: 0, 4: 0 },
      byAction: {
        extend: 0,
        switch_model: 0,
        use_cache: 0,
        notify: 0,
        fail: 0,
        skip: 0,
      },
      recoveredCount: 0,
      failedCount: 0,
      avgLevelReached: 0,
    }
    this.completedLevels = []
  }

  /**
   * Get active operation count
   */
  getActiveCount(): number {
    return this.activeOperations.size
  }

  /**
   * Clear all active operations
   */
  clearActive(): void {
    this.activeOperations.clear()
  }
}

// =============================================================================
// Factory Functions
// =============================================================================

/** Singleton instance */
let defaultManager: TimeoutEscalationManager | null = null

/**
 * Get or create the default timeout escalation manager
 */
export function getTimeoutEscalationManager(config?: EscalationConfig): TimeoutEscalationManager {
  if (!defaultManager) {
    defaultManager = new TimeoutEscalationManager(config)
  }
  return defaultManager
}

/**
 * Reset the default manager (for testing)
 */
export function resetTimeoutEscalationManager(): void {
  defaultManager = null
}

/**
 * Create a new timeout escalation manager
 */
export function createTimeoutEscalationManager(
  config?: EscalationConfig
): TimeoutEscalationManager {
  return new TimeoutEscalationManager(config)
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Select appropriate chain based on operation characteristics
 */
export function selectChain(options: {
  complexity?: 'low' | 'medium' | 'high' | 'critical'
  timeSensitive?: boolean
  agentType?: string
}): string {
  const { complexity = 'medium', timeSensitive = false, agentType } = options

  // Time-sensitive operations use aggressive chain
  if (timeSensitive) {
    return 'aggressive'
  }

  // Complex operations use patient chain
  if (complexity === 'high' || complexity === 'critical') {
    return 'patient'
  }

  // Certain agent types are inherently complex
  if (agentType === 'operator' || agentType === 'validator') {
    return 'patient'
  }

  return 'default'
}

/**
 * Describe an escalation decision
 */
export function describeEscalationDecision(decision: EscalationDecision): string {
  const lines: string[] = [`Level ${decision.level}: ${decision.action}`, `  ${decision.message}`]

  if (decision.newTimeoutMs) {
    lines.push(`  New timeout: ${(decision.newTimeoutMs / 1000).toFixed(1)}s`)
  }

  if (decision.newModel) {
    lines.push(`  New model: ${decision.newModel}`)
  }

  if (decision.maxTimeExceeded) {
    lines.push(`  ⚠️ Max time exceeded`)
  }

  if (decision.isFinal) {
    lines.push(`  ⏹️ Final escalation`)
  }

  return lines.join('\n')
}

/**
 * Describe escalation metrics
 */
export function describeEscalationMetrics(metrics: EscalationMetrics): string {
  const lines: string[] = [
    `Total Escalations: ${metrics.totalEscalations}`,
    `Recovered: ${metrics.recoveredCount}`,
    `Failed: ${metrics.failedCount}`,
    `Avg Level: ${metrics.avgLevelReached.toFixed(2)}`,
    '',
    'By Level:',
    ...Object.entries(metrics.byLevel).map(([level, count]) => `  L${level}: ${count}`),
    '',
    'By Action:',
    ...Object.entries(metrics.byAction)
      .filter(([, count]) => count > 0)
      .map(([action, count]) => `  ${action}: ${count}`),
  ]

  return lines.join('\n')
}
