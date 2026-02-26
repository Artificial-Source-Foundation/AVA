/**
 * Built-in validators — syntax, typescript, lint, test.
 *
 * Each validator uses platform shell to run external tools.
 */

import { getPlatform } from '@ava/core-v2/platform'
import type { ValidationContext, ValidationResult, Validator } from './types.js'

// ─── Syntax Validator ───────────────────────────────────────────────────────

const JS_EXTENSIONS = new Set(['.ts', '.tsx', '.js', '.jsx', '.mjs', '.cjs'])

function getExtension(path: string): string {
  const dot = path.lastIndexOf('.')
  return dot === -1 ? '' : path.slice(dot)
}

export const syntaxValidator: Validator = {
  name: 'syntax',
  description: 'Check files for syntax errors using esbuild',
  critical: true,

  async run(ctx: ValidationContext): Promise<ValidationResult> {
    const start = Date.now()
    const jsFiles = ctx.files.filter((f) => JS_EXTENSIONS.has(getExtension(f)))

    if (jsFiles.length === 0) {
      return {
        validator: 'syntax',
        passed: true,
        errors: [],
        warnings: ['No JS/TS files to check'],
        durationMs: Date.now() - start,
      }
    }

    const errors: string[] = []
    const shell = getPlatform().shell

    for (const file of jsFiles) {
      if (ctx.signal.aborted) break
      const ext = getExtension(file)
      const loader = ext === '.tsx' ? 'tsx' : ext === '.ts' ? 'ts' : ext === '.jsx' ? 'jsx' : 'js'

      try {
        const result = await shell.exec(
          `npx esbuild "${file}" --bundle=false --loader=${loader} --format=esm`
        )
        if (result.exitCode !== 0 && result.stderr) {
          errors.push(`${file}: ${result.stderr.split('\n')[0]}`)
        }
      } catch {
        // esbuild not available, skip
      }
    }

    return {
      validator: 'syntax',
      passed: errors.length === 0,
      errors,
      warnings: [],
      durationMs: Date.now() - start,
    }
  },
}

// ─── TypeScript Validator ───────────────────────────────────────────────────

export const typescriptValidator: Validator = {
  name: 'typescript',
  description: 'Run tsc --noEmit for type checking',
  critical: true,

  async canRun(ctx: ValidationContext): Promise<boolean> {
    return getPlatform().fs.exists(`${ctx.cwd}/tsconfig.json`)
  },

  async run(_ctx: ValidationContext): Promise<ValidationResult> {
    const start = Date.now()
    const shell = getPlatform().shell

    try {
      const result = await shell.exec('npx tsc --noEmit --pretty false')
      if (result.exitCode === 0) {
        return {
          validator: 'typescript',
          passed: true,
          errors: [],
          warnings: [],
          durationMs: Date.now() - start,
        }
      }

      const errors: string[] = []
      const warnings: string[] = []
      const lines = (result.stdout + result.stderr).split('\n')

      for (const line of lines) {
        const match = /^(.+)\((\d+),(\d+)\):\s+(error|warning)\s+(TS\d+):\s+(.+)$/.exec(line)
        if (match) {
          const msg = `${match[1]}:${match[2]} ${match[5]}: ${match[6]}`
          if (match[4] === 'error') errors.push(msg)
          else warnings.push(msg)
        }
      }

      return {
        validator: 'typescript',
        passed: false,
        errors,
        warnings,
        durationMs: Date.now() - start,
      }
    } catch (err) {
      return {
        validator: 'typescript',
        passed: false,
        errors: [err instanceof Error ? err.message : 'tsc execution failed'],
        warnings: [],
        durationMs: Date.now() - start,
      }
    }
  },
}

// ─── Lint Validator ─────────────────────────────────────────────────────────

