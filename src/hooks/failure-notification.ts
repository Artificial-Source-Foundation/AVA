/**
 * Delta9 Failure Notification Hooks
 *
 * Provides mechanisms to track, aggregate, and notify about failures
 * across the system. Integrates with:
 * - Dead Letter Queue (for unrecoverable failures)
 * - Event Store (for persistence)
 * - External notification systems (webhooks, etc.)
 */

import { getNamedLogger } from '../lib/logger.js'

const log = getNamedLogger('failure-notification')

// =============================================================================
// Types
// =============================================================================

/** Failure severity levels */
export type FailureSeverity = 'low' | 'medium' | 'high' | 'critical'

/** Failure notification types */
export type FailureNotificationType =
  | 'task_failure'
  | 'agent_failure'
  | 'validation_failure'
  | 'budget_exceeded'
  | 'rate_limit'
  | 'timeout'
  | 'system_error'
  | 'checkpoint_failure'
  | 'dlq_added'

/** Failure notification payload */
export interface FailureNotification {
  /** Unique notification ID */
  id: string
  /** Type of failure */
  type: FailureNotificationType
  /** Severity level */
  severity: FailureSeverity
  /** Failure message */
  message: string
  /** Detailed error */
  error?: string
  /** Stack trace */
  stackTrace?: string
  /** When the failure occurred */
  timestamp: number
  /** Related task ID */
  taskId?: string
  /** Related session ID */
  sessionId?: string
  /** Related mission ID */
  missionId?: string
  /** Agent involved */
  agent?: string
  /** Number of similar failures recently */
  recentCount?: number
  /** Additional context */
  context?: Record<string, unknown>
  /** Whether notification was sent */
  notified: boolean
  /** Notification channels used */
  channels?: string[]
}

/** Notification handler function */
export type NotificationHandler = (
  notification: FailureNotification
) => void | Promise<void>

/** Aggregation rule for grouping similar failures */
export interface AggregationRule {
  /** Rule ID */
  id: string
  /** Match pattern for grouping */
  match: {
    type?: FailureNotificationType | FailureNotificationType[]
    severity?: FailureSeverity | FailureSeverity[]
    agent?: string
    messagePattern?: RegExp
  }
  /** Time window for aggregation in ms */
  windowMs: number
  /** Minimum failures before notifying */
  threshold: number
  /** Whether to suppress individual notifications */
  suppressIndividual: boolean
}

/** Configuration for failure notification hooks */
export interface FailureNotificationConfig {
  /** Notification handlers by channel */
  handlers?: Record<string, NotificationHandler>
  /** Severity threshold for immediate notification */
  immediateSeverity?: FailureSeverity
  /** Aggregation rules */
  aggregationRules?: AggregationRule[]
  /** Maximum notifications to store */
  maxNotifications?: number
  /** Default channels to use */
  defaultChannels?: string[]
  /** Callback when failure is recorded */
  onFailure?: (notification: FailureNotification) => void | Promise<void>
}

/** Aggregated failure group */
interface FailureGroup {
  key: string
  rule: AggregationRule
  failures: FailureNotification[]
  firstFailureAt: number
  lastFailureAt: number
  notified: boolean
}

// =============================================================================
// Failure Notification Manager
// =============================================================================

export class FailureNotificationManager {
  private notifications: FailureNotification[] = []
  private handlers: Map<string, NotificationHandler> = new Map()
  private aggregationRules: AggregationRule[] = []
  private failureGroups: Map<string, FailureGroup> = new Map()
  private maxNotifications: number
  private defaultChannels: string[]
  private immediateSeverity: FailureSeverity
  private onFailure?: (notification: FailureNotification) => void | Promise<void>
  private notificationCounter = 0

  constructor(config: FailureNotificationConfig = {}) {
    this.maxNotifications = config.maxNotifications ?? 1000
    this.defaultChannels = config.defaultChannels ?? ['log']
    this.immediateSeverity = config.immediateSeverity ?? 'high'
    this.aggregationRules = config.aggregationRules ?? []
    this.onFailure = config.onFailure

    // Register handlers
    if (config.handlers) {
      for (const [channel, handler] of Object.entries(config.handlers)) {
        this.handlers.set(channel, handler)
      }
    }

    // Default log handler
    if (!this.handlers.has('log')) {
      this.handlers.set('log', (n) => {
        const icon = { low: 'ℹ️', medium: '⚠️', high: '🔴', critical: '🚨' }[n.severity]
        log.warn(`${icon} [${n.type}] ${n.message}`, { notification: n })
      })
    }
  }

