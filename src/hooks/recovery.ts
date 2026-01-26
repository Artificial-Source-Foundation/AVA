/**
 * Delta9 Recovery Hooks
 *
 * Hooks for automatic error recovery and resilience.
 * Monitors tool execution and triggers recovery when needed.
 */

import type { MissionState } from '../mission/state.js'
import {
  handleTaskFailure,
  canAutoRecover,
  type FailureContext,
} from '../mission/failure-handler.js'
import { appendHistory } from '../mission/history.js'
import { info, warn, error as logError } from '../lib/logger.js'
import type { ToolExecuteAfterInput, ToolExecuteAfterOutput } from './tool-output.js'

// =============================================================================
// Types
// =============================================================================

export interface RecoveryHooksInput {
  state: MissionState
  cwd: string
}

export interface RecoveryHooks {
  'tool.execute.after': (
    input: ToolExecuteAfterInput,
    output: ToolExecuteAfterOutput
  ) => Promise<void>
}

// =============================================================================
// Recovery Detection
// =============================================================================

/**
 * Detect if a tool output indicates failure
 */
function detectFailure(
  _tool: string,
  output: string,
  error?: Error
): { isFailed: boolean; failureType: FailureContext['failureType']; reason: string } | null {
  // Check for explicit error
  if (error) {
    return {
      isFailed: true,
      failureType: 'error',
      reason: error.message,
    }
  }

  // Parse output as JSON if possible
  let parsed: Record<string, unknown> | null = null
  try {
    parsed = JSON.parse(output)
  } catch {
    // Not JSON, continue with string analysis
  }

  // Check for failure indicators in parsed output
  if (parsed) {
    // Check for explicit success: false
    if (parsed.success === false) {
      return {
        isFailed: true,
        failureType: 'execution',
        reason: (parsed.message as string) || 'Tool execution failed',
      }
    }

    // Check for validation failure
    if (parsed.validationStatus === 'fail') {
      return {
        isFailed: true,
        failureType: 'validation',
        reason: (parsed.message as string) || 'Validation failed',
      }
    }

    // Check for fixable validation (counts as partial failure)
    if (parsed.validationStatus === 'fixable') {
      return {
        isFailed: true,
        failureType: 'validation',
        reason: (parsed.message as string) || 'Validation requires fixes',
      }
    }
  }

  // Check for failure keywords in string output
  const failureKeywords = ['error:', 'failed:', 'exception:', 'fatal:']
  const lowerOutput = output.toLowerCase()

  for (const keyword of failureKeywords) {
    if (lowerOutput.includes(keyword)) {
      return {
        isFailed: true,
        failureType: 'error',
        reason: `Output contains failure indicator: ${keyword}`,
      }
    }
  }

  return null
}

/**
 * Extract task ID from tool output
 */
function extractTaskId(output: string): string | null {
  try {
    const parsed = JSON.parse(output)
    if (typeof parsed.taskId === 'string') {
      return parsed.taskId
    }
  } catch {
    // Not JSON, try to extract from text
    const match = output.match(/taskId["\s:]+["']?([a-zA-Z0-9_-]+)["']?/i)
    if (match) {
      return match[1]
    }
  }
  return null
}

// =============================================================================
// Recovery Hook Factory
// =============================================================================

/**
 * Create recovery hooks for automatic error handling
 */
export function createRecoveryHooks(input: RecoveryHooksInput): RecoveryHooks {
  const { state, cwd } = input

  // Track recent failures to avoid infinite recovery loops
  const recentFailures = new Map<string, number>()
  const MAX_FAILURES_PER_TASK = 5
  const FAILURE_WINDOW_MS = 5 * 60 * 1000 // 5 minutes

  return {
    'tool.execute.after': async (
      toolInput: ToolExecuteAfterInput,
      toolOutput: ToolExecuteAfterOutput
    ) => {
      // Only monitor dispatch and validation tools
      const monitoredTools = [
        'dispatch_task',
        'validation_result',
        'run_tests',
        'check_lint',
        'check_types',
        'delegate_task',
      ]

      if (!monitoredTools.includes(toolInput.tool)) {
        return
      }

      // Detect failure
      const failure = detectFailure(toolInput.tool, toolOutput.output, toolOutput.error)

      if (!failure || !failure.isFailed) {
        return
      }

      // Get task ID from output
      const taskId = extractTaskId(toolOutput.output)
      if (!taskId) {
        warn(`Recovery hook: Failure detected but no taskId in ${toolInput.tool}`)
        return
      }

      // Check for recovery loop prevention
      const failureKey = `${taskId}:${Date.now()}`
      const taskFailureCount = Array.from(recentFailures.entries())
        .filter(([key]) => key.startsWith(taskId))
        .filter(([, timestamp]) => Date.now() - timestamp < FAILURE_WINDOW_MS).length

      if (taskFailureCount >= MAX_FAILURES_PER_TASK) {
        logError(
          `Recovery hook: Task ${taskId} has failed ${taskFailureCount} times recently, skipping auto-recovery`
        )
        return
      }

      recentFailures.set(failureKey, Date.now())

      // Clean up old failure records
      for (const [key, timestamp] of recentFailures.entries()) {
        if (Date.now() - timestamp > FAILURE_WINDOW_MS) {
          recentFailures.delete(key)
        }
      }

      // Get task for attempt count
      const task = state.getTask(taskId)
      const attempts = task?.attempts ?? 1

      // Build failure context
      const context: FailureContext = {
        taskId,
        reason: failure.reason,
        attempts,
        failureType: failure.failureType,
        error: toolOutput.error?.message,
      }

      // Check if auto-recovery is possible
      const autoRecovery = canAutoRecover(state, taskId)

      if (autoRecovery.canRecover) {
        info(`Recovery hook: Auto-recovering task ${taskId} via ${autoRecovery.method}`)

        // Handle the failure with automatic recovery
        const response = handleTaskFailure(state, cwd, context)

        // Log recovery attempt
        const mission = state.getMission()
        if (mission) {
          appendHistory(cwd, {
            type: 'recovery_attempted',
            timestamp: new Date().toISOString(),
            missionId: mission.id,
            taskId,
            data: {
              recoveryType: 'auto',
              method: autoRecovery.method,
              action: response.action,
              success: response.action !== 'abort',
            },
          })
        }
      } else {
        warn(`Recovery hook: Task ${taskId} cannot be auto-recovered: ${autoRecovery.reason}`)

        // Log that manual intervention is needed
        const mission = state.getMission()
        if (mission) {
          appendHistory(cwd, {
            type: 'recovery_attempted',
            timestamp: new Date().toISOString(),
            missionId: mission.id,
            taskId,
            data: {
              recoveryType: 'manual_required',
              reason: autoRecovery.reason,
              failureContext: context,
            },
          })
        }
      }
    },
  }
}

// =============================================================================
// Hook Integration
// =============================================================================

/**
 * Get recovery hooks for integration with main hooks
 */
export function getRecoveryHooks(state: MissionState, cwd: string): RecoveryHooks {
  return createRecoveryHooks({ state, cwd })
}
