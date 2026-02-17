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
// Agent Modes
export {
  checkMinimalModeAccess,
  checkPlanModeAccess,
  clearAllMinimalModeStates,
  clearAllPlanModeStates,
  enterMinimalMode,
  enterPlanMode,
  exitMinimalMode,
  exitPlanMode,
  getPlanModeState,
  getPlanModeStatus,
  getRestrictionReason,
  isMinimalModeActive,
  isPlanModeEnabled,
  isPlanModeRestricted,
  MINIMAL_MODE_ALLOWED_TOOLS,
  PLAN_MODE_ALLOWED_TOOLS,
  PLAN_MODE_BLOCKED_TOOLS,
  type PlanModeConfig,
  type PlanModeState,
  planEnterTool,
  planExitTool,
  setPlanModeState,
} from './modes/index.js'
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
// Prompts (System prompt builders)
export {
  BEST_PRACTICES,
  buildScenarioPrompt,
  buildSystemPrompt,
  buildWorkerPrompt,
  CAPABILITIES,
  getModelAdjustments,
  RULES,
  type SystemPromptContext,
} from './prompts/index.js'
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
// Subagent System
export {
  createSubagentManager,
  generateSubagentSessionId,
  getParentSessionId,
  isSubagentSession,
  SUBAGENT_PRESETS,
  type SubagentConfig,
  type SubagentEvent,
  type SubagentEventListener,
  SubagentManager,
  type SubagentResult,
  type SubagentTask,
  type SubagentType,
} from './subagent.js'
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
  type ProviderSwitchEvent,
  type RecoveryFinishEvent,
  type RecoveryStartEvent,
  type ThoughtEvent,
  type ToolCallInfo,
  type ToolErrorEvent,
  type ToolFinishEvent,
  type ToolStartEvent,
  type TurnFinishEvent,
  type TurnStartEvent,
  type ValidationFinishEvent,
  type ValidationResultEvent,
  type ValidationStartEvent,
} from './types.js'
