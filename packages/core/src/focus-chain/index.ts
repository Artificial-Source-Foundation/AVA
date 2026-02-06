/**
 * Focus Chain Module
 *
 * Task progress tracking via markdown checklist
 */

// Manager
export {
  createFocusChainManager,
  FocusChainManager,
  getFocusChainManager,
} from './manager.js'
// Parser functions (renamed to avoid conflict with agent/evaluator.ts)
export {
  addTaskToMarkdown,
  calculateProgress as calculateFocusProgress,
  getNextTask,
  parseMarkdown,
  removeTaskFromMarkdown,
  serializeToMarkdown,
  updateTaskInMarkdown,
} from './parser.js'
// Types (renamed to avoid conflict with commander/types.ts)
export type {
  FocusChain,
  FocusChainEvent,
  FocusChainEventListener,
  FocusChainOptions,
  FocusTask,
  TaskStatus as FocusTaskStatus,
} from './types.js'
export { DEFAULT_FOCUS_CHAIN_OPTIONS } from './types.js'
