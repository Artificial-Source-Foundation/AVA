/**
 * Delta9 Automatic Rollback Triggers
 *
 * Monitors mission state and automatically triggers rollback
 * when configurable failure conditions are met.
 *
 * Trigger Conditions:
 * - consecutive_failures: N consecutive task failures
 * - test_failure: Critical test failure detected
 * - budget_exceeded: Budget limit exceeded
 * - timeout_cascade: Multiple timeouts in short period
 * - error_rate: Error rate exceeds threshold
 *
 * Actions:
 * - checkpoint_restore: Restore to last checkpoint
 * - notify: Send notification (no auto-action)
 * - abort: Abort the mission
 * - pause: Pause mission for human review
 */

import { getNamedLogger } from './logger.js'

const log = getNamedLogger('rollback-triggers')

// =============================================================================
// Types
// =============================================================================

/** Trigger condition types */
export type TriggerCondition =
  | 'consecutive_failures'
  | 'test_failure'
  | 'budget_exceeded'
  | 'timeout_cascade'
  | 'error_rate'

/** Actions to take when trigger fires */
export type TriggerAction = 'checkpoint_restore' | 'notify' | 'abort' | 'pause'

/** Rollback trigger definition */
export interface RollbackTrigger {
  /** Unique trigger ID */
  id: string
  /** Condition that triggers rollback */
  condition: TriggerCondition
  /** Threshold value for the condition */
  threshold: number
  /** Action to take when triggered */
  action: TriggerAction
  /** Whether this trigger is enabled */
  enabled: boolean
  /** Optional time window in ms (for rate-based triggers) */
  windowMs?: number
  /** Optional cooldown after firing (prevents rapid re-triggering) */
  cooldownMs?: number
  /** Description of the trigger */
  description?: string
}

/** Event that could trigger a rollback */
export interface TriggerEvent {
  /** Event type */
  type: 'task_failed' | 'test_failed' | 'budget_update' | 'timeout' | 'error'
  /** Timestamp */
  timestamp: number
  /** Related task ID (if applicable) */
  taskId?: string
  /** Error message (if applicable) */
  error?: string
  /** Additional metadata */
  metadata?: Record<string, unknown>
}

/** Result of checking triggers */
export interface TriggerCheckResult {
  /** Whether any trigger fired */
  triggered: boolean
  /** Triggers that fired */
  firedTriggers: Array<{
    trigger: RollbackTrigger
    reason: string
  }>
  /** Recommended action (highest priority) */
  recommendedAction?: TriggerAction
  /** Summary message */
  summary: string
}

/** Rollback execution result */
export interface RollbackResult {
  /** Whether rollback succeeded */
  success: boolean
  /** Action that was taken */
  action: TriggerAction
  /** Trigger that caused the rollback */
  trigger: RollbackTrigger
  /** Checkpoint restored to (if applicable) */
  checkpointName?: string
  /** Error if rollback failed */
  error?: string
  /** Duration of rollback operation */
  durationMs: number
}

/** Configuration for the rollback trigger manager */
export interface RollbackTriggerConfig {
  /** Custom triggers (merged with defaults) */
  triggers?: RollbackTrigger[]
  /** Whether to use default triggers */
  useDefaults?: boolean
  /** Maximum events to keep in history */
  maxEventHistory?: number
  /** Callback when rollback is triggered */
  onRollback?: (result: RollbackResult) => void | Promise<void>
}

// =============================================================================
// Constants
// =============================================================================

