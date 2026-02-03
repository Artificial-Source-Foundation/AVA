/**
 * Agent Module
 * Provides autonomous agent loop for multi-step task execution
 */

// Evaluator (Progress Tracking)
export {
  analyzeToolUsage,
  calculateMetrics,
  calculateProgress,
  evaluateGoal,
  type GoalEvaluation,
  type ProgressReport,
  type ProgressStatus,
} from './evaluator.js'
// Events (Event Management)
export {
  AgentEventEmitter,
  createBufferedCallback,
  createEventBuffer,
  createEventEmitter,
  EventBuffer,
  filterByType,
  getErrorEvents,
  getEventDuration,
  getEventStats,
  getThoughts,
  getToolEvents,
  getTotalDuration,
  getTurnDurations,
} from './events.js'
// Agent Loop
export { AgentExecutor, runAgent } from './loop.js'
// Planner
export {
  AgentPlanner,
  type PlannedStep,
  type PlannerConfig,
  planRecovery,
  planTask,
  type RecoveryPlan,
  type RecoveryStrategy,
  type TaskPlan,
} from './planner.js'
// Recovery (Self-Correction)
export {
  calculateBackoffDelay,
  classifyError,
  createRecoveryManager,
  type ErrorCategory,
  getStrategyForCategory,
  isRetryableCategory,
  type RecoveryActionResult,
  RecoveryManager,
  type RetryOptions,
  type RollbackState,
  retryWithBackoff,
} from './recovery.js'
// Types
export {
  type AgentConfig,
  type AgentEvent,
  type AgentEventBase,
  type AgentEventCallback,
  type AgentEventType,
  type AgentFinishEvent,
  type AgentInputs,
  type AgentResult,
  type AgentStartEvent,
  type AgentStep,
  type AgentStepStatus,
  AgentTerminateMode,
  type AgentTurnResult,
  COMPLETE_TASK_TOOL,
  DEFAULT_AGENT_CONFIG,
  type ErrorEvent,
  type RecoveryFinishEvent,
  type RecoveryStartEvent,
  type ThoughtEvent,
  type ToolCallInfo,
  type ToolErrorEvent,
  type ToolFinishEvent,
  type ToolStartEvent,
  type TurnFinishEvent,
  type TurnStartEvent,
} from './types.js'
