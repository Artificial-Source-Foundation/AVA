/**
 * Batch Parallel Execution
 * Execute multiple workers concurrently with concurrency control
 *
 * Uses Promise.allSettled for failure isolation and semaphore for concurrency limiting
 */

import { AgentTerminateMode } from '../../agent/types.js'
import { executeWorker } from '../executor.js'
import type {
  BatchTask,
  CombinedWorkerResult,
  ParallelConfig,
  WorkerActivityCallback,
  WorkerActivityEvent,
  WorkerResult,
} from '../types.js'
import { DEFAULT_PARALLEL_CONFIG } from '../types.js'
import { combineWorkerResults } from '../utils.js'

// ============================================================================
// Semaphore for Concurrency Control
// ============================================================================

/**
 * Simple semaphore for limiting concurrent operations
 *
 * Uses a promise-based queue for fair scheduling
 */
export class Semaphore {
  private available: number
  private queue: Array<() => void> = []

  constructor(maxConcurrency: number) {
    this.available = Math.max(1, Math.min(maxConcurrency, 8))
  }

  /**
   * Acquire a permit (waits if none available)
   */
  async acquire(): Promise<void> {
    if (this.available > 0) {
      this.available--
      return
    }

    // Wait in queue
    return new Promise<void>((resolve) => {
      this.queue.push(resolve)
    })
  }

  /**
   * Release a permit (signals next waiter)
   */
  release(): void {
    const next = this.queue.shift()
    if (next) {
      // Give permit to next waiter
      next()
    } else {
      // Return permit to pool
      this.available++
    }
  }

  /**
   * Get current available permits
   */
  getAvailable(): number {
    return this.available
  }

  /**
   * Get number of waiters in queue
   */
  getQueueLength(): number {
    return this.queue.length
  }
}

// ============================================================================
// Batch Execution
// ============================================================================

/**
 * Result of a single task execution (internal)
 */
interface TaskExecutionResult {
  id: string
  worker: string
  result: WorkerResult
}

/**
 * Execute multiple workers in parallel with concurrency control
 *
 * Features:
 * - Configurable concurrency limit (1-8 workers)
 * - Failure isolation (one failure doesn't stop others)
 * - Optional fail-fast mode
 * - Activity streaming from all workers
 *
 * @param tasks - Array of tasks to execute
 * @param config - Parallel execution configuration
 * @param signal - AbortSignal for cancellation
 * @param onActivity - Callback for activity events from all workers
 * @returns Combined result from all workers
 */
export async function executeBatch(
  tasks: BatchTask[],
  config: Partial<ParallelConfig> = {},
  signal: AbortSignal,
  onActivity?: WorkerActivityCallback
): Promise<CombinedWorkerResult> {
  const finalConfig: ParallelConfig = {
    ...DEFAULT_PARALLEL_CONFIG,
    ...config,
  }

  if (tasks.length === 0) {
    return {
      success: true,
      summary: 'No tasks to execute.',
      details: '',
      results: [],
      totalTokensUsed: 0,
      totalDurationMs: 0,
    }
  }

  // Create semaphore for concurrency control
  const semaphore = new Semaphore(finalConfig.maxConcurrency)

  // Track abort state for fail-fast mode
  const failFastController = new AbortController()
  const combinedSignal = AbortSignal.any([signal, failFastController.signal])

  // Create execution promises
  const executionPromises = tasks.map((task) =>
    executeTaskWithSemaphore(
      task,
      semaphore,
      combinedSignal,
      finalConfig,
      failFastController,
      onActivity
    )
  )

  // Wait for all to settle (success or failure)
  const settlements = await Promise.allSettled(executionPromises)

  // Convert settlements to results
  const taskResults: Array<{ worker: string; result: WorkerResult }> = []

  for (let i = 0; i < settlements.length; i++) {
    const settlement = settlements[i]
    const task = tasks[i]

    if (settlement.status === 'fulfilled') {
      taskResults.push({
        worker: settlement.value.worker,
        result: settlement.value.result,
      })
    } else {
      // Promise rejected (unexpected error)
      taskResults.push({
        worker: task.worker.name,
        result: {
          success: false,
          output: `Task execution error: ${settlement.reason}`,
          terminateMode: AgentTerminateMode.ERROR,
          tokensUsed: 0,
          durationMs: 0,
          turns: 0,
          error: String(settlement.reason),
        },
      })
    }
  }

  // Use existing combineWorkerResults for aggregation
  return combineWorkerResults(taskResults)
}

/**
 * Execute a single task with semaphore control
 */
async function executeTaskWithSemaphore(
  task: BatchTask,
  semaphore: Semaphore,
  signal: AbortSignal,
  config: ParallelConfig,
  failFastController: AbortController,
  onActivity?: WorkerActivityCallback
): Promise<TaskExecutionResult> {
  // Wait for semaphore permit
  await semaphore.acquire()

  try {
    // Check if aborted while waiting
    if (signal.aborted) {
      return {
        id: task.id,
        worker: task.worker.name,
        result: {
          success: false,
          output: 'Task aborted before execution',
          terminateMode: AgentTerminateMode.ABORTED,
          tokensUsed: 0,
          durationMs: 0,
          turns: 0,
          error: 'Aborted',
        },
      }
    }

    // Create worker-specific activity callback
    const workerCallback: WorkerActivityCallback | undefined = onActivity
      ? (event: WorkerActivityEvent) => {
          // Tag event with task ID for parallel tracking
          onActivity({
            ...event,
            data: {
              ...event.data,
              taskId: task.id,
            },
          })
        }
      : undefined

    // Execute the worker
    const result = await executeWorker(task.worker, task.inputs, signal, workerCallback)

    // Fail-fast: abort others on failure
    if (!result.success && config.failFast) {
      failFastController.abort()
    }

    return {
      id: task.id,
      worker: task.worker.name,
      result,
    }
  } finally {
    // Always release semaphore
    semaphore.release()
  }
}

// ============================================================================
// Convenience Functions
// ============================================================================

/**
 * Execute tasks sequentially (maxConcurrency = 1)
 */
export async function executeSequential(
  tasks: BatchTask[],
  signal: AbortSignal,
  onActivity?: WorkerActivityCallback
): Promise<CombinedWorkerResult> {
  return executeBatch(tasks, { maxConcurrency: 1 }, signal, onActivity)
}

/**
 * Execute tasks with maximum parallelism
 */
export async function executeFullParallel(
  tasks: BatchTask[],
  signal: AbortSignal,
  onActivity?: WorkerActivityCallback
): Promise<CombinedWorkerResult> {
  return executeBatch(tasks, { maxConcurrency: tasks.length }, signal, onActivity)
}