/** Default rollback triggers */
export const DEFAULT_TRIGGERS: RollbackTrigger[] = [
  {
    id: 'consecutive_failures_3',
    condition: 'consecutive_failures',
    threshold: 3,
    action: 'checkpoint_restore',
    enabled: true,
    cooldownMs: 60000, // 1 minute cooldown
    description: 'Restore checkpoint after 3 consecutive task failures',
  },
  {
    id: 'budget_exceeded',
    condition: 'budget_exceeded',
    threshold: 1.0, // 100% of budget
    action: 'abort',
    enabled: true,
    description: 'Abort mission when budget is exceeded',
  },
  {
    id: 'timeout_cascade_5',
    condition: 'timeout_cascade',
    threshold: 5,
    action: 'pause',
    enabled: true,
    windowMs: 300000, // 5 minute window
    cooldownMs: 120000, // 2 minute cooldown
    description: 'Pause mission after 5 timeouts in 5 minutes',
  },
  {
    id: 'error_rate_high',
    condition: 'error_rate',
    threshold: 0.5, // 50% error rate
    action: 'notify',
    enabled: true,
    windowMs: 600000, // 10 minute window
    description: 'Notify when error rate exceeds 50%',
  },
  {
    id: 'test_failure_critical',
    condition: 'test_failure',
    threshold: 1,
    action: 'checkpoint_restore',
    enabled: true,
    cooldownMs: 30000, // 30 second cooldown
    description: 'Restore checkpoint on critical test failure',
  },
]

/** Action priority (lower = higher priority) */
const ACTION_PRIORITY: Record<TriggerAction, number> = {
  abort: 1,
  checkpoint_restore: 2,
  pause: 3,
  notify: 4,
}

// =============================================================================
// Rollback Trigger Manager
// =============================================================================

export class RollbackTriggerManager {
  private triggers: Map<string, RollbackTrigger> = new Map()
  private eventHistory: TriggerEvent[] = []
  private consecutiveFailures = 0
  private lastTriggerTimes: Map<string, number> = new Map()
  private maxEventHistory: number
  private onRollback?: (result: RollbackResult) => void | Promise<void>

  constructor(config: RollbackTriggerConfig = {}) {
    const { triggers = [], useDefaults = true, maxEventHistory = 1000, onRollback } = config

    this.maxEventHistory = maxEventHistory
    this.onRollback = onRollback

    // Load default triggers if enabled
    if (useDefaults) {
      for (const trigger of DEFAULT_TRIGGERS) {
        this.triggers.set(trigger.id, { ...trigger })
      }
    }

    // Load custom triggers (overwrite defaults with same ID)
    for (const trigger of triggers) {
      this.triggers.set(trigger.id, { ...trigger })
    }
  }

  // ===========================================================================
  // Trigger Management
  // ===========================================================================

  /**
   * Add or update a trigger
   */
  addTrigger(trigger: RollbackTrigger): void {
    this.triggers.set(trigger.id, { ...trigger })
    log.debug(`Added trigger: ${trigger.id}`)
  }

  /**
   * Remove a trigger
   */
  removeTrigger(id: string): boolean {
    const removed = this.triggers.delete(id)
    if (removed) {
      log.debug(`Removed trigger: ${id}`)
    }
    return removed
  }

  /**
   * Enable or disable a trigger
   */
  setTriggerEnabled(id: string, enabled: boolean): boolean {
    const trigger = this.triggers.get(id)
    if (!trigger) return false

    trigger.enabled = enabled
    log.debug(`Trigger ${id} ${enabled ? 'enabled' : 'disabled'}`)
    return true
  }

  /**
   * Get all triggers
   */
  getTriggers(): RollbackTrigger[] {
    return Array.from(this.triggers.values())
  }

  /**
   * Get a specific trigger
   */
  getTrigger(id: string): RollbackTrigger | undefined {
    return this.triggers.get(id)
  }

  // ===========================================================================
  // Event Recording
  // ===========================================================================

  /**
   * Record an event and check triggers
   */
  recordEvent(event: TriggerEvent): TriggerCheckResult {
    // Add to history
    this.eventHistory.push(event)

    // Trim history if needed
    if (this.eventHistory.length > this.maxEventHistory) {
      this.eventHistory = this.eventHistory.slice(-this.maxEventHistory)
    }

    // Update consecutive failure count
    // Only increment on task failures, only reset on explicit success events
    // Don't reset on budget_update, timeout, or error events
    if (event.type === 'task_failed') {
      this.consecutiveFailures++
    }

    // Check all triggers
    return this.checkTriggers()
  }

