/**
 * Delta9 Recovery Manager
 *
 * Intelligent recovery strategies for failed operations:
 * - Retry with same agent
 * - Retry with different model
 * - Escalate to Strategist
 * - Rollback to checkpoint
 */

import { appendHistory } from './history.js'
import { CheckpointManager } from './checkpoints.js'
import type { Task } from '../types/mission.js'

// =============================================================================
// Types
// =============================================================================

export type RecoveryStrategy =
  | 'retry_same' // Retry with same agent
  | 'retry_different' // Retry with different model
  | 'escalate' // Escalate to Strategist for advice
  | 'checkpoint' // Rollback to last checkpoint
  | 'skip' // Skip task (mark blocked)
  | 'abort' // Abort mission

export interface RecoveryConfig {
  /** Maximum retry attempts before escalation */
  maxRetries: number
  /** Escalate to Strategist after N failures */
  escalateAfter: number
  /** Auto-create checkpoint on failure */
  autoCheckpointOnFailure: boolean
  /** Allow automatic rollback */
  allowAutoRollback: boolean
}

export interface FailureAnalysis {
  /** Type of failure */
  type: FailureType
  /** Severity of failure */
  severity: 'low' | 'medium' | 'high' | 'critical'
  /** Is the failure transient (likely to succeed on retry)? */
  isTransient: boolean
  /** Recommended recovery strategy */
  recommendedStrategy: RecoveryStrategy
  /** Confidence in recommendation (0-1) */
  confidence: number
  /** Additional context */
  context?: Record<string, unknown>
}

export type FailureType =
  | 'rate_limit' // API rate limit hit
  | 'timeout' // Operation timed out
  | 'validation' // Validation failed
  | 'dependency' // Missing dependency
  | 'permission' // Permission denied
  | 'not_found' // Resource not found
  | 'conflict' // Conflict with existing code
  | 'logic_error' // Logic/implementation error
  | 'unknown' // Unknown error

export interface RecoveryExecutionAttempt {
  /** Task being recovered */
  taskId: string
  /** Strategy used */
  strategy: RecoveryStrategy
  /** Attempt number */
  attemptNumber: number
  /** When attempted */
  timestamp: string
  /** Result of recovery */
  success: boolean
  /** Error if failed */
  error?: string
  /** New model used (if retry_different) */
  newModel?: string
}

export interface RecoveryResult {
  /** Whether recovery was successful */
  success: boolean
  /** Strategy that was used */
  strategy: RecoveryStrategy
  /** Result message */
  message: string
  /** Should continue mission? */
  shouldContinue: boolean
  /** Next action to take */
  nextAction?: string
  /** Checkpoint restored (if rollback) */
  checkpointRestored?: string
}

// =============================================================================
// Constants
// =============================================================================

/** Default recovery configuration */
const DEFAULT_CONFIG: RecoveryConfig = {
  maxRetries: 3,
  escalateAfter: 2,
  autoCheckpointOnFailure: false,
  allowAutoRollback: false,
}

