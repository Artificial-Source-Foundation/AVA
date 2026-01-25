/**
 * Delta9 Auto-Validation
 *
 * Automated validation flow that runs all checks in parallel.
 * Determines overall validation status based on results.
 */

import { spawn } from 'child_process'
import type { MissionState } from '../mission/state.js'
import type { ValidationResult, ValidationStatus } from '../types/mission.js'
import { appendHistory } from '../mission/history.js'

// =============================================================================
// Types
// =============================================================================

export interface AutoValidateOptions {
  /** Run tests */
  runTests?: boolean
  /** Test command override */
  testCommand?: string
  /** Run linter */
  runLint?: boolean
  /** Lint command override */
  lintCommand?: string
  /** Run type checking */
  runTypeCheck?: boolean
  /** Type check command override */
  tscCommand?: string
  /** Specific files to validate (for targeted validation) */
  files?: string[]
}

export interface CheckResult {
  name: string
  passed: boolean
  exitCode: number
  summary: string
  issues?: string[]
  durationMs: number
}

export interface AutoValidateResult {
  /** Overall status */
  status: ValidationStatus
  /** Timestamp */
  validatedAt: string
  /** Summary of all checks */
  summary: string
  /** Individual check results */
  checks: CheckResult[]
  /** Combined issues from all checks */
  issues?: string[]
  /** Suggestions for fixing */
  suggestions?: string[]
  /** Total duration */
  totalDurationMs: number
}

// =============================================================================
// Command Execution
// =============================================================================

/**
 * Execute a shell command and capture output
 */
async function executeCommand(
  command: string
): Promise<{ success: boolean; exitCode: number; output: string; durationMs: number }> {
  const startTime = Date.now()

  return new Promise((resolve) => {
    const proc = spawn('sh', ['-c', command], {
      env: {
        ...process.env,
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
        output: stdout + (stderr ? `\n${stderr}` : ''),
        durationMs: Date.now() - startTime,
      })
    })

    proc.on('error', (error: Error) => {
      resolve({
        success: false,
        exitCode: 1,
        output: error.message,
        durationMs: Date.now() - startTime,
      })
    })
  })
}

// =============================================================================
// Check Runners
// =============================================================================

/**
 * Run test check
 */
async function runTestCheck(
  command: string = 'npm test',
  files?: string[]
): Promise<CheckResult> {
  let fullCommand = command

  if (files && files.length > 0) {
    fullCommand += ' -- ' + files.join(' ')
  }

  const result = await executeCommand(fullCommand)

  // Parse test output
  let passed = 0
  let failed = 0
  let total = 0

  const passMatch = result.output.match(/(\d+)\s+pass(?:ed|ing)?/i)
  const failMatch = result.output.match(/(\d+)\s+fail(?:ed|ing)?/i)

  if (passMatch) passed = parseInt(passMatch[1], 10)
  if (failMatch) failed = parseInt(failMatch[1], 10)
  total = passed + failed

  const issues: string[] = []
  if (!result.success) {
    // Extract failed test names
    const failedTests = result.output.matchAll(/FAIL\s+(.+?)$/gm)
    for (const match of failedTests) {
      if (issues.length < 5) {
        issues.push(`Failed: ${match[1]}`)
      }
    }
  }

  return {
    name: 'tests',
    passed: result.success,
    exitCode: result.exitCode,
    summary: result.success
      ? `Tests passed: ${passed}/${total}`
      : `Tests failed: ${failed}/${total}`,
    issues: issues.length > 0 ? issues : undefined,
    durationMs: result.durationMs,
  }
}

/**
 * Run lint check
 */