  /**
   * Record a task failure
   */
  recordTaskFailure(taskId: string, error?: string): TriggerCheckResult {
    return this.recordEvent({
      type: 'task_failed',
      timestamp: Date.now(),
      taskId,
      error,
    })
  }

  /**
   * Record a test failure
   */
  recordTestFailure(taskId: string, error?: string): TriggerCheckResult {
    return this.recordEvent({
      type: 'test_failed',
      timestamp: Date.now(),
      taskId,
      error,
    })
  }

  /**
   * Record a budget update
   */
  recordBudgetUpdate(spent: number, limit: number): TriggerCheckResult {
    return this.recordEvent({
      type: 'budget_update',
      timestamp: Date.now(),
      metadata: { spent, limit, percentage: spent / limit },
    })
  }

  /**
   * Record a timeout
   */
  recordTimeout(taskId: string, timeoutMs: number): TriggerCheckResult {
    return this.recordEvent({
      type: 'timeout',
      timestamp: Date.now(),
      taskId,
      metadata: { timeoutMs },
    })
  }

  /**
   * Record a task success (resets consecutive failures)
   */
  recordTaskSuccess(taskId: string): void {
    this.consecutiveFailures = 0
    log.debug(`Task ${taskId} succeeded, reset consecutive failures`)
  }

  // ===========================================================================
  // Trigger Checking
  // ===========================================================================

  /**
   * Check all triggers against current state
   */
  checkTriggers(): TriggerCheckResult {
    const firedTriggers: TriggerCheckResult['firedTriggers'] = []
    const now = Date.now()

    for (const trigger of this.triggers.values()) {
      if (!trigger.enabled) continue

      // Check cooldown
      const lastFired = this.lastTriggerTimes.get(trigger.id) ?? 0
      if (trigger.cooldownMs && now - lastFired < trigger.cooldownMs) {
        continue
      }

      const result = this.checkSingleTrigger(trigger, now)
      if (result.fired) {
        firedTriggers.push({
          trigger,
          reason: result.reason,
        })
        this.lastTriggerTimes.set(trigger.id, now)
        log.warn(`Trigger fired: ${trigger.id} - ${result.reason}`)
      }
    }

    // Sort by action priority
    firedTriggers.sort(
      (a, b) => ACTION_PRIORITY[a.trigger.action] - ACTION_PRIORITY[b.trigger.action]
    )

    const triggered = firedTriggers.length > 0
    const recommendedAction = firedTriggers[0]?.trigger.action

    return {
      triggered,
      firedTriggers,
      recommendedAction,
      summary: triggered
        ? `${firedTriggers.length} trigger(s) fired, recommended action: ${recommendedAction}`
        : 'No triggers fired',
    }
  }

