/**
 * Validation pipeline — runs validators in sequence.
 */

import type {
  ValidationContext,
  ValidationPipelineResult,
  ValidationResult,
  Validator,
  ValidatorConfig,
} from './types.js'

// ─── Registry ───────────────────────────────────────────────────────────────

const registry = new Map<string, Validator>()

export function registerValidator(validator: Validator): void {
  registry.set(validator.name, validator)
}

export function getValidator(name: string): Validator | undefined {
  return registry.get(name)
}

export function getAllValidators(): Validator[] {
  return [...registry.values()]
}

export function resetValidators(): void {
  registry.clear()
}

// ─── Pipeline ───────────────────────────────────────────────────────────────

export async function runPipeline(
  files: string[],
  config: ValidatorConfig,
  signal: AbortSignal,
  cwd: string
): Promise<ValidationPipelineResult> {
  const start = Date.now()
  const results: ValidationResult[] = []
  let blockedBy: string | undefined
  let aborted = false

  for (const name of config.enabledValidators) {
    if (signal.aborted) {
      aborted = true
      break
    }

    const validator = registry.get(name)
    if (!validator) {
      results.push(createWarningResult(name, `Validator "${name}" not registered`))
      continue
    }

    const ctx: ValidationContext = {
      files,
      cwd,
      signal,
      config,
      previousResults: [...results],
    }

    // Check canRun
    if (validator.canRun) {
      try {
        const canRun = await validator.canRun(ctx)
        if (!canRun) {
          results.push(createWarningResult(name, `Validator "${name}" skipped (canRun=false)`))
          continue
        }
      } catch {
        results.push(createWarningResult(name, `Validator "${name}" canRun check failed`))
        continue
      }
    }

    // Run with timeout
    try {
      const result = await Promise.race([validator.run(ctx), timeoutPromise(config.timeout, name)])
      results.push(result)

      // Fail fast on critical failure
      if (!result.passed && validator.critical && config.failFast) {
        blockedBy = name
        break
      }
    } catch (err) {
      results.push({
        validator: name,
        passed: false,
        errors: [err instanceof Error ? err.message : String(err)],
        warnings: [],
        durationMs: 0,
      })

      if (validator.critical && config.failFast) {
        blockedBy = name
        break
      }
    }
  }

  const totalDurationMs = Date.now() - start
  const passed = results.every((r) => r.passed)
  const totalErrors = results.reduce((sum, r) => sum + r.errors.length, 0)
  const totalWarnings = results.reduce((sum, r) => sum + r.warnings.length, 0)

  return {
    passed: passed && !blockedBy,
    results,
    totalDurationMs,
    blockedBy,
    aborted,
    summary: {
      total: results.length,
      passed: results.filter((r) => r.passed).length,
      failed: results.filter((r) => !r.passed).length,
      totalErrors,
      totalWarnings,
    },
  }
}

export function formatReport(result: ValidationPipelineResult): string {
  const lines: string[] = []
  const status = result.passed ? 'PASSED' : 'FAILED'
  lines.push(`Validation ${status} (${result.totalDurationMs}ms)`)
  lines.push(
    `  ${result.summary.passed}/${result.summary.total} passed, ${result.summary.totalErrors} errors, ${result.summary.totalWarnings} warnings`
  )

  for (const r of result.results) {
    const icon = r.passed ? '[PASS]' : '[FAIL]'
    lines.push(`  ${icon} ${r.validator} (${r.durationMs}ms)`)
    for (const e of r.errors) lines.push(`    ERROR: ${e}`)
    for (const w of r.warnings) lines.push(`    WARN: ${w}`)
  }

  if (result.blockedBy) {
    lines.push(`  Blocked by: ${result.blockedBy}`)
  }

  return lines.join('\n')
}

// ─── Helpers ────────────────────────────────────────────────────────────────

export function createPassedResult(
  validator: string,
  durationMs: number,
  warnings: string[] = []
): ValidationResult {
  return { validator, passed: true, errors: [], warnings, durationMs }
}

export function createFailedResult(
  validator: string,
  durationMs: number,
  errors: string[],
  warnings: string[] = []
): ValidationResult {
  return { validator, passed: false, errors, warnings, durationMs }
}

function createWarningResult(validator: string, message: string): ValidationResult {
  return { validator, passed: true, errors: [], warnings: [message], durationMs: 0 }
}

function timeoutPromise(ms: number, name: string): Promise<never> {
  return new Promise((_, reject) =>
    setTimeout(() => reject(new Error(`Validator "${name}" timed out after ${ms}ms`)), ms)
  )
}
