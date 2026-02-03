/**
 * Scheduler
 * Background task scheduling for cleanup and maintenance
 */

import type { ScheduledTask, SchedulerConfig, TaskResult } from './types.js'

// ============================================================================
// Scheduler
// ============================================================================

/**
 * Manages scheduled background tasks
 */
export class Scheduler {
  private tasks = new Map<string, ScheduledTask>()
  private timers = new Map<string, NodeJS.Timeout>()
  private running = new Set<string>()
  private started = false
  private readonly config: Required<SchedulerConfig>

  constructor(config: SchedulerConfig = {}) {
    this.config = {
      autoStart: config.autoStart ?? false,
      maxConcurrent: config.maxConcurrent ?? 3,
      onTaskComplete: config.onTaskComplete ?? (() => {}),
    }

    if (this.config.autoStart) {
      this.start()
    }
  }

  // ==========================================================================
  // Task Registration
  // ==========================================================================

  /**
   * Register a scheduled task
   */
  register(task: ScheduledTask): void {
    if (this.tasks.has(task.id)) {
      throw new Error(`Task already registered: ${task.id}`)
    }

    this.tasks.set(task.id, { ...task, enabled: task.enabled ?? true })

    // Start timer if scheduler is running and task is enabled
    if (this.started && task.enabled !== false) {
      this.scheduleTask(task)
    }
  }

  /**
   * Unregister a task
   */
  unregister(taskId: string): void {
    this.cancelTimer(taskId)
    this.tasks.delete(taskId)
  }

  /**
   * Enable a task
   */
  enable(taskId: string): void {
    const task = this.tasks.get(taskId)
    if (task) {
      task.enabled = true
      if (this.started) {
        this.scheduleTask(task)
      }
    }
  }

  /**
   * Disable a task
   */
  disable(taskId: string): void {
    const task = this.tasks.get(taskId)
    if (task) {
      task.enabled = false
      this.cancelTimer(taskId)
    }
  }

  /**
   * Get all registered tasks
   */
  getTasks(): ScheduledTask[] {
    return Array.from(this.tasks.values())
  }

  /**
   * Get a task by ID
   */
  getTask(taskId: string): ScheduledTask | undefined {
    return this.tasks.get(taskId)
  }

  // ==========================================================================
  // Scheduler Control
  // ==========================================================================

  /**
   * Start the scheduler
   */
  start(): void {
    if (this.started) return
    this.started = true

    // Schedule all enabled tasks
    for (const task of this.tasks.values()) {
      if (task.enabled !== false) {
        this.scheduleTask(task)
      }
    }
  }

  /**
   * Stop the scheduler
   */
  stop(): void {
    if (!this.started) return
    this.started = false

    // Cancel all timers
    for (const taskId of this.timers.keys()) {
      this.cancelTimer(taskId)
    }
  }

  /**
   * Check if scheduler is running
   */
  isStarted(): boolean {
    return this.started
  }

  /**
   * Run a task immediately (outside of schedule)
   */
  async runNow(taskId: string): Promise<TaskResult> {
    const task = this.tasks.get(taskId)
    if (!task) {
      throw new Error(`Task not found: ${taskId}`)
    }

    return this.executeTask(task)
  }

  // ==========================================================================
  // Internal Methods
  // ==========================================================================

  private scheduleTask(task: ScheduledTask): void {
    // Cancel existing timer
    this.cancelTimer(task.id)

    // Schedule new timer
    const timer = setInterval(() => {
      void this.maybeRunTask(task)
    }, task.intervalMs)

    this.timers.set(task.id, timer)

    // Run immediately on first schedule
    void this.maybeRunTask(task)
  }

  private cancelTimer(taskId: string): void {
    const timer = this.timers.get(taskId)
    if (timer) {
      clearInterval(timer)
      this.timers.delete(taskId)
    }
  }

  private async maybeRunTask(task: ScheduledTask): Promise<void> {
    // Skip if already running or too many concurrent
    if (this.running.has(task.id)) return
    if (this.running.size >= this.config.maxConcurrent) return

    await this.executeTask(task)
  }

  private async executeTask(task: ScheduledTask): Promise<TaskResult> {
    this.running.add(task.id)
    const startTime = Date.now()

    let success = true
    let error: string | undefined

    try {
      await task.run()
    } catch (err) {
      success = false
      error = err instanceof Error ? err.message : String(err)
    } finally {
      this.running.delete(task.id)
    }

    const result: TaskResult = {
      taskId: task.id,
      success,
      durationMs: Date.now() - startTime,
      error,
      timestamp: Date.now(),
    }

    this.config.onTaskComplete(result)
    return result
  }

  /**
   * Dispose of the scheduler
   */
  dispose(): void {
    this.stop()
    this.tasks.clear()
  }
}

// ============================================================================
// Factory Function
// ============================================================================

/**
 * Create a scheduler with configuration
 */
export function createScheduler(config?: SchedulerConfig): Scheduler {
  return new Scheduler(config)
}

// ============================================================================
// Singleton
// ============================================================================

let globalScheduler: Scheduler | undefined

/**
 * Get the global scheduler
 */
export function getScheduler(): Scheduler {
  if (!globalScheduler) {
    globalScheduler = new Scheduler({ autoStart: true })
  }
  return globalScheduler
}

/**
 * Set the global scheduler
 */
export function setScheduler(scheduler: Scheduler): void {
  globalScheduler = scheduler
}

/**
 * Dispose the global scheduler
 */
export function disposeScheduler(): void {
  globalScheduler?.dispose()
  globalScheduler = undefined
}