async function runLintCheck(
  command: string = 'npm run lint',
  files?: string[]
): Promise<CheckResult> {
  let fullCommand = command

  if (files && files.length > 0) {
    fullCommand += ' -- ' + files.join(' ')
  }

  const result = await executeCommand(fullCommand)

  // Parse lint output
  let errors = 0
  let warnings = 0

  const eslintMatch = result.output.match(
    /(\d+)\s+problems?\s+\((\d+)\s+errors?,\s*(\d+)\s+warnings?\)/i
  )
  if (eslintMatch) {
    errors = parseInt(eslintMatch[2], 10)
    warnings = parseInt(eslintMatch[3], 10)
  }

  const issues: string[] = []
  if (!result.success) {
    // Extract error lines
    const errorMatches = result.output.matchAll(/error:\s*(.+?)$/gm)
    for (const match of errorMatches) {
      if (issues.length < 5) {
        issues.push(match[1].trim())
      }
    }
  }

  return {
    name: 'lint',
    passed: result.success,
    exitCode: result.exitCode,
    summary: result.success
      ? 'Lint check passed'
      : `Lint failed: ${errors} errors, ${warnings} warnings`,
    issues: issues.length > 0 ? issues : undefined,
    durationMs: result.durationMs,
  }
}

/**
 * Run type check
 */
async function runTypeCheckCheck(command: string = 'npx tsc --noEmit'): Promise<CheckResult> {
  const result = await executeCommand(command)

  // Parse type errors
  let errors = 0
  const errorMatch = result.output.match(/Found\s+(\d+)\s+error/i)
  if (errorMatch) {
    errors = parseInt(errorMatch[1], 10)
  } else {
    const errorLines = result.output.match(/error TS\d+:/gi)
    if (errorLines) {
      errors = errorLines.length
    }
  }

  const issues: string[] = []
  if (!result.success) {
    // Extract type error messages
    const tsErrors = result.output.matchAll(/error\s+TS\d+:\s*(.+?)$/gm)
    for (const match of tsErrors) {
      if (issues.length < 5) {
        issues.push(match[1].trim())
      }
    }
  }

  return {
    name: 'types',
    passed: result.success,
    exitCode: result.exitCode,
    summary: result.success ? 'Type check passed' : `Type check failed: ${errors} errors`,
    issues: issues.length > 0 ? issues : undefined,
    durationMs: result.durationMs,
  }
}

// =============================================================================
// Status Determination
// =============================================================================

/**
 * Determine overall validation status from check results
 */
function determineStatus(checks: CheckResult[]): ValidationStatus {
  const failedChecks = checks.filter((c) => !c.passed)

  if (failedChecks.length === 0) {
    return 'pass'
  }

  // If only lint warnings, it's still fixable
  const criticalFailures = failedChecks.filter((c) => c.name !== 'lint' || c.exitCode !== 0)

  if (criticalFailures.length === 0) {
    return 'pass' // Lint warnings only
  }

  // Check if failures are fixable (lint errors, some type errors)
  const isFixable = failedChecks.every(
    (c) => c.name === 'lint' || (c.name === 'types' && (c.issues?.length ?? 0) < 10)
  )

  return isFixable ? 'fixable' : 'fail'
}

/**
 * Generate suggestions based on failures
 */
function generateSuggestions(checks: CheckResult[]): string[] {
  const suggestions: string[] = []

  for (const check of checks) {
    if (!check.passed) {
      switch (check.name) {
        case 'tests':
          suggestions.push('Review failing tests and fix the underlying issues')
          suggestions.push('Run tests locally with verbose output for more details')
          break
        case 'lint':
          suggestions.push('Run `npm run lint -- --fix` to auto-fix lint issues')
          suggestions.push('Check ESLint rules and update code accordingly')
          break
        case 'types':
          suggestions.push('Check TypeScript errors and add proper type annotations')
          suggestions.push('Ensure all imported types are correctly defined')
          break
      }
    }
  }

  return suggestions.slice(0, 3) // Limit to 3 suggestions
}

// =============================================================================
// Main API
// =============================================================================

/**
 * Run automated validation for a task
 */
