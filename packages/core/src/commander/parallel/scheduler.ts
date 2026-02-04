/**
 * Task Scheduler (DAG)
 * Dependency-aware task scheduling with parallel execution
 *
 * Executes tasks respecting dependencies while maximizing parallelism
 */

import { AgentTerminateMode } from '../../agent/types.js'
import { executeWorker } from '../executor.js'
import type {
  DependentTask,
  ParallelConfig,
  ParallelExecutionResult,
  TaskStatus,
  WorkerActivityCallback,
  WorkerActivityEvent,
  WorkerResult,
} from '../types.js'
import { DEFAULT_PARALLEL_CONFIG } from '../types.js'
import { combineWorkerResults } from '../utils.js'
import { Semaphore } from './batch.js'
import { ConflictDetector, partitionTasks } from './conflict.js'

// ============================================================================
// Task Scheduler
// ============================================================================

/**
 * Internal task state
 */
interface TaskState {
  task: DependentTask
  status: TaskStatus
  result?: WorkerResult
  promise?: Promise<WorkerResult>
}

/**
 * DAG-based task scheduler
 *
 * Executes tasks respecting dependencies while maximizing parallelism.
 * Detects cycles and validates the dependency graph before execution.
 */
export class TaskScheduler {
  private tasks: Map<string, TaskState> = new Map()
  private executionOrder: string[] = []
  private parallelGroups: string[][] = []

  /**
   * Add a task to the scheduler
   *
   * @param task - Scheduled task with dependencies
   */
  add(task: DependentTask): void {
    if (this.tasks.has(task.id)) {
      throw new Error(`Task with ID '${task.id}' already exists`)
    }

    this.tasks.set(task.id, {
      task,
      status: 'pending',
    })
  }

  /**
   * Add multiple tasks
   */
  addAll(tasks: DependentTask[]): void {
    for (const task of tasks) {
      this.add(task)
    }
  }

  /**
   * Validate the task graph (no cycles, all dependencies exist)
   *
   * @returns Validation result
   */
  validate(): { valid: boolean; error?: string } {
    // Check all dependencies exist
    for (const [id, state] of this.tasks) {
      for (const depId of state.task.dependsOn) {
        if (!this.tasks.has(depId)) {
          return {
            valid: false,
            error: `Task '${id}' depends on unknown task '${depId}'`,
          }
        }
      }
    }

    // Check for cycles using DFS
    const visited = new Set<string>()
    const visiting = new Set<string>()

    const hasCycle = (id: string): boolean => {
      if (visiting.has(id)) return true // Cycle detected
      if (visited.has(id)) return false // Already processed

      visiting.add(id)

      const state = this.tasks.get(id)
      if (state) {
        for (const depId of state.task.dependsOn) {
          if (hasCycle(depId)) return true
        }
      }

      visiting.delete(id)
      visited.add(id)
      return false
    }

    for (const id of this.tasks.keys()) {
      if (hasCycle(id)) {
        return {
          valid: false,
          error: `Cycle detected in task dependencies involving '${id}'`,
        }
      }
    }

    return { valid: true }
  }

  /**
   * Get tasks that are ready to run (all dependencies completed successfully)
   *
   * @returns Array of ready tasks
   */
  getReady(): DependentTask[] {
    const ready: DependentTask[] = []

    for (const state of this.tasks.values()) {
      if (state.status !== 'pending') continue

      // Check all dependencies are completed
      const allDepsComplete = state.task.dependsOn.every((depId) => {
        const depState = this.tasks.get(depId)
        return depState?.status === 'completed'
      })

      if (allDepsComplete) {
        ready.push(state.task)
      }
    }

    return ready
  }

  /**
   * Check if any tasks have failed dependencies
   */
  private markBlockedTasks(): void {
    for (const state of this.tasks.values()) {
      if (state.status !== 'pending') continue

      // Check if any dependency failed
      const hasFailedDep = state.task.dependsOn.some((depId) => {
        const depState = this.tasks.get(depId)
        return depState?.status === 'failed'
      })

      if (hasFailedDep) {
        state.status = 'failed'
        state.result = {
          success: false,
          output: 'Task blocked due to failed dependency',
          terminateMode: AgentTerminateMode.ERROR,
          tokensUsed: 0,
          durationMs: 0,
          turns: 0,
          error: 'Dependency failed',
        }
      }
    }
  }

