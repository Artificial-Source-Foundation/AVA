/**
 * Scheduler Types
 * Types for background task scheduling
 */

// ============================================================================
// Types
// ============================================================================

/**
 * Scope of a scheduled task
 */
export type TaskScope = 'global' | 'session'

/**
 * A scheduled background task
 */
export interface ScheduledTask {
  /** Unique task identifier */
  id: string
  /** Human-readable name */
  name: string
  /** Interval between runs in milliseconds */
  intervalMs: number
  /** Task function to run */
  run: () => Promise<void>
  /** Task scope */
  scope: TaskScope
  /** Whether task is currently enabled */
  enabled?: boolean
}

/**
 * Task execution result
 */
export interface TaskResult {
  /** Task ID */
  taskId: string
  /** Whether execution succeeded */
  success: boolean
  /** Execution time in milliseconds */
  durationMs: number
  /** Error message if failed */
  error?: string
  /** When the task ran */
  timestamp: number
}

/**
 * Scheduler configuration
 */
export interface SchedulerConfig {
  /** Whether to start automatically */
  autoStart?: boolean
  /** Maximum concurrent tasks */
  maxConcurrent?: number
  /** Callback when task completes */
  onTaskComplete?: (result: TaskResult) => void
}
