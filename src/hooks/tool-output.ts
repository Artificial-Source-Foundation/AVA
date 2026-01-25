/**
 * Delta9 Tool Output Hooks
 *
 * Intercepts tool outputs to:
 * - Track file changes
 * - Log tool usage
 * - Monitor Delta9 tool activity
 */

import type { MissionState } from '../mission/state.js'
import { appendHistory } from '../mission/history.js'

// =============================================================================
// Types
// =============================================================================

export interface ToolOutputHooksInput {
  /** Mission state instance */
  state: MissionState
  /** Project root directory */
  cwd: string
  /** Logger function */
  log: (level: string, message: string, data?: Record<string, unknown>) => void
}

/** Tool execute before hook input (from OpenCode) */
export interface ToolExecuteBeforeInput {
  tool: string
  sessionID: string
  callID: string
}

/** Tool execute before hook output (mutable) */
export interface ToolExecuteBeforeOutput {
  args: Record<string, unknown>
}

/** Tool execute after hook input (from OpenCode) */
export interface ToolExecuteAfterInput {
  tool: string
  sessionID: string
  callID: string
}

/** Tool execute after hook output (mutable) */
export interface ToolExecuteAfterOutput {
  title: string
  output: string
  metadata: Record<string, unknown>
  error?: Error
}

export interface ToolOutputHooks {
  /** Hook before tool execution */
  'tool.execute.before': (
    input: ToolExecuteBeforeInput,
    output: ToolExecuteBeforeOutput
  ) => Promise<void>
  /** Hook after tool execution */
  'tool.execute.after': (
    input: ToolExecuteAfterInput,
    output: ToolExecuteAfterOutput
  ) => Promise<void>
}

// =============================================================================
// File Tracking
// =============================================================================

/** Files changed during current session */
const changedFilesSet = new Set<string>()

/**
 * Get all files changed in current session
 */
export function getChangedFiles(): string[] {
  return Array.from(changedFilesSet)
}

/**
 * Clear changed files tracking
 */
export function clearChangedFiles(): void {
  changedFilesSet.clear()
}

// =============================================================================
// Hook Factory
// =============================================================================

/**
 * Create tool output hooks
 */
export function createToolOutputHooks(input: ToolOutputHooksInput): ToolOutputHooks {
  const { state, cwd, log } = input

  return {
    /**
     * Before Tool Execution
     *
     * - Log tool invocation for debugging
     * - Track Delta9 tool usage
     */
    'tool.execute.before': async (toolInput, output) => {
      const { tool: toolName, sessionID } = toolInput
      const args = output.args

      // Log tool invocation at debug level
      log('debug', `Tool invoked: ${toolName}`, { args, sessionID })

      // Check if this is a Delta9 tool (for tracking)
      if (isDelta9Tool(toolName)) {
        const mission = state.getMission()
        if (mission) {
          log('debug', `Delta9 tool: ${toolName}`, {
            missionId: mission.id,
            missionStatus: mission.status,
          })
        }
      }
    },

    /**
     * After Tool Execution
     *
     * - Track file changes from edit/write tools
     * - Log validation results
     */
    'tool.execute.after': async (toolInput, output) => {
      const { tool: toolName } = toolInput
      const { output: result, metadata } = output

      // Track file changes from edit/write operations
      if (isFileModifyingTool(toolName)) {
        const filePath = extractFilePath(metadata)
        if (filePath) {
          changedFilesSet.add(filePath)
          log('debug', `File changed: ${filePath}`, { tool: toolName })

          // Update current task's filesChanged if we have an active task
          const mission = state.getMission()
          if (mission) {
            const inProgressTask = findInProgressTask(state)
            if (inProgressTask) {
              const existingFiles = inProgressTask.filesChanged || []
              if (!existingFiles.includes(filePath)) {
                state.updateTask(inProgressTask.id, {
                  filesChanged: [...existingFiles, filePath],
                })
              }
            }
          }
        }
      }

      // Log validation tool results
      if (toolName === 'validation_result') {
        try {
          const parsed = JSON.parse(result)
          const validationStatus = parsed.validationStatus
          const taskId = parsed.taskId

          if (validationStatus && taskId) {
            appendHistory(cwd, {
              type: validationStatus === 'pass' ? 'task_completed' : 'validation_fixable',
              timestamp: new Date().toISOString(),
              missionId: state.getMissionId() || 'unknown',
              taskId,
              data: { status: validationStatus },
            })
          }
        } catch {
          // Ignore parse errors
        }
      }

      // Log dispatch events
      if (toolName === 'dispatch_task') {
        try {
          const parsed = JSON.parse(result)
          if (parsed.taskId) {
            log('info', `Task dispatched: ${parsed.taskId}`)
          }
        } catch {
          // Ignore parse errors
        }
      }
    },
  }
}

// =============================================================================
// Helpers
// =============================================================================

/**
 * Check if tool is a Delta9 tool
 */
function isDelta9Tool(toolName: string): boolean {
  const delta9Tools = [
    'mission_create',
    'mission_status',
    'mission_update',
    'mission_add_objective',
    'mission_add_task',
    'dispatch_task',
    'task_complete',
    'request_validation',
    'retry_task',
    'validation_result',
    'run_tests',
    'check_lint',
    'check_types',
    'delegate_task',
    'background_output',
    'background_cancel',
    'background_list',
    'consult_council',
  ]
  return delta9Tools.includes(toolName)
}

/**
 * Check if tool modifies files
 */
function isFileModifyingTool(toolName: string): boolean {
  const fileTools = [
    'Write',
    'Edit',
    'MultiEdit',
    'write',
    'edit',
    'file_write',
    'file_edit',
  ]
  return fileTools.includes(toolName)
}

/**
 * Extract file path from tool metadata
 */
function extractFilePath(metadata: Record<string, unknown>): string | null {
  return (
    (metadata.file_path as string | undefined) ??
    (metadata.path as string | undefined) ??
    (metadata.filePath as string | undefined) ??
    null
  )
}

/**
 * Find the currently in-progress task
 */
function findInProgressTask(state: MissionState): { id: string; filesChanged?: string[] } | null {
  const mission = state.getMission()
  if (!mission) return null

  for (const objective of mission.objectives) {
    for (const task of objective.tasks) {
      if (task.status === 'in_progress') {
        return { id: task.id, filesChanged: task.filesChanged }
      }
    }
  }
  return null
}
