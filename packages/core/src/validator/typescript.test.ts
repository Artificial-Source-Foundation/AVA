/**
 * TypeScript Validator Tests
 */

import { afterEach, describe, expect, it, vi } from 'vitest'

const mockShellExec = vi.fn()
const mockFsExists = vi.fn()

vi.mock('../platform.js', () => ({
  getPlatform: () => ({
    shell: { exec: mockShellExec },
    fs: { exists: mockFsExists },
  }),
}))

import type { ValidationContext } from './types.js'
import { DEFAULT_VALIDATOR_CONFIG } from './types.js'
import { typescriptValidator } from './typescript.js'

function createCtx(overrides: Partial<ValidationContext> = {}): ValidationContext {
  return {
    files: ['src/app.ts'],
    cwd: '/project',
    signal: new AbortController().signal,
    config: { ...DEFAULT_VALIDATOR_CONFIG },
    ...overrides,
  }
}

afterEach(() => {
  vi.clearAllMocks()
})

// ============================================================================
// canRun
// ============================================================================

describe('canRun', () => {
  it('should return true when tsconfig.json exists', async () => {
    mockFsExists.mockResolvedValue(true)

    const ctx = createCtx()
    const result = await typescriptValidator.canRun!(ctx)

    expect(result).toBe(true)
    expect(mockFsExists).toHaveBeenCalledWith('/project/tsconfig.json')
  })

  it('should return false when tsconfig.json does not exist', async () => {
    mockFsExists.mockResolvedValue(false)

    const ctx = createCtx()
    const result = await typescriptValidator.canRun!(ctx)

    expect(result).toBe(false)
  })

  it('should return false when fs.exists throws', async () => {
    mockFsExists.mockRejectedValue(new Error('permission denied'))

    const ctx = createCtx()
    const result = await typescriptValidator.canRun!(ctx)

    expect(result).toBe(false)
  })
})

// ============================================================================
// run — success
// ============================================================================

describe('run — success', () => {
  it('should pass when tsc exits 0', async () => {
    mockShellExec.mockResolvedValue({
      stdout: '',
      stderr: '',
      exitCode: 0,
    })

    const ctx = createCtx()
    const result = await typescriptValidator.run(ctx)

    expect(result.passed).toBe(true)
    expect(result.validator).toBe('typescript')
    expect(result.durationMs).toBeGreaterThanOrEqual(0)
  })

  it('should extract warnings from successful output', async () => {
    mockShellExec.mockResolvedValue({
      stdout: 'src/app.ts(10,5): warning TS6133: unused variable\n',
      stderr: '',
      exitCode: 0,
    })

    const ctx = createCtx()
    const result = await typescriptValidator.run(ctx)

    expect(result.passed).toBe(true)
    expect(result.warnings.length).toBeGreaterThan(0)
    expect(result.warnings[0]).toContain('TS6133')
  })
})

// ============================================================================
// run — failure
// ============================================================================

describe('run — failure', () => {
  it('should fail when tsc exits 1 with errors', async () => {
    const tscOutput = [
      "src/app.ts(5,10): error TS2304: Cannot find name 'foo'.",
      "src/utils.ts(12,3): error TS2322: Type 'string' is not assignable to type 'number'.",
      'Found 2 errors.',
    ].join('\n')

    mockShellExec.mockResolvedValue({
      stdout: tscOutput,
      stderr: '',
      exitCode: 1,
    })

    const ctx = createCtx()
    const result = await typescriptValidator.run(ctx)

    expect(result.passed).toBe(false)
    expect(result.errors).toHaveLength(2)
    expect(result.errors[0]).toContain('TS2304')
    expect(result.errors[0]).toContain('src/app.ts:5:10')
    expect(result.errors[1]).toContain('TS2322')
  })

  it('should report unknown error when exit code 1 but no parseable errors', async () => {
    mockShellExec.mockResolvedValue({
      stdout: 'some unexpected output',
      stderr: '',
      exitCode: 1,
    })

    const ctx = createCtx()
    const result = await typescriptValidator.run(ctx)

    expect(result.passed).toBe(false)
    expect(result.errors.length).toBeGreaterThan(0)
  })
})

// ============================================================================
// run — exec failure
// ============================================================================

describe('run — exec failure', () => {
  it('should fail when shell.exec throws', async () => {
    mockShellExec.mockRejectedValue(new Error('tsc not found'))

    const ctx = createCtx()
    const result = await typescriptValidator.run(ctx)

    expect(result.passed).toBe(false)
    expect(result.errors[0]).toContain('tsc not found')
  })
})

// ============================================================================
// Validator Properties
// ============================================================================

describe('validator properties', () => {
  it('should be named "typescript"', () => {
    expect(typescriptValidator.name).toBe('typescript')
  })

  it('should be critical', () => {
    expect(typescriptValidator.critical).toBe(true)
  })
})