  /**
   * Check a single trigger
   */
  private checkSingleTrigger(
    trigger: RollbackTrigger,
    now: number
  ): { fired: boolean; reason: string } {
    switch (trigger.condition) {
      case 'consecutive_failures':
        if (this.consecutiveFailures >= trigger.threshold) {
          return {
            fired: true,
            reason: `${this.consecutiveFailures} consecutive failures (threshold: ${trigger.threshold})`,
          }
        }
        break

      case 'test_failure': {
        const recentTestFailures = this.getRecentEvents('test_failed', trigger.windowMs ?? 60000)
        if (recentTestFailures.length >= trigger.threshold) {
          return {
            fired: true,
            reason: `${recentTestFailures.length} test failure(s) (threshold: ${trigger.threshold})`,
          }
        }
        break
      }

      case 'budget_exceeded': {
        const budgetEvents = this.eventHistory.filter((e) => e.type === 'budget_update')
        const latestBudget = budgetEvents[budgetEvents.length - 1]
        if (latestBudget?.metadata) {
          const percentage = latestBudget.metadata.percentage as number
          if (percentage >= trigger.threshold) {
            return {
              fired: true,
              reason: `Budget at ${(percentage * 100).toFixed(1)}% (threshold: ${trigger.threshold * 100}%)`,
            }
          }
        }
        break
      }

      case 'timeout_cascade': {
        const recentTimeouts = this.getRecentEvents('timeout', trigger.windowMs ?? 300000)
        if (recentTimeouts.length >= trigger.threshold) {
          return {
            fired: true,
            reason: `${recentTimeouts.length} timeouts in ${(trigger.windowMs ?? 300000) / 1000}s window (threshold: ${trigger.threshold})`,
          }
        }
        break
      }

      case 'error_rate': {
        const windowMs = trigger.windowMs ?? 600000
        const recentEvents = this.eventHistory.filter((e) => now - e.timestamp < windowMs)
        const errorEvents = recentEvents.filter(
          (e) => e.type === 'task_failed' || e.type === 'error'
        )

        // Require at least 5 events to calculate meaningful error rate
        const minEventsForErrorRate = 5
        if (recentEvents.length >= minEventsForErrorRate) {
          const errorRate = errorEvents.length / recentEvents.length
          if (errorRate >= trigger.threshold) {
            return {
              fired: true,
              reason: `Error rate ${(errorRate * 100).toFixed(1)}% (threshold: ${trigger.threshold * 100}%)`,
            }
          }
        }
        break
      }
    }

    return { fired: false, reason: '' }
  }

  /**
   * Get recent events of a specific type
   */
  private getRecentEvents(type: TriggerEvent['type'], windowMs: number): TriggerEvent[] {
    const cutoff = Date.now() - windowMs
    return this.eventHistory.filter((e) => e.type === type && e.timestamp >= cutoff)
  }

  // ===========================================================================
  // Rollback Execution
  // ===========================================================================

  /**
   * Execute rollback based on trigger result
   */
  async executeRollback(
    checkResult: TriggerCheckResult,
    options: {
      /** Function to restore checkpoint */
      restoreCheckpoint?: (name?: string) => Promise<boolean>
      /** Function to abort mission */
      abortMission?: () => Promise<void>
      /** Function to pause mission */
      pauseMission?: () => Promise<void>
      /** Function to send notification */
      notify?: (message: string) => Promise<void>
      /** Checkpoint name to restore (if not provided, uses latest) */
      checkpointName?: string
    } = {}
  ): Promise<RollbackResult | null> {
    if (!checkResult.triggered || !checkResult.recommendedAction) {
      return null
    }

    const trigger = checkResult.firedTriggers[0].trigger
    const startTime = Date.now()

    try {
      switch (checkResult.recommendedAction) {
        case 'checkpoint_restore':
          if (options.restoreCheckpoint) {
            const success = await options.restoreCheckpoint(options.checkpointName)
            const result: RollbackResult = {
              success,
              action: 'checkpoint_restore',
              trigger,
              checkpointName: options.checkpointName,
              durationMs: Date.now() - startTime,
            }

            if (this.onRollback) {
              await this.onRollback(result)
            }

            // Reset consecutive failures on successful restore
            if (success) {
              this.consecutiveFailures = 0
            }

            return result
          }
          break

        case 'abort':
          if (options.abortMission) {
            await options.abortMission()
            const result: RollbackResult = {
              success: true,
              action: 'abort',
              trigger,
              durationMs: Date.now() - startTime,
            }

            if (this.onRollback) {
              await this.onRollback(result)
            }

            return result
          }
          break

        case 'pause':
          if (options.pauseMission) {
            await options.pauseMission()
            const result: RollbackResult = {
              success: true,
              action: 'pause',
              trigger,
              durationMs: Date.now() - startTime,
            }

            if (this.onRollback) {
              await this.onRollback(result)
            }

            return result
          }
          break

        case 'notify':
          if (options.notify) {
            const message = `Rollback trigger fired: ${trigger.description || trigger.id}`
            await options.notify(message)
            const result: RollbackResult = {
              success: true,
              action: 'notify',
              trigger,
              durationMs: Date.now() - startTime,
            }

            if (this.onRollback) {
              await this.onRollback(result)
            }

            return result
          }
          break
      }

      // No handler available for the action
      return {
        success: false,
        action: checkResult.recommendedAction,
        trigger,
        error: `No handler available for action: ${checkResult.recommendedAction}`,
        durationMs: Date.now() - startTime,
      }
    } catch (error) {
      const result: RollbackResult = {
        success: false,
        action: checkResult.recommendedAction,
        trigger,
        error: error instanceof Error ? error.message : String(error),
        durationMs: Date.now() - startTime,
      }

      if (this.onRollback) {
        await this.onRollback(result)
      }

      return result
    }
  }

