/**
 * Delta9 Dispatch Tools
 *
 * Tools for task dispatch and completion.
 * Used by Commander to dispatch tasks and by Operators to report completion.
 */

import { z } from 'zod'
import type { MissionState } from '../mission/state.js'

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create dispatch tools
 */
export function createDispatchTools(state: MissionState) {
  return {
    /**
     * Dispatch a task to an operator
     */
    dispatch_task: {
      description: 'Dispatch a task to an Operator for execution. Use this to start work on a task.',
      parameters: z.object({
        taskId: z.string().describe('ID of the task to dispatch'),
        operator: z.string().optional().describe('Specific operator to use (default: operator)'),
        context: z.string().optional().describe('Additional context for the operator'),
      }),

      execute: async (args: {
        taskId: string
        operator?: string
        context?: string
      }) => {
        const task = state.getTask(args.taskId)

        if (!task) {
          return {
            success: false,
            message: `Task ${args.taskId} not found`,
          }
        }

        if (task.status !== 'pending') {
          return {
            success: false,
            message: `Task ${args.taskId} is ${task.status}, cannot dispatch`,
          }
        }

        const mission = state.getMission()
        const currentObj = state.getCurrentObjective()

        // Mark task as started
        state.startTask(args.taskId, args.operator || 'operator')

        // Build dispatch payload
        const dispatchPayload = {
          taskId: task.id,
          description: task.description,
          acceptanceCriteria: task.acceptanceCriteria,
          missionContext: mission?.description || '',
          objectiveContext: currentObj?.description || '',
          additionalContext: args.context || '',
          previousAttempts: task.attempts > 1 ? task.attempts - 1 : 0,
          routing: task.routedTo,
        }

        return {
          success: true,
          dispatched: true,
          taskId: args.taskId,
          operator: args.operator || 'operator',
          payload: dispatchPayload,
          message: `Task ${args.taskId} dispatched to ${args.operator || 'operator'}`,
        }
      },
    },

    /**
     * Report task completion (used by Operators)
     */
    task_complete: {
      description: 'Report that a task has been completed. Include a summary of changes made.',
      parameters: z.object({
        taskId: z.string().describe('ID of the completed task'),
        summary: z.string().describe('Summary of what was done'),
        filesChanged: z.array(z.string()).optional().describe('List of files that were changed'),
        notes: z.string().optional().describe('Any notes for Commander or Validator'),
      }),

      execute: async (args: {
        taskId: string
        summary: string
        filesChanged?: string[]
        notes?: string
      }) => {
        const task = state.getTask(args.taskId)

        if (!task) {
          return {
            success: false,
            message: `Task ${args.taskId} not found`,
          }
        }

        if (task.status !== 'in_progress') {
          return {
            success: false,
            message: `Task ${args.taskId} is ${task.status}, not in progress`,
          }
        }

        // Update task with completion info (but don't mark complete yet - wait for validation)
        state.updateTask(args.taskId, {
          filesChanged: args.filesChanged,
        })

        return {
          success: true,
          taskId: args.taskId,
          summary: args.summary,
          filesChanged: args.filesChanged || [],
          notes: args.notes,
          message: 'Task completion reported. Awaiting validation.',
          nextStep: 'request_validation',
        }
      },
    },

    /**
     * Request validation for a completed task
     */
    request_validation: {
      description: 'Request validation of a completed task. Validator will check against acceptance criteria.',
      parameters: z.object({
        taskId: z.string().describe('ID of the task to validate'),
        completionSummary: z.string().describe('Summary of what was done'),
        filesChanged: z.array(z.string()).optional().describe('Files that were changed'),
      }),

      execute: async (args: {
        taskId: string
        completionSummary: string
        filesChanged?: string[]
      }) => {
        const task = state.getTask(args.taskId)

        if (!task) {
          return {
            success: false,
            message: `Task ${args.taskId} not found`,
          }
        }

        // Build validation request
        const validationRequest = {
          taskId: task.id,
          description: task.description,
          acceptanceCriteria: task.acceptanceCriteria,
          completionSummary: args.completionSummary,
          filesChanged: args.filesChanged || task.filesChanged || [],
          attempts: task.attempts,
          maxAttempts: 2, // From config ideally
        }

        return {
          success: true,
          validationRequested: true,
          taskId: args.taskId,
          request: validationRequest,
          message: 'Validation requested. Validator will review.',
        }
      },
    },

    /**
     * Retry a fixable task
     */
    retry_task: {
      description: 'Retry a task that received FIXABLE validation. Provides feedback to operator.',
      parameters: z.object({
        taskId: z.string().describe('ID of the task to retry'),
        feedback: z.string().describe('Feedback from validator on what to fix'),
        suggestions: z.array(z.string()).optional().describe('Specific suggestions'),
      }),

      execute: async (args: {
        taskId: string
        feedback: string
        suggestions?: string[]
      }) => {
        const task = state.getTask(args.taskId)

        if (!task) {
          return {
            success: false,
            message: `Task ${args.taskId} not found`,
          }
        }

        // Check retry limit
        const maxAttempts = 3 // From config ideally
        if (task.attempts >= maxAttempts) {
          return {
            success: false,
            message: `Task ${args.taskId} has exceeded retry limit (${maxAttempts})`,
            shouldFail: true,
          }
        }

        // Re-dispatch with feedback
        const retryPayload = {
          taskId: task.id,
          description: task.description,
          acceptanceCriteria: task.acceptanceCriteria,
          previousAttempt: {
            attempt: task.attempts,
            feedback: args.feedback,
            suggestions: args.suggestions,
            validation: task.validation,
          },
        }

        return {
          success: true,
          retrying: true,
          taskId: args.taskId,
          attempt: task.attempts + 1,
          payload: retryPayload,
          message: `Retrying task ${args.taskId} (attempt ${task.attempts + 1}/${maxAttempts})`,
        }
      },
    },
  }
}

// =============================================================================
// Type Export
// =============================================================================

export type DispatchTools = ReturnType<typeof createDispatchTools>
