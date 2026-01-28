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
import { checkTaskConflicts, formatConflicts } from '../mission/conflict-detector.js'
import { getBackgroundManager, type OpenCodeClient } from '../lib/background-manager.js'
import { trackBackgroundTask } from '../hooks/session.js'
import { buildOperatorHandoff, formatHandoffForPrompt } from '../dispatch/handoff.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create dispatch tools
 *
 * @param state - MissionState instance
 * @param cwd - Project root directory
 * @param client - Optional OpenCode SDK client for agent spawning (BUG-34 fix)
 */
export function createDispatchTools(
  state: MissionState,
  cwd?: string,
  client?: OpenCodeClient
): Record<string, ToolDefinition> {
  const projectCwd = cwd ?? process.cwd()
  const manager = getBackgroundManager(state, projectCwd, client)

  /**
   * Dispatch a task to an operator
   *
   * BUG-34 FIX: Now actually spawns background agents instead of just recording intent.
   * This unifies dispatch_task with delegate_task behavior.
   */
  const dispatch_task = tool({
    description: `Dispatch a mission task to an agent for execution.

**Purpose:** Launch a background agent to execute a specific mission task.
Auto-routes to appropriate support agent if not specified. Checks for file conflicts.

**What it does:**
1. Validates task exists and is pending
2. Routes to best agent based on task type
3. Spawns background agent (auto-delegates)
4. Links execution to mission tracking

**Use for:** Mission tasks. For ad-hoc work without mission context, use delegate_task.

**Related:** delegate_task, background_output, background_list, mission_status`,
    args: {
      taskId: s.string().describe('ID of the mission task to dispatch'),
      agent: s.string().optional().describe('Specific agent to use (auto-routed if not specified)'),
      context: s.string().optional().describe('Additional context for the agent'),
      autoRoute: s.boolean().optional().describe('Use auto-routing (default: true)'),
      run_in_background: s
        .boolean()
        .optional()
        .describe('Run in background (default: true). Set false for synchronous execution.'),
      files: s
        .string()
        .optional()
        .describe('JSON array of files this task will modify (exclusive ownership)'),
      filesReadonly: s.string().optional().describe('JSON array of files this task can read only'),
      mustNot: s
        .string()
        .optional()
        .describe('JSON array of explicit constraints - things NOT to do'),
    },

    async execute(args, ctx) {
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

      // Parse file lists if provided
      let files: string[] | undefined
      let filesReadonly: string[] | undefined
      let mustNot: string[] | undefined

      if (args.files) {
        try {
          files = JSON.parse(args.files)
        } catch {
          files = [args.files]
        }
      }

      if (args.filesReadonly) {
        try {
          filesReadonly = JSON.parse(args.filesReadonly)
        } catch {
          filesReadonly = [args.filesReadonly]
        }
      }

      if (args.mustNot) {
        try {
          mustNot = JSON.parse(args.mustNot)
        } catch {
          mustNot = [args.mustNot]
        }
      }

      // Update task with file assignments if provided
      if (files || filesReadonly || mustNot) {
        state.updateTask(args.taskId, {
          files,
          filesReadonly,
          mustNot,
        })
      }

      // Check for file conflicts with other active tasks
      const mission = state.getMission()
      if (mission) {
        const allTasks = mission.objectives.flatMap((o) => o.tasks)
        const taskWithFiles = {
          id: args.taskId,
          files: files ?? task.files,
          filesReadonly: filesReadonly ?? task.filesReadonly,
        }

        const conflictResult = checkTaskConflicts(taskWithFiles, allTasks)
        if (conflictResult.hasConflicts) {
          return JSON.stringify({
            success: false,
            message: 'File conflicts detected',
            conflicts: conflictResult.conflicts,
            formatted: formatConflicts(conflictResult),
          })
        }
      }

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
            taskDescription: task.description, // BUG-37 FIX: Include task description
          },
        })
      }

      // Get suggested support agents
      const suggestedSupport = suggestSupportAgents(task)

      // BUG-34 FIX: Build prompt and actually spawn agent
      // Build structured handoff contract for the agent
      const allTasks = mission?.objectives.flatMap((o) => o.tasks) || []
      const handoff = buildOperatorHandoff({
        task,
        mission: mission!,
        allTasks,
        additionalContext: args.context,
      })
      const handoffPrompt = formatHandoffForPrompt(handoff)
      const fullPrompt = `${handoffPrompt}\n\n---\n\nEXECUTE THIS TASK:\n${task.description}`

      // Check if SDK is available
      const sdkAvailable = !!client

      // Extract session ID for background task tracking
      const extractSessionId = (c: unknown): string | null => {
        if (typeof c !== 'object' || c === null) return null
        const context = c as {
          sessionID?: string
          sessionId?: string
          session?: { id?: string }
          info?: { sessionId?: string }
        }
        return (
          context.sessionID ??
          context.sessionId ??
          context.session?.id ??
          context.info?.sessionId ??
          null
        )
      }
      const parentSessionId = extractSessionId(ctx)

      // Spawn agent (background by default, sync if explicitly requested)
      const runInBackground = args.run_in_background !== false

      if (runInBackground) {
        // Spawn background agent
        const bgTaskId = await manager.launch({
          prompt: fullPrompt,
          agent: selectedAgent,
          missionTaskId: args.taskId,
          parentSessionId: parentSessionId ?? undefined,
          missionContext: mission
            ? {
                id: mission.id,
                description: mission.description,
                status: mission.status,
              }
            : undefined,
        })

        // Track in session state
        if (parentSessionId) {
          trackBackgroundTask(parentSessionId, bgTaskId)
        }

        return JSON.stringify({
          success: true,
          dispatched: true,
          taskId: args.taskId,
          backgroundTaskId: bgTaskId,
          agent: selectedAgent,
          status: '\u23F3 spawned',
          mode: sdkAvailable ? 'live' : 'simulation',
          routing: routingDecision
            ? {
                autoRouted: true,
                confidence: routingDecision.confidence,
                reason: routingDecision.reason,
              }
            : { autoRouted: false, explicit: true },
          suggestedSupport: suggestedSupport.length > 0 ? suggestedSupport : undefined,
          message: `Task ${args.taskId} dispatched to ${selectedAgent}. Use background_output(taskId="${bgTaskId}") to check progress.`,
        })
      } else {
        // Synchronous execution
        try {
          const result = await manager.executeSync({
            prompt: fullPrompt,
            agent: selectedAgent,
            missionTaskId: args.taskId,
          })

          return JSON.stringify({
            success: true,
            dispatched: true,
            taskId: args.taskId,
            agent: selectedAgent,
            status: '\u2705 completed',
            mode: sdkAvailable ? 'live' : 'simulation',
            result: JSON.parse(result),
            routing: routingDecision
              ? {
                  autoRouted: true,
                  confidence: routingDecision.confidence,
                  reason: routingDecision.reason,
                }
              : { autoRouted: false, explicit: true },
            suggestedSupport: suggestedSupport.length > 0 ? suggestedSupport : undefined,
            message: `Task ${args.taskId} completed by ${selectedAgent}`,
          })
        } catch (error) {
          return JSON.stringify({
            success: false,
            taskId: args.taskId,
            agent: selectedAgent,
            error: error instanceof Error ? error.message : String(error),
            message: 'Task execution failed',
          })
        }
      }
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
    description:
      'Request validation of a completed task. Validator will check against acceptance criteria.',
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
