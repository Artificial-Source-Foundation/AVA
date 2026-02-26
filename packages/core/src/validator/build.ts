/**
 * Build Validator
 * Runs build command to verify compilation succeeds
 *
 * Optional validator - only runs if buildCommand is configured
 * or a build script is detected in package.json.
 */

import { getPlatform } from '../platform.js'
import { createFailedResult, createPassedResult } from './pipeline.js'
import type { ValidationContext, ValidationResult, Validator } from './types.js'

// ============================================================================
// Build Validator
// ============================================================================

/**
 * Build Validator
 *
 * Verifies that the project builds successfully.
 * Non-critical by default - build issues warn but can be configured to block.
 */
export const buildValidator: Validator = {
  name: 'build',
  description: 'Run build command to verify compilation',
  critical: false, // Build failures during development are common

  async canRun(ctx: ValidationContext): Promise<boolean> {
    // If custom command specified, can run
    if (ctx.config.buildCommand) {
      return true
    }

    // Check if there's a build script in package.json
    const buildCommand = await detectBuildCommand(ctx.cwd)
    return buildCommand !== null
  },

  async run(ctx: ValidationContext): Promise<ValidationResult> {
    const startTime = Date.now()

    // Use custom command if specified
    if (ctx.config.buildCommand) {
      return runBuildCommand(ctx.config.buildCommand, ctx)
    }

    // Auto-detect build command
    const buildCommand = await detectBuildCommand(ctx.cwd)

    if (!buildCommand) {
      return createPassedResult('build', Date.now() - startTime, ['No build command detected'])
    }

    return runBuildCommand(buildCommand, ctx)
  },
}

// ============================================================================
// Build Detection
// ============================================================================

/**
 * Detect the build command from package.json
 */
async function detectBuildCommand(cwd: string): Promise<string | null> {
  const fs = getPlatform().fs

  try {
    const pkgContent = await fs.readFile(`${cwd}/package.json`)
    const pkg = JSON.parse(pkgContent)

    // Check for common build scripts
    if (pkg.scripts?.build) {
      return 'npm run build'
    }

    if (pkg.scripts?.compile) {
      return 'npm run compile'
    }

    // Check for TypeScript project without explicit build script
    const deps = { ...pkg.dependencies, ...pkg.devDependencies }
    if (deps.typescript) {
      // Check if tsconfig exists
      if (await fs.exists(`${cwd}/tsconfig.json`)) {
        return 'npx tsc --build'
      }
    }

    return null
  } catch {
    return null
  }
}

// ============================================================================
// Build Execution
// ============================================================================

/**
 * Run the build command
 */
async function runBuildCommand(command: string, ctx: ValidationContext): Promise<ValidationResult> {
  const startTime = Date.now()
  const shell = getPlatform().shell

  try {
    const result = await shell.exec(`${command} 2>&1`, {
      cwd: ctx.cwd,
      timeout: ctx.config.timeout,
    })

    const durationMs = Date.now() - startTime

    if (result.exitCode === 0) {
      // Extract build stats if available
      const stats = extractBuildStats(result.stdout + result.stderr)
      return createPassedResult('build', durationMs, stats ? [stats] : [])
    }

    // Parse build errors
    const errors = parseBuildErrors(result.stdout + result.stderr)

    return createFailedResult(
      'build',
      durationMs,
      errors.length > 0 ? errors : ['Build failed'],
      []
    )
  } catch (error) {
    return createFailedResult('build', Date.now() - startTime, [`Build execution failed: ${error}`])
  }
}

// ============================================================================
// Output Parsing
// ============================================================================

/**
 * Parse build errors from output
 */
function parseBuildErrors(output: string): string[] {
  const errors: string[] = []
  const lines = output.split('\n')

  for (const line of lines) {
    const trimmed = line.trim()
    if (!trimmed) continue

    // Common error patterns
    if (
      trimmed.toLowerCase().includes('error') ||
      trimmed.includes('ERROR') ||
      trimmed.includes('Failed to')
    ) {
      // Skip noise
      if (trimmed.includes('error Command failed')) continue
      if (trimmed.includes('npm ERR!')) continue

      errors.push(trimmed)
    }
  }

  // Limit to first 20 errors
  return errors.slice(0, 20)
}

/**
 * Extract build statistics from output
 */
function extractBuildStats(output: string): string | null {
  // Look for common build stat patterns
  const patterns = [
    // Webpack: "Built at: ..."
    /Built at:\s*(.+)/,
    // Vite: "built in Xms"
    /built in\s*(\d+(?:\.\d+)?(?:ms|s))/i,
    // esbuild: "X files built in Xms"
    /(\d+)\s+files?\s+built\s+in\s+(\d+(?:\.\d+)?(?:ms|s))/i,
    // tsc: "Found 0 errors"
    /Found\s+0\s+errors/,
    // Generic: "Build successful"
    /build\s+(?:succeeded|successful|complete)/i,
  ]

  for (const pattern of patterns) {
    const match = output.match(pattern)
    if (match) {
      return match[0]
    }
  }

  return null
}