export const lintValidator: Validator = {
  name: 'lint',
  description: 'Run linter (auto-detects Biome, ESLint, Oxlint)',
  critical: false,

  async run(ctx: ValidationContext): Promise<ValidationResult> {
    const start = Date.now()
    const shell = getPlatform().shell
    const fs = getPlatform().fs

    // Auto-detect linter
    let command: string | null = null
    if (ctx.config.lintCommand) {
      command = ctx.config.lintCommand
    } else if (
      (await fs.exists(`${ctx.cwd}/biome.json`)) ||
      (await fs.exists(`${ctx.cwd}/biome.jsonc`))
    ) {
      command = 'npx biome check --reporter=json'
    } else if (
      (await fs.exists(`${ctx.cwd}/eslint.config.js`)) ||
      (await fs.exists(`${ctx.cwd}/.eslintrc.json`))
    ) {
      command = 'npx eslint --format=json'
    } else if (
      (await fs.exists(`${ctx.cwd}/oxlint.json`)) ||
      (await fs.exists(`${ctx.cwd}/.oxlintrc.json`))
    ) {
      command = 'npx oxlint'
    }

    if (!command) {
      return {
        validator: 'lint',
        passed: true,
        errors: [],
        warnings: ['No linter detected'],
        durationMs: Date.now() - start,
      }
    }

    try {
      const result = await shell.exec(command)
      const warnings: string[] = []

      if (result.exitCode !== 0) {
        const output = result.stdout + result.stderr
        const issueLines = output
          .split('\n')
          .filter((l) => l.trim())
          .slice(0, 20)
        for (const line of issueLines) warnings.push(line)
      }

      return {
        validator: 'lint',
        passed: true,
        errors: [],
        warnings,
        durationMs: Date.now() - start,
      }
    } catch (err) {
      return {
        validator: 'lint',
        passed: true,
        errors: [],
        warnings: [err instanceof Error ? err.message : 'Lint execution failed'],
        durationMs: Date.now() - start,
      }
    }
  },
}

// ─── Test Validator ─────────────────────────────────────────────────────────

export const testValidator: Validator = {
  name: 'test',
  description: 'Run test suite (auto-detects Vitest, Jest, Mocha)',
  critical: false,

  async canRun(ctx: ValidationContext): Promise<boolean> {
    if (ctx.config.testCommand) return true
    try {
      const content = await getPlatform().fs.readFile(`${ctx.cwd}/package.json`)
      const pkg = JSON.parse(content) as Record<string, unknown>
      const devDeps = (pkg.devDependencies ?? {}) as Record<string, string>
      return 'vitest' in devDeps || 'jest' in devDeps || 'mocha' in devDeps
    } catch {
      return false
    }
  },

  async run(ctx: ValidationContext): Promise<ValidationResult> {
    const start = Date.now()
    const shell = getPlatform().shell

    let command = ctx.config.testCommand
    if (!command) {
      try {
        const content = await getPlatform().fs.readFile(`${ctx.cwd}/package.json`)
        const pkg = JSON.parse(content) as Record<string, unknown>
        const devDeps = (pkg.devDependencies ?? {}) as Record<string, string>

        if ('vitest' in devDeps) command = 'npx vitest run --reporter=basic'
        else if ('jest' in devDeps) command = 'npx jest --ci --passWithNoTests'
        else if ('mocha' in devDeps) command = 'npx mocha --reporter spec'
        else command = 'npm test'
      } catch {
        command = 'npm test'
      }
    }

    try {
      const result = await shell.exec(command)
      const output = result.stdout + result.stderr
      const passed = result.exitCode === 0

      return {
        validator: 'test',
        passed,
        errors: passed ? [] : [output.split('\n').slice(-5).join('\n')],
        warnings: [],
        durationMs: Date.now() - start,
      }
    } catch (err) {
      return {
        validator: 'test',
        passed: false,
        errors: [err instanceof Error ? err.message : 'Test execution failed'],
        warnings: [],
        durationMs: Date.now() - start,
      }
    }
  },
}
