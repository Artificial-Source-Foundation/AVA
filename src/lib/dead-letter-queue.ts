/**
 * Delta9 Dead Letter Queue
 *
 * Stores failed tasks that cannot be immediately retried.
 * Provides mechanisms for:
 * - Manual inspection and retry
 * - Automatic expiration
 * - Failure pattern analysis
 * - Integration with recovery systems
 */

import { getNamedLogger } from './logger.js'

const log = getNamedLogger('dead-letter-queue')

// =============================================================================
// Types
// =============================================================================

/** Entry in the dead letter queue */
export interface DeadLetterEntry {
  /** Unique entry ID */
  id: string
  /** Original task ID */
  taskId: string
  /** Task type/category */
  taskType: string
  /** Task description or summary */
  description: string
  /** Error that caused the failure */
  error: string
  /** Full error stack trace (if available) */
  stackTrace?: string
  /** Number of retry attempts made */
  retryCount: number
  /** Maximum retries allowed before DLQ */
  maxRetries: number
  /** Timestamp when first failed */
  firstFailedAt: number
  /** Timestamp of last retry attempt */
  lastAttemptAt: number
  /** Timestamp when added to DLQ */
  addedAt: number
  /** Original task payload */
  payload?: Record<string, unknown>
  /** Session ID that created the task */
  sessionId?: string
  /** Mission ID */
  missionId?: string
  /** Agent that was processing the task */
  agent?: string
  /** Metadata for debugging */
  metadata?: Record<string, unknown>
  /** Entry status */
  status: 'pending' | 'retrying' | 'resolved' | 'expired' | 'discarded'
  /** Resolution details (if resolved) */
  resolution?: {
    resolvedAt: number
    resolvedBy: string
    method: 'retry' | 'manual' | 'skip' | 'expired'
    notes?: string
  }
}

/** Configuration for the dead letter queue */
export interface DeadLetterQueueConfig {
  /** Maximum entries to keep */
  maxEntries?: number
  /** Default TTL for entries in ms (0 = no expiration) */
  defaultTtlMs?: number
  /** Callback when entry is added */
  onEntryAdded?: (entry: DeadLetterEntry) => void | Promise<void>
  /** Callback when entry is resolved */
  onEntryResolved?: (entry: DeadLetterEntry) => void | Promise<void>
  /** Callback when queue reaches capacity */
  onQueueFull?: (count: number) => void | Promise<void>
}

/** Filter options for querying entries */
export interface DeadLetterFilter {
  /** Filter by status */
  status?: DeadLetterEntry['status']
  /** Filter by task type */
  taskType?: string
  /** Filter by session ID */
  sessionId?: string
  /** Filter by mission ID */
  missionId?: string
  /** Filter entries added after this timestamp */
  addedAfter?: number
  /** Filter entries added before this timestamp */
  addedBefore?: number
  /** Maximum number of entries to return */
  limit?: number
}

/** Statistics about the dead letter queue */
export interface DeadLetterStats {
  /** Total entries in queue */
  total: number
  /** Entries by status */
  byStatus: Record<DeadLetterEntry['status'], number>
  /** Entries by task type */
  byTaskType: Record<string, number>
  /** Average retry count */
  avgRetryCount: number
  /** Oldest entry timestamp */
  oldestEntry?: number
  /** Newest entry timestamp */
  newestEntry?: number
  /** Most common errors */
  topErrors: Array<{ error: string; count: number }>
}

// =============================================================================
// Dead Letter Queue Manager
// =============================================================================

export class DeadLetterQueueManager {
  private entries: Map<string, DeadLetterEntry> = new Map()
  private maxEntries: number
  private defaultTtlMs: number
  private onEntryAdded?: (entry: DeadLetterEntry) => void | Promise<void>
  private onEntryResolved?: (entry: DeadLetterEntry) => void | Promise<void>
  private onQueueFull?: (count: number) => void | Promise<void>
  private cleanupInterval?: ReturnType<typeof setInterval>

  constructor(config: DeadLetterQueueConfig = {}) {
    this.maxEntries = config.maxEntries ?? 1000
    this.defaultTtlMs = config.defaultTtlMs ?? 7 * 24 * 60 * 60 * 1000 // 7 days
    this.onEntryAdded = config.onEntryAdded
    this.onEntryResolved = config.onEntryResolved
    this.onQueueFull = config.onQueueFull

    // Start cleanup timer if TTL is set
    if (this.defaultTtlMs > 0) {
      this.startCleanupTimer()
    }
  }

  // ===========================================================================
  // Entry Management
  // ===========================================================================

