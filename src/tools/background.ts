/**
 * Delta9 Background Task Tools
 *
 * Tools for managing background task execution:
 * - background_output: Get output from background task
 * - background_cancel: Cancel a running task
 * - background_list: List active tasks with status and timing
 * - background_cleanup: Remove old completed/failed tasks
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import type { MissionState } from '../mission/state.js'
import { getBackgroundManager, type BackgroundTaskStatus, type OpenCodeClient } from '../lib/background-manager.js'
import { errors } from '../lib/errors.js'
import { getBackgroundListHint } from '../lib/hints.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Constants
// =============================================================================

const STATUS_EMOJI: Record<BackgroundTaskStatus, string> = {
  pending: '\u23F3',    // hourglass
  running: '\uD83D\uDD04', // arrows counterclockwise
  completed: '\u2705',  // check mark
  failed: '\u274C',     // cross mark
  cancelled: '\uD83D\uDEAB', // prohibited
}

// =============================================================================
// Helpers
// =============================================================================

/**
 * Format duration in human-readable format
 */
function formatDuration(ms: number): string {
  if (ms < 1000) return `${ms}ms`
  if (ms < 60000) return `${(ms / 1000).toFixed(1)}s`
  if (ms < 3600000) return `${(ms / 60000).toFixed(1)}m`
  return `${(ms / 3600000).toFixed(1)}h`
}

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create background task tools
 *
 * @param state - MissionState instance
 * @param cwd - Project root directory
 * @param client - Optional OpenCode SDK client for real agent execution
 */