  /**
   * Execute all tasks respecting dependencies
   *
   * @param config - Parallel configuration
   * @param signal - AbortSignal for cancellation
   * @param onActivity - Activity callback
   * @returns Parallel execution result
   */
  async executeAll(
    config: Partial<ParallelConfig> = {},
    signal: AbortSignal,
    onActivity?: WorkerActivityCallback
  ): Promise<ParallelExecutionResult> {
    const finalConfig: ParallelConfig = {
      ...DEFAULT_PARALLEL_CONFIG,
      ...config,
    }

    // Validate graph
    const validation = this.validate()
    if (!validation.valid) {
      throw new Error(validation.error)
    }

    // Create semaphore for concurrency control
    const semaphore = new Semaphore(finalConfig.maxConcurrency)

    // Track running tasks
    const runningTasks: Map<string, Promise<void>> = new Map()

    // Conflict detector for file access
    const conflictDetector = new ConflictDetector()

    // Main execution loop
    while (true) {
      // Mark tasks with failed dependencies
      this.markBlockedTasks()

      // Get ready tasks
      const ready = this.getReady()

      // Check if we're done
      const stillPending = Array.from(this.tasks.values()).some(
        (s) => s.status === 'pending' || s.status === 'running'
      )

      if (!stillPending && runningTasks.size === 0) {
        break
      }

      // Check for abort
      if (signal.aborted) {
        // Mark all pending as aborted
        for (const state of this.tasks.values()) {
          if (state.status === 'pending') {
            state.status = 'failed'
            state.result = {
              success: false,
              output: 'Task aborted',
              terminateMode: AgentTerminateMode.ABORTED,
              tokensUsed: 0,
              durationMs: 0,
              turns: 0,
              error: 'Aborted',
            }
          }
        }
        break
      }

      // Track this parallel group
      const parallelGroup: string[] = []

      // Start ready tasks (up to concurrency limit)
      for (const task of ready) {
        // Skip if already running or semaphore full
        if (runningTasks.has(task.id)) continue
        if (semaphore.getAvailable() === 0 && runningTasks.size > 0) break

        // Acquire semaphore
        await semaphore.acquire()

        // Mark as running
        const state = this.tasks.get(task.id)!
        state.status = 'running'
        parallelGroup.push(task.id)

        // Create worker callback
        const workerCallback: WorkerActivityCallback | undefined = onActivity
          ? (event: WorkerActivityEvent) => {
              onActivity({
                ...event,
                data: { ...event.data, taskId: task.id },
              })
            }
          : undefined

        // Execute task
        const taskPromise = executeWorker(task.worker, task.inputs, signal, workerCallback)
          .then((result) => {
            state.status = result.success ? 'completed' : 'failed'
            state.result = result
            this.executionOrder.push(task.id)
          })
          .catch((error) => {
            state.status = 'failed'
            state.result = {
              success: false,
              output: `Execution error: ${error}`,
              terminateMode: AgentTerminateMode.ERROR,
              tokensUsed: 0,
              durationMs: 0,
              turns: 0,
              error: String(error),
            }
            this.executionOrder.push(task.id)
          })
          .finally(() => {
            semaphore.release()
            runningTasks.delete(task.id)
            conflictDetector.release(task.id)
          })

        runningTasks.set(task.id, taskPromise)
      }

      // Record parallel group if multiple tasks started
      if (parallelGroup.length > 1) {
        this.parallelGroups.push(parallelGroup)
      }

      // Wait for at least one task to complete
      if (runningTasks.size > 0) {
        await Promise.race(runningTasks.values())
      } else if (ready.length === 0) {
        // No tasks ready and none running - might be stuck
        break
      }
    }

    // Collect results
    const taskResults: Array<{ worker: string; result: WorkerResult }> = []

    for (const state of this.tasks.values()) {
      if (state.result) {
        taskResults.push({
          worker: state.task.worker.name,
          result: state.result,
        })
      }
    }

    // Combine results
    const combined = combineWorkerResults(taskResults)

    // Detect any file conflicts (for reporting)
    const tasks = Array.from(this.tasks.values()).map((s) => s.task)
    const { conflicts } = partitionTasks(tasks)

    return {
      ...combined,
      executionOrder: this.executionOrder,
      parallelGroups: this.parallelGroups,
      conflicts: conflicts.length > 0 ? conflicts : undefined,
    }
  }

  /**
   * Get all task states
   */
  getTaskStates(): Map<string, { status: TaskStatus; result?: WorkerResult }> {
    const states = new Map<string, { status: TaskStatus; result?: WorkerResult }>()
    for (const [id, state] of this.tasks) {
      states.set(id, { status: state.status, result: state.result })
    }
    return states
  }

  /**
   * Clear all tasks
   */
  clear(): void {
    this.tasks.clear()
    this.executionOrder = []
    this.parallelGroups = []
  }
}

// ============================================================================
// Convenience Functions
// ============================================================================

/**
 * Create a simple linear chain of tasks
 *
 * Each task depends on the previous one
 */
export function createLinearChain(tasks: Omit<DependentTask, 'dependsOn'>[]): DependentTask[] {
  return tasks.map((task, i) => ({
    ...task,
    dependsOn: i > 0 ? [tasks[i - 1].id] : [],
  }))
}

/**
 * Create a fan-out pattern (one task, then multiple parallel)
 */
export function createFanOut(
  initial: Omit<DependentTask, 'dependsOn'>,
  parallel: Omit<DependentTask, 'dependsOn'>[]
): DependentTask[] {
  return [
    { ...initial, dependsOn: [] },
    ...parallel.map((task) => ({ ...task, dependsOn: [initial.id] })),
  ]
}

/**
 * Create a fan-in pattern (multiple parallel, then one)
 */
export function createFanIn(
  parallel: Omit<DependentTask, 'dependsOn'>[],
  final: Omit<DependentTask, 'dependsOn'>
): DependentTask[] {
  return [
    ...parallel.map((task) => ({ ...task, dependsOn: [] as string[] })),
    { ...final, dependsOn: parallel.map((t) => t.id) },
  ]
}