  /**
   * Add a failed task to the dead letter queue
   */
  async add(params: {
    taskId: string
    taskType: string
    description: string
    error: string
    stackTrace?: string
    retryCount: number
    maxRetries: number
    firstFailedAt?: number
    lastAttemptAt?: number
    payload?: Record<string, unknown>
    sessionId?: string
    missionId?: string
    agent?: string
    metadata?: Record<string, unknown>
  }): Promise<DeadLetterEntry> {
    const now = Date.now()
    const id = `dlq_${params.taskId}_${now}`

    const entry: DeadLetterEntry = {
      id,
      taskId: params.taskId,
      taskType: params.taskType,
      description: params.description,
      error: params.error,
      stackTrace: params.stackTrace,
      retryCount: params.retryCount,
      maxRetries: params.maxRetries,
      firstFailedAt: params.firstFailedAt ?? now,
      lastAttemptAt: params.lastAttemptAt ?? now,
      addedAt: now,
      payload: params.payload,
      sessionId: params.sessionId,
      missionId: params.missionId,
      agent: params.agent,
      metadata: params.metadata,
      status: 'pending',
    }

    // Check capacity
    if (this.entries.size >= this.maxEntries) {
      await this.evictOldest()
      if (this.onQueueFull) {
        await this.onQueueFull(this.entries.size)
      }
    }

    this.entries.set(id, entry)
    log.warn(`Added to DLQ: ${params.taskId} - ${params.error}`)

    if (this.onEntryAdded) {
      await this.onEntryAdded(entry)
    }

    return entry
  }

  /**
   * Get an entry by ID
   */
  get(id: string): DeadLetterEntry | undefined {
    return this.entries.get(id)
  }

  /**
   * Get entry by original task ID (returns most recent)
   */
  getByTaskId(taskId: string): DeadLetterEntry | undefined {
    const entries = Array.from(this.entries.values())
      .filter((e) => e.taskId === taskId)
      .sort((a, b) => b.addedAt - a.addedAt)
    return entries[0]
  }

  /**
   * Query entries with filters
   */
  query(filter: DeadLetterFilter = {}): DeadLetterEntry[] {
    let entries = Array.from(this.entries.values())

    if (filter.status) {
      entries = entries.filter((e) => e.status === filter.status)
    }

    if (filter.taskType) {
      entries = entries.filter((e) => e.taskType === filter.taskType)
    }

    if (filter.sessionId) {
      entries = entries.filter((e) => e.sessionId === filter.sessionId)
    }

    if (filter.missionId) {
      entries = entries.filter((e) => e.missionId === filter.missionId)
    }

    if (filter.addedAfter !== undefined) {
      entries = entries.filter((e) => e.addedAt >= filter.addedAfter!)
    }

    if (filter.addedBefore !== undefined) {
      entries = entries.filter((e) => e.addedAt <= filter.addedBefore!)
    }

    // Sort by addedAt descending (newest first)
    entries.sort((a, b) => b.addedAt - a.addedAt)

    if (filter.limit) {
      entries = entries.slice(0, filter.limit)
    }

    return entries
  }

  /**
   * Mark entry as being retried
   */
  markRetrying(id: string): boolean {
    const entry = this.entries.get(id)
    if (!entry || entry.status !== 'pending') {
      return false
    }

    entry.status = 'retrying'
    entry.lastAttemptAt = Date.now()
    entry.retryCount++
    log.debug(`Marked entry ${id} as retrying (attempt ${entry.retryCount})`)
    return true
  }

  /**
   * Resolve an entry (mark as handled)
   */
  async resolve(
    id: string,
    params: {
      resolvedBy: string
      method: 'retry' | 'manual' | 'skip' | 'expired'
      notes?: string
    }
  ): Promise<boolean> {
    const entry = this.entries.get(id)
    if (!entry) {
      return false
    }

    entry.status = 'resolved'
    entry.resolution = {
      resolvedAt: Date.now(),
      resolvedBy: params.resolvedBy,
      method: params.method,
      notes: params.notes,
    }

    log.info(`Resolved DLQ entry ${id} via ${params.method} by ${params.resolvedBy}`)

    if (this.onEntryResolved) {
      await this.onEntryResolved(entry)
    }

    return true
  }

  /**
   * Discard an entry (mark as not worth retrying)
   */
  discard(id: string, reason?: string): boolean {
    const entry = this.entries.get(id)
    if (!entry) {
      return false
    }

    entry.status = 'discarded'
    entry.resolution = {
      resolvedAt: Date.now(),
      resolvedBy: 'system',
      method: 'skip',
      notes: reason ?? 'Discarded',
    }

    log.debug(`Discarded DLQ entry ${id}: ${reason ?? 'no reason'}`)
    return true
  }

