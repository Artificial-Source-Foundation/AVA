/**
 * Test Validator
 * Runs test suite to verify code correctness
 *
 * Auto-detects test runner (Vitest, Jest, Mocha) and runs tests.
 * Supports running only affected tests when possible.
 */

import { getPlatform } from '../platform.js'
import { createFailedResult, createPassedResult } from './pipeline.js'
import type { ValidationContext, ValidationResult, Validator } from './types.js'

// ============================================================================
// Test Validator
// ============================================================================

/**
 * Test Validator
 *
 * Runs the project's test suite.
 * Non-critical by default - test failures warn but don't block.
 * Can be configured as critical if needed.
 */
export const testValidator: Validator = {
  name: 'test',
  description: 'Run test suite (Vitest, Jest, or Mocha)',
  critical: false, // Tests failing usually means work in progress

  async canRun(ctx: ValidationContext): Promise<boolean> {
    // Check if a test runner is available
    const runner = await detectTestRunner(ctx.cwd)
    return runner !== null
  },

  async run(ctx: ValidationContext): Promise<ValidationResult> {
    const startTime = Date.now()

    // Use custom command if specified
    if (ctx.config.testCommand) {
      return runCustomTestCommand(ctx.config.testCommand, ctx)
    }

    // Auto-detect test runner
    const runner = await detectTestRunner(ctx.cwd)

    if (!runner) {
      return createPassedResult('test', Date.now() - startTime, ['No test runner detected'])
    }

    // Run tests
    return runTests(runner, ctx)
  },
}

// ============================================================================
// Test Runner Detection
// ============================================================================

type TestRunner = 'vitest' | 'jest' | 'mocha' | 'npm-test'

/**
 * Detect the test runner used in the project
 */
async function detectTestRunner(cwd: string): Promise<TestRunner | null> {
  const fs = getPlatform().fs

  try {
    const pkgContent = await fs.readFile(`${cwd}/package.json`)
    const pkg = JSON.parse(pkgContent)

    // Check devDependencies
    const deps = { ...pkg.dependencies, ...pkg.devDependencies }

    // Priority: Vitest > Jest > Mocha > npm test
    if (deps.vitest) {
      return 'vitest'
    }

    if (deps.jest || deps['@jest/core']) {
      return 'jest'
    }

    if (deps.mocha) {
      return 'mocha'
    }

    // Check if there's a test script
    if (pkg.scripts?.test && pkg.scripts.test !== 'echo "Error: no test specified" && exit 1') {
      return 'npm-test'
    }

    return null
  } catch {
    return null
  }
}

// ============================================================================
// Test Runners
// ============================================================================

/**
 * Run tests using the detected runner
 */
async function runTests(runner: TestRunner, ctx: ValidationContext): Promise<ValidationResult> {
  switch (runner) {
    case 'vitest':
      return runVitest(ctx)
    case 'jest':
      return runJest(ctx)
    case 'mocha':
      return runMocha(ctx)
    case 'npm-test':
      return runNpmTest(ctx)
    default:
      return createPassedResult('test', 0, [`Unknown test runner: ${runner}`])
  }
}

/**
 * Run Vitest
 */
async function runVitest(ctx: ValidationContext): Promise<ValidationResult> {
  const startTime = Date.now()
  const shell = getPlatform().shell

  try {
    // Build command - run tests in CI mode
    let command = 'npx vitest run --reporter=basic'

    // If we have specific files, use --related to run only affected tests
    if (ctx.files.length > 0 && ctx.files.length <= 10) {
      const fileList = ctx.files.map((f) => `"${f}"`).join(' ')
      command = `npx vitest run --related ${fileList} --reporter=basic`
    }

    const result = await shell.exec(`${command} 2>&1`, {
      cwd: ctx.cwd,
      timeout: ctx.config.timeout,
    })

    const durationMs = Date.now() - startTime

    if (result.exitCode === 0) {
      const summary = extractTestSummary(result.stdout + result.stderr)
      return createPassedResult('test', durationMs, summary ? [summary] : [])
    }

    // Parse failures
    const { errors, warnings } = parseTestOutput(result.stdout + result.stderr, 'vitest')

    return createFailedResult('test', durationMs, errors, warnings)
  } catch (error) {
    return createFailedResult('test', Date.now() - startTime, [`Vitest execution failed: ${error}`])
  }
}

/**
 * Run Jest
 */
