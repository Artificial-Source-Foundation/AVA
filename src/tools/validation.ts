/**
 * Delta9 Validation Tools
 *
 * Tools for executing validation commands and recording results.
 * Used by Validator to run tests, linting, and type checking.
 */

import { spawn } from 'child_process'
import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import type { MissionState } from '../mission/state.js'
import type { ValidationResult, ValidationStatus } from '../types/mission.js'
import { appendHistory } from '../mission/history.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Types
// =============================================================================

interface TestParseResult {
  total: number
  passed: number
  failed: number
  skipped: number
  duration?: number
}

interface LintParseResult {
  errors: number
  warnings: number
  files: number
}

interface CommandResult {
  success: boolean
  exitCode: number
  stdout: string
  stderr: string
  output: string
  durationMs: number
}

// =============================================================================
// Command Execution
// =============================================================================

/**
 * Execute a shell command and capture output
 */
async function executeCommand(command: string): Promise<CommandResult> {
  const startTime = Date.now()

  return new Promise((resolve) => {
    const proc = spawn('sh', ['-c', command], {
      env: {
        ...process.env,
        // Force plain output for parseable results
        FORCE_COLOR: '0',
        NO_COLOR: '1',
        CI: '1',
      },
    })

    let stdout = ''
    let stderr = ''

    proc.stdout.on('data', (data: Buffer) => {
      stdout += data.toString()
    })

    proc.stderr.on('data', (data: Buffer) => {
      stderr += data.toString()
    })

    proc.on('close', (exitCode: number | null) => {
      const code = exitCode ?? 1
      resolve({
        success: code === 0,
        exitCode: code,
        stdout,
        stderr,
        output: stdout + (stderr ? `\n${stderr}` : ''),
        durationMs: Date.now() - startTime,
      })
    })

    proc.on('error', (error: Error) => {
      resolve({
        success: false,
        exitCode: 1,
        stdout: '',
        stderr: error.message,
        output: error.message,
        durationMs: Date.now() - startTime,
      })
    })
  })
}

/**
 * Truncate output to a reasonable size
 */
function truncateOutput(output: string, maxLength: number = 2000): string {
  if (output.length <= maxLength) return output
  const half = Math.floor(maxLength / 2) - 50
  return (
    output.substring(0, half) +
    '\n\n... [truncated] ...\n\n' +
    output.substring(output.length - half)
  )
}

// =============================================================================
// Output Parsing
// =============================================================================

/**
 * Parse test output to extract statistics
 *
 * Supports: Jest, Vitest, Mocha, Node test runner
 */
function parseTestOutput(output: string): TestParseResult {
  const result: TestParseResult = {
    total: 0,
    passed: 0,
    failed: 0,
    skipped: 0,
  }

  // Vitest / Jest patterns
  const vitestMatch = output.match(/Tests\s+(\d+)\s+passed.*?(\d+)\s+failed/i)
  if (vitestMatch) {
    result.passed = parseInt(vitestMatch[1], 10)
    result.failed = parseInt(vitestMatch[2], 10)
    result.total = result.passed + result.failed
    return result
  }

  // Jest summary pattern
  const jestMatch = output.match(
    /Tests:\s+(\d+)\s+passed,\s*(\d+)\s+failed,\s*(\d+)\s+total/i
  )
  if (jestMatch) {
    result.passed = parseInt(jestMatch[1], 10)
    result.failed = parseInt(jestMatch[2], 10)
    result.total = parseInt(jestMatch[3], 10)
    return result
  }

  // Generic pass/fail pattern
  const passMatch = output.match(/(\d+)\s+pass(?:ed|ing)?/i)
  const failMatch = output.match(/(\d+)\s+fail(?:ed|ing)?/i)
  const skipMatch = output.match(/(\d+)\s+skip(?:ped)?/i)

  if (passMatch) result.passed = parseInt(passMatch[1], 10)
  if (failMatch) result.failed = parseInt(failMatch[1], 10)
  if (skipMatch) result.skipped = parseInt(skipMatch[1], 10)

  result.total = result.passed + result.failed + result.skipped

  // Duration pattern
  const durationMatch = output.match(/Duration:\s*([\d.]+)\s*s/i)
  if (durationMatch) {
    result.duration = parseFloat(durationMatch[1])
  }

  return result
}