  /**
   * Remove an entry completely
   */
  remove(id: string): boolean {
    const removed = this.entries.delete(id)
    if (removed) {
      log.debug(`Removed DLQ entry ${id}`)
    }
    return removed
  }

  /**
   * Return entry to pending status (after failed retry)
   */
  returnToPending(id: string): boolean {
    const entry = this.entries.get(id)
    if (!entry || entry.status === 'resolved' || entry.status === 'discarded') {
      return false
    }

    entry.status = 'pending'
    log.debug(`Returned entry ${id} to pending`)
    return true
  }

  // ===========================================================================
  // Bulk Operations
  // ===========================================================================

  /**
   * Get all pending entries ready for retry
   */
  getPendingForRetry(limit?: number): DeadLetterEntry[] {
    return this.query({
      status: 'pending',
      limit,
    })
  }

  /**
   * Discard all entries matching filter
   */
  discardMatching(filter: DeadLetterFilter, reason?: string): number {
    const entries = this.query(filter)
    let count = 0

    for (const entry of entries) {
      if (this.discard(entry.id, reason)) {
        count++
      }
    }

    return count
  }

  /**
   * Remove all resolved/discarded entries older than given age
   */
  cleanup(maxAgeMs?: number): number {
    const cutoff = Date.now() - (maxAgeMs ?? this.defaultTtlMs)
    let removed = 0

    for (const [id, entry] of this.entries) {
      if (
        (entry.status === 'resolved' ||
          entry.status === 'discarded' ||
          entry.status === 'expired') &&
        entry.addedAt < cutoff
      ) {
        this.entries.delete(id)
        removed++
      }
    }

    if (removed > 0) {
      log.debug(`Cleaned up ${removed} old DLQ entries`)
    }

    return removed
  }

  /**
   * Expire entries older than TTL
   */
  expireOld(): number {
    if (this.defaultTtlMs <= 0) return 0

    const cutoff = Date.now() - this.defaultTtlMs
    let expired = 0

    for (const entry of this.entries.values()) {
      if (entry.status === 'pending' && entry.addedAt < cutoff) {
        entry.status = 'expired'
        entry.resolution = {
          resolvedAt: Date.now(),
          resolvedBy: 'system',
          method: 'expired',
          notes: `Expired after ${this.defaultTtlMs}ms`,
        }
        expired++
      }
    }

    if (expired > 0) {
      log.info(`Expired ${expired} old DLQ entries`)
    }

    return expired
  }

  // ===========================================================================
  // Statistics
  // ===========================================================================

  /**
   * Get statistics about the queue
   */
  getStats(): DeadLetterStats {
    const entries = Array.from(this.entries.values())
    const total = entries.length

    // Count by status
    const byStatus: Record<DeadLetterEntry['status'], number> = {
      pending: 0,
      retrying: 0,
      resolved: 0,
      expired: 0,
      discarded: 0,
    }
    for (const entry of entries) {
      byStatus[entry.status]++
    }

    // Count by task type
    const byTaskType: Record<string, number> = {}
    for (const entry of entries) {
      byTaskType[entry.taskType] = (byTaskType[entry.taskType] ?? 0) + 1
    }

    // Average retry count
    const avgRetryCount = total > 0 ? entries.reduce((sum, e) => sum + e.retryCount, 0) / total : 0

    // Timestamps
    const timestamps = entries.map((e) => e.addedAt)
    const oldestEntry = timestamps.length > 0 ? Math.min(...timestamps) : undefined
    const newestEntry = timestamps.length > 0 ? Math.max(...timestamps) : undefined

    // Top errors
    const errorCounts: Record<string, number> = {}
    for (const entry of entries) {
      // Normalize error message (take first line)
      const normalizedError = entry.error.split('\n')[0].slice(0, 100)
      errorCounts[normalizedError] = (errorCounts[normalizedError] ?? 0) + 1
    }

    const topErrors = Object.entries(errorCounts)
      .map(([error, count]) => ({ error, count }))
      .sort((a, b) => b.count - a.count)
      .slice(0, 10)

    return {
      total,
      byStatus,
      byTaskType,
      avgRetryCount,
      oldestEntry,
      newestEntry,
      topErrors,
    }
  }

  /**
   * Get count of entries by status
   */
  getCount(status?: DeadLetterEntry['status']): number {
    if (!status) return this.entries.size

    return Array.from(this.entries.values()).filter((e) => e.status === status).length
  }