export async function autoValidate(
  state: MissionState,
  cwd: string,
  taskId: string,
  options: AutoValidateOptions = {}
): Promise<AutoValidateResult> {
  const startTime = Date.now()
  const task = state.getTask(taskId)

  if (!task) {
    return {
      status: 'fail',
      validatedAt: new Date().toISOString(),
      summary: `Task ${taskId} not found`,
      checks: [],
      totalDurationMs: 0,
    }
  }

  // Default options: run all checks
  const {
    runTests = true,
    testCommand = 'npm test',
    runLint = true,
    lintCommand = 'npm run lint',
    runTypeCheck = true,
    tscCommand = 'npx tsc --noEmit',
    files,
  } = options

  // Run checks in parallel
  const checkPromises: Promise<CheckResult>[] = []

  if (runTests) {
    checkPromises.push(runTestCheck(testCommand, files))
  }

  if (runLint) {
    checkPromises.push(runLintCheck(lintCommand, files))
  }

  if (runTypeCheck) {
    checkPromises.push(runTypeCheckCheck(tscCommand))
  }

  const checks = await Promise.all(checkPromises)
  const totalDurationMs = Date.now() - startTime

  // Determine status
  const status = determineStatus(checks)

  // Collect issues
  const allIssues: string[] = []
  for (const check of checks) {
    if (check.issues) {
      allIssues.push(...check.issues.map((i) => `[${check.name}] ${i}`))
    }
  }

  // Generate suggestions
  const suggestions = status !== 'pass' ? generateSuggestions(checks) : undefined

  // Build summary
  const passedCount = checks.filter((c) => c.passed).length
  const summary = `Validation ${status.toUpperCase()}: ${passedCount}/${checks.length} checks passed`

  // Log to history
  const mission = state.getMission()
  if (mission) {
    appendHistory(cwd, {
      type: 'task_completed',
      timestamp: new Date().toISOString(),
      missionId: mission.id,
      data: {
        taskId,
        validationStatus: status,
        checks: checks.map((c) => ({ name: c.name, passed: c.passed })),
      },
    })
  }

  // Apply validation result to task
  const validationResult: ValidationResult = {
    status,
    validatedAt: new Date().toISOString(),
    summary,
    issues: allIssues.length > 0 ? allIssues : undefined,
    suggestions,
  }
  state.completeTask(taskId, validationResult)

  return {
    status,
    validatedAt: validationResult.validatedAt,
    summary,
    checks,
    issues: allIssues.length > 0 ? allIssues : undefined,
    suggestions,
    totalDurationMs,
  }
}

/**
 * Quick validation with only type checking
 *
 * For fast feedback during development.
 */
export async function quickValidate(
  state: MissionState,
  cwd: string,
  taskId: string
): Promise<AutoValidateResult> {
  return autoValidate(state, cwd, taskId, {
    runTests: false,
    runLint: false,
    runTypeCheck: true,
  })
}

/**
 * Full validation with all checks
 *
 * For comprehensive validation before completion.
 */
export async function fullValidate(
  state: MissionState,
  cwd: string,
  taskId: string,
  files?: string[]
): Promise<AutoValidateResult> {
  return autoValidate(state, cwd, taskId, {
    runTests: true,
    runLint: true,
    runTypeCheck: true,
    files,
  })
}

/**
 * Check if validation should be run based on task changes
 */
export function shouldAutoValidate(
  state: MissionState,
  taskId: string
): { shouldValidate: boolean; reason: string } {
  const task = state.getTask(taskId)

  if (!task) {
    return { shouldValidate: false, reason: 'Task not found' }
  }

  if (task.status !== 'in_progress') {
    return { shouldValidate: false, reason: 'Task not in progress' }
  }

  // Check if task has acceptance criteria
  if (!task.acceptanceCriteria || task.acceptanceCriteria.length === 0) {
    return { shouldValidate: false, reason: 'No acceptance criteria defined' }
  }

  return { shouldValidate: true, reason: 'Task ready for validation' }
}
