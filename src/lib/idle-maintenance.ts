/**
 * Delta9 Idle Maintenance
 *
 * Automatic maintenance tasks that run during session idle:
 * - Auto-save mission state
 * - Cleanup stale background tasks
 * - Compact memory if needed
 * - Update session metrics
 *
 * Pattern from: oh-my-opencode auto-capture on idle
 *
 * This module provides debounced maintenance that triggers
 * when the user is idle, minimizing interruption.
 */

import { getNamedLogger } from './logger.js'

const log = getNamedLogger('idle-maintenance')

// =============================================================================
// Types
// =============================================================================

/**
 * Idle maintenance task definition
 */
export interface MaintenanceTask {
  /** Task name */
  name: string
  /** Task handler */
  handler: () => Promise<void>
  /** Minimum interval between runs (ms) */
  minInterval: number
  /** Last run timestamp */
  lastRun?: number
  /** Whether task is enabled */
  enabled: boolean
  /** Priority (lower = runs first) */
  priority: number
}

/**
 * Idle maintenance configuration
 */
export interface IdleMaintenanceConfig {
  /** Global enable/disable */
  enabled: boolean
  /** Debounce delay before running tasks (ms) */
  debounceMs: number
  /** Maximum time to spend on maintenance (ms) */
  maxDurationMs: number
}

/**
 * Maintenance result
 */
export interface MaintenanceResult {
  /** Total tasks run */
  tasksRun: number
  /** Tasks that succeeded */
  succeeded: number
  /** Tasks that failed */
  failed: number
  /** Total duration (ms) */
  durationMs: number
  /** Task-level results */
  taskResults: Array<{
    name: string
    success: boolean
    durationMs: number
    error?: string
  }>
}

// =============================================================================
// Idle Maintenance Manager
// =============================================================================

/**
 * Manages idle maintenance tasks
 */
export class IdleMaintenanceManager {
  private tasks: Map<string, MaintenanceTask> = new Map()
  private config: IdleMaintenanceConfig
  private debounceTimer: ReturnType<typeof setTimeout> | null = null
  private isRunning = false
  private lastMaintenanceRun = 0

  constructor(config: Partial<IdleMaintenanceConfig> = {}) {
    this.config = {
      enabled: config.enabled ?? true,
      debounceMs: config.debounceMs ?? 2000, // 2 seconds
      maxDurationMs: config.maxDurationMs ?? 5000, // 5 seconds
    }
  }

  // ===========================================================================
  // Task Registration
  // ===========================================================================

  /**
   * Register a maintenance task
   */
  registerTask(task: Omit<MaintenanceTask, 'lastRun'>): void {
    this.tasks.set(task.name, {
      ...task,
      lastRun: undefined,
    })
    log.debug(`Registered maintenance task: ${task.name}`)
  }

  /**
   * Unregister a maintenance task
   */
  unregisterTask(name: string): boolean {
    return this.tasks.delete(name)
  }

  /**
   * Enable a task
   */
  enableTask(name: string): void {
    const task = this.tasks.get(name)
    if (task) {
      task.enabled = true
    }
  }

  /**
   * Disable a task
   */
  disableTask(name: string): void {
    const task = this.tasks.get(name)
    if (task) {
      task.enabled = false
    }
  }

  // ===========================================================================
  // Idle Handling
  // ===========================================================================

  /**
   * Trigger idle maintenance (debounced)
   *
   * Call this when session goes idle.
   * Multiple rapid calls will be debounced.
   */
  triggerIdle(): void {
    if (!this.config.enabled) return

    // Clear existing debounce timer
    if (this.debounceTimer) {
      clearTimeout(this.debounceTimer)
    }

    // Set new debounce timer
    this.debounceTimer = setTimeout(() => {
      this.runMaintenance().catch((error) => {
        log.error(`Maintenance failed: ${error instanceof Error ? error.message : String(error)}`)
      })
    }, this.config.debounceMs)
  }

  /**
   * Cancel pending idle maintenance
   */
  cancelPending(): void {
    if (this.debounceTimer) {
      clearTimeout(this.debounceTimer)
      this.debounceTimer = null
    }
  }

  // ===========================================================================
  // Maintenance Execution
  // ===========================================================================

  /**
   * Run all eligible maintenance tasks
   */
  async runMaintenance(): Promise<MaintenanceResult> {
    if (this.isRunning) {
      log.debug('Maintenance already running, skipping')
      return {
        tasksRun: 0,
        succeeded: 0,
        failed: 0,
        durationMs: 0,
        taskResults: [],
      }
    }

    this.isRunning = true
    const startTime = Date.now()
    const result: MaintenanceResult = {
      tasksRun: 0,
      succeeded: 0,
      failed: 0,
      durationMs: 0,
      taskResults: [],
    }

    try {
      // Get eligible tasks sorted by priority
      const eligibleTasks = this.getEligibleTasks()

      for (const task of eligibleTasks) {
        // Check timeout
        if (Date.now() - startTime > this.config.maxDurationMs) {
          log.debug('Maintenance timeout reached, stopping')
          break
        }

        const taskStart = Date.now()
        try {
          await task.handler()
          task.lastRun = Date.now()

          const taskDuration = Date.now() - taskStart
          result.taskResults.push({
            name: task.name,
            success: true,
            durationMs: taskDuration,
          })
          result.succeeded++
        } catch (error) {
          const taskDuration = Date.now() - taskStart
          const message = error instanceof Error ? error.message : String(error)

          result.taskResults.push({
            name: task.name,
            success: false,
            durationMs: taskDuration,
            error: message,
          })
          result.failed++

          log.warn(`Maintenance task ${task.name} failed: ${message}`)
        }

        result.tasksRun++
      }

      result.durationMs = Date.now() - startTime
      this.lastMaintenanceRun = Date.now()

      if (result.tasksRun > 0) {
        log.debug(
          `Maintenance completed: ${result.succeeded}/${result.tasksRun} tasks in ${result.durationMs}ms`
        )
      }

      return result
    } finally {
      this.isRunning = false
    }
  }

