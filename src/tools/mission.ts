/**
 * Delta9 Mission Tools
 *
 * Tools for managing mission state.
 * These tools are used by Commander to create and update missions.
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import type { MissionState } from '../mission/state.js'
import type { CouncilMode, Complexity } from '../types/index.js'
import { appendHistory } from '../mission/history.js'
import { getNamedLogger } from '../lib/logger.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema
const log = getNamedLogger('mission-tools')

// =============================================================================
// Dependency Resolution Helper (BUG-25 Fix)
// =============================================================================

/**
 * Resolve symbolic dependency names ("task_1", "task_2") to actual task IDs.
 *
 * Commander often passes dependencies as symbolic names like "task_1", "task_2"
 * referring to tasks by their order within an objective. This function resolves
 * these symbolic names to actual task IDs like "task_XUUNsk".
 *
 * Resolution rules:
 * 1. If dep is already a valid task ID (exists in state), use it directly
 * 2. If dep matches "task_N" pattern, resolve to Nth task in the objective
 * 3. If unresolvable, log warning and skip
 */
function resolveDependencyIds(
  state: MissionState,
  objectiveId: string,
  rawDeps: string[]
): string[] {
  const objective = state.getObjective(objectiveId)
  if (!objective) return rawDeps

  const resolvedDeps: string[] = []

  for (const dep of rawDeps) {
    // If it's already a valid task ID that exists in state, use it
    if (state.getTask(dep)) {
      resolvedDeps.push(dep)
      continue
    }

    // Try to resolve symbolic names like "task_1", "task_2"
    const match = dep.match(/^task_(\d+)$/)
    if (match) {
      const index = parseInt(match[1], 10) - 1 // "task_1" → index 0
      if (index >= 0 && index < objective.tasks.length) {
        const resolvedId = objective.tasks[index].id
        log.debug(`[mission] Resolved dependency "${dep}" → "${resolvedId}"`)
        resolvedDeps.push(resolvedId)
        continue
      }
    }

    // Warn about unresolvable dependency
    log.warn(`[mission] Unresolvable dependency: "${dep}" - skipping`)
  }

  return resolvedDeps
}

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create mission management tools
 */
