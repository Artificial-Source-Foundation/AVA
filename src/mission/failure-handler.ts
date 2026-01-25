/**
 * Delta9 Failure Handler
 *
 * Handles task failures and implements cascading failure logic.
 * Manages task dependencies and triggers replanning when needed.
 */

import type { MissionState } from './state.js'
import type { Task } from '../types/mission.js'
import { appendHistory } from './history.js'

// =============================================================================
// Types
// =============================================================================

export interface FailureContext {
  /** Task that failed */
  taskId: string
  /** Failure reason */
  reason: string
  /** Number of attempts made */
  attempts: number
  /** Whether this was a validation failure or execution failure */
  failureType: 'validation' | 'execution' | 'timeout' | 'error'
  /** Error message if available */
  error?: string
}

export interface FailureResponse {
  /** Action to take */
  action: 'retry' | 'skip' | 'block_dependents' | 'replan' | 'abort'
  /** Affected task IDs */
  affectedTasks: string[]
  /** Reason for the action */
  reason: string
  /** Whether human intervention is needed */
  needsIntervention: boolean
  /** Suggested next steps */
  suggestions?: string[]
}

export interface RecoveryAttempt {
  /** Timestamp of recovery attempt */
  timestamp: string
  /** Type of recovery */
  type: 'retry' | 'replan' | 'skip' | 'manual'
  /** Whether recovery was successful */
  success: boolean
  /** Details about the attempt */
  details?: string
}

// =============================================================================
// Failure Analysis
// =============================================================================

/**
 * Analyze a task failure and determine the appropriate response
 */
export function analyzeFailure(
  state: MissionState,
  context: FailureContext
): FailureResponse {
  const task = state.getTask(context.taskId)
  const mission = state.getMission()

  if (!task || !mission) {
    return {
      action: 'abort',
      affectedTasks: [],
      reason: 'Task or mission not found',
      needsIntervention: true,
    }
  }

  // Check retry eligibility
  const maxRetries = getMaxRetries()
  if (context.attempts < maxRetries && context.failureType === 'validation') {
    return {
      action: 'retry',
      affectedTasks: [context.taskId],
      reason: `Validation failure, attempt ${context.attempts}/${maxRetries}`,
      needsIntervention: false,
      suggestions: [
        'Review validation feedback',
        'Fix identified issues',
        'Run validation checks locally before resubmitting',
      ],
    }
  }

  // Check for dependent tasks
  const dependentTasks = findDependentTasks(state, context.taskId)

  if (dependentTasks.length > 0) {
    // Block dependent tasks if this task is critical
    if (isTaskCritical(task)) {
      return {
        action: 'block_dependents',
        affectedTasks: [context.taskId, ...dependentTasks],
        reason: `Critical task failed, blocking ${dependentTasks.length} dependent tasks`,
        needsIntervention: true,
        suggestions: [
          'Review the failed task and its requirements',
          'Consider replanning the objective',
          'Check if dependent tasks can be restructured',
        ],
      }
    }

    // Try to skip and continue with other tasks
    if (canSkipTask(task)) {
      return {
        action: 'skip',
        affectedTasks: [context.taskId],
        reason: 'Non-critical task failed, skipping to unblock dependents',
        needsIntervention: false,
        suggestions: [
          'Task will be marked as skipped',
          'Dependent tasks may need adjustment',
          'Consider revisiting skipped task later',
        ],
      }
    }
  }

  // Determine if replanning is needed
  const replanThreshold = 2 // Number of failures before suggesting replan
  if (context.attempts >= replanThreshold) {
    return {
      action: 'replan',
      affectedTasks: [context.taskId, ...dependentTasks],
      reason: `Task failed ${context.attempts} times, replanning recommended`,
      needsIntervention: true,
      suggestions: [
        'Analyze why the task keeps failing',
        'Consider breaking it into smaller tasks',
        'Review acceptance criteria for feasibility',
        'Check if approach needs to change',
      ],
    }
  }

  // Default: retry if possible
  return {
    action: 'retry',
    affectedTasks: [context.taskId],
    reason: 'Attempting retry with adjusted approach',
    needsIntervention: false,
  }
}

/**
 * Get maximum retries for a task
 */
function getMaxRetries(): number {
  // Could be configurable per task or mission
  return 3
}

/**
 * Check if a task is critical (blocks important work)
 */
function isTaskCritical(task: Task): boolean {
  // Check for critical keywords in description
  const criticalKeywords = ['critical', 'blocking', 'essential', 'required']
  const description = task.description.toLowerCase()

  return criticalKeywords.some((kw) => description.includes(kw))
}

/**
 * Check if a task can be skipped
 */
function canSkipTask(task: Task): boolean {
  // Check for optional keywords
  const optionalKeywords = ['optional', 'nice-to-have', 'bonus', 'if time']
  const description = task.description.toLowerCase()

  return optionalKeywords.some((kw) => description.includes(kw))
}

/**
 * Find tasks that depend on a given task
 */
function findDependentTasks(state: MissionState, taskId: string): string[] {
  const mission = state.getMission()
  if (!mission) return []

  const dependentIds: string[] = []

  // Check all objectives for tasks that depend on this one
  for (const objective of mission.objectives) {
    for (const task of objective.tasks) {
      if (task.dependencies?.includes(taskId)) {
        dependentIds.push(task.id)
      }
    }
  }

  return dependentIds
}

// =============================================================================
// Failure Handling
// =============================================================================

/**
 * Handle a task failure
 */