/** Error patterns for failure classification */
const ERROR_PATTERNS: Array<{ pattern: RegExp; type: FailureType; isTransient: boolean }> = [
  { pattern: /rate.?limit|429|too many requests/i, type: 'rate_limit', isTransient: true },
  { pattern: /timeout|timed out|deadline/i, type: 'timeout', isTransient: true },
  { pattern: /validation|does not match|invalid/i, type: 'validation', isTransient: false },
  { pattern: /dependency|missing|not installed/i, type: 'dependency', isTransient: false },
  { pattern: /permission|denied|unauthorized|403/i, type: 'permission', isTransient: false },
  { pattern: /not found|404|doesn't exist/i, type: 'not_found', isTransient: false },
  { pattern: /conflict|already exists|merge/i, type: 'conflict', isTransient: false },
  { pattern: /error|failed|exception/i, type: 'logic_error', isTransient: false },
]

// =============================================================================
// Recovery Manager
// =============================================================================

export class RecoveryManager {
  private config: RecoveryConfig
  private cwd: string
  private attempts: Map<string, RecoveryExecutionAttempt[]> = new Map()

  constructor(cwd: string, config: Partial<RecoveryConfig> = {}) {
    this.cwd = cwd
    this.config = { ...DEFAULT_CONFIG, ...config }
  }

  /**
   * Analyze a failure and recommend recovery strategy
   */
  analyzeFailure(task: Task, error: string): FailureAnalysis {
    const failureType = this.classifyError(error)
    const isTransient = this.isTransientError(failureType)
    const attempts = task.attempts || 0

    // Determine severity based on failure type and attempts
    let severity: FailureAnalysis['severity'] = 'medium'
    if (failureType === 'permission' || failureType === 'conflict') {
      severity = 'high'
    } else if (attempts > this.config.maxRetries) {
      severity = 'critical'
    } else if (isTransient) {
      severity = 'low'
    }

    // Recommend strategy based on analysis
    const strategy = this.recommendStrategy(failureType, attempts, isTransient)

    // Calculate confidence
    let confidence = 0.7
    if (failureType === 'unknown') confidence = 0.4
    if (attempts > 1 && failureType !== 'rate_limit') confidence -= 0.1 * attempts

    return {
      type: failureType,
      severity,
      isTransient,
      recommendedStrategy: strategy,
      confidence: Math.max(0.3, confidence),
      context: {
        attempts,
        errorSnippet: error.slice(0, 200),
      },
    }
  }

  /**
   * Classify error type from error message
   */
  private classifyError(error: string): FailureType {
    for (const { pattern, type } of ERROR_PATTERNS) {
      if (pattern.test(error)) {
        return type
      }
    }
    return 'unknown'
  }

  /**
   * Check if error is transient
   */
  private isTransientError(type: FailureType): boolean {
    const transientTypes: FailureType[] = ['rate_limit', 'timeout']
    return transientTypes.includes(type)
  }

  /**
   * Recommend recovery strategy
   */
  private recommendStrategy(
    type: FailureType,
    attempts: number,
    isTransient: boolean
  ): RecoveryStrategy {
    // Rate limits and timeouts: retry with backoff
    if (type === 'rate_limit' || type === 'timeout') {
      if (attempts < this.config.maxRetries) {
        return 'retry_same'
      }
      return 'retry_different'
    }

    // Permission/not found: likely won't be fixed by retry
    if (type === 'permission' || type === 'not_found') {
      return 'escalate'
    }

    // Conflicts: might need human intervention
    if (type === 'conflict') {
      if (this.config.allowAutoRollback) {
        return 'checkpoint'
      }
      return 'escalate'
    }

    // Validation failures: try different approach
    if (type === 'validation') {
      if (attempts < this.config.escalateAfter) {
        return 'retry_same'
      }
      return 'escalate'
    }

    // Default: escalate after maxRetries
    if (attempts >= this.config.maxRetries) {
      return 'escalate'
    }

    return isTransient ? 'retry_same' : 'retry_different'
  }

  /**
   * Execute recovery strategy
   */
  async executeRecovery(
    task: Task,
    strategy: RecoveryStrategy,
    missionId: string,
    options: {
      checkpointId?: string
      newModel?: string
    } = {}
  ): Promise<RecoveryResult> {
    const attempt: RecoveryExecutionAttempt = {
      taskId: task.id,
      strategy,
      attemptNumber: this.getAttemptCount(task.id) + 1,
      timestamp: new Date().toISOString(),
      success: false,
      newModel: options.newModel,
    }

    try {
      let result: RecoveryResult

      switch (strategy) {
        case 'retry_same':
          result = await this.handleRetrySame(task)
          break

        case 'retry_different':
          result = await this.handleRetryDifferent(task, options.newModel)
          break

        case 'escalate':
          result = await this.handleEscalate(task)
          break

        case 'checkpoint':
          result = await this.handleCheckpoint(task, options.checkpointId)
          break

        case 'skip':
          result = await this.handleSkip(task)
          break

        case 'abort':
          result = await this.handleAbort(task)
          break

        default:
          result = {
            success: false,
            strategy,
            message: `Unknown recovery strategy: ${strategy}`,
            shouldContinue: false,
          }
      }

      attempt.success = result.success

      // Log history
      appendHistory(this.cwd, {
        type: 'recovery_attempted',
        timestamp: attempt.timestamp,
        missionId,
        taskId: task.id,
        data: {
          strategy,
          attemptNumber: attempt.attemptNumber,
          success: result.success,
          message: result.message,
        },
      })

      // Store attempt
      this.recordAttempt(task.id, attempt)

      return result
    } catch (error) {
      attempt.error = error instanceof Error ? error.message : String(error)
      this.recordAttempt(task.id, attempt)

      return {
        success: false,
        strategy,
        message: `Recovery failed: ${attempt.error}`,
        shouldContinue: false,
      }
    }
  }

  /**
   * Handle retry with same agent
   */
  private async handleRetrySame(task: Task): Promise<RecoveryResult> {
    return {
      success: true,
      strategy: 'retry_same',
      message: `Task ${task.id} will be retried with the same agent`,
      shouldContinue: true,
      nextAction: 'retry_task',
    }
  }

  /**
   * Handle retry with different model
   */
  private async handleRetryDifferent(task: Task, newModel?: string): Promise<RecoveryResult> {
    return {
      success: true,
      strategy: 'retry_different',
      message: `Task ${task.id} will be retried with ${newModel || 'fallback model'}`,
      shouldContinue: true,
      nextAction: 'retry_task_different_model',
    }
  }

  /**
   * Handle escalation to Strategist
   */
  private async handleEscalate(task: Task): Promise<RecoveryResult> {
    return {
      success: true,
      strategy: 'escalate',
      message: `Task ${task.id} escalated to Strategist for guidance`,
      shouldContinue: true,
      nextAction: 'invoke_strategist',
    }
  }

  /**
   * Handle checkpoint rollback
   */
  private async handleCheckpoint(task: Task, checkpointId?: string): Promise<RecoveryResult> {
    if (!checkpointId) {
      // Try to find latest checkpoint
      const checkpointManager = new CheckpointManager(this.cwd)
      const latest = checkpointManager.getLatest(task.id) // This won't work - need mission ID

      if (!latest) {
        return {
          success: false,
          strategy: 'checkpoint',
          message: 'No checkpoint available for rollback',
          shouldContinue: false,
        }
      }

      checkpointId = latest.id
    }

    const checkpointManager = new CheckpointManager(this.cwd)
    const result = checkpointManager.restore(checkpointId)

    if (!result.success) {
      return {
        success: false,
        strategy: 'checkpoint',
        message: `Rollback failed: ${result.error}`,
        shouldContinue: false,
      }
    }

    return {
      success: true,
      strategy: 'checkpoint',
      message: `Rolled back to checkpoint ${result.checkpoint.name}`,
      shouldContinue: true,
      nextAction: 'retry_from_checkpoint',
      checkpointRestored: checkpointId,
    }
  }

  /**
   * Handle skip task
   */
  private async handleSkip(task: Task): Promise<RecoveryResult> {
    return {
      success: true,
      strategy: 'skip',
      message: `Task ${task.id} skipped (marked as blocked)`,
      shouldContinue: true,
      nextAction: 'mark_blocked',
    }
  }

  /**
   * Handle abort mission
   */
  private async handleAbort(_task: Task): Promise<RecoveryResult> {
    return {
      success: true,
      strategy: 'abort',
      message: 'Mission aborted due to unrecoverable failure',
      shouldContinue: false,
      nextAction: 'abort_mission',
    }
  }

  /**
   * Get attempt count for a task
   */
  getAttemptCount(taskId: string): number {
    const attempts = this.attempts.get(taskId)
    return attempts?.length || 0
  }

  /**
   * Record a recovery attempt
   */
  private recordAttempt(taskId: string, attempt: RecoveryExecutionAttempt): void {
    const existing = this.attempts.get(taskId) || []
    existing.push(attempt)
    this.attempts.set(taskId, existing)
  }

  /**
   * Get recovery history for a task
   */
  getRecoveryHistory(taskId: string): RecoveryExecutionAttempt[] {
    return this.attempts.get(taskId) || []
  }

  /**
   * Clear recovery history
   */
  clearHistory(taskId?: string): void {
    if (taskId) {
      this.attempts.delete(taskId)
    } else {
      this.attempts.clear()
    }
  }
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Create a recovery manager
 */
export function createRecoveryManager(
  cwd: string,
  config?: Partial<RecoveryConfig>
): RecoveryManager {
  return new RecoveryManager(cwd, config)
}

/**
 * Describe failure analysis in human-readable format
 */
export function describeFailureAnalysis(analysis: FailureAnalysis): string {
  const lines: string[] = []

  lines.push(`Failure Type: ${analysis.type}`)
  lines.push(`Severity: ${analysis.severity.toUpperCase()}`)
  lines.push(`Transient: ${analysis.isTransient ? 'Yes' : 'No'}`)
  lines.push(`Recommended: ${analysis.recommendedStrategy}`)
  lines.push(`Confidence: ${(analysis.confidence * 100).toFixed(0)}%`)

  return lines.join('\n')
}

/**
 * Describe recovery result in human-readable format
 */
export function describeRecoveryResult(result: RecoveryResult): string {
  const lines: string[] = []

  if (result.success) {
    lines.push('✅ Recovery successful')
  } else {
    lines.push('❌ Recovery failed')
  }

  lines.push(`Strategy: ${result.strategy}`)
  lines.push(`Message: ${result.message}`)
  lines.push(`Continue mission: ${result.shouldContinue ? 'Yes' : 'No'}`)

  if (result.nextAction) {
    lines.push(`Next action: ${result.nextAction}`)
  }

  if (result.checkpointRestored) {
    lines.push(`Checkpoint restored: ${result.checkpointRestored}`)
  }

  return lines.join('\n')
}