export function createMissionTools(
  state: MissionState,
  cwd?: string
): Record<string, ToolDefinition> {
  const projectCwd = cwd ?? process.cwd()
  /**
   * Create a new mission
   */
  const mission_create = tool({
    description:
      'Create a new mission with objectives and tasks. Use this to start planning a new request.',
    args: {
      description: s.string().describe('Mission description - what needs to be accomplished'),
      complexity: s
        .enum(['low', 'medium', 'high', 'critical'])
        .optional()
        .describe('Detected complexity level'),
      councilMode: s
        .enum(['none', 'quick', 'standard', 'xhigh'])
        .optional()
        .describe('Council mode to use'),
      objectives: s.string().optional().describe('JSON array of objectives with tasks'),
    },

    async execute(args, _ctx) {
      const mission = state.create(args.description, {
        complexity: args.complexity as Complexity | undefined,
        councilMode: args.councilMode as CouncilMode | undefined,
      })

      // Track created objectives and tasks for return
      const createdObjectives: Array<{
        id: string
        description: string
        tasks: Array<{ id: string; description: string }>
      }> = []

      // Add objectives and tasks if provided
      if (args.objectives) {
        try {
          const objectives = JSON.parse(args.objectives) as Array<{
            description: string
            tasks: Array<{
              description: string
              acceptanceCriteria: string[]
              routing?: string
              dependencies?: string[]
            }>
          }>

          for (const objData of objectives) {
            const objective = state.addObjective({
              description: objData.description,
            })

            const createdTasks: Array<{ id: string; description: string }> = []
            for (const taskData of objData.tasks) {
              // BUG-25 FIX: Resolve symbolic dependency names to actual task IDs
              const resolvedDeps = taskData.dependencies
                ? resolveDependencyIds(state, objective.id, taskData.dependencies)
                : undefined

              const task = state.addTask(objective.id, {
                description: taskData.description,
                acceptanceCriteria: taskData.acceptanceCriteria,
                routedTo: taskData.routing,
                dependencies: resolvedDeps,
              })
              createdTasks.push({ id: task.id, description: task.description })
            }

            createdObjectives.push({
              id: objective.id,
              description: objective.description,
              tasks: createdTasks,
            })
          }
        } catch {
          // Ignore JSON parse errors
        }
      }

      return JSON.stringify({
        success: true,
        missionId: mission.id,
        status: mission.status,
        objectives: createdObjectives,
        message: `Mission created: ${mission.id}`,
        hint: 'Use the objective IDs above when adding tasks with mission_add_task',
      })
    },
  })

  /**
   * Get mission status
   */
  const mission_status = tool({
    description: 'Get the current mission status, progress, and next available task.',
    args: {},

    async execute(_args, _ctx) {
      const mission = state.getMission()

      if (!mission) {
        return JSON.stringify({
          success: false,
          hasMission: false,
          message: 'No active mission',
        })
      }

      const progress = state.getProgress()
      const nextTask = state.getNextTask()
      const budget = state.getBudgetStatus()

      return JSON.stringify({
        success: true,
        hasMission: true,
        mission: {
          id: mission.id,
          description: mission.description,
          status: mission.status,
          complexity: mission.complexity,
          councilMode: mission.councilMode,
        },
        progress: {
          ...progress,
          currentObjective: mission.currentObjective,
          totalObjectives: mission.objectives.length,
        },
        budget,
        nextTask: nextTask
          ? {
              id: nextTask.id,
              description: nextTask.description,
              acceptanceCriteria: nextTask.acceptanceCriteria,
              attempts: nextTask.attempts,
            }
          : null,
      })
    },
  })

  /**
   * Update mission state
   */
  const mission_update = tool({
    description: 'Update mission state - approve, pause, resume, or abort the mission.',
    args: {
      action: s.enum(['approve', 'start', 'pause', 'resume', 'abort']).describe('Action to take'),
    },

    async execute(args, _ctx) {
      const mission = state.getMission()

      if (!mission) {
        return JSON.stringify({
          success: false,
          message: 'No active mission',
        })
      }

      switch (args.action) {
        case 'approve':
          state.approveMission()
          break
        case 'start':
          state.startMission()
          break
        case 'pause':
          state.pauseMission()
          break
        case 'resume':
          state.startMission()
          break
        case 'abort':
          state.abortMission()
          break
      }

      return JSON.stringify({
        success: true,
        action: args.action,
        newStatus: state.getMission()?.status,
        message: `Mission ${args.action}ed`,
      })
    },
  })

  /**
   * Add an objective to the mission
   */
  const mission_add_objective = tool({
    description: 'Add a new objective to the current mission.',
    args: {
      description: s.string().describe('Objective description'),
      tasks: s.string().optional().describe('JSON array of tasks for this objective'),
    },

    async execute(args, _ctx) {
      const objective = state.addObjective({
        description: args.description,
      })

      if (args.tasks) {
        try {
          const tasks = JSON.parse(args.tasks) as Array<{
            description: string
            acceptanceCriteria: string[]
            routing?: string
            dependencies?: string[]
          }>

          for (const taskData of tasks) {
            // BUG-25 FIX: Resolve symbolic dependency names to actual task IDs
            const resolvedDeps = taskData.dependencies
              ? resolveDependencyIds(state, objective.id, taskData.dependencies)
              : undefined

            state.addTask(objective.id, {
              description: taskData.description,
              acceptanceCriteria: taskData.acceptanceCriteria,
              routedTo: taskData.routing,
              dependencies: resolvedDeps,
            })
          }
        } catch {
          // Ignore JSON parse errors
        }
      }

      return JSON.stringify({
        success: true,
        objectiveId: objective.id,
        message: `Objective added: ${objective.id}`,
      })
    },
  })

  /**
   * Add a task to an objective
   */
  const mission_add_task = tool({
    description: 'Add a new task to an existing objective.',
    args: {
      objectiveId: s.string().describe('ID of the objective to add task to'),
      description: s.string().describe('Task description'),
      acceptanceCriteria: s.string().describe('JSON array of acceptance criteria strings'),
      routing: s.string().optional().describe('Agent routing'),
      dependencies: s.string().optional().describe('JSON array of task dependency IDs'),
    },

    async execute(args, _ctx) {
      let criteria: string[] = []
      let deps: string[] | undefined

      try {
        criteria = JSON.parse(args.acceptanceCriteria)
      } catch {
        criteria = [args.acceptanceCriteria]
      }

      // Check if objective exists, provide helpful error if not
      const mission = state.getMission()
      const objective = state.getObjective(args.objectiveId)
      if (!objective) {
        const availableObjectives =
          mission?.objectives.map((o) => ({ id: o.id, description: o.description })) ?? []
        return JSON.stringify({
          success: false,
          error: `Objective ${args.objectiveId} not found`,
          availableObjectives,
          hint: 'Use one of the objective IDs listed above, or create a new objective with mission_add_objective',
        })
      }

      // BUG-25 FIX: Parse and resolve dependencies to actual task IDs
      if (args.dependencies) {
        try {
          const rawDeps = JSON.parse(args.dependencies) as string[]
          deps = resolveDependencyIds(state, args.objectiveId, rawDeps)
        } catch {
          // Single dependency string
          deps = resolveDependencyIds(state, args.objectiveId, [args.dependencies])
        }
      }

      const task = state.addTask(args.objectiveId, {
        description: args.description,
        acceptanceCriteria: criteria,
        routedTo: args.routing,
        dependencies: deps,
      })

      return JSON.stringify({
        success: true,
        taskId: task.id,
        message: `Task added: ${task.id}`,
      })
    },
  })

  // ===========================================================================
  // Emergency Recovery Tools (BUG-25 Fix)
  // ===========================================================================

  /**
   * Force unblock a task by clearing its dependencies
   */
  const mission_unblock_task = tool({
    description:
      'Force unblock a task by clearing its dependencies. Use when dependencies are broken or tasks are permanently blocked.',
    args: {
      taskId: s.string().describe('ID of the task to unblock'),
      reason: s.string().optional().describe('Reason for unblocking'),
    },

    async execute(args, _ctx) {
      const task = state.getTask(args.taskId)
      if (!task) {
        return JSON.stringify({
          success: false,
          error: `Task ${args.taskId} not found`,
        })
      }

      const previousDeps = task.dependencies || []
      const mission = state.getMission()

      // Clear all dependencies
      task.dependencies = []
      state.save()

      // Log to history (only if mission exists)
      if (mission) {
        appendHistory(projectCwd, {
          type: 'task_unblocked',
          timestamp: new Date().toISOString(),
          missionId: mission.id,
          taskId: args.taskId,
          data: { previousDeps, reason: args.reason },
        })
      }

      log.info(`[mission] Task ${args.taskId} unblocked (had ${previousDeps.length} dependencies)`)

      return JSON.stringify({
        success: true,
        taskId: args.taskId,
        previousDependencies: previousDeps,
        message: `Task ${args.taskId} unblocked`,
      })
    },
  })

  /**
   * Scan and fix orphan task dependencies across the mission
   */
  const mission_fix_dependencies = tool({
    description:
      'Scan all mission tasks and fix broken/orphan dependencies. Resolves symbolic names like "task_1" to actual IDs and removes unresolvable references.',
    args: {},

    async execute(_args, _ctx) {
      const mission = state.getMission()
      if (!mission) {
        return JSON.stringify({
          success: false,
          error: 'No active mission',
        })
      }

      let fixed = 0
      const fixes: Array<{
        taskId: string
        removed: string[]
        resolved: string[]
      }> = []

      for (const objective of mission.objectives) {
        for (const task of objective.tasks) {
          if (!task.dependencies || task.dependencies.length === 0) continue

          const resolved: string[] = []
          const removed: string[] = []

          for (const dep of task.dependencies) {
            // Already valid - task exists
            if (state.getTask(dep)) {
              resolved.push(dep)
              continue
            }

            // Try symbolic resolution within objective (e.g., "task_1" → first task)
            const match = dep.match(/^task_(\d+)$/)
            if (match) {
              const index = parseInt(match[1], 10) - 1
              if (index >= 0 && index < objective.tasks.length) {
                const resolvedId = objective.tasks[index].id
                resolved.push(resolvedId)
                log.debug(`[mission] Fixed dependency "${dep}" → "${resolvedId}"`)
                continue
              }
            }

            // Orphan - cannot resolve, remove it
            removed.push(dep)
          }

          // Check if anything changed
          if (removed.length > 0 || resolved.length !== task.dependencies.length) {
            task.dependencies = resolved
            fixes.push({ taskId: task.id, removed, resolved })
            fixed++
          }
        }
      }

      if (fixed > 0) {
        state.save()

        // Log to history
        appendHistory(projectCwd, {
          type: 'dependencies_fixed',
          timestamp: new Date().toISOString(),
          missionId: mission.id,
          data: { tasksFixed: fixed, fixes },
        })

        log.info(`[mission] Fixed dependencies for ${fixed} tasks`)
      }

      return JSON.stringify({
        success: true,
        tasksFixed: fixed,
        fixes,
        message:
          fixed > 0
            ? `Fixed ${fixed} tasks with broken dependencies`
            : 'No broken dependencies found',
      })
    },
  })

  return {
    mission_create,
    mission_status,
    mission_update,
    mission_add_objective,
    mission_add_task,
    mission_unblock_task,
    mission_fix_dependencies,
  }
}

// =============================================================================
// Type Export
// =============================================================================

export type MissionTools = ReturnType<typeof createMissionTools>