/**
 * Parse lint output to extract statistics
 *
 * Supports: ESLint, TSLint
 */
function parseLintOutput(output: string): LintParseResult {
  const result: LintParseResult = {
    errors: 0,
    warnings: 0,
    files: 0,
  }

  // ESLint pattern
  const eslintMatch = output.match(
    /(\d+)\s+problems?\s+\((\d+)\s+errors?,\s*(\d+)\s+warnings?\)/i
  )
  if (eslintMatch) {
    result.errors = parseInt(eslintMatch[2], 10)
    result.warnings = parseInt(eslintMatch[3], 10)
    return result
  }

  // Count individual error/warning lines
  const errorLines = output.match(/error\s*:/gi)
  const warningLines = output.match(/warning\s*:/gi)

  if (errorLines) result.errors = errorLines.length
  if (warningLines) result.warnings = warningLines.length

  return result
}

/**
 * Parse TypeScript type check output
 */
function parseTypeCheckOutput(output: string): { errors: number; issues: string[] } {
  const issues: string[] = []
  let errors = 0

  // Count TS errors
  const errorMatch = output.match(/Found\s+(\d+)\s+error/i)
  if (errorMatch) {
    errors = parseInt(errorMatch[1], 10)
  } else {
    // Count error lines (TS error format: file.ts(line,col): error TS...)
    const errorLines = output.match(/error TS\d+:/gi)
    if (errorLines) {
      errors = errorLines.length
    }
  }

  // Extract first few issues for context
  const issueMatches = output.matchAll(/(.+?\.tsx?)?\(\d+,\d+\):\s*error\s+TS\d+:\s*(.+)/g)
  for (const match of issueMatches) {
    if (issues.length < 5) {
      issues.push(match[2])
    }
  }

  return { errors, issues }
}

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create validation tools
 */
