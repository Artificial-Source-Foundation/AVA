/**
 * Validation Pipeline
 * Orchestrates running validators in sequence with fail-fast support
 *
 * Runs validators in order, respecting critical/non-critical status
 */

import type {
  ValidationContext,
  ValidationPipelineResult,
  ValidationResult,
  Validator,
  ValidatorConfig,
  ValidatorName,
  ValidatorRegistry,
} from './types.js'
import { DEFAULT_VALIDATOR_CONFIG } from './types.js'

// ============================================================================
// Timeout Utility
// ============================================================================

/**
 * Run a promise with a timeout
 */
async function withTimeout<T>(promise: Promise<T>, ms: number, name: string): Promise<T> {
  const timeoutPromise = new Promise<never>((_, reject) => {
    const timer = setTimeout(() => {
      reject(new Error(`Validator '${name}' timed out after ${ms}ms`))
    }, ms)
    // Clean up timer if promise resolves first
    promise.finally(() => clearTimeout(timer))
  })

  return Promise.race([promise, timeoutPromise])
}

// ============================================================================
// Simple Validator Registry
// ============================================================================

/**
 * Simple in-memory validator registry
 */
export class SimpleValidatorRegistry implements ValidatorRegistry {
  private validators: Map<string, Validator> = new Map()

  get(name: ValidatorName): Validator | undefined {
    return this.validators.get(name)
  }

  register(validator: Validator): void {
    this.validators.set(validator.name, validator)
  }

  getAll(): Validator[] {
    return Array.from(this.validators.values())
  }

  has(name: ValidatorName): boolean {
    return this.validators.has(name)
  }
}

// ============================================================================
// Validation Pipeline
// ============================================================================

/**
 * Validation Pipeline
 *
 * Executes validators in sequence, collecting results.
 * Stops early on critical failures when failFast is enabled.
 *
 * Usage:
 * ```typescript
 * const pipeline = new ValidationPipeline(registry)
 * const result = await pipeline.run(files, config, signal)
 * if (!result.passed) {
 *   console.error('Validation failed:', result.blockedBy)
 * }
 * ```
 */
export class ValidationPipeline {
  constructor(private registry: ValidatorRegistry) {}

  /**
   * Run the validation pipeline
   *
   * @param files - Files to validate
   * @param config - Validator configuration
   * @param signal - AbortSignal for cancellation
   * @param cwd - Working directory
   * @returns Pipeline result with all validator results
   */
  async run(
    files: string[],
    config: Partial<ValidatorConfig> = {},
    signal: AbortSignal,
    cwd: string
  ): Promise<ValidationPipelineResult> {
    const finalConfig: ValidatorConfig = {
      ...DEFAULT_VALIDATOR_CONFIG,
      ...config,
    }

    const startTime = Date.now()
    const results: ValidationResult[] = []
    let blockedBy: string | undefined
    let aborted = false

    // Create validation context
    const ctx: ValidationContext = {
      files,
      cwd,
      signal,
      config: finalConfig,
    }

    // Run each enabled validator in order
    for (const validatorName of finalConfig.enabledValidators) {
      // Check for abort
      if (signal.aborted) {
        aborted = true
        break
      }

      // Get validator from registry
      const validator = this.registry.get(validatorName)
      if (!validator) {
        // Skip unknown validators with warning
        results.push({
          validator: validatorName,
          passed: true,
          errors: [],
          warnings: [`Validator '${validatorName}' not found in registry`],
          durationMs: 0,
        })
        continue
      }

      // Check if validator can run
      if (validator.canRun) {
        try {
          const canRun = await validator.canRun(ctx)
          if (!canRun) {
            results.push({
              validator: validatorName,
              passed: true,
              errors: [],
              warnings: [`Validator '${validatorName}' skipped (not available)`],
              durationMs: 0,
            })
            continue
          }
        } catch (error) {
          results.push({
            validator: validatorName,
            passed: true,
            errors: [],
            warnings: [`Validator '${validatorName}' canRun check failed: ${error}`],
            durationMs: 0,
          })
          continue
        }
      }

      // Run validator with timeout
      let result: ValidationResult
      try {
        result = await withTimeout(validator.run(ctx), finalConfig.timeout, validatorName)
      } catch (error) {
        // Timeout or other error
        result = {
          validator: validatorName,
          passed: false,
          errors: [String(error)],
          warnings: [],
          durationMs: finalConfig.timeout,
        }
      }

      results.push(result)

      // Update context with previous results
      ctx.previousResults = [...results]

      // Check for fail-fast on critical failure
      if (!result.passed && (validator.critical || finalConfig.failFast)) {
        blockedBy = validatorName
        break
      }
    }

    // Calculate summary
    const passed = results.every((r) => r.passed)
    const totalDurationMs = Date.now() - startTime

    return {
      passed,
      results,
      totalDurationMs,
      blockedBy,
      aborted,
      summary: this.calculateSummary(results),
    }
  }

