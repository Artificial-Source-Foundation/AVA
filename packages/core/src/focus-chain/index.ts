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
// Parser functions
export {
  addTaskToMarkdown,
  calculateProgress,
  getNextTask,
  parseMarkdown,
  removeTaskFromMarkdown,
  serializeToMarkdown,
  updateTaskInMarkdown,
} from './parser.js'
// Types
export type {
  FocusChain,
  FocusChainEvent,
  FocusChainEventListener,
  FocusChainOptions,
  FocusTask,
  TaskStatus,
} from './types.js'
export { DEFAULT_FOCUS_CHAIN_OPTIONS } from './types.js'
