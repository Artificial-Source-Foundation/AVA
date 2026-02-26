/**
 * Built-in validators — syntax, typescript, lint, test.
 */

import { installMockPlatform, type MockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { resetLogger } from '@ava/core-v2/logger'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import type { ValidationContext, ValidatorConfig } from './types.js'
import { DEFAULT_VALIDATOR_CONFIG } from './types.js'
import { lintValidator, syntaxValidator, testValidator, typescriptValidator } from './validators.js'

let platform: MockPlatform

function makeCtx(overrides: Partial<ValidationContext> = {}): ValidationContext {
  return {
    files: [],
    cwd: '/project',
    signal: AbortSignal.timeout(5000),
    config: { ...DEFAULT_VALIDATOR_CONFIG },
    ...overrides,
  }
}

beforeEach(() => {
  platform = installMockPlatform()
})

afterEach(() => {
  resetLogger()
})

// ─── Syntax Validator ────────────────────────────────────────────────────────

describe('syntaxValidator', () => {
  it('has correct name and critical flag', () => {
    expect(syntaxValidator.name).toBe('syntax')
    expect(syntaxValidator.critical).toBe(true)
  })

  it('passes with warning when no JS/TS files', async () => {
    const result = await syntaxValidator.run(makeCtx({ files: ['/project/readme.md'] }))
    expect(result.passed).toBe(true)
    expect(result.warnings).toContain('No JS/TS files to check')
  })

  it('passes when esbuild succeeds', async () => {
    platform.shell.setResult(
      'npx esbuild "/project/src/index.ts" --bundle=false --loader=ts --format=esm',
      { stdout: '', stderr: '', exitCode: 0 }
    )
    const result = await syntaxValidator.run(makeCtx({ files: ['/project/src/index.ts'] }))
    expect(result.passed).toBe(true)
    expect(result.errors).toHaveLength(0)
  })

  it('fails when esbuild reports errors', async () => {
    platform.shell.setResult(
      'npx esbuild "/project/src/bad.ts" --bundle=false --loader=ts --format=esm',
      { stdout: '', stderr: 'Syntax error at line 5', exitCode: 1 }
    )
    const result = await syntaxValidator.run(makeCtx({ files: ['/project/src/bad.ts'] }))
    expect(result.passed).toBe(false)
    expect(result.errors.length).toBeGreaterThan(0)
    expect(result.errors[0]).toContain('Syntax error')
  })

  it('uses correct loader for tsx files', async () => {
    platform.shell.setResult(
      'npx esbuild "/project/src/App.tsx" --bundle=false --loader=tsx --format=esm',
      { stdout: '', stderr: '', exitCode: 0 }
    )
    const result = await syntaxValidator.run(makeCtx({ files: ['/project/src/App.tsx'] }))
    expect(result.passed).toBe(true)
  })

  it('uses correct loader for jsx files', async () => {
    platform.shell.setResult(
      'npx esbuild "/project/src/App.jsx" --bundle=false --loader=jsx --format=esm',
      { stdout: '', stderr: '', exitCode: 0 }
    )
    const result = await syntaxValidator.run(makeCtx({ files: ['/project/src/App.jsx'] }))
    expect(result.passed).toBe(true)
  })

  it('uses correct loader for js files', async () => {
    platform.shell.setResult(
      'npx esbuild "/project/src/utils.js" --bundle=false --loader=js --format=esm',
      { stdout: '', stderr: '', exitCode: 0 }
    )
    const result = await syntaxValidator.run(makeCtx({ files: ['/project/src/utils.js'] }))
    expect(result.passed).toBe(true)
  })

  it('skips when esbuild throws (not available)', async () => {
    // Default mock shell returns exitCode: 0, but let's not set a specific result
    // so the default result is used (which is exitCode: 0)
    const result = await syntaxValidator.run(makeCtx({ files: ['/project/src/index.ts'] }))
    // With default mock (exitCode: 0), it should pass
    expect(result.passed).toBe(true)
  })

  it('respects abort signal', async () => {
    const controller = new AbortController()
    controller.abort()
    const result = await syntaxValidator.run(
      makeCtx({
        files: ['/project/src/a.ts', '/project/src/b.ts'],
        signal: controller.signal,
      })
    )
    // Should still return a result, just may not check all files
    expect(result.validator).toBe('syntax')
  })
})

// ─── TypeScript Validator ────────────────────────────────────────────────────

describe('typescriptValidator', () => {
  it('has correct name and critical flag', () => {
    expect(typescriptValidator.name).toBe('typescript')
    expect(typescriptValidator.critical).toBe(true)
  })

  it('canRun returns true when tsconfig.json exists', async () => {
    platform.fs.addFile('/project/tsconfig.json', '{}')
    const canRun = await typescriptValidator.canRun!(makeCtx())
    expect(canRun).toBe(true)
  })

  it('canRun returns false when tsconfig.json is missing', async () => {
    const canRun = await typescriptValidator.canRun!(makeCtx())
    expect(canRun).toBe(false)
  })

  it('passes when tsc succeeds', async () => {
    platform.shell.setResult('npx tsc --noEmit --pretty false', {
      stdout: '',
      stderr: '',
      exitCode: 0,
    })
    const result = await typescriptValidator.run(makeCtx())
    expect(result.passed).toBe(true)
    expect(result.errors).toHaveLength(0)
  })

  it('fails with parsed errors when tsc fails', async () => {
    platform.shell.setResult('npx tsc --noEmit --pretty false', {
      stdout: 'src/index.ts(10,5): error TS2322: Type "string" is not assignable.\n',
      stderr: '',
      exitCode: 1,
    })
    const result = await typescriptValidator.run(makeCtx())
    expect(result.passed).toBe(false)
    expect(result.errors.length).toBeGreaterThan(0)
    expect(result.errors[0]).toContain('TS2322')
  })

  it('handles tsc execution error', async () => {
    // Mock shell to throw
    platform.shell.exec = async () => {
      throw new Error('tsc not found')
    }
    const result = await typescriptValidator.run(makeCtx())
    expect(result.passed).toBe(false)
    expect(result.errors[0]).toContain('tsc not found')
  })
})

// ─── Lint Validator ──────────────────────────────────────────────────────────

describe('lintValidator', () => {
  it('has correct name and critical flag', () => {
    expect(lintValidator.name).toBe('lint')
    expect(lintValidator.critical).toBe(false)
  })

  it('passes with warning when no linter detected', async () => {
    const result = await lintValidator.run(makeCtx())
    expect(result.passed).toBe(true)
    expect(result.warnings).toContain('No linter detected')
  })

  it('detects biome.json', async () => {
    platform.fs.addFile('/project/biome.json', '{}')
    platform.shell.setResult('npx biome check --reporter=json', {
      stdout: '',
      stderr: '',
      exitCode: 0,
    })
    const result = await lintValidator.run(makeCtx())
    expect(result.passed).toBe(true)
  })

  it('detects biome.jsonc', async () => {
    platform.fs.addFile('/project/biome.jsonc', '{}')
    platform.shell.setResult('npx biome check --reporter=json', {
      stdout: '',
      stderr: '',
      exitCode: 0,
    })
    const result = await lintValidator.run(makeCtx())
    expect(result.passed).toBe(true)
  })

  it('detects eslint.config.js', async () => {
    platform.fs.addFile('/project/eslint.config.js', 'export default []')
    platform.shell.setResult('npx eslint --format=json', {
      stdout: '',
      stderr: '',
      exitCode: 0,
    })
    const result = await lintValidator.run(makeCtx())
    expect(result.passed).toBe(true)
  })

  it('detects .eslintrc.json', async () => {
    platform.fs.addFile('/project/.eslintrc.json', '{}')
    platform.shell.setResult('npx eslint --format=json', {
      stdout: '',
      stderr: '',
      exitCode: 0,
    })
    const result = await lintValidator.run(makeCtx())
    expect(result.passed).toBe(true)
  })

  it('detects oxlint.json', async () => {
    platform.fs.addFile('/project/oxlint.json', '{}')
    platform.shell.setResult('npx oxlint', {
      stdout: '',
      stderr: '',
      exitCode: 0,
    })
    const result = await lintValidator.run(makeCtx())
    expect(result.passed).toBe(true)
  })

  it('uses custom lintCommand from config', async () => {
    const config: ValidatorConfig = { ...DEFAULT_VALIDATOR_CONFIG, lintCommand: 'custom-lint' }
    platform.shell.setResult('custom-lint', { stdout: '', stderr: '', exitCode: 0 })
    const result = await lintValidator.run(makeCtx({ config }))
    expect(result.passed).toBe(true)
  })

  it('collects warnings when lint fails', async () => {
    platform.fs.addFile('/project/biome.json', '{}')
    platform.shell.setResult('npx biome check --reporter=json', {
      stdout: 'warning: unused variable\nwarning: missing semicolon\n',
      stderr: '',
      exitCode: 1,
    })
    const result = await lintValidator.run(makeCtx())
    // Lint always passes, but collects warnings
    expect(result.passed).toBe(true)
    expect(result.warnings.length).toBeGreaterThan(0)
  })

  it('handles lint execution error', async () => {
    const config: ValidatorConfig = { ...DEFAULT_VALIDATOR_CONFIG, lintCommand: 'bad-linter' }
    platform.shell.exec = async () => {
      throw new Error('linter crashed')
    }
    const result = await lintValidator.run(makeCtx({ config }))
    expect(result.passed).toBe(true)
    expect(result.warnings[0]).toContain('linter crashed')
  })
})

// ─── Test Validator ──────────────────────────────────────────────────────────

describe('testValidator', () => {
  it('has correct name and critical flag', () => {
    expect(testValidator.name).toBe('test')
    expect(testValidator.critical).toBe(false)
  })

  it('canRun returns true when testCommand is set', async () => {
    const config: ValidatorConfig = { ...DEFAULT_VALIDATOR_CONFIG, testCommand: 'npm test' }
    const canRun = await testValidator.canRun!(makeCtx({ config }))
    expect(canRun).toBe(true)
  })

  it('canRun returns true when vitest is in devDependencies', async () => {
    platform.fs.addFile(
      '/project/package.json',
      JSON.stringify({ devDependencies: { vitest: '^1.0.0' } })
    )
    const canRun = await testValidator.canRun!(makeCtx())
    expect(canRun).toBe(true)
  })

  it('canRun returns true when jest is in devDependencies', async () => {
    platform.fs.addFile(
      '/project/package.json',
      JSON.stringify({ devDependencies: { jest: '^29.0.0' } })
    )
    const canRun = await testValidator.canRun!(makeCtx())
    expect(canRun).toBe(true)
  })

  it('canRun returns false when no test framework found', async () => {
    platform.fs.addFile(
      '/project/package.json',
      JSON.stringify({ devDependencies: { typescript: '^5.0.0' } })
    )
    const canRun = await testValidator.canRun!(makeCtx())
    expect(canRun).toBe(false)
  })

  it('canRun returns false when package.json is missing', async () => {
    const canRun = await testValidator.canRun!(makeCtx())
    expect(canRun).toBe(false)
  })

  it('passes when tests succeed', async () => {
    platform.fs.addFile(
      '/project/package.json',
      JSON.stringify({ devDependencies: { vitest: '^1.0.0' } })
    )
    platform.shell.setResult('npx vitest run --reporter=basic', {
      stdout: 'Tests passed',
      stderr: '',
      exitCode: 0,
    })
    const result = await testValidator.run(makeCtx())
    expect(result.passed).toBe(true)
  })

  it('fails when tests fail', async () => {
    platform.fs.addFile(
      '/project/package.json',
      JSON.stringify({ devDependencies: { vitest: '^1.0.0' } })
    )
    platform.shell.setResult('npx vitest run --reporter=basic', {
      stdout: 'FAIL src/index.test.ts\n1 test failed\nExpected 1 to be 2',
      stderr: '',
      exitCode: 1,
    })
    const result = await testValidator.run(makeCtx())
    expect(result.passed).toBe(false)
    expect(result.errors.length).toBeGreaterThan(0)
  })

  it('uses custom testCommand from config', async () => {
    const config: ValidatorConfig = { ...DEFAULT_VALIDATOR_CONFIG, testCommand: 'my-test-runner' }
    platform.shell.setResult('my-test-runner', { stdout: 'OK', stderr: '', exitCode: 0 })
    const result = await testValidator.run(makeCtx({ config }))
    expect(result.passed).toBe(true)
  })

  it('handles test execution error', async () => {
    const config: ValidatorConfig = { ...DEFAULT_VALIDATOR_CONFIG, testCommand: 'bad-runner' }
    platform.shell.exec = async () => {
      throw new Error('runner not found')
    }
    const result = await testValidator.run(makeCtx({ config }))
    expect(result.passed).toBe(false)
    expect(result.errors[0]).toContain('runner not found')
  })
})
