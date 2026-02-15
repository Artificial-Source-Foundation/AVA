/**
 * Focus Chain Types
 *
 * Task progress tracking via markdown checklist
 */

// ============================================================================
// Core Types
// ============================================================================

/**
 * Status of a task item
 */
export type TaskStatus = 'pending' | 'in_progress' | 'completed' | 'blocked'

/**
 * A single task item in the focus chain
 */
export interface FocusTask {
  /** Unique identifier */
  id: string
  /** Task description */
  text: string
  /** Current status */
  status: TaskStatus
  /** Indentation level (0 = top-level) */
  level: number
  /** Line number in the markdown file */
  line: number
  /** Parent task ID (for nested tasks) */
  parentId?: string
  /** Child task IDs */
  childIds: string[]
  /** Notes or context */
  notes?: string
}

/**
 * The complete focus chain state
 */
export interface FocusChain {
  /** All tasks */
  tasks: FocusTask[]
  /** Currently active task ID */
  activeTaskId?: string
  /** File path of the markdown file */
  filePath: string
  /** Last modified timestamp */
  lastModified: number
  /** Session metadata */
  metadata: FocusChainMetadata
}

/**
 * Metadata about the focus chain
 */
export interface FocusChainMetadata {
  /** Session start time */
  startTime: number
  /** Total tasks */
  totalTasks: number
  /** Completed tasks */
  completedTasks: number
  /** Blocked tasks */
  blockedTasks: number
}

// ============================================================================
// Events
// ============================================================================

/**
 * Events emitted by the focus chain manager
 */
export type FocusChainEvent =
  | { type: 'task:added'; task: FocusTask }
  | { type: 'task:updated'; task: FocusTask; previousStatus: TaskStatus }
  | { type: 'task:removed'; taskId: string }
  | { type: 'chain:loaded'; chain: FocusChain }
  | { type: 'chain:saved'; filePath: string }
  | { type: 'chain:external_change'; chain: FocusChain }

/**
 * Event listener for focus chain events
 */
export type FocusChainEventListener = (event: FocusChainEvent) => void

// ============================================================================
// Options
// ============================================================================

/**
 * Options for creating a focus chain manager
 */
export interface FocusChainOptions {
  /** Directory to store the focus chain file */
  directory?: string
  /** File name (default: tasks.md) */
  fileName?: string
  /** Enable file watching for external edits */
  watchFile?: boolean
  /** Debounce time for file watch (ms) */
  watchDebounce?: number
}

/**
 * Default options
 */
export const DEFAULT_FOCUS_CHAIN_OPTIONS: Required<FocusChainOptions> = {
  directory: '.ava',
  fileName: 'tasks.md',
  watchFile: true,
  watchDebounce: 300,
}
