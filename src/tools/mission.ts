/**
 * Delta9 Mission Tools
 *
 * Tools for managing mission state.
 * These tools are used by Commander to create and update missions.
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import type { MissionState } from '../mission/state.js'
import type { CouncilMode, Complexity } from '../types/index.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create mission management tools
 */
export function createMissionTools(state: MissionState): Record<string, ToolDefinition> {
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
              const task = state.addTask(objective.id, {
                description: taskData.description,
                acceptanceCriteria: taskData.acceptanceCriteria,
                routedTo: taskData.routing,
                dependencies: taskData.dependencies,
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
            state.addTask(objective.id, {
              description: taskData.description,
              acceptanceCriteria: taskData.acceptanceCriteria,
              routedTo: taskData.routing,
              dependencies: taskData.dependencies,
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

      if (args.dependencies) {
        try {
          deps = JSON.parse(args.dependencies)
        } catch {
          deps = [args.dependencies]
        }
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

  return {
    mission_create,
    mission_status,
    mission_update,
    mission_add_objective,
    mission_add_task,
  }
}

// =============================================================================
// Type Export
// =============================================================================

export type MissionTools = ReturnType<typeof createMissionTools>
