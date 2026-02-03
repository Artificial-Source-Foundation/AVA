/**
 * Lint Validator
 * Runs linting tools (Biome, ESLint, Oxlint) to check for style issues
 *
 * Non-critical validator - lint errors produce warnings but don't block.
 */

import { getPlatform } from '../platform.js'
import { createPassedResult, mergeResults } from './pipeline.js'
import type { ValidationContext, ValidationResult, Validator } from './types.js'

// ============================================================================
// Lint Validator
// ============================================================================

/**
 * Lint Validator
 *
 * Auto-detects available linting tools and runs them.
 * Supports: Biome, ESLint, Oxlint
 * Non-critical - issues are reported as warnings.
 */
export const lintValidator: Validator = {
  name: 'lint',
  description: 'Run linting tools (Biome, ESLint, Oxlint)',
  critical: false, // Lint issues are warnings, not blockers

  async run(ctx: ValidationContext): Promise<ValidationResult> {
    const startTime = Date.now()

    // If custom lint command is specified, use it
    if (ctx.config.lintCommand) {
      return runCustomLint(ctx)
    }

    // Auto-detect available linters
    const linters = await detectLinters(ctx.cwd)

    if (linters.length === 0) {
      return createPassedResult('lint', Date.now() - startTime, ['No linting tools detected'])
    }

    // Run all detected linters
    const results: ValidationResult[] = []

    for (const linter of linters) {
      if (ctx.signal.aborted) break

      const result = await runLinter(linter, ctx)
      results.push(result)
    }

    // Merge results
    const merged = mergeResults('lint', results)
    merged.durationMs = Date.now() - startTime

    return merged
  },
}

// ============================================================================
// Linter Detection
// ============================================================================

type LinterType = 'biome' | 'eslint' | 'oxlint'

/**
 * Detect available linting tools in the project
 */
async function detectLinters(cwd: string): Promise<LinterType[]> {
  const fs = getPlatform().fs
  const linters: LinterType[] = []

  // Check for Biome
  try {
    const biomeConfigExists =
      (await fs.exists(`${cwd}/biome.json`)) || (await fs.exists(`${cwd}/biome.jsonc`))
    if (biomeConfigExists) {
      linters.push('biome')
    }
  } catch {
    // Ignore
  }

  // Check for Oxlint
  try {
    const oxlintConfigExists =
      (await fs.exists(`${cwd}/oxlint.json`)) || (await fs.exists(`${cwd}/.oxlintrc.json`))
    // Also check package.json for oxlint script
    if (oxlintConfigExists) {
      linters.push('oxlint')
    } else {
      // Check if oxlint is in package.json scripts
      try {
        const pkgContent = await fs.readFile(`${cwd}/package.json`)
        const pkg = JSON.parse(pkgContent)
        if (pkg.scripts?.lint?.includes('oxlint') || pkg.devDependencies?.oxlint) {
          linters.push('oxlint')
        }
      } catch {
        // Ignore
      }
    }
  } catch {
    // Ignore
  }

  // Check for ESLint
  try {
    const eslintConfigExists =
      (await fs.exists(`${cwd}/eslint.config.js`)) ||
      (await fs.exists(`${cwd}/eslint.config.mjs`)) ||
      (await fs.exists(`${cwd}/eslint.config.cjs`)) ||
      (await fs.exists(`${cwd}/.eslintrc.js`)) ||
      (await fs.exists(`${cwd}/.eslintrc.json`)) ||
      (await fs.exists(`${cwd}/.eslintrc`))
    if (eslintConfigExists) {
      linters.push('eslint')
    }
  } catch {
    // Ignore
  }

  return linters
}

// ============================================================================
// Linter Runners
// ============================================================================

/**
 * Run a specific linter
 */
async function runLinter(linter: LinterType, ctx: ValidationContext): Promise<ValidationResult> {
  switch (linter) {
    case 'biome':
      return runBiome(ctx)
    case 'eslint':
      return runESLint(ctx)
    case 'oxlint':
      return runOxlint(ctx)
    default:
      return createPassedResult(linter, 0, [`Unknown linter: ${linter}`])
  }
}

/**
 * Run Biome linter
 */
async function runBiome(ctx: ValidationContext): Promise<ValidationResult> {
  const startTime = Date.now()
  const shell = getPlatform().shell

  try {
    // Run biome check (lint + format check)
    const result = await shell.exec('npx biome check --reporter=json . 2>&1', {
      cwd: ctx.cwd,
      timeout: ctx.config.timeout,
    })

    const durationMs = Date.now() - startTime

    if (result.exitCode === 0) {
      return createPassedResult('biome', durationMs)
    }

    // Parse Biome JSON output
    const { errors, warnings } = parseBiomeOutput(result.stdout + result.stderr)

    // Biome issues are warnings (non-critical)
    return createPassedResult('biome', durationMs, [...errors, ...warnings])
  } catch (error) {
    return createPassedResult('biome', Date.now() - startTime, [`Biome check failed: ${error}`])
  }
}

