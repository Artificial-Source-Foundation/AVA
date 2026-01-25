/**
 * Delta9 Dispatch Tools
 *
 * Tools for task dispatch and completion.
 * Used by Commander to dispatch tasks and by Operators to report completion.
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import type { MissionState } from '../mission/state.js'
import { routeTask, suggestSupportAgents, type AgentType } from '../agents/router.js'
import { appendHistory } from '../mission/history.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create dispatch tools
 */
export function createDispatchTools(
  state: MissionState,
  cwd?: string
): Record<string, ToolDefinition> {
  const projectCwd = cwd ?? process.cwd()

  /**
   * Dispatch a task to an operator
   */
  const dispatch_task = tool({
    description:
      'Dispatch a task to an agent for execution. Auto-routes to appropriate support agent if not specified.',
    args: {
      taskId: s.string().describe('ID of the task to dispatch'),
      agent: s
        .string()
        .optional()
        .describe('Specific agent to use (auto-routed if not specified)'),
      context: s.string().optional().describe('Additional context for the agent'),
      autoRoute: s.boolean().optional().describe('Use auto-routing (default: true)'),
    },

    async execute(args, _ctx) {
      const task = state.getTask(args.taskId)

      if (!task) {
        return JSON.stringify({
          success: false,
          message: `Task ${args.taskId} not found`,
        })
      }

      if (task.status !== 'pending') {
        return JSON.stringify({
          success: false,
          message: `Task ${args.taskId} is ${task.status}, cannot dispatch`,
        })
      }

      const mission = state.getMission()
      const currentObj = state.getCurrentObjective()

      // Determine agent to use
      let selectedAgent: AgentType = 'operator'
      let routingDecision = null

      if (args.agent) {
        // Explicit agent specified
        selectedAgent = args.agent as AgentType
      } else if (args.autoRoute !== false) {
        // Auto-route based on task description
        routingDecision = routeTask(task, projectCwd)
        selectedAgent = routingDecision.agent
      }

      // Mark task as started
      state.startTask(args.taskId, selectedAgent)

      // Log dispatch to history
      if (mission) {
        appendHistory(projectCwd, {
          type: 'task_started',
          timestamp: new Date().toISOString(),
          missionId: mission.id,
          taskId: args.taskId,
          data: {
            agent: selectedAgent,
            autoRouted: !!routingDecision,
            routingConfidence: routingDecision?.confidence,
            routingReason: routingDecision?.reason,
          },
        })
      }

      // Get suggested support agents
      const suggestedSupport = suggestSupportAgents(task)

      // Build dispatch payload
      const dispatchPayload = {
        taskId: task.id,
        description: task.description,
        acceptanceCriteria: task.acceptanceCriteria,
        missionContext: mission?.description || '',
        objectiveContext: currentObj?.description || '',
        additionalContext: args.context || '',
        previousAttempts: task.attempts > 1 ? task.attempts - 1 : 0,
        routing: {
          agent: selectedAgent,
          autoRouted: !!routingDecision,
          confidence: routingDecision?.confidence,
          reason: routingDecision?.reason,
        },
      }

      return JSON.stringify({
        success: true,
        dispatched: true,
        taskId: args.taskId,
        agent: selectedAgent,
        routing: routingDecision
          ? {
              autoRouted: true,
              confidence: routingDecision.confidence,
              reason: routingDecision.reason,
            }
          : { autoRouted: false, explicit: true },
        suggestedSupport: suggestedSupport.length > 0 ? suggestedSupport : undefined,
        payload: dispatchPayload,
        message: `Task ${args.taskId} dispatched to ${selectedAgent}`,
      })
    },
  })

  /**
   * Report task completion (used by Operators)
   */
  const task_complete = tool({
    description: 'Report that a task has been completed. Include a summary of changes made.',
    args: {
      taskId: s.string().describe('ID of the completed task'),
      summary: s.string().describe('Summary of what was done'),
      filesChanged: s.string().optional().describe('JSON array of files that were changed'),
      notes: s.string().optional().describe('Any notes for Commander or Validator'),
    },

    async execute(args, _ctx) {
      const task = state.getTask(args.taskId)

      if (!task) {
        return JSON.stringify({
          success: false,
          message: `Task ${args.taskId} not found`,
        })
      }

      if (task.status !== 'in_progress') {
        return JSON.stringify({
          success: false,
          message: `Task ${args.taskId} is ${task.status}, not in progress`,
        })
      }

      let files: string[] | undefined
      if (args.filesChanged) {
        try {
          files = JSON.parse(args.filesChanged)
        } catch {
          files = [args.filesChanged]
        }
      }

      // Update task with completion info (but don't mark complete yet - wait for validation)
      state.updateTask(args.taskId, {
        filesChanged: files,
      })

      return JSON.stringify({
        success: true,
        taskId: args.taskId,
        summary: args.summary,
        filesChanged: files || [],
        notes: args.notes,
        message: 'Task completion reported. Awaiting validation.',
        nextStep: 'request_validation',
      })
    },
  })

  /**
   * Request validation for a completed task
   */
  const request_validation = tool({
    description: 'Request validation of a completed task. Validator will check against acceptance criteria.',
    args: {
      taskId: s.string().describe('ID of the task to validate'),
      completionSummary: s.string().describe('Summary of what was done'),
      filesChanged: s.string().optional().describe('JSON array of files that were changed'),
    },

    async execute(args, _ctx) {
      const task = state.getTask(args.taskId)

      if (!task) {
        return JSON.stringify({
          success: false,
          message: `Task ${args.taskId} not found`,
        })
      }

      let files: string[] = []
      if (args.filesChanged) {
        try {
          files = JSON.parse(args.filesChanged)
        } catch {
          files = [args.filesChanged]
        }
      }

      // Build validation request
      const validationRequest = {
        taskId: task.id,
        description: task.description,
        acceptanceCriteria: task.acceptanceCriteria,
        completionSummary: args.completionSummary,
        filesChanged: files.length > 0 ? files : task.filesChanged || [],
        attempts: task.attempts,
        maxAttempts: 2,
      }

      return JSON.stringify({
        success: true,
        validationRequested: true,
        taskId: args.taskId,
        request: validationRequest,
        message: 'Validation requested. Validator will review.',
      })
    },
  })

  /**
   * Retry a fixable task
   */
  const retry_task = tool({
    description: 'Retry a task that received FIXABLE validation. Provides feedback to operator.',
    args: {
      taskId: s.string().describe('ID of the task to retry'),
      feedback: s.string().describe('Feedback from validator on what to fix'),
      suggestions: s.string().optional().describe('JSON array of specific suggestions'),
    },

    async execute(args, _ctx) {
      const task = state.getTask(args.taskId)

      if (!task) {
        return JSON.stringify({
          success: false,
          message: `Task ${args.taskId} not found`,
        })
      }

      // Check retry limit
      const maxAttempts = 3
      if (task.attempts >= maxAttempts) {
        return JSON.stringify({
          success: false,
          message: `Task ${args.taskId} has exceeded retry limit (${maxAttempts})`,
          shouldFail: true,
        })
      }

      let suggs: string[] | undefined
      if (args.suggestions) {
        try {
          suggs = JSON.parse(args.suggestions)
        } catch {
          suggs = [args.suggestions]
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
          suggestions: suggs,
          validation: task.validation,
        },
      }

      return JSON.stringify({
        success: true,
        retrying: true,
        taskId: args.taskId,
        attempt: task.attempts + 1,
        payload: retryPayload,
        message: `Retrying task ${args.taskId} (attempt ${task.attempts + 1}/${maxAttempts})`,
      })
    },
  })

  return {
    dispatch_task,
    task_complete,
    request_validation,
    retry_task,
  }
}

// =============================================================================
// Type Export
// =============================================================================

export type DispatchTools = ReturnType<typeof createDispatchTools>