  /**
   * Run a single validator
   */
  async runSingle(
    validatorName: ValidatorName,
    files: string[],
    config: Partial<ValidatorConfig> = {},
    signal: AbortSignal,
    cwd: string
  ): Promise<ValidationResult> {
    const validator = this.registry.get(validatorName)
    if (!validator) {
      return {
        validator: validatorName,
        passed: false,
        errors: [`Validator '${validatorName}' not found`],
        warnings: [],
        durationMs: 0,
      }
    }

    const finalConfig: ValidatorConfig = {
      ...DEFAULT_VALIDATOR_CONFIG,
      ...config,
    }

    const ctx: ValidationContext = {
      files,
      cwd,
      signal,
      config: finalConfig,
    }

    try {
      return await withTimeout(validator.run(ctx), finalConfig.timeout, validatorName)
    } catch (error) {
      return {
        validator: validatorName,
        passed: false,
        errors: [String(error)],
        warnings: [],
        durationMs: finalConfig.timeout,
      }
    }
  }

  /**
   * Calculate summary statistics from results
   */
  private calculateSummary(results: ValidationResult[]): ValidationPipelineResult['summary'] {
    return {
      total: results.length,
      passed: results.filter((r) => r.passed).length,
      failed: results.filter((r) => !r.passed).length,
      totalErrors: results.reduce((sum, r) => sum + r.errors.length, 0),
      totalWarnings: results.reduce((sum, r) => sum + r.warnings.length, 0),
    }
  }

  /**
   * Format pipeline result as a human-readable report
   */
  formatReport(result: ValidationPipelineResult): string {
    const lines: string[] = []

    // Header
    lines.push(result.passed ? '✓ Validation PASSED' : '✗ Validation FAILED')
    lines.push('')

    // Summary
    const { summary } = result
    lines.push(`Summary: ${summary.passed}/${summary.total} validators passed`)
    if (summary.totalErrors > 0) {
      lines.push(`  Errors: ${summary.totalErrors}`)
    }
    if (summary.totalWarnings > 0) {
      lines.push(`  Warnings: ${summary.totalWarnings}`)
    }
    lines.push(`  Duration: ${result.totalDurationMs}ms`)
    lines.push('')

    // Individual results
    for (const res of result.results) {
      const status = res.passed ? '✓' : '✗'
      lines.push(`${status} ${res.validator} (${res.durationMs}ms)`)

      for (const error of res.errors) {
        lines.push(`    ✗ ${error}`)
      }
      for (const warning of res.warnings) {
        lines.push(`    ⚠ ${warning}`)
      }
    }

    if (result.blockedBy) {
      lines.push('')
      lines.push(`Pipeline blocked by: ${result.blockedBy}`)
    }

    return lines.join('\n')
  }
}

// ============================================================================
// Convenience Functions
// ============================================================================

/**
 * Create a validation result for a passed validation
 */
export function createPassedResult(
  validator: string,
  durationMs: number,
  warnings: string[] = []
): ValidationResult {
  return {
    validator,
    passed: true,
    errors: [],
    warnings,
    durationMs,
  }
}

/**
 * Create a validation result for a failed validation
 */
export function createFailedResult(
  validator: string,
  durationMs: number,
  errors: string[],
  warnings: string[] = []
): ValidationResult {
  return {
    validator,
    passed: false,
    errors,
    warnings,
    durationMs,
  }
}

/**
 * Merge multiple validation results into one
 */
export function mergeResults(validator: string, results: ValidationResult[]): ValidationResult {
  return {
    validator,
    passed: results.every((r) => r.passed),
    errors: results.flatMap((r) => r.errors),
    warnings: results.flatMap((r) => r.warnings),
    durationMs: results.reduce((sum, r) => sum + r.durationMs, 0),
  }
}