export function createBackgroundTools(
  state: MissionState,
  cwd: string,
  client?: OpenCodeClient
): Record<string, ToolDefinition> {
  const manager = getBackgroundManager(state, cwd, client)

  /**
   * Get output from a background task
   */
  const background_output = tool({
    description: `Get the output from a background task.

**Purpose:** Retrieve results from background agent tasks spawned with delegate_task.

**Behavior:**
- If task is running and wait=true (default), blocks until completion
- If task is running and wait=false, returns current status immediately
- If task is complete, returns the full output

**Examples:**
- Wait for result: background_output(taskId="bg_abc123")
- Check without waiting: background_output(taskId="bg_abc123", wait=false)

**Related:** delegate_task, background_list, background_cancel`,

    args: {
      taskId: s.string().describe('Background task ID (format: bg_xxxxx)'),
      wait: s
        .boolean()
        .optional()
        .describe('Wait for completion if still running (default: true)'),
    },

    async execute(args, _ctx) {
      const task = manager.getTask(args.taskId)

      if (!task) {
        return errors.taskNotFound(args.taskId).toToolResponse()
      }

      // If task is still running and wait is true (default), poll for completion
      if (task.status === 'running' && args.wait !== false) {
        try {
          const output = await manager.getOutput(args.taskId)
          return JSON.stringify({
            success: true,
            taskId: args.taskId,
            status: 'completed',
            output: output ? JSON.parse(output) : null,
          })
        } catch (error) {
          return JSON.stringify({
            success: false,
            taskId: args.taskId,
            status: 'failed',
            error: error instanceof Error ? error.message : String(error),
          })
        }
      }

      // Return current status
      return JSON.stringify({
        success: task.status === 'completed',
        taskId: args.taskId,
        status: task.status,
        agent: task.agent,
        queuedAt: task.queuedAt,
        startedAt: task.startedAt,
        completedAt: task.completedAt,
        output: task.output ? JSON.parse(task.output) : null,
        error: task.error,
      })
    },
  })

  /**
   * Cancel a running background task
   */
  const background_cancel = tool({
    description: `Cancel a pending or running background task.

**Purpose:** Stop a background task that is no longer needed or appears stuck.

**Behavior:**
- Pending tasks are removed from the queue immediately
- Running tasks are aborted (may take a moment to stop)
- Completed/failed/cancelled tasks cannot be cancelled

**Examples:**
- Cancel task: background_cancel(taskId="bg_abc123")

**Related:** background_list, background_output, delegate_task`,

    args: {
      taskId: s.string().describe('Background task ID (format: bg_xxxxx)'),
    },

    async execute(args, _ctx) {
      const cancelled = manager.cancel(args.taskId)

      if (cancelled) {
        return JSON.stringify({
          success: true,
          taskId: args.taskId,
          status: `${STATUS_EMOJI.cancelled} cancelled`,
          message: `Task ${args.taskId} cancelled successfully`,
        })
      }

      const task = manager.getTask(args.taskId)
      if (!task) {
        return errors.taskNotFound(args.taskId).toToolResponse()
      }

      return errors.taskCancelFailed(args.taskId, task.status).toToolResponse()
    },
  })

  /**
   * List background tasks
   */
  const background_list = tool({
    description: `List background tasks with status and timing information.

**Purpose:** Monitor background agent tasks and check their progress.

**Output includes:**
- Task ID, agent type, and status with emoji indicators
- Duration (how long task has been running/took to complete)
- Summary counts by status
- Pool utilization (active slots / max concurrency)

**Status meanings:**
- \u23F3 pending: Waiting in queue for available slot
- \uD83D\uDD04 running: Currently executing
- \u2705 completed: Finished successfully
- \u274C failed: Encountered an error
- \uD83D\uDEAB cancelled: Stopped by user

**Examples:**
- List all: background_list()
- Running only: background_list(status="running")
- Recent 5: background_list(limit=5)

**Related:** background_output, background_cancel, delegate_task`,

    args: {
      status: s
        .enum(['pending', 'running', 'completed', 'failed', 'cancelled', 'all'])
        .optional()
        .describe('Filter by status (default: all)'),
      limit: s.number().optional().describe('Maximum number of tasks to return (default: 20)'),
    },

    async execute(args, _ctx) {
      const statusFilter =
        args.status && args.status !== 'all'
          ? ({ status: args.status as BackgroundTaskStatus })
          : undefined

      let tasks = manager.listTasks(statusFilter)

      // Apply limit
      const limit = args.limit ?? 20
      tasks = tasks.slice(0, limit)

      // Get summary counts
      const allTasks = manager.listTasks()
      const counts = {
        pending: allTasks.filter((t) => t.status === 'pending').length,
        running: allTasks.filter((t) => t.status === 'running').length,
        completed: allTasks.filter((t) => t.status === 'completed').length,
        failed: allTasks.filter((t) => t.status === 'failed').length,
        cancelled: allTasks.filter((t) => t.status === 'cancelled').length,
        total: allTasks.length,
      }

      // Build summary line with emoji
      const summary = [
        counts.running > 0 ? `${STATUS_EMOJI.running} ${counts.running} running` : null,
        counts.pending > 0 ? `${STATUS_EMOJI.pending} ${counts.pending} pending` : null,
        counts.completed > 0 ? `${STATUS_EMOJI.completed} ${counts.completed} done` : null,
        counts.failed > 0 ? `${STATUS_EMOJI.failed} ${counts.failed} failed` : null,
      ]
        .filter(Boolean)
        .join(' | ')

      // Get contextual hint
      const hint = getBackgroundListHint(
        counts.running,
        counts.completed,
        counts.failed,
        counts.total
      )

      return JSON.stringify({
        success: true,
        summary: summary || 'No tasks',
        tasks: tasks.map((t) => {
          const now = Date.now()
          const startTime = t.startedAt ? new Date(t.startedAt).getTime() : new Date(t.queuedAt).getTime()
          const endTime = t.completedAt
            ? new Date(t.completedAt).getTime()
            : t.status === 'running'
              ? now
              : undefined
          const duration = endTime ? formatDuration(endTime - startTime) : '-'

          return {
            id: t.id,
            status: `${STATUS_EMOJI[t.status]} ${t.status}`,
            agent: t.agent,
            duration,
            missionTaskId: t.missionTaskId || undefined,
            hasError: t.error ? true : undefined,
          }
        }),
        counts,
        pool: {
          active: manager.getActiveCount(),
          pending: manager.getPendingCount(),
          maxConcurrency: 3,
          utilization: `${Math.round((manager.getActiveCount() / 3) * 100)}%`,
        },
        hint,
      })
    },
  })

  /**
   * Clean up old completed tasks
   */
  const background_cleanup = tool({
    description: `Clean up old completed/failed/cancelled tasks to free memory.

**Purpose:** Remove old task records that are no longer needed.

**Behavior:**
- Only removes tasks older than the specified age
- Only removes completed, failed, or cancelled tasks
- Running and pending tasks are never cleaned up
- Default age is 60 minutes

**Examples:**
- Clean tasks older than 1 hour: background_cleanup()
- Clean tasks older than 30 minutes: background_cleanup(maxAgeMinutes=30)

**Note:** Tasks are also automatically cleaned up after 30 minutes via TTL.

**Related:** background_list`,

    args: {
      maxAgeMinutes: s
        .number()
        .optional()
        .describe('Maximum age in minutes for tasks to keep (default: 60)'),
    },

    async execute(args, _ctx) {
      const maxAge = (args.maxAgeMinutes ?? 60) * 60 * 1000 // Convert to ms
      const cleaned = manager.cleanup(maxAge)

      return JSON.stringify({
        success: true,
        cleaned,
        message: cleaned > 0
          ? `${STATUS_EMOJI.completed} Cleaned up ${cleaned} old task(s)`
          : 'No tasks to clean up',
      })
    },
  })

  return {
    background_output,
    background_cancel,
    background_list,
    background_cleanup,
  }
}

// =============================================================================
// Type Export
// =============================================================================

export type BackgroundTools = ReturnType<typeof createBackgroundTools>
