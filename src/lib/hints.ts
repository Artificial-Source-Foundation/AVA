/**
 * Delta9 Helpful Hints System
 *
 * Provides context-aware hints for empty states and error situations.
 * These hints guide users to the next action they should take.
 */

// =============================================================================
// Hint Definitions
// =============================================================================

/**
 * Static hints for common empty/error states
 */
export const hints = {
  // -------------------------------------------------------------------------
  // Background Task Hints
  // -------------------------------------------------------------------------

  noTasks: 'No background tasks. Use delegate_task with run_in_background=true to spawn an agent.',

  noRunningTasks: 'No running tasks. All tasks have completed or been cancelled.',

  tasksAllComplete: 'All tasks completed successfully. Use background_cleanup to remove old tasks.',

  taskFailed: (agent: string) =>
    `${agent} task failed. Use background_output for details or retry_task to try again.`,

  taskStale: (taskId: string) =>
    `Task ${taskId} appears stale (no activity). It may be hung - consider cancelling.`,

  // -------------------------------------------------------------------------
  // Mission Hints
  // -------------------------------------------------------------------------

  noMission: 'No active mission. Create one with mission_create to track objectives and tasks.',

  missionComplete: 'Mission complete! Use mission_status to review or create a new mission.',

  missionBlocked: 'Mission is blocked. Check task dependencies with mission_status.',

  noObjectives: 'Mission has no objectives. Add objectives with mission_add_objective.',

  missionNoTasks: 'Mission has no tasks. Add tasks with mission_add_task or use dispatch_task.',

  tasksNeedValidation:
    'Tasks awaiting validation. Use request_validation to verify completed work.',

  // -------------------------------------------------------------------------
  // Council Hints
  // -------------------------------------------------------------------------

  councilEmpty:
    'Council has no Strategic Advisors configured. Add models to delta9.json to enable multi-perspective planning.',

  councilPartial: (count: number, total: number) =>
    `Only ${count}/${total} Strategic Advisors responded. Results may be incomplete.`,

  quickConsultAvailable: 'For faster responses, use quick_consult with a single Strategic Advisor.',

  // -------------------------------------------------------------------------
  // Delegation Hints
  // -------------------------------------------------------------------------

  simulationMode: 'Running in simulation mode (SDK not available). Tasks will be simulated.',

  agentRecommendation: (complexity: string) =>
    complexity === 'complex'
      ? 'Consider using operator_complex for multi-file changes.'
      : 'Use operator (default) for standard implementation tasks.',

  backgroundRecommendation:
    'For parallel work, use run_in_background=true to spawn agents without blocking.',

  // -------------------------------------------------------------------------
  // Validation Hints
  // -------------------------------------------------------------------------

  validationPending: 'Tasks pending validation. Use request_validation to verify completed work.',

  allTasksValidated: 'All tasks have been validated. Mission may be ready to complete.',

  validationFailed: 'Some validations failed. Review failures and use retry_task to fix.',

  // -------------------------------------------------------------------------
  // Memory Hints
  // -------------------------------------------------------------------------

  memoryEmpty: 'Memory is empty. Use memory_set to store key-value pairs across sessions.',

  memoryAvailable: (count: number) => `${count} keys in memory. Use memory_list to see all keys.`,

  // -------------------------------------------------------------------------
  // Config Hints
  // -------------------------------------------------------------------------

  usingDefaults: 'Using default configuration. Create delta9.json to customize behavior.',

  configLoaded: 'Configuration loaded from delta9.json.',
}

// =============================================================================
// Hint Selection Functions
// =============================================================================

export interface HintContext {
  // Background task state
  totalTasks?: number
  runningTasks?: number
  failedTasks?: number
  completedTasks?: number

  // Mission state
  hasMission?: boolean
  missionStatus?: string
  objectiveCount?: number
  taskCount?: number
  pendingValidation?: number

  // Council state
  oracleCount?: number
  respondedOracles?: number

  // SDK state
  sdkAvailable?: boolean

  // Memory state
  memoryKeyCount?: number

  // Config state
  configLoaded?: boolean
}

/**
 * Get contextual hint based on current state
 */
export function getHint(context: HintContext): string | undefined {
  // Background task hints
  if (context.totalTasks === 0) {
    return hints.noTasks
  }

  if (context.runningTasks === 0 && context.totalTasks && context.totalTasks > 0) {
    if (context.failedTasks && context.failedTasks > 0) {
      return `${context.failedTasks} task(s) failed. Use background_output for details.`
    }
    return hints.noRunningTasks
  }

  // Mission hints
  if (context.hasMission === false) {
    return hints.noMission
  }

  if (context.missionStatus === 'completed') {
    return hints.missionComplete
  }

  if (context.pendingValidation && context.pendingValidation > 0) {
    return hints.tasksNeedValidation
  }

  // Council hints
  if (context.oracleCount === 0) {
    return hints.councilEmpty
  }

  if (
    context.respondedOracles !== undefined &&
    context.oracleCount !== undefined &&
    context.respondedOracles < context.oracleCount
  ) {
    return hints.councilPartial(context.respondedOracles, context.oracleCount)
  }

  // SDK hints
  if (context.sdkAvailable === false) {
    return hints.simulationMode
  }

  // Memory hints
  if (context.memoryKeyCount === 0) {
    return hints.memoryEmpty
  }

  // Config hints
  if (context.configLoaded === false) {
    return hints.usingDefaults
  }

  return undefined
}

/**
 * Get hint for background_list tool
 */
export function getBackgroundListHint(
  running: number,
  completed: number,
  failed: number,
  total: number
): string | undefined {
  if (total === 0) {
    return hints.noTasks
  }

  if (running === 0 && failed > 0) {
    return `${failed} task(s) failed. Use background_output with taskId for details.`
  }

  if (running === 0 && completed === total) {
    return hints.tasksAllComplete
  }

  return undefined
}

/**
 * Get hint for mission_status tool
 */
export function getMissionStatusHint(
  hasMission: boolean,
  status?: string,
  taskCount?: number,
  pendingValidation?: number
): string | undefined {
  if (!hasMission) {
    return hints.noMission
  }

  if (status === 'completed') {
    return hints.missionComplete
  }

  if (status === 'blocked') {
    return hints.missionBlocked
  }

  if (taskCount === 0) {
    return hints.noTasks
  }

  if (pendingValidation && pendingValidation > 0) {
    return `${pendingValidation} task(s) awaiting validation.`
  }

  return undefined
}

/**
 * Get hint for council_status tool
 */
export function getCouncilStatusHint(oracleCount: number): string | undefined {
  if (oracleCount === 0) {
    return hints.councilEmpty
  }

  if (oracleCount === 1) {
    return 'Single Strategic Advisor configured. Consider adding more for diverse perspectives.'
  }

  return undefined
}
