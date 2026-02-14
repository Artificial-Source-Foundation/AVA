/**
 * Parallel Execution Module
 * Enable parallel execution of independent workers
 *
 * @example
 * ```typescript
 * import {
 *   executeBatch,
 *   TaskScheduler,
 *   ConflictDetector,
 *   ActivityMultiplexer,
 * } from '@ava/core/commander'
 *
 * // Simple parallel batch
 * const results = await executeBatch(tasks, { maxConcurrency: 4 }, signal)
 *
 * // With dependencies
 * const scheduler = new TaskScheduler()
 * scheduler.add({ id: 'code', worker: coder, inputs, dependsOn: [] })
 * scheduler.add({ id: 'test', worker: tester, inputs, dependsOn: ['code'] })
 * const results = await scheduler.executeAll(config, signal, onActivity)
 * ```
 */

// Activity multiplexing
export {
  ActivityMultiplexer,
  createAggregator,
  createFilteredCallback,
  createTaggedCallback,
  type MultiplexedActivity,
  type MultiplexedActivityCallback,
} from './activity.js'
// Batch execution
export { executeBatch, executeFullParallel, executeSequential, Semaphore } from './batch.js'
// Conflict detection
export {
  ConflictDetector,
  executeWithConflictDetection,
  type PartitionResult,
  partitionTasks,
} from './conflict.js'
// Task scheduling
export {
  createFanIn,
  createFanOut,
  createLinearChain,
  TaskScheduler,
} from './scheduler.js'