  /**
   * Get tasks that are eligible to run
   */
  private getEligibleTasks(): MaintenanceTask[] {
    const now = Date.now()
    const eligible: MaintenanceTask[] = []

    for (const task of this.tasks.values()) {
      if (!task.enabled) continue

      // Check if enough time has passed since last run
      const timeSinceLastRun = task.lastRun ? now - task.lastRun : Infinity
      if (timeSinceLastRun < task.minInterval) continue

      eligible.push(task)
    }

    // Sort by priority (lower first)
    return eligible.sort((a, b) => a.priority - b.priority)
  }

  // ===========================================================================
  // Configuration
  // ===========================================================================

  /**
   * Update configuration
   */
  configure(config: Partial<IdleMaintenanceConfig>): void {
    this.config = { ...this.config, ...config }
  }

  /**
   * Get current configuration
   */
  getConfig(): IdleMaintenanceConfig {
    return { ...this.config }
  }

  /**
   * Enable maintenance
   */
  enable(): void {
    this.config.enabled = true
  }

  /**
   * Disable maintenance
   */
  disable(): void {
    this.config.enabled = false
    this.cancelPending()
  }

  // ===========================================================================
  // Status
  // ===========================================================================

  /**
   * Get manager status
   */
  getStatus(): {
    enabled: boolean
    isRunning: boolean
    lastRunAt: number
    registeredTasks: number
    pendingMaintenance: boolean
  } {
    return {
      enabled: this.config.enabled,
      isRunning: this.isRunning,
      lastRunAt: this.lastMaintenanceRun,
      registeredTasks: this.tasks.size,
      pendingMaintenance: this.debounceTimer !== null,
    }
  }

  /**
   * Get list of registered task names
   */
  getTaskNames(): string[] {
    return Array.from(this.tasks.keys())
  }

  /**
   * Clear all tasks and state
   */
  clear(): void {
    this.cancelPending()
    this.tasks.clear()
    this.isRunning = false
    this.lastMaintenanceRun = 0
  }
}

// =============================================================================
// Singleton & Factory
// =============================================================================

let instance: IdleMaintenanceManager | null = null

/**
 * Get or create the idle maintenance manager
 */
export function getIdleMaintenanceManager(): IdleMaintenanceManager {
  if (!instance) {
    instance = new IdleMaintenanceManager()
    log.info('Idle maintenance manager initialized')
  }
  return instance
}

/**
 * Clear the idle maintenance manager (for testing)
 */
export function clearIdleMaintenanceManager(): void {
  if (instance) {
    instance.clear()
    instance = null
  }
}

// =============================================================================
// Predefined Maintenance Tasks
// =============================================================================

/**
 * Default maintenance task priorities
 */
export const MAINTENANCE_PRIORITY = {
  /** Critical tasks (state saving) */
  CRITICAL: 10,
  /** High priority (cleanup) */
  HIGH: 20,
  /** Normal priority */
  NORMAL: 50,
  /** Low priority (metrics, logging) */
  LOW: 80,
} as const

/**
 * Register common maintenance tasks
 */
export function registerCommonTasks(
  manager: IdleMaintenanceManager,
  handlers: {
    autoSave?: () => Promise<void>
    cleanupStaleTasks?: () => Promise<void>
    compactMemory?: () => Promise<void>
    updateMetrics?: () => Promise<void>
  }
): void {
  if (handlers.autoSave) {
    manager.registerTask({
      name: 'auto-save',
      handler: handlers.autoSave,
      minInterval: 30000, // 30 seconds
      enabled: true,
      priority: MAINTENANCE_PRIORITY.CRITICAL,
    })
  }

  if (handlers.cleanupStaleTasks) {
    manager.registerTask({
      name: 'cleanup-stale-tasks',
      handler: handlers.cleanupStaleTasks,
      minInterval: 60000, // 1 minute
      enabled: true,
      priority: MAINTENANCE_PRIORITY.HIGH,
    })
  }

  if (handlers.compactMemory) {
    manager.registerTask({
      name: 'compact-memory',
      handler: handlers.compactMemory,
      minInterval: 300000, // 5 minutes
      enabled: true,
      priority: MAINTENANCE_PRIORITY.NORMAL,
    })
  }

  if (handlers.updateMetrics) {
    manager.registerTask({
      name: 'update-metrics',
      handler: handlers.updateMetrics,
      minInterval: 60000, // 1 minute
      enabled: true,
      priority: MAINTENANCE_PRIORITY.LOW,
    })
  }
}
