/**
 * TypeScript Validator
 * Runs `tsc --noEmit` to check for type errors
 *
 * Critical validator that ensures code has no type errors.
 */

import { getPlatform } from '../platform.js'
import { createFailedResult, createPassedResult } from './pipeline.js'
import type { ValidationContext, ValidationResult, Validator } from './types.js'

// ============================================================================
// TypeScript Validator
// ============================================================================

/**
 * TypeScript Validator
 *
 * Runs tsc --noEmit to check for type errors.
 * Critical validator - type errors block the pipeline.
 */
export const typescriptValidator: Validator = {
  name: 'typescript',
  description: 'Run TypeScript type checking (tsc --noEmit)',
  critical: true,

  async canRun(ctx: ValidationContext): Promise<boolean> {
    // Check if tsconfig.json exists
    const fs = getPlatform().fs
    try {
      const tsconfigPath = `${ctx.cwd}/tsconfig.json`
      return await fs.exists(tsconfigPath)
    } catch {
      return false
    }
  },

  async run(ctx: ValidationContext): Promise<ValidationResult> {
    const startTime = Date.now()
    const shell = getPlatform().shell

    try {
      // Run tsc with --noEmit and --pretty false for parseable output
      const result = await shell.exec('npx tsc --noEmit --pretty false 2>&1', {
        cwd: ctx.cwd,
        timeout: ctx.config.timeout,
      })

      const durationMs = Date.now() - startTime

      if (result.exitCode === 0) {
        // Check for warnings in output
        const warnings = parseTypeScriptWarnings(result.stdout + result.stderr)
        return createPassedResult('typescript', durationMs, warnings)
      }

      // Parse errors from output
      const { errors, warnings } = parseTypeScriptOutput(result.stdout + result.stderr)

      if (errors.length === 0) {
        // No errors found but exit code was non-zero
        return createFailedResult(
          'typescript',
          durationMs,
          ['TypeScript compilation failed (unknown error)'],
          warnings
        )
      }

      return createFailedResult('typescript', durationMs, errors, warnings)
    } catch (error) {
      const durationMs = Date.now() - startTime
      return createFailedResult('typescript', durationMs, [`TypeScript check failed: ${error}`])
    }
  },
}

// ============================================================================
// Output Parsing
// ============================================================================

interface TypeScriptParseResult {
  errors: string[]
  warnings: string[]
}

/**
 * Parse TypeScript compiler output
 *
 * Format: file(line,col): error TSxxxx: message
 */
function parseTypeScriptOutput(output: string): TypeScriptParseResult {
  const errors: string[] = []
  const warnings: string[] = []

  const lines = output.split('\n')

  for (const line of lines) {
    const trimmed = line.trim()
    if (!trimmed) continue

    // Skip summary line
    if (trimmed.match(/^Found \d+ errors?/)) continue

    // Check for error pattern: file(line,col): error TSxxxx: message
    const errorMatch = trimmed.match(/^(.+)\((\d+),(\d+)\): error (TS\d+): (.+)$/)
    if (errorMatch) {
      const [, file, line, col, code, message] = errorMatch
      errors.push(`${file}:${line}:${col} - ${code}: ${message}`)
      continue
    }

    // Check for warning pattern
    const warningMatch = trimmed.match(/^(.+)\((\d+),(\d+)\): warning (TS\d+): (.+)$/)
    if (warningMatch) {
      const [, file, line, col, code, message] = warningMatch
      warnings.push(`${file}:${line}:${col} - ${code}: ${message}`)
      continue
    }

    // Generic error (no file/line info)
    if (trimmed.toLowerCase().includes('error')) {
      errors.push(trimmed)
    }
  }

  return { errors, warnings }
}

/**
 * Parse TypeScript warnings from successful output
 */
function parseTypeScriptWarnings(output: string): string[] {
  const warnings: string[] = []

  const lines = output.split('\n')
  for (const line of lines) {
    const trimmed = line.trim()
    if (!trimmed) continue

    // Check for warning pattern
    const warningMatch = trimmed.match(/^(.+)\((\d+),(\d+)\): warning (TS\d+): (.+)$/)
    if (warningMatch) {
      const [, file, lineNum, col, code, message] = warningMatch
      warnings.push(`${file}:${lineNum}:${col} - ${code}: ${message}`)
    }
  }

  return warnings
}
