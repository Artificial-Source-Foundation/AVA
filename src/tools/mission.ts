/**
 * Delta9 Mission Tools
 *
 * Tools for managing mission state.
 * These tools are used by Commander to create and update missions.
 */

import { z } from 'zod'
import type { MissionState } from '../mission/state.js'
import type { CouncilMode, Complexity } from '../types/index.js'

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create mission management tools
 */
export function createMissionTools(state: MissionState) {
  return {
    /**
     * Create a new mission
     */
    mission_create: {
      description: 'Create a new mission with objectives and tasks. Use this to start planning a new request.',
      parameters: z.object({
        description: z.string().describe('Mission description - what needs to be accomplished'),
        complexity: z.enum(['low', 'medium', 'high', 'critical']).optional().describe('Detected complexity level'),
        councilMode: z.enum(['none', 'quick', 'standard', 'xhigh']).optional().describe('Council mode to use'),
        objectives: z.array(z.object({
          description: z.string().describe('Objective description'),
          tasks: z.array(z.object({
            description: z.string().describe('Task description'),
            acceptanceCriteria: z.array(z.string()).describe('List of acceptance criteria'),
            routing: z.string().optional().describe('Suggested agent routing'),
            dependencies: z.array(z.string()).optional().describe('Task IDs this depends on'),
          })).describe('Tasks within this objective'),
        })).optional().describe('Mission objectives'),
      }),

      execute: async (args: {
        description: string
        complexity?: Complexity
        councilMode?: CouncilMode
        objectives?: Array<{
          description: string
          tasks: Array<{
            description: string
            acceptanceCriteria: string[]
            routing?: string
            dependencies?: string[]
          }>
        }>
      }) => {
        const mission = state.create(args.description, {
          complexity: args.complexity,
          councilMode: args.councilMode,
        })

        // Add objectives and tasks if provided
        if (args.objectives) {
          for (const objData of args.objectives) {
            const objective = state.addObjective({
              description: objData.description,
            })

            for (const taskData of objData.tasks) {
              state.addTask(objective.id, {
                description: taskData.description,
                acceptanceCriteria: taskData.acceptanceCriteria,
                routedTo: taskData.routing,
                dependencies: taskData.dependencies,
              })
            }
          }
        }

        return {
          success: true,
          missionId: mission.id,
          status: mission.status,
          message: `Mission created: ${mission.id}`,
        }
      },
    },

    /**
     * Get mission status
     */
    mission_status: {
      description: 'Get the current mission status, progress, and next available task.',
      parameters: z.object({}),

      execute: async () => {
        const mission = state.getMission()

        if (!mission) {
          return {
            success: false,
            hasMission: false,
            message: 'No active mission',
          }
        }

        const progress = state.getProgress()
        const nextTask = state.getNextTask()
        const budget = state.getBudgetStatus()

        return {
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
          nextTask: nextTask ? {
            id: nextTask.id,
            description: nextTask.description,
            acceptanceCriteria: nextTask.acceptanceCriteria,
            attempts: nextTask.attempts,
          } : null,
        }
      },
    },

    /**
     * Update mission state
     */
    mission_update: {
      description: 'Update mission state - approve, pause, resume, or abort the mission.',
      parameters: z.object({
        action: z.enum(['approve', 'start', 'pause', 'resume', 'abort']).describe('Action to take'),
      }),

      execute: async (args: { action: 'approve' | 'start' | 'pause' | 'resume' | 'abort' }) => {
        const mission = state.getMission()

        if (!mission) {
          return {
            success: false,
            message: 'No active mission',
          }
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

        return {
          success: true,
          action: args.action,
          newStatus: state.getMission()?.status,
          message: `Mission ${args.action}ed`,
        }
      },
    },

    /**
     * Add an objective to the mission
     */
    mission_add_objective: {
      description: 'Add a new objective to the current mission.',
      parameters: z.object({
        description: z.string().describe('Objective description'),
        tasks: z.array(z.object({
          description: z.string().describe('Task description'),
          acceptanceCriteria: z.array(z.string()).describe('Acceptance criteria'),
          routing: z.string().optional().describe('Agent routing'),
          dependencies: z.array(z.string()).optional().describe('Dependencies'),
        })).optional().describe('Tasks for this objective'),
      }),

      execute: async (args: {
        description: string
        tasks?: Array<{
          description: string
          acceptanceCriteria: string[]
          routing?: string
          dependencies?: string[]
        }>
      }) => {
        const objective = state.addObjective({
          description: args.description,
        })

        if (args.tasks) {
          for (const taskData of args.tasks) {
            state.addTask(objective.id, {
              description: taskData.description,
              acceptanceCriteria: taskData.acceptanceCriteria,
              routedTo: taskData.routing,
              dependencies: taskData.dependencies,
            })
          }
        }

        return {
          success: true,
          objectiveId: objective.id,
          message: `Objective added: ${objective.id}`,
        }
      },
    },

    /**
     * Add a task to an objective
     */
    mission_add_task: {
      description: 'Add a new task to an existing objective.',
      parameters: z.object({
        objectiveId: z.string().describe('ID of the objective to add task to'),
        description: z.string().describe('Task description'),
        acceptanceCriteria: z.array(z.string()).describe('Acceptance criteria'),
        routing: z.string().optional().describe('Agent routing'),
        dependencies: z.array(z.string()).optional().describe('Task dependencies'),
      }),

      execute: async (args: {
        objectiveId: string
        description: string
        acceptanceCriteria: string[]
        routing?: string
        dependencies?: string[]
      }) => {
        const task = state.addTask(args.objectiveId, {
          description: args.description,
          acceptanceCriteria: args.acceptanceCriteria,
          routedTo: args.routing,
          dependencies: args.dependencies,
        })

        return {
          success: true,
          taskId: task.id,
          message: `Task added: ${task.id}`,
        }
      },
    },
  }
}

// =============================================================================
// Type Export
// =============================================================================

export type MissionTools = ReturnType<typeof createMissionTools>