async function runJest(ctx: ValidationContext): Promise<ValidationResult> {
  const startTime = Date.now()
  const shell = getPlatform().shell

  try {
    // Build command
    let command = 'npx jest --ci --passWithNoTests'

    // If we have specific files, run only related tests
    if (ctx.files.length > 0 && ctx.files.length <= 10) {
      const fileList = ctx.files.map((f) => `"${f}"`).join(' ')
      command = `npx jest --findRelatedTests ${fileList} --ci --passWithNoTests`
    }

    const result = await shell.exec(`${command} 2>&1`, {
      cwd: ctx.cwd,
      timeout: ctx.config.timeout,
    })

    const durationMs = Date.now() - startTime

    if (result.exitCode === 0) {
      const summary = extractTestSummary(result.stdout + result.stderr)
      return createPassedResult('test', durationMs, summary ? [summary] : [])
    }

    // Parse failures
    const { errors, warnings } = parseTestOutput(result.stdout + result.stderr, 'jest')

    return createFailedResult('test', durationMs, errors, warnings)
  } catch (error) {
    return createFailedResult('test', Date.now() - startTime, [`Jest execution failed: ${error}`])
  }
}

/**
 * Run Mocha
 */
async function runMocha(ctx: ValidationContext): Promise<ValidationResult> {
  const startTime = Date.now()
  const shell = getPlatform().shell

  try {
    const result = await shell.exec('npx mocha --reporter spec 2>&1', {
      cwd: ctx.cwd,
      timeout: ctx.config.timeout,
    })

    const durationMs = Date.now() - startTime

    if (result.exitCode === 0) {
      const summary = extractTestSummary(result.stdout + result.stderr)
      return createPassedResult('test', durationMs, summary ? [summary] : [])
    }

    const { errors, warnings } = parseTestOutput(result.stdout + result.stderr, 'mocha')

    return createFailedResult('test', durationMs, errors, warnings)
  } catch (error) {
    return createFailedResult('test', Date.now() - startTime, [`Mocha execution failed: ${error}`])
  }
}

/**
 * Run npm test
 */
async function runNpmTest(ctx: ValidationContext): Promise<ValidationResult> {
  const startTime = Date.now()
  const shell = getPlatform().shell

  try {
    const result = await shell.exec('npm test 2>&1', {
      cwd: ctx.cwd,
      timeout: ctx.config.timeout,
    })

    const durationMs = Date.now() - startTime

    if (result.exitCode === 0) {
      return createPassedResult('test', durationMs)
    }

    // Generic failure
    const lines = (result.stdout + result.stderr)
      .split('\n')
      .filter((l) => l.includes('fail') || l.includes('error'))
      .slice(0, 10)

    return createFailedResult('test', durationMs, lines.length > 0 ? lines : ['Tests failed'])
  } catch (error) {
    return createFailedResult('test', Date.now() - startTime, [`npm test failed: ${error}`])
  }
}

/**
 * Run custom test command
 */
async function runCustomTestCommand(
  command: string,
  ctx: ValidationContext
): Promise<ValidationResult> {
  const startTime = Date.now()
  const shell = getPlatform().shell

  try {
    const result = await shell.exec(`${command} 2>&1`, {
      cwd: ctx.cwd,
      timeout: ctx.config.timeout,
    })

    const durationMs = Date.now() - startTime

    if (result.exitCode === 0) {
      return createPassedResult('test', durationMs)
    }

    // Treat as failure
    const lines = (result.stdout + result.stderr)
      .split('\n')
      .filter((l) => l.trim())
      .slice(0, 20)

    return createFailedResult('test', durationMs, lines)
  } catch (error) {
    return createFailedResult('test', Date.now() - startTime, [
      `Custom test command failed: ${error}`,
    ])
  }
}

// ============================================================================
// Output Parsing
// ============================================================================

interface TestParseResult {
  errors: string[]
  warnings: string[]
}

/**
 * Parse test output to extract failures
 */
function parseTestOutput(output: string, runner: TestRunner): TestParseResult {
  const errors: string[] = []
  const warnings: string[] = []

  const lines = output.split('\n')

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]

    // Look for failure patterns
    if (runner === 'vitest' || runner === 'jest') {
      // FAIL pattern
      if (line.includes('FAIL') || line.includes('✗') || line.includes('×')) {
        errors.push(line.trim())
      }
      // Error stack trace
      if (line.includes('Error:') || line.includes('AssertionError')) {
        errors.push(line.trim())
      }
    }

    if (runner === 'mocha') {
      // Mocha failure patterns
      if (line.includes('failing') || line.includes('AssertionError')) {
        errors.push(line.trim())
      }
    }
  }

  return { errors: errors.slice(0, 20), warnings }
}

/**
 * Extract test summary from output
 */
function extractTestSummary(output: string): string | null {
  // Look for common summary patterns
  const patterns = [
    // Vitest: "Test Files  1 passed (1)"
    /Test Files\s+(\d+)\s+passed/,
    // Jest: "Tests:       5 passed, 5 total"
    /Tests:\s+(\d+)\s+passed/,
    // Generic: "X tests passed"
    /(\d+)\s+tests?\s+passed/i,
  ]

  for (const pattern of patterns) {
    const match = output.match(pattern)
    if (match) {
      return match[0]
    }
  }

  return null
}
