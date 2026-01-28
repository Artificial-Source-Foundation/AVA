/**
 * Delta9 Mission State Types
 *
 * Mission state is persisted to .delta9/mission.json
 * and survives context compaction.
 */

import type { CouncilMode } from './config.js'

// =============================================================================
// Status Types
// =============================================================================

export type MissionStatus =
  | 'planning'
  | 'approved'
  | 'in_progress'
  | 'paused'
  | 'completed'
  | 'aborted'
export type ObjectiveStatus = 'pending' | 'in_progress' | 'completed' | 'failed'
export type TaskStatus = 'pending' | 'blocked' | 'in_progress' | 'completed' | 'failed'
export type ValidationStatus = 'pass' | 'fixable' | 'fail'

// =============================================================================
// Complexity
// =============================================================================

export type Complexity = 'low' | 'medium' | 'high' | 'critical'

// =============================================================================
// Validation Result
// =============================================================================

export interface ValidationResult {
  /** Validation status */
  status: ValidationStatus
  /** When validation occurred */
  validatedAt: string
  /** Summary of validation */
  summary: string
  /** Specific issues found (for fixable/fail) */
  issues?: string[]
  /** Suggestions for fixes (for fixable) */
  suggestions?: string[]
}

// =============================================================================
// Task
// =============================================================================

export interface Task {
  /** Unique task ID */
  id: string
  /** Task description */
  description: string
  /** Current status */
  status: TaskStatus
  /** Agent assigned to this task */
  assignedTo?: string
  /** Specialized routing (ui-ops, qa, scribe, etc.) */
  routedTo?: string
  /** Worker session ID */
  workerSession?: string
  /** Number of attempts */
  attempts: number
  /** Acceptance criteria */
  acceptanceCriteria: string[]
  /** Validation result */
  validation?: ValidationResult
  /** Files changed by this task (tracked during execution) */
  filesChanged?: string[]
  /** Files this task will modify - exclusive ownership (set during planning) */
  files?: string[]
  /** Files this task can read only - no write access (set during planning) */
  filesReadonly?: string[]
  /** Explicit constraints - things this task must NOT do */
  mustNot?: string[]
  /** Tokens used */
  tokensUsed?: number
  /** Cost in dollars */
  cost?: number
  /** Task dependencies (task IDs that must complete first) */
  dependencies?: string[]
  /** When task started */
  startedAt?: string
  /** When task completed */
  completedAt?: string
  /** Error message if failed */
  error?: string
}

// =============================================================================
// Objective
// =============================================================================

export interface Objective {
  /** Unique objective ID */
  id: string
  /** Objective description */
  description: string
  /** Current status */
  status: ObjectiveStatus
  /** Tasks within this objective */
  tasks: Task[]
  /** Checkpoint name (if created) */
  checkpoint?: string
  /** When objective started */
  startedAt?: string
  /** When objective completed */
  completedAt?: string
}

// =============================================================================
// Council Summary
// =============================================================================

export interface OracleOpinion {
  /** Oracle name */
  oracle: string
  /** Recommendation text */
  recommendation: string
  /** Confidence score (0-1) */
  confidence: number
  /** Caveats or concerns */
  caveats?: string[]
  /** Suggested tasks */
  suggestedTasks?: string[]
}

export interface CouncilSummary {
  /** Council mode used */
  mode: CouncilMode
  /** Points of consensus */
  consensus: string[]
  /** Disagreements that were resolved */
  disagreementsResolved?: string[]
  /** Average confidence score */
  confidenceAvg: number
  /** Individual oracle opinions */
  opinions?: OracleOpinion[]
}

// =============================================================================
// Budget Tracking
// =============================================================================

export interface BudgetBreakdown {
  /** Council costs */
  council: number
  /** Operator costs */
  operators: number
  /** Validator costs */
  validators: number
  /** Support agent costs */
  support: number
}

export interface BudgetTracking {
  /** Budget limit */
  limit: number
  /** Total spent */
  spent: number
  /** Breakdown by category */
  breakdown: BudgetBreakdown
}

// =============================================================================
// Mission
// =============================================================================

export interface Mission {
  /** Schema version for migrations */
  $schema?: string
  /** Unique mission ID */
  id: string
  /** Mission description (from user) */
  description: string
  /** Current status */
  status: MissionStatus
  /** Detected complexity */
  complexity: Complexity
  /** Council mode used */
  councilMode: CouncilMode
  /** Council summary (if council was convened) */
  councilSummary?: CouncilSummary
  /** Objectives */
  objectives: Objective[]
  /** Current objective index */
  currentObjective: number
  /** Budget tracking */
  budget: BudgetTracking
  /** Task dependency graph */
  dependencies?: Record<string, string[]>
  /** When mission was created */
  createdAt: string
  /** When mission was last updated */
  updatedAt: string
  /** When mission was approved by user */
  approvedAt?: string
  /** When mission started execution (first task started) */
  startedAt?: string
  /** When mission completed */
  completedAt?: string
}

// =============================================================================
// History Events
// =============================================================================

export type HistoryEventType =
  | 'mission_created'
  | 'mission_approved'
  | 'mission_paused'
  | 'mission_resumed'
  | 'mission_completed'
  | 'mission_aborted'
  | 'mission_status_changed' // BUG-38: State machine transitions
  | 'objective_started'
  | 'objective_completed'
  | 'objective_failed'
  | 'task_started'
  | 'task_completed'
  | 'task_failed'
  | 'task_retried'
  | 'task_unblocked' // BUG-25: Emergency unblock
  | 'dependencies_fixed' // BUG-25: Bulk dependency repair
  | 'validation_passed'
  | 'validation_fixable'
  | 'validation_failed'
  | 'checkpoint_created'
  | 'rollback_executed'
  | 'budget_warning'
  | 'budget_exceeded'
  | 'context_compacted'
  | 'council_convened'
  | 'council_completed'
  | 'xhigh_recon_completed'
  | 'background_task_started'
  | 'background_task_completed'
  | 'background_task_failed'
  | 'replan_triggered'
  | 'recovery_attempted'

export interface HistoryEvent {
  /** Event type */
  type: HistoryEventType
  /** When event occurred */
  timestamp: string
  /** Related mission ID */
  missionId: string
  /** Related objective ID */
  objectiveId?: string
  /** Related task ID */
  taskId?: string
  /** Additional data */
  data?: Record<string, unknown>
}

// =============================================================================
// Memory Entry (Cross-session learning)
// =============================================================================

export interface MemoryEntry {
  /** Unique entry ID */
  id: string
  /** Type of memory */
  type: 'pattern' | 'failure' | 'success' | 'preference'
  /** What was learned */
  content: string
  /** Confidence in this memory */
  confidence: number
  /** How many times this pattern was seen */
  occurrences: number
  /** When first created */
  createdAt: string
  /** When last updated */
  updatedAt: string
  /** Related tags */
  tags?: string[]
}

// =============================================================================
// Mission Progress
// =============================================================================

export interface MissionProgress {
  /** Total tasks */
  total: number
  /** Completed tasks */
  completed: number
  /** In-progress tasks */
  inProgress: number
  /** Failed tasks */
  failed: number
  /** Blocked tasks */
  blocked: number
  /** Pending tasks */
  pending: number
  /** Progress percentage */
  percentage: number
}