  // ===========================================================================
  // State & Stats
  // ===========================================================================

  /**
   * Get current state summary
   */
  getState(): {
    consecutiveFailures: number
    eventCount: number
    enabledTriggers: number
    totalTriggers: number
    recentEvents: TriggerEvent[]
  } {
    return {
      consecutiveFailures: this.consecutiveFailures,
      eventCount: this.eventHistory.length,
      enabledTriggers: Array.from(this.triggers.values()).filter((t) => t.enabled).length,
      totalTriggers: this.triggers.size,
      recentEvents: this.eventHistory.slice(-10),
    }
  }

  /**
   * Reset all state
   */
  reset(): void {
    this.eventHistory = []
    this.consecutiveFailures = 0
    this.lastTriggerTimes.clear()
    log.debug('Rollback trigger manager reset')
  }

  /**
   * Clear event history only
   */
  clearHistory(): void {
    this.eventHistory = []
    this.consecutiveFailures = 0
    log.debug('Event history cleared')
  }
}

// =============================================================================
// Factory Functions
// =============================================================================

/** Singleton instance */
let defaultManager: RollbackTriggerManager | null = null

/**
 * Get or create the default rollback trigger manager
 */
export function getRollbackTriggerManager(config?: RollbackTriggerConfig): RollbackTriggerManager {
  if (!defaultManager) {
    defaultManager = new RollbackTriggerManager(config)
  }
  return defaultManager
}

/**
 * Reset the default rollback trigger manager (for testing)
 */
export function resetRollbackTriggerManager(): void {
  defaultManager = null
}

/**
 * Create a new rollback trigger manager
 */
export function createRollbackTriggerManager(
  config?: RollbackTriggerConfig
): RollbackTriggerManager {
  return new RollbackTriggerManager(config)
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Describe a trigger in human-readable format
 */
export function describeTrigger(trigger: RollbackTrigger): string {
  const status = trigger.enabled ? '✓' : '✗'
  const condition = trigger.condition.replace(/_/g, ' ')
  return `[${status}] ${trigger.id}: ${condition} >= ${trigger.threshold} → ${trigger.action}`
}

/**
 * Describe a rollback result
 */
export function describeRollbackResult(result: RollbackResult): string {
  const lines: string[] = []

  if (result.success) {
    lines.push(`✅ Rollback successful: ${result.action}`)
  } else {
    lines.push(`❌ Rollback failed: ${result.action}`)
    if (result.error) {
      lines.push(`   Error: ${result.error}`)
    }
  }

  lines.push(`   Trigger: ${result.trigger.id}`)
  lines.push(`   Duration: ${result.durationMs}ms`)

  if (result.checkpointName) {
    lines.push(`   Checkpoint: ${result.checkpointName}`)
  }

  return lines.join('\n')
}