  // ===========================================================================
  // Handler Registration
  // ===========================================================================

  /**
   * Register a notification handler
   */
  registerHandler(channel: string, handler: NotificationHandler): void {
    this.handlers.set(channel, handler)
    log.debug(`Registered notification handler: ${channel}`)
  }

  /**
   * Unregister a notification handler
   */
  unregisterHandler(channel: string): boolean {
    const removed = this.handlers.delete(channel)
    if (removed) {
      log.debug(`Unregistered notification handler: ${channel}`)
    }
    return removed
  }

  /**
   * Get all registered channels
   */
  getChannels(): string[] {
    return Array.from(this.handlers.keys())
  }

  // ===========================================================================
  // Failure Recording
  // ===========================================================================

  /**
   * Record a failure
   */
  async record(params: {
    type: FailureNotificationType
    severity: FailureSeverity
    message: string
    error?: string
    stackTrace?: string
    taskId?: string
    sessionId?: string
    missionId?: string
    agent?: string
    context?: Record<string, unknown>
  }): Promise<FailureNotification> {
    const notification: FailureNotification = {
      id: `fn_${++this.notificationCounter}_${Date.now()}`,
      type: params.type,
      severity: params.severity,
      message: params.message,
      error: params.error,
      stackTrace: params.stackTrace,
      timestamp: Date.now(),
      taskId: params.taskId,
      sessionId: params.sessionId,
      missionId: params.missionId,
      agent: params.agent,
      context: params.context,
      notified: false,
    }

    // Add to history
    this.notifications.push(notification)
    this.trimNotifications()

    // Check aggregation rules
    const matchedRule = this.findMatchingRule(notification)
    let shouldNotify = false

    if (matchedRule) {
      const group = this.getOrCreateGroup(notification, matchedRule)
      group.failures.push(notification)
      group.lastFailureAt = notification.timestamp

      // Count recent failures for this type
      notification.recentCount = group.failures.length

      // Check if threshold reached
      if (group.failures.length >= matchedRule.threshold && !group.notified) {
        shouldNotify = true
        group.notified = true
      } else if (!matchedRule.suppressIndividual) {
        shouldNotify = this.shouldNotifyImmediately(notification)
      }
    } else {
      shouldNotify = this.shouldNotifyImmediately(notification)
    }

    // Send notification if needed
    if (shouldNotify) {
      await this.sendNotification(notification)
    }

    // Call onFailure callback
    if (this.onFailure) {
      await this.onFailure(notification)
    }

    return notification
  }

  /**
   * Record a task failure
   */
  async recordTaskFailure(params: {
    taskId: string
    message: string
    error?: string
    sessionId?: string
    missionId?: string
    agent?: string
    context?: Record<string, unknown>
  }): Promise<FailureNotification> {
    return this.record({
      type: 'task_failure',
      severity: 'medium',
      ...params,
    })
  }

  /**
   * Record a critical system error
   */
  async recordCriticalError(params: {
    message: string
    error?: string
    stackTrace?: string
    context?: Record<string, unknown>
  }): Promise<FailureNotification> {
    return this.record({
      type: 'system_error',
      severity: 'critical',
      ...params,
    })
  }

  /**
   * Record a rate limit hit
   */
  async recordRateLimit(params: {
    message: string
    agent?: string
    sessionId?: string
    context?: Record<string, unknown>
  }): Promise<FailureNotification> {
    return this.record({
      type: 'rate_limit',
      severity: 'low',
      ...params,
    })
  }

  /**
   * Record a timeout
   */
  async recordTimeout(params: {
    taskId?: string
    message: string
    agent?: string
    sessionId?: string
    context?: Record<string, unknown>
  }): Promise<FailureNotification> {
    return this.record({
      type: 'timeout',
      severity: 'medium',
      ...params,
    })
  }

  /**
   * Record budget exceeded
   */
  async recordBudgetExceeded(params: {
    message: string
    missionId?: string
    context?: Record<string, unknown>
  }): Promise<FailureNotification> {
    return this.record({
      type: 'budget_exceeded',
      severity: 'high',
      ...params,
    })
  }

  // ===========================================================================
  // Notification Sending
  // ===========================================================================

