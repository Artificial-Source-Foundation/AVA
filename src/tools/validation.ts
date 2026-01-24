/**
 * Delta9 Validation Tools
 *
 * Tools for recording validation results.
 * Used by Validator to report task validation outcomes.
 */

import { z } from 'zod'
import type { MissionState } from '../mission/state.js'
import type { ValidationResult, ValidationStatus } from '../types/mission.js'

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create validation tools
 */
export function createValidationTools(state: MissionState) {
  return {
    /**
     * Record validation result
     */
    validation_result: {
      description: 'Record the result of task validation. Use PASS, FIXABLE, or FAIL.',
      parameters: z.object({
        taskId: z.string().describe('ID of the validated task'),
        status: z.enum(['pass', 'fixable', 'fail']).describe('Validation status'),
        summary: z.string().describe('Summary of validation'),
        issues: z.array(z.string()).optional().describe('Issues found (for fixable/fail)'),
        suggestions: z.array(z.string()).optional().describe('Suggestions for fixing (for fixable)'),
        criteriaResults: z.array(z.object({
          criterion: z.string(),
          passed: z.boolean(),
          notes: z.string().optional(),
        })).optional().describe('Individual criteria results'),
      }),

      execute: async (args: {
        taskId: string
        status: ValidationStatus
        summary: string
        issues?: string[]
        suggestions?: string[]
        criteriaResults?: Array<{
          criterion: string
          passed: boolean
          notes?: string
        }>
      }) => {
        const task = state.getTask(args.taskId)

        if (!task) {
          return {
            success: false,
            message: `Task ${args.taskId} not found`,
          }
        }

        const validationResult: ValidationResult = {
          status: args.status,
          validatedAt: new Date().toISOString(),
          summary: args.summary,
          issues: args.issues,
          suggestions: args.suggestions,
        }

        // Apply validation result to task
        state.completeTask(args.taskId, validationResult)

        const updatedTask = state.getTask(args.taskId)
        const progress = state.getProgress()

        // Determine next action based on status
        let nextAction = ''
        if (args.status === 'pass') {
          const nextTask = state.getNextTask()
          if (nextTask) {
            nextAction = `dispatch_next_task:${nextTask.id}`
          } else if (progress.completed === progress.total) {
            nextAction = 'mission_complete'
          } else {
            nextAction = 'await_unblocked_tasks'
          }
        } else if (args.status === 'fixable') {
          if (task.attempts < 3) {
            nextAction = `retry_task:${args.taskId}`
          } else {
            nextAction = `fail_task:${args.taskId}`
          }
        } else {
          nextAction = 'replan_or_abort'
        }

        return {
          success: true,
          taskId: args.taskId,
          validationStatus: args.status,
          taskStatus: updatedTask?.status,
          missionProgress: progress,
          nextAction,
          message: `Validation recorded: ${args.status.toUpperCase()}`,
        }
      },
    },

    /**
     * Run tests for validation
     */
    run_tests: {
      description: 'Run test suite as part of validation. Returns test results.',
      parameters: z.object({
        testCommand: z.string().optional().describe('Test command to run (default: npm test)'),
        coverage: z.boolean().optional().describe('Include coverage report'),
        files: z.array(z.string()).optional().describe('Specific test files to run'),
      }),

      execute: async (args: {
        testCommand?: string
        coverage?: boolean
        files?: string[]
      }) => {
        // This would actually run tests via bash in real implementation
        // For now, return a placeholder that Commander/Validator can interpret

        const command = args.testCommand || 'npm test'
        const fileArgs = args.files ? args.files.join(' ') : ''
        const coverageArg = args.coverage ? '--coverage' : ''

        return {
          success: true,
          shouldRun: true,
          command: `${command} ${fileArgs} ${coverageArg}`.trim(),
          message: 'Test command prepared. Execute with bash tool.',
        }
      },
    },

    /**
     * Check linting
     */
    check_lint: {
      description: 'Run linter as part of validation.',
      parameters: z.object({
        lintCommand: z.string().optional().describe('Lint command (default: npm run lint)'),
        files: z.array(z.string()).optional().describe('Specific files to lint'),
        fix: z.boolean().optional().describe('Auto-fix issues'),
      }),

      execute: async (args: {
        lintCommand?: string
        files?: string[]
        fix?: boolean
      }) => {
        const command = args.lintCommand || 'npm run lint'
        const fileArgs = args.files ? args.files.join(' ') : ''
        const fixArg = args.fix ? '--fix' : ''

        return {
          success: true,
          shouldRun: true,
          command: `${command} ${fileArgs} ${fixArg}`.trim(),
          message: 'Lint command prepared. Execute with bash tool.',
        }
      },
    },

    /**
     * Check type errors
     */
    check_types: {
      description: 'Run TypeScript type checking as part of validation.',
      parameters: z.object({
        tscCommand: z.string().optional().describe('TypeScript command (default: npx tsc --noEmit)'),
      }),

      execute: async (args: {
        tscCommand?: string
      }) => {
        const command = args.tscCommand || 'npx tsc --noEmit'

        return {
          success: true,
          shouldRun: true,
          command,
          message: 'Type check command prepared. Execute with bash tool.',
        }
      },
    },
  }
}

// =============================================================================
// Type Export
// =============================================================================

export type ValidationTools = ReturnType<typeof createValidationTools>