  // ===========================================================================
  // Internal
  // ===========================================================================

  /**
   * Evict oldest entries to make room
   */
  private async evictOldest(): Promise<void> {
    const entries = Array.from(this.entries.values())
      .filter((e) => e.status === 'resolved' || e.status === 'discarded' || e.status === 'expired')
      .sort((a, b) => a.addedAt - b.addedAt)

    // Remove 10% of resolved entries or at least 1
    const toRemove = Math.max(1, Math.floor(entries.length * 0.1))

    for (let i = 0; i < toRemove && i < entries.length; i++) {
      this.entries.delete(entries[i].id)
    }

    log.debug(`Evicted ${toRemove} old DLQ entries`)
  }

  /**
   * Start periodic cleanup timer
   */
  private startCleanupTimer(): void {
    // Run cleanup every hour
    this.cleanupInterval = setInterval(
      () => {
        this.expireOld()
        this.cleanup()
      },
      60 * 60 * 1000
    )

    // Don't prevent process exit
    if (this.cleanupInterval.unref) {
      this.cleanupInterval.unref()
    }
  }

  /**
   * Stop cleanup timer
   */
  stopCleanupTimer(): void {
    if (this.cleanupInterval) {
      clearInterval(this.cleanupInterval)
      this.cleanupInterval = undefined
    }
  }

  /**
   * Clear all entries (for testing)
   */
  clear(): void {
    this.entries.clear()
    log.debug('DLQ cleared')
  }
}

// =============================================================================
// Factory Functions
// =============================================================================

/** Singleton instance */
let defaultManager: DeadLetterQueueManager | null = null

/**
 * Get or create the default dead letter queue manager
 */
export function getDeadLetterQueueManager(config?: DeadLetterQueueConfig): DeadLetterQueueManager {
  if (!defaultManager) {
    defaultManager = new DeadLetterQueueManager(config)
  }
  return defaultManager
}

/**
 * Reset the default dead letter queue manager (for testing)
 */
export function resetDeadLetterQueueManager(): void {
  if (defaultManager) {
    defaultManager.stopCleanupTimer()
    defaultManager = null
  }
}

/**
 * Create a new dead letter queue manager
 */
export function createDeadLetterQueueManager(
  config?: DeadLetterQueueConfig
): DeadLetterQueueManager {
  return new DeadLetterQueueManager(config)
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Format a DLQ entry for display
 */
export function formatDeadLetterEntry(entry: DeadLetterEntry): string {
  const lines: string[] = []

  const statusIcon = {
    pending: '⏳',
    retrying: '🔄',
    resolved: '✅',
    expired: '⏰',
    discarded: '🗑️',
  }[entry.status]

  lines.push(`${statusIcon} [${entry.id}] ${entry.taskType}: ${entry.description}`)
  lines.push(`   Error: ${entry.error.split('\n')[0]}`)
  lines.push(`   Retries: ${entry.retryCount}/${entry.maxRetries}`)
  lines.push(`   Added: ${new Date(entry.addedAt).toISOString()}`)

  if (entry.agent) {
    lines.push(`   Agent: ${entry.agent}`)
  }

  if (entry.resolution) {
    lines.push(`   Resolution: ${entry.resolution.method} by ${entry.resolution.resolvedBy}`)
    if (entry.resolution.notes) {
      lines.push(`   Notes: ${entry.resolution.notes}`)
    }
  }

  return lines.join('\n')
}

/**
 * Format DLQ stats for display
 */
export function formatDeadLetterStats(stats: DeadLetterStats): string {
  const lines: string[] = []

  lines.push('Dead Letter Queue Statistics')
  lines.push('============================')
  lines.push(`Total Entries: ${stats.total}`)
  lines.push('')
  lines.push('By Status:')
  for (const [status, count] of Object.entries(stats.byStatus)) {
    if (count > 0) {
      lines.push(`  ${status}: ${count}`)
    }
  }

  if (Object.keys(stats.byTaskType).length > 0) {
    lines.push('')
    lines.push('By Task Type:')
    for (const [type, count] of Object.entries(stats.byTaskType)) {
      lines.push(`  ${type}: ${count}`)
    }
  }

  lines.push('')
  lines.push(`Avg Retry Count: ${stats.avgRetryCount.toFixed(1)}`)

  if (stats.topErrors.length > 0) {
    lines.push('')
    lines.push('Top Errors:')
    for (const { error, count } of stats.topErrors.slice(0, 5)) {
      lines.push(`  (${count}x) ${error}`)
    }
  }

  return lines.join('\n')
}