/**
 * Run ESLint
 */
async function runESLint(ctx: ValidationContext): Promise<ValidationResult> {
  const startTime = Date.now()
  const shell = getPlatform().shell

  try {
    // Run eslint with JSON output
    const result = await shell.exec('npx eslint --format=json . 2>&1', {
      cwd: ctx.cwd,
      timeout: ctx.config.timeout,
    })

    const durationMs = Date.now() - startTime

    if (result.exitCode === 0) {
      return createPassedResult('eslint', durationMs)
    }

    // Parse ESLint JSON output
    const { errors, warnings } = parseESLintOutput(result.stdout)

    // ESLint issues are warnings (non-critical)
    return createPassedResult('eslint', durationMs, [...errors, ...warnings])
  } catch (error) {
    return createPassedResult('eslint', Date.now() - startTime, [`ESLint check failed: ${error}`])
  }
}

/**
 * Run Oxlint
 */
async function runOxlint(ctx: ValidationContext): Promise<ValidationResult> {
  const startTime = Date.now()
  const shell = getPlatform().shell

  try {
    // Run oxlint
    const result = await shell.exec('npx oxlint . 2>&1', {
      cwd: ctx.cwd,
      timeout: ctx.config.timeout,
    })

    const durationMs = Date.now() - startTime

    if (result.exitCode === 0) {
      return createPassedResult('oxlint', durationMs)
    }

    // Parse oxlint output (text format)
    const warnings = parseOxlintOutput(result.stdout + result.stderr)

    return createPassedResult('oxlint', durationMs, warnings)
  } catch (error) {
    return createPassedResult('oxlint', Date.now() - startTime, [`Oxlint check failed: ${error}`])
  }
}

/**
 * Run custom lint command
 */
async function runCustomLint(ctx: ValidationContext): Promise<ValidationResult> {
  const startTime = Date.now()
  const shell = getPlatform().shell

  try {
    const result = await shell.exec(`${ctx.config.lintCommand} 2>&1`, {
      cwd: ctx.cwd,
      timeout: ctx.config.timeout,
    })

    const durationMs = Date.now() - startTime

    if (result.exitCode === 0) {
      return createPassedResult('lint', durationMs)
    }

    // Custom lint failed - treat output as warnings
    const lines = (result.stdout + result.stderr).split('\n').filter((l) => l.trim())
    return createPassedResult('lint', durationMs, lines)
  } catch (error) {
    return createPassedResult('lint', Date.now() - startTime, [
      `Custom lint command failed: ${error}`,
    ])
  }
}

// ============================================================================
// Output Parsing
// ============================================================================

interface LintParseResult {
  errors: string[]
  warnings: string[]
}

/**
 * Parse Biome JSON output
 */
function parseBiomeOutput(output: string): LintParseResult {
  const errors: string[] = []
  const warnings: string[] = []

  try {
    // Try to parse as JSON
    const json = JSON.parse(output)
    if (Array.isArray(json.diagnostics)) {
      for (const diag of json.diagnostics) {
        const message = `${diag.location?.path || 'unknown'}:${diag.location?.span?.start?.line || '?'} - ${diag.message}`
        if (diag.severity === 'error') {
          errors.push(message)
        } else {
          warnings.push(message)
        }
      }
    }
  } catch {
    // Not JSON, parse as text
    const lines = output.split('\n').filter((l) => l.trim())
    for (const line of lines) {
      if (line.includes('error')) {
        errors.push(line)
      } else if (line.includes('warning') || line.includes('warn')) {
        warnings.push(line)
      }
    }
  }

  return { errors, warnings }
}

/**
 * Parse ESLint JSON output
 */
function parseESLintOutput(output: string): LintParseResult {
  const errors: string[] = []
  const warnings: string[] = []

  try {
    const json = JSON.parse(output)
    if (Array.isArray(json)) {
      for (const file of json) {
        for (const message of file.messages || []) {
          const msg = `${file.filePath}:${message.line}:${message.column} - ${message.ruleId}: ${message.message}`
          if (message.severity === 2) {
            errors.push(msg)
          } else {
            warnings.push(msg)
          }
        }
      }
    }
  } catch {
    // Not JSON, return as-is
    const lines = output.split('\n').filter((l) => l.trim())
    errors.push(...lines)
  }

  return { errors, warnings }
}

/**
 * Parse Oxlint text output
 */
function parseOxlintOutput(output: string): string[] {
  const warnings: string[] = []
  const lines = output.split('\n')

  for (const line of lines) {
    const trimmed = line.trim()
    if (!trimmed) continue

    // Skip summary lines
    if (trimmed.match(/^\d+ problems?/) || trimmed.match(/^Finished in/)) continue

    // Add non-empty lines as warnings
    if (trimmed.includes('×') || trimmed.includes('⚠')) {
      warnings.push(trimmed)
    }
  }

  return warnings
}

// ============================================================================
// Export
// ============================================================================

export default lintValidator