export function handleTaskFailure(
  state: MissionState,
  cwd: string,
  context: FailureContext
): FailureResponse {
  const response = analyzeFailure(state, context)
  const mission = state.getMission()

  // Log the failure
  if (mission) {
    appendHistory(cwd, {
      type: 'task_failed',
      timestamp: new Date().toISOString(),
      missionId: mission.id,
      taskId: context.taskId,
      data: {
        reason: context.reason,
        attempts: context.attempts,
        failureType: context.failureType,
        action: response.action,
        affectedTasks: response.affectedTasks,
      },
    })
  }

  // Execute the response action
  switch (response.action) {
    case 'block_dependents':
      blockDependentTasks(state, cwd, context.taskId, response.affectedTasks)
      break

    case 'replan':
      triggerReplan(state, cwd, context, response)
      break

    case 'skip':
      skipTask(state, cwd, context.taskId)
      break

    case 'abort':
      abortMission(state, cwd, context.reason)
      break

    // 'retry' doesn't require immediate action - handled by dispatch
    default:
      break
  }

  return response
}

/**
 * Block tasks that depend on a failed task
 */
function blockDependentTasks(
  state: MissionState,
  cwd: string,
  failedTaskId: string,
  dependentIds: string[]
): void {
  const mission = state.getMission()

  for (const taskId of dependentIds) {
    if (taskId === failedTaskId) continue

    const task = state.getTask(taskId)
    if (task && task.status === 'pending') {
      // Mark task as blocked - it can't proceed because a dependency failed
      state.updateTask(taskId, {
        status: 'blocked',
      })
    }
  }

  if (mission) {
    appendHistory(cwd, {
      type: 'recovery_attempted',
      timestamp: new Date().toISOString(),
      missionId: mission.id,
      data: {
        type: 'block_dependents',
        failedTaskId,
        blockedTasks: dependentIds.filter((id) => id !== failedTaskId),
      },
    })
  }
}

/**
 * Trigger a replan for the current objective
 */
function triggerReplan(
  state: MissionState,
  cwd: string,
  context: FailureContext,
  response: FailureResponse
): void {
  const mission = state.getMission()

  if (mission) {
    appendHistory(cwd, {
      type: 'replan_triggered',
      timestamp: new Date().toISOString(),
      missionId: mission.id,
      taskId: context.taskId,
      data: {
        reason: response.reason,
        failedAttempts: context.attempts,
        affectedTasks: response.affectedTasks,
        suggestions: response.suggestions,
      },
    })
  }

  // Update mission state to indicate replanning is needed
  state.updateMission({
    status: 'paused',
  })
}

/**
 * Skip a non-critical task
 */
function skipTask(state: MissionState, cwd: string, taskId: string): void {
  const mission = state.getMission()
  const task = state.getTask(taskId)

  if (task) {
    // Mark task as skipped (using 'failed' status with a note)
    state.completeTask(taskId, {
      status: 'fail',
      validatedAt: new Date().toISOString(),
      summary: 'Task skipped due to repeated failures',
      issues: ['Task was skipped to unblock dependent work'],
    })

    if (mission) {
      appendHistory(cwd, {
        type: 'recovery_attempted',
        timestamp: new Date().toISOString(),
        missionId: mission.id,
        taskId,
        data: {
          type: 'skip',
          reason: 'Non-critical task skipped',
        },
      })
    }
  }
}

/**
 * Abort the entire mission
 */
function abortMission(state: MissionState, cwd: string, reason: string): void {
  const mission = state.getMission()

  if (mission) {
    state.updateMission({
      status: 'aborted',
    })

    appendHistory(cwd, {
      type: 'mission_aborted',
      timestamp: new Date().toISOString(),
      missionId: mission.id,
      data: {
        reason,
        timestamp: new Date().toISOString(),
      },
    })
  }
}

// =============================================================================
// Recovery Helpers
// =============================================================================

/**
 * Check if a task can be recovered automatically
 */
export function canAutoRecover(
  state: MissionState,
  taskId: string
): { canRecover: boolean; method: 'retry' | 'skip' | 'none'; reason: string } {
  const task = state.getTask(taskId)
  const mission = state.getMission()

  if (!task || !mission) {
    return { canRecover: false, method: 'none', reason: 'Task or mission not found' }
  }

  // Check retry count
  if (task.attempts < 3) {
    return { canRecover: true, method: 'retry', reason: 'Retry attempts remaining' }
  }

  // Check if skippable
  if (canSkipTask(task)) {
    return { canRecover: true, method: 'skip', reason: 'Task is optional and can be skipped' }
  }

  return { canRecover: false, method: 'none', reason: 'Max retries exceeded, task is required' }
}

/**
 * Get failure statistics for a mission
 */
export function getFailureStats(state: MissionState): {
  totalTasks: number
  failedTasks: number
  retriedTasks: number
  skippedTasks: number
  blockedTasks: number
} {
  const mission = state.getMission()

  if (!mission) {
    return {
      totalTasks: 0,
      failedTasks: 0,
      retriedTasks: 0,
      skippedTasks: 0,
      blockedTasks: 0,
    }
  }

  let totalTasks = 0
  let failedTasks = 0
  let retriedTasks = 0
  let skippedTasks = 0
  let blockedTasks = 0

  for (const objective of mission.objectives) {
    for (const task of objective.tasks) {
      totalTasks++

      if (task.status === 'failed') {
        failedTasks++
      }

      if (task.attempts > 1) {
        retriedTasks++
      }

      if (task.validation?.summary?.includes('skipped')) {
        skippedTasks++
      }

      if (task.dependencies && task.dependencies.length > 0 && task.status === 'pending') {
        blockedTasks++
      }
    }
  }

  return {
    totalTasks,
    failedTasks,
    retriedTasks,
    skippedTasks,
    blockedTasks,
  }
}
