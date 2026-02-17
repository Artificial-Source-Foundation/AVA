/**
 * Commander Module
 * Hierarchical agent delegation system
 *
 * The commander enables a parent agent to delegate specialized tasks
 * to worker agents with isolated tool access.
 *
 * Key patterns (from Gemini CLI and OpenCode):
 * - Workers as tools (delegate_* naming)
 * - Recursion prevention (workers cannot call workers)
 * - Phone book injection (directory of available workers)
 * - Activity streaming (events flow from workers to commander)
 *
 * @example
 * ```typescript
 * import {
 *   createDefaultRegistry,
 *   createAllWorkerTools,
 *   generatePhoneBook
 * } from '@ava/core/commander'
 *
 * // Create registry with built-in workers
 * const registry = createDefaultRegistry()
 *
 * // Generate phone book for commander's system prompt
 * const phoneBook = generatePhoneBook(registry)
 *
 * // Create worker tools for the commander
 * const workerTools = createAllWorkerTools(registry, (event) => {
 *   console.log(`[${event.workerName}] ${event.type}:`, event.data)
 * })
 * ```
 */

// Executor
export {
  DELEGATE_TOOL_PREFIX,
  executeWithAutoRouting,
  executeWorker,
  getFilteredTools,
  isDelegationTool,
} from './executor.js'
export type {
  MultiplexedActivity,
  MultiplexedActivityCallback,
  PartitionResult,
} from './parallel/index.js'
// Parallel Execution
export {
  // Activity multiplexing
  ActivityMultiplexer,
  // Conflict detection
  ConflictDetector,
  createAggregator,
  // Task scheduling
  createFanIn,
  createFanOut,
  createFilteredCallback,
  createLinearChain,
  createTaggedCallback,
  // Batch execution
  executeBatch,
  executeFullParallel,
  executeSequential,
  executeWithConflictDetection,
  partitionTasks,
  Semaphore,
  TaskScheduler,
} from './parallel/index.js'
// Registry
export { createWorkerRegistry, WorkerRegistry } from './registry.js'
// Router
export { analyzeTask, selectWorker, type TaskAnalysis } from './router.js'
// Tool Wrapper
export type { WorkerToolParams } from './tool-wrapper.js'
export {
  createAllWorkerTools,
  createWorkerTool,
  getDelegateToolNames,
  isDelegateToolFromRegistry,
} from './tool-wrapper.js'
// Types
export type {
  BatchTask,
  CombinedWorkerResult,
  ConflictInfo,
  ConflictResult,
  DependentTask,
  FileAccess,
  ParallelConfig,
  ParallelExecutionResult,
  TaskStatus,
  WorkerActivityCallback,
  WorkerActivityEvent,
  WorkerActivityType,
  WorkerDefinition,
  WorkerInputs,
  WorkerResult,
} from './types.js'
export { DEFAULT_PARALLEL_CONFIG } from './types.js'
// Utilities
export {
  aggregateErrors,
  calculateWorkerStats,
  combineWorkerResults,
  formatAggregatedErrors,
  formatWorkerOutput,
  formatWorkerSummary,
  generateCompactPhoneBook,
  generatePhoneBook,
  getFailedWorkers,
  hasWorkerFailures,
} from './utils.js'
// Built-in Workers
export {
  BUILT_IN_WORKERS,
  CODER_WORKER,
  createDefaultRegistry,
  DEBUGGER_WORKER,
  RESEARCHER_WORKER,
  REVIEWER_WORKER,
  TESTER_WORKER,
} from './workers/definitions.js'