  /**
   * Send notification to all registered channels
   */
  private async sendNotification(
    notification: FailureNotification,
    channels?: string[]
  ): Promise<void> {
    const targetChannels = channels ?? this.defaultChannels
    const usedChannels: string[] = []

    for (const channel of targetChannels) {
      const handler = this.handlers.get(channel)
      if (handler) {
        try {
          await handler(notification)
          usedChannels.push(channel)
        } catch (error) {
          log.error(`Failed to send notification via ${channel}: ${error}`)
        }
      }
    }

    notification.notified = true
    notification.channels = usedChannels
  }

  /**
   * Check if notification should be sent immediately
   */
  private shouldNotifyImmediately(notification: FailureNotification): boolean {
    const severityOrder: FailureSeverity[] = ['low', 'medium', 'high', 'critical']
    const thresholdIndex = severityOrder.indexOf(this.immediateSeverity)
    const notificationIndex = severityOrder.indexOf(notification.severity)
    return notificationIndex >= thresholdIndex
  }

  // ===========================================================================
  // Aggregation
  // ===========================================================================

  /**
   * Find matching aggregation rule
   */
  private findMatchingRule(notification: FailureNotification): AggregationRule | undefined {
    for (const rule of this.aggregationRules) {
      if (this.matchesRule(notification, rule)) {
        return rule
      }
    }
    return undefined
  }

  /**
   * Check if notification matches rule
   */
  private matchesRule(notification: FailureNotification, rule: AggregationRule): boolean {
    const { match } = rule

    if (match.type) {
      const types = Array.isArray(match.type) ? match.type : [match.type]
      if (!types.includes(notification.type)) return false
    }

    if (match.severity) {
      const severities = Array.isArray(match.severity) ? match.severity : [match.severity]
      if (!severities.includes(notification.severity)) return false
    }

    if (match.agent && notification.agent !== match.agent) {
      return false
    }

    if (match.messagePattern && !match.messagePattern.test(notification.message)) {
      return false
    }

    return true
  }

  /**
   * Get or create failure group
   */
  private getOrCreateGroup(
    notification: FailureNotification,
    rule: AggregationRule
  ): FailureGroup {
    const key = `${rule.id}_${notification.type}_${notification.agent ?? 'any'}`

    let group = this.failureGroups.get(key)

    if (!group || Date.now() - group.firstFailureAt > rule.windowMs) {
      // Create new group or reset expired group
      group = {
        key,
        rule,
        failures: [],
        firstFailureAt: notification.timestamp,
        lastFailureAt: notification.timestamp,
        notified: false,
      }
      this.failureGroups.set(key, group)
    }

    return group
  }

  /**
   * Add an aggregation rule
   */
  addAggregationRule(rule: AggregationRule): void {
    this.aggregationRules.push(rule)
    log.debug(`Added aggregation rule: ${rule.id}`)
  }

  /**
   * Remove an aggregation rule
   */
  removeAggregationRule(id: string): boolean {
    const index = this.aggregationRules.findIndex((r) => r.id === id)
    if (index >= 0) {
      this.aggregationRules.splice(index, 1)
      return true
    }
    return false
  }

  // ===========================================================================
  // Query & Stats
  // ===========================================================================

  /**
   * Get recent notifications
   */
  getRecent(limit?: number): FailureNotification[] {
    const sorted = [...this.notifications].sort((a, b) => b.timestamp - a.timestamp)
    return limit ? sorted.slice(0, limit) : sorted
  }

  /**
   * Get notifications by type
   */
  getByType(type: FailureNotificationType, limit?: number): FailureNotification[] {
    const filtered = this.notifications.filter((n) => n.type === type)
    const sorted = filtered.sort((a, b) => b.timestamp - a.timestamp)
    return limit ? sorted.slice(0, limit) : sorted
  }

  /**
   * Get notifications by severity
   */
  getBySeverity(severity: FailureSeverity, limit?: number): FailureNotification[] {
    const filtered = this.notifications.filter((n) => n.severity === severity)
    const sorted = filtered.sort((a, b) => b.timestamp - a.timestamp)
    return limit ? sorted.slice(0, limit) : sorted
  }

  /**
   * Get notification count by type
   */
  getCountByType(): Record<FailureNotificationType, number> {
    const counts: Partial<Record<FailureNotificationType, number>> = {}
    for (const n of this.notifications) {
      counts[n.type] = (counts[n.type] ?? 0) + 1
    }
    return counts as Record<FailureNotificationType, number>
  }

  /**
   * Get notification count by severity
   */
  getCountBySeverity(): Record<FailureSeverity, number> {
    const counts: Record<FailureSeverity, number> = {
      low: 0,
      medium: 0,
      high: 0,
      critical: 0,
    }
    for (const n of this.notifications) {
      counts[n.severity]++
    }
    return counts
  }

