/**
 * Delta9 Epic Tools
 *
 * Tools for managing epics:
 * - create_epic: Create a new epic
 * - link_tasks_to_epic: Link tasks to an epic
 * - epic_status: Get epic status
 * - epic_breakdown: Get detailed breakdown
 * - sync_to_git: Git branch/commit operations
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import type { MissionState } from '../mission/state.js'
import { getEpicManager, type EpicPriority, type EpicStatus } from '../mission/epic.js'
import { GitSync } from '../mission/git-sync.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Tool Factory
// =============================================================================

export interface EpicToolsConfig {
  /** Project root directory */
  cwd?: string
  /** Dry run mode for git operations */
  gitDryRun?: boolean
  /** Logger function */
  log?: (level: string, message: string, data?: Record<string, unknown>) => void
}

/**
 * Create epic tools with bound context
 */
export function createEpicTools(
  state: MissionState,
  config: EpicToolsConfig = {}
): Record<string, ToolDefinition> {
  const { cwd = process.cwd(), gitDryRun = false, log } = config
  const epicManager = getEpicManager({ baseDir: `${cwd}/.delta9` })
  const gitSync = new GitSync({ cwd, dryRun: gitDryRun })

  /**
   * Create a new epic
   */
  const create_epic = tool({
    description: `Create a new epic for grouping related work across objectives.

Epics are high-level work items that can span multiple objectives and contain many tasks.
Use epics for:
- Large features requiring multiple objectives
- Cross-cutting concerns (auth, infrastructure)
- Multi-sprint initiatives

Priority levels:
- low: Nice to have
- normal: Standard priority (default)
- high: Important, should be prioritized
- critical: Blocker, needs immediate attention`,
    args: {
      title: s.string().describe('Epic title (concise, descriptive)'),
      description: s.string().describe('Detailed description of the epic'),
      priority: s.string().optional().describe('Priority: low, normal (default), high, critical'),
      acceptanceCriteria: s
        .string()
        .optional()
        .describe('JSON array of acceptance criteria strings'),
      labels: s.string().optional().describe('JSON array of label strings'),
    },

    async execute(args, _ctx) {
      log?.('info', 'Creating epic', { title: args.title })

      // Parse acceptance criteria
      let acceptanceCriteria: string[] = []
      if (args.acceptanceCriteria) {
        try {
          acceptanceCriteria = JSON.parse(args.acceptanceCriteria)
        } catch {
          return JSON.stringify({
            success: false,
            error: 'Failed to parse acceptanceCriteria JSON',
          })
        }
      }

      // Parse labels
      let labels: string[] | undefined
      if (args.labels) {
        try {
          labels = JSON.parse(args.labels)
        } catch {
          return JSON.stringify({
            success: false,
            error: 'Failed to parse labels JSON',
          })
        }
      }

      // Get current mission ID if available
      const mission = state.getMission()

      const epic = epicManager.create({
        title: args.title,
        description: args.description,
        priority: (args.priority as EpicPriority) || 'normal',
        acceptanceCriteria,
        labels,
        missionId: mission?.id,
      })

      return JSON.stringify({
        success: true,
        epic: {
          id: epic.id,
          title: epic.title,
          status: epic.status,
          priority: epic.priority,
          suggestedBranch: epicManager.suggestBranchName(epic),
        },
      })
    },
  })

  /**
   * Link tasks to an epic
   */
  const link_tasks_to_epic = tool({
    description: `Link tasks to an epic.

Tasks can be linked from any objective. The epic will track progress across all linked tasks.
You can also link objectives to group all their tasks under an epic.`,
    args: {
      epicId: s.string().describe('Epic ID to link to'),
      taskIds: s.string().optional().describe('JSON array of task IDs to link'),
      objectiveIds: s.string().optional().describe('JSON array of objective IDs to link'),
    },

    async execute(args, _ctx) {
      log?.('info', 'Linking to epic', { epicId: args.epicId })

      const epic = epicManager.get(args.epicId)
      if (!epic) {
        return JSON.stringify({
          success: false,
          error: `Epic ${args.epicId} not found`,
        })
      }

      let linkedTasks = 0
      let linkedObjectives = 0

      // Link tasks
      if (args.taskIds) {
        try {
          const taskIds = JSON.parse(args.taskIds)
          epicManager.linkTasks(args.epicId, taskIds)
          linkedTasks = taskIds.length
        } catch {
          return JSON.stringify({
            success: false,
            error: 'Failed to parse taskIds JSON',
          })
        }
      }

      // Link objectives
      if (args.objectiveIds) {
        try {
          const objectiveIds: string[] = JSON.parse(args.objectiveIds)
          epicManager.linkObjectives(args.epicId, objectiveIds)
          linkedObjectives = objectiveIds.length

          // Also link all tasks from those objectives
          const mission = state.getMission()
          if (mission) {
            const taskIds: string[] = []
            for (const objId of objectiveIds) {
              const objective = mission.objectives.find((o) => o.id === objId)
              if (objective) {
                for (const task of objective.tasks) {
                  taskIds.push(task.id)
                  epicManager.updateTaskStatus(task.id, task.status, objId)
                }
              }
            }
            if (taskIds.length > 0) {
              epicManager.linkTasks(args.epicId, taskIds)
              linkedTasks += taskIds.length
            }
          }
        } catch {
          return JSON.stringify({
            success: false,
            error: 'Failed to parse objectiveIds JSON',
          })
        }
      }

      const updated = epicManager.get(args.epicId)

      return JSON.stringify({
        success: true,
        epicId: args.epicId,
        linkedTasks,
        linkedObjectives,
        totalTasks: updated?.tasks.length ?? 0,
        totalObjectives: updated?.objectives.length ?? 0,
      })
    },
  })

  /**
   * Get epic status
   */
  const epic_status = tool({
    description: `Get the current status and progress of an epic.

Returns:
- Overall progress percentage
- Task breakdown (completed, in-progress, blocked, pending, failed)
- Epic status (planning, in_progress, completed, blocked)`,
    args: {
      epicId: s.string().describe('Epic ID to check'),
    },

    async execute(args, _ctx) {
      log?.('debug', 'Getting epic status', { epicId: args.epicId })

      const epic = epicManager.get(args.epicId)
      if (!epic) {
        return JSON.stringify({
          success: false,
          error: `Epic ${args.epicId} not found`,
        })
      }

      const progress = epicManager.getStatus(args.epicId)

      return JSON.stringify({
        success: true,
        epic: {
          id: epic.id,
          title: epic.title,
          status: epic.status,
          priority: epic.priority,
          gitBranch: epic.gitBranch,
        },
        progress: progress ?? {
          totalTasks: 0,
          completedTasks: 0,
          failedTasks: 0,
          inProgressTasks: 0,
          blockedTasks: 0,
          pendingTasks: 0,
          percentage: 0,
        },
        acceptanceCriteria: epic.acceptanceCriteria,
      })
    },
  })

  /**
   * Get epic breakdown by objective
   */
  const epic_breakdown = tool({
    description: `Get detailed breakdown of an epic by objective.

Shows task counts and completion status for each objective linked to the epic.`,
    args: {
      epicId: s.string().describe('Epic ID to get breakdown for'),
    },

    async execute(args, _ctx) {
      log?.('debug', 'Getting epic breakdown', { epicId: args.epicId })

      const breakdown = epicManager.getBreakdown(args.epicId)
      if (!breakdown) {
        return JSON.stringify({
          success: false,
          error: `Epic ${args.epicId} not found`,
        })
      }

      return JSON.stringify({
        success: true,
        epic: {
          id: breakdown.epic.id,
          title: breakdown.epic.title,
          status: breakdown.epic.status,
        },
        progress: breakdown.progress,
        byObjective: breakdown.byObjective,
      })
    },
  })

  /**
   * Sync to git
   */
  const sync_to_git = tool({
    description: `Perform git operations for an epic or task.

Operations:
- Create a branch for an epic
- Commit changes for a completed task
- Create a checkpoint tag for a completed objective

Use this after completing tasks to keep git in sync with mission progress.`,
    args: {
      epicId: s.string().optional().describe('Epic ID to create branch for'),
      taskId: s.string().optional().describe('Task ID to commit changes for'),
      objectiveId: s.string().optional().describe('Objective ID to create checkpoint for'),
      operation: s.string().describe('Operation: create_branch, commit_task, checkpoint'),
    },

    async execute(args, _ctx) {
      log?.('info', 'Git sync', {
        operation: args.operation,
        epicId: args.epicId,
        taskId: args.taskId,
      })

      // Check if git repo
      const isRepo = await gitSync.isGitRepo()
      if (!isRepo) {
        return JSON.stringify({
          success: false,
          error: 'Not a git repository',
        })
      }

      switch (args.operation) {
        case 'create_branch': {
          if (!args.epicId) {
            return JSON.stringify({
              success: false,
              error: 'epicId required for create_branch operation',
            })
          }

          const epic = epicManager.get(args.epicId)
          if (!epic) {
            return JSON.stringify({
              success: false,
              error: `Epic ${args.epicId} not found`,
            })
          }

          const result = await gitSync.createEpicBranch(epic)
          if (result.success && result.branch) {
            // Update epic with branch name
            epicManager.setGitBranch(args.epicId, result.branch.name)

            return JSON.stringify({
              success: true,
              operation: 'create_branch',
              branch: result.branch,
            })
          }

          return JSON.stringify({
            success: false,
            error: result.error || 'Failed to create branch',
          })
        }

        case 'commit_task': {
          if (!args.taskId) {
            return JSON.stringify({
              success: false,
              error: 'taskId required for commit_task operation',
            })
          }

          const mission = state.getMission()
          let taskDescription = args.taskId

          if (mission) {
            for (const obj of mission.objectives) {
              const task = obj.tasks.find((t) => t.id === args.taskId)
              if (task) {
                taskDescription = task.description
                break
              }
            }
          }

          const result = await gitSync.commitTask(args.taskId, taskDescription)

          return JSON.stringify({
            success: result.success,
            operation: 'commit_task',
            taskId: args.taskId,
            message: result.stdout || result.error,
          })
        }

        case 'checkpoint': {
          if (!args.objectiveId) {
            return JSON.stringify({
              success: false,
              error: 'objectiveId required for checkpoint operation',
            })
          }

          const mission = state.getMission()
          let objectiveDescription = args.objectiveId

          if (mission) {
            const objective = mission.objectives.find((o) => o.id === args.objectiveId)
            if (objective) {
              objectiveDescription = objective.description
            }
          }

          const result = await gitSync.checkpointObjective(
            args.objectiveId,
            objectiveDescription,
            args.epicId
          )

          return JSON.stringify({
            success: result.success,
            operation: 'checkpoint',
            tag: result.tag,
            error: result.error,
          })
        }

        default:
          return JSON.stringify({
            success: false,
            error: `Unknown operation: ${args.operation}. Use: create_branch, commit_task, checkpoint`,
          })
      }
    },
  })

  /**
   * List epics
   */
  const list_epics = tool({
    description: `List all epics, optionally filtered by status or priority.`,
    args: {
      status: s
        .string()
        .optional()
        .describe('Filter by status: planning, in_progress, completed, blocked, archived'),
      priority: s.string().optional().describe('Filter by priority: low, normal, high, critical'),
    },

    async execute(args, _ctx) {
      log?.('debug', 'Listing epics', { status: args.status, priority: args.priority })

      const epics = epicManager.list({
        status: args.status as EpicStatus | undefined,
        priority: args.priority as EpicPriority | undefined,
      })

      return JSON.stringify({
        success: true,
        count: epics.length,
        epics: epics.map((e) => ({
          id: e.id,
          title: e.title,
          status: e.status,
          priority: e.priority,
          taskCount: e.tasks.length,
          objectiveCount: e.objectives.length,
          gitBranch: e.gitBranch,
        })),
      })
    },
  })

  /**
   * Update epic status
   */
  const update_epic = tool({
    description: `Update an epic's status or priority.`,
    args: {
      epicId: s.string().describe('Epic ID to update'),
      status: s
        .string()
        .optional()
        .describe('New status: planning, in_progress, completed, blocked, archived'),
      priority: s.string().optional().describe('New priority: low, normal, high, critical'),
      title: s.string().optional().describe('New title'),
      description: s.string().optional().describe('New description'),
    },

    async execute(args, _ctx) {
      log?.('info', 'Updating epic', { epicId: args.epicId })

      const epic = epicManager.get(args.epicId)
      if (!epic) {
        return JSON.stringify({
          success: false,
          error: `Epic ${args.epicId} not found`,
        })
      }

      const updates: Record<string, unknown> = {}
      if (args.status) updates.status = args.status
      if (args.priority) updates.priority = args.priority
      if (args.title) updates.title = args.title
      if (args.description) updates.description = args.description

      if (args.status === 'completed') {
        updates.completedAt = new Date().toISOString()
      }

      const updated = epicManager.update(args.epicId, updates)

      return JSON.stringify({
        success: true,
        epic: updated
          ? {
              id: updated.id,
              title: updated.title,
              status: updated.status,
              priority: updated.priority,
            }
          : null,
      })
    },
  })

  return {
    create_epic,
    link_tasks_to_epic,
    epic_status,
    epic_breakdown,
    sync_to_git,
    list_epics,
    update_epic,
  }
}