export function createValidationTools(
  state: MissionState,
  cwd?: string
): Record<string, ToolDefinition> {
  const projectCwd = cwd ?? process.cwd()

  /**
   * Record validation result
   */
  const validation_result = tool({
    description: 'Record the result of task validation. Use PASS, FIXABLE, or FAIL.',
    args: {
      taskId: s.string().describe('ID of the validated task'),
      status: s.enum(['pass', 'fixable', 'fail']).describe('Validation status'),
      summary: s.string().describe('Summary of validation'),
      issues: s.string().optional().describe('JSON array of issues found (for fixable/fail)'),
      suggestions: s
        .string()
        .optional()
        .describe('JSON array of suggestions for fixing (for fixable)'),
      criteriaResults: s
        .string()
        .optional()
        .describe('JSON array of individual criteria results'),
    },

    async execute(args, _ctx) {
      const task = state.getTask(args.taskId)

      if (!task) {
        return JSON.stringify({
          success: false,
          message: `Task ${args.taskId} not found`,
        })
      }

      let issues: string[] | undefined
      let suggestions: string[] | undefined

      if (args.issues) {
        try {
          issues = JSON.parse(args.issues)
        } catch {
          issues = [args.issues]
        }
      }

      if (args.suggestions) {
        try {
          suggestions = JSON.parse(args.suggestions)
        } catch {
          suggestions = [args.suggestions]
        }
      }

      const validationResult: ValidationResult = {
        status: args.status as ValidationStatus,
        validatedAt: new Date().toISOString(),
        summary: args.summary,
        issues,
        suggestions,
      }

      // Apply validation result to task
      state.completeTask(args.taskId, validationResult)

      const updatedTask = state.getTask(args.taskId)
      const progress = state.getProgress()

      // Log validation to history
      const mission = state.getMission()
      if (mission) {
        appendHistory(projectCwd, {
          type: 'task_completed',
          timestamp: new Date().toISOString(),
          missionId: mission.id,
          data: {
            taskId: args.taskId,
            validationStatus: args.status,
          },
        })
      }

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

      return JSON.stringify({
        success: true,
        taskId: args.taskId,
        validationStatus: args.status,
        taskStatus: updatedTask?.status,
        missionProgress: progress,
        nextAction,
        message: `Validation recorded: ${args.status.toUpperCase()}`,
      })
    },
  })

  /**
   * Run tests for validation
   */
  const run_tests = tool({
    description:
      'Execute test suite and return parsed results. Runs tests via shell and analyzes output.',
    args: {
      testCommand: s.string().optional().describe('Test command to run (default: npm test)'),
      coverage: s.boolean().optional().describe('Include coverage report'),
      files: s.string().optional().describe('JSON array of specific test files to run'),
      taskId: s.string().optional().describe('Link results to a specific task'),
    },

    async execute(args, _ctx) {
      // Build command
      let command = args.testCommand || 'npm test'

      if (args.files) {
        try {
          const files = JSON.parse(args.files) as string[]
          command += ' -- ' + files.join(' ')
        } catch {
          command += ' -- ' + args.files
        }
      }

      if (args.coverage) {
        command += ' --coverage'
      }

      // Execute tests
      const result = await executeCommand(command)
      const parsed = parseTestOutput(result.output)

      // Log test results if linked to a task (for debugging/tracking purposes)
      // Note: Not a significant history event, just track via data field
      void args.taskId // Acknowledge taskId is available for future use

      return JSON.stringify({
        success: result.success,
        exitCode: result.exitCode,
        tests: {
          total: parsed.total,
          passed: parsed.passed,
          failed: parsed.failed,
          skipped: parsed.skipped,
          duration: parsed.duration,
        },
        durationMs: result.durationMs,
        output: truncateOutput(result.output),
        message: result.success
          ? `Tests passed: ${parsed.passed}/${parsed.total}`
          : `Tests failed: ${parsed.failed}/${parsed.total}`,
      })
    },
  })

  /**
   * Check linting
   */
  const check_lint = tool({
    description: 'Execute linter and return parsed results. Runs ESLint or configured linter.',
    args: {
      lintCommand: s.string().optional().describe('Lint command (default: npm run lint)'),
      files: s.string().optional().describe('JSON array of specific files to lint'),
      fix: s.boolean().optional().describe('Auto-fix issues'),
      taskId: s.string().optional().describe('Link results to a specific task'),
    },

    async execute(args, _ctx) {
      // Build command
      let command = args.lintCommand || 'npm run lint'

      if (args.files) {
        try {
          const files = JSON.parse(args.files) as string[]
          command += ' -- ' + files.join(' ')
        } catch {
          command += ' -- ' + args.files
        }
      }

      if (args.fix) {
        command += ' --fix'
      }

      // Execute linter
      const result = await executeCommand(command)
      const parsed = parseLintOutput(result.output)

      return JSON.stringify({
        success: result.success,
        exitCode: result.exitCode,
        lint: {
          errors: parsed.errors,
          warnings: parsed.warnings,
          files: parsed.files,
        },
        durationMs: result.durationMs,
        output: truncateOutput(result.output),
        message: result.success
          ? 'Lint check passed'
          : `Lint check failed: ${parsed.errors} errors, ${parsed.warnings} warnings`,
      })
    },
  })

  /**
   * Check type errors
   */
  const check_types = tool({
    description:
      'Execute TypeScript type checking and return parsed results. Runs tsc --noEmit by default.',
    args: {
      tscCommand: s.string().optional().describe('TypeScript command (default: npx tsc --noEmit)'),
      taskId: s.string().optional().describe('Link results to a specific task'),
    },

    async execute(args, _ctx) {
      const command = args.tscCommand || 'npx tsc --noEmit'

      // Execute type check
      const result = await executeCommand(command)
      const parsed = parseTypeCheckOutput(result.output)

      return JSON.stringify({
        success: result.success,
        exitCode: result.exitCode,
        typeCheck: {
          errors: parsed.errors,
          issues: parsed.issues,
        },
        durationMs: result.durationMs,
        output: truncateOutput(result.output),
        message: result.success
          ? 'Type check passed'
          : `Type check failed: ${parsed.errors} errors`,
      })
    },
  })

  return {
    validation_result,
    run_tests,
    check_lint,
    check_types,
  }
}

// =============================================================================
// Type Export
// =============================================================================

export type ValidationTools = ReturnType<typeof createValidationTools>