  /**
   * Get failure rate in a time window
   */
  getFailureRate(windowMs: number): { count: number; rate: number } {
    const cutoff = Date.now() - windowMs
    const recentFailures = this.notifications.filter((n) => n.timestamp >= cutoff)
    const count = recentFailures.length
    const rate = count / (windowMs / 1000 / 60) // failures per minute
    return { count, rate }
  }

  // ===========================================================================
  // Maintenance
  // ===========================================================================

  /**
   * Trim notifications to max size
   */
  private trimNotifications(): void {
    if (this.notifications.length > this.maxNotifications) {
      this.notifications = this.notifications.slice(-this.maxNotifications)
    }
  }

  /**
   * Clear all notifications
   */
  clear(): void {
    this.notifications = []
    this.failureGroups.clear()
    log.debug('Cleared all failure notifications')
  }

  /**
   * Clear old notifications
   */
  clearOld(maxAgeMs: number): number {
    const cutoff = Date.now() - maxAgeMs
    const before = this.notifications.length
    this.notifications = this.notifications.filter((n) => n.timestamp >= cutoff)
    return before - this.notifications.length
  }
}

// =============================================================================
// Factory Functions
// =============================================================================

/** Singleton instance */
let defaultManager: FailureNotificationManager | null = null

/**
 * Get or create the default failure notification manager
 */
export function getFailureNotificationManager(
  config?: FailureNotificationConfig
): FailureNotificationManager {
  if (!defaultManager) {
    defaultManager = new FailureNotificationManager(config)
  }
  return defaultManager
}

/**
 * Reset the default failure notification manager (for testing)
 */
export function resetFailureNotificationManager(): void {
  defaultManager = null
}

/**
 * Create a new failure notification manager
 */
export function createFailureNotificationManager(
  config?: FailureNotificationConfig
): FailureNotificationManager {
  return new FailureNotificationManager(config)
}

// =============================================================================
// Default Aggregation Rules
// =============================================================================

/** Default aggregation rules */
export const DEFAULT_AGGREGATION_RULES: AggregationRule[] = [
  {
    id: 'rate_limit_burst',
    match: { type: 'rate_limit' },
    windowMs: 60000, // 1 minute
    threshold: 5, // 5 rate limits
    suppressIndividual: true,
  },
  {
    id: 'timeout_burst',
    match: { type: 'timeout' },
    windowMs: 300000, // 5 minutes
    threshold: 3, // 3 timeouts
    suppressIndividual: false,
  },
  {
    id: 'validation_burst',
    match: { type: 'validation_failure' },
    windowMs: 120000, // 2 minutes
    threshold: 5, // 5 validation failures
    suppressIndividual: true,
  },
]

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Format a failure notification for display
 */
export function formatFailureNotification(notification: FailureNotification): string {
  const icon = {
    low: 'ℹ️',
    medium: '⚠️',
    high: '🔴',
    critical: '🚨',
  }[notification.severity]

  const lines: string[] = []
  lines.push(`${icon} [${notification.type}] ${notification.message}`)

  if (notification.error) {
    lines.push(`   Error: ${notification.error}`)
  }

  if (notification.taskId) {
    lines.push(`   Task: ${notification.taskId}`)
  }

  if (notification.agent) {
    lines.push(`   Agent: ${notification.agent}`)
  }

  if (notification.recentCount && notification.recentCount > 1) {
    lines.push(`   Recent similar failures: ${notification.recentCount}`)
  }

  lines.push(`   Time: ${new Date(notification.timestamp).toISOString()}`)

  return lines.join('\n')
}

/**
 * Create failure notification hooks that integrate with tool execution
 */
export function createFailureNotificationHooks(config?: FailureNotificationConfig): {
  manager: FailureNotificationManager
  afterToolHook: (input: { tool: string; error?: Error }, output: unknown) => Promise<void>
} {
  const manager = createFailureNotificationManager(config)

  const afterToolHook = async (
    input: { tool: string; error?: Error; sessionId?: string },
    _output: unknown
  ): Promise<void> => {
    if (input.error) {
      await manager.record({
        type: 'task_failure',
        severity: 'medium',
        message: `Tool ${input.tool} failed`,
        error: input.error.message,
        stackTrace: input.error.stack,
        sessionId: input.sessionId,
        context: { tool: input.tool },
      })
    }
  }

  return { manager, afterToolHook }
}
