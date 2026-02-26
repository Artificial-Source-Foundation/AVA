/**
 * Syntax Validator Tests
 */

import { afterEach, describe, expect, it, vi } from 'vitest'

const mockShellExec = vi.fn()
const mockFsReadFile = vi.fn()
const mockFsExists = vi.fn()

vi.mock('../platform.js', () => ({
  getPlatform: () => ({
    shell: { exec: mockShellExec },
    fs: { readFile: mockFsReadFile, exists: mockFsExists },
  }),
}))

import { syntaxValidator } from './syntax.js'
import type { ValidationContext } from './types.js'
import { DEFAULT_VALIDATOR_CONFIG } from './types.js'

function createCtx(files: string[], overrides: Partial<ValidationContext> = {}): ValidationContext {
  return {
    files,
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
// File Filtering
// ============================================================================

describe('file filtering', () => {
  it('should skip non-JS/TS files', async () => {
    const ctx = createCtx(['readme.md', 'style.css', 'data.json'])

    const result = await syntaxValidator.run(ctx)

    expect(result.passed).toBe(true)
    expect(result.warnings).toContainEqual(expect.stringContaining('No TypeScript/JavaScript'))
    expect(mockShellExec).not.toHaveBeenCalled()
  })

  it('should check .ts, .tsx, .js, .jsx, .mjs, .cjs files', async () => {
    mockShellExec.mockResolvedValue({ stdout: '', stderr: '', exitCode: 0 })

    const files = ['a.ts', 'b.tsx', 'c.js', 'd.jsx', 'e.mjs', 'f.cjs']
    const ctx = createCtx(files)

    const result = await syntaxValidator.run(ctx)

    expect(result.passed).toBe(true)
    expect(mockShellExec).toHaveBeenCalledTimes(6)
  })
})

// ============================================================================
// esbuild Path
// ============================================================================

describe('esbuild path', () => {
  it('should pass when esbuild succeeds', async () => {
    mockShellExec.mockResolvedValue({ stdout: '', stderr: '', exitCode: 0 })

    const ctx = createCtx(['app.ts'])
    const result = await syntaxValidator.run(ctx)

    expect(result.passed).toBe(true)
    expect(mockShellExec).toHaveBeenCalledWith(
      expect.stringContaining('npx esbuild'),
      expect.objectContaining({ cwd: '/project' })
    )
  })

  it('should fail when esbuild reports syntax error', async () => {
    mockShellExec.mockResolvedValue({
      stdout: '',
      stderr: 'error: Expected ";" but found "}"',
      exitCode: 1,
    })

    const ctx = createCtx(['bad.ts'])
    const result = await syntaxValidator.run(ctx)

    expect(result.passed).toBe(false)
    expect(result.errors[0]).toContain('bad.ts')
    expect(result.errors[0]).toContain('Expected ";"')
  })

  it('should use correct loader for tsx files', async () => {
    mockShellExec.mockResolvedValue({ stdout: '', stderr: '', exitCode: 0 })

    const ctx = createCtx(['comp.tsx'])
    await syntaxValidator.run(ctx)

    expect(mockShellExec).toHaveBeenCalledWith(
      expect.stringContaining('--loader=tsx'),
      expect.any(Object)
    )
  })

  it('should use correct loader for jsx files', async () => {
    mockShellExec.mockResolvedValue({ stdout: '', stderr: '', exitCode: 0 })

    const ctx = createCtx(['comp.jsx'])
    await syntaxValidator.run(ctx)

    expect(mockShellExec).toHaveBeenCalledWith(
      expect.stringContaining('--loader=jsx'),
      expect.any(Object)
    )
  })
})

// ============================================================================
// basicSyntaxCheck Fallback
// ============================================================================

describe('basicSyntaxCheck fallback', () => {
  it('should fallback to basic check when esbuild throws', async () => {
    mockShellExec.mockRejectedValue(new Error('command not found'))
    mockFsReadFile.mockResolvedValue('const x = 1;')

    const ctx = createCtx(['file.ts'])
    const result = await syntaxValidator.run(ctx)

    expect(result.passed).toBe(true)
  })

  it('should detect unclosed braces in fallback', async () => {
    mockShellExec.mockRejectedValue(new Error('command not found'))
    mockFsReadFile.mockResolvedValue('function foo() {')

    const ctx = createCtx(['file.ts'])
    const result = await syntaxValidator.run(ctx)

    expect(result.passed).toBe(false)
    expect(result.errors[0]).toContain('Unclosed braces')
  })

  it('should detect unclosed parentheses in fallback', async () => {
    mockShellExec.mockRejectedValue(new Error('command not found'))
    mockFsReadFile.mockResolvedValue('const x = (1 + 2')

    const ctx = createCtx(['file.ts'])
    const result = await syntaxValidator.run(ctx)

    expect(result.passed).toBe(false)
    expect(result.errors[0]).toContain('Unclosed parentheses')
  })

  it('should detect unclosed brackets in fallback', async () => {
    mockShellExec.mockRejectedValue(new Error('command not found'))
    mockFsReadFile.mockResolvedValue('const arr = [1, 2')

    const ctx = createCtx(['file.ts'])
    const result = await syntaxValidator.run(ctx)

    expect(result.passed).toBe(false)
    expect(result.errors[0]).toContain('Unclosed brackets')
  })

  it('should warn about possible unterminated strings in fallback', async () => {
    mockShellExec.mockRejectedValue(new Error('command not found'))
    mockFsReadFile.mockResolvedValue("const x = 'hello\nconst y = 2;")

    const ctx = createCtx(['file.ts'])
    const result = await syntaxValidator.run(ctx)

    // Brackets balanced, so it passes but with warnings
    expect(result.passed).toBe(true)
    expect(result.warnings?.some((w) => w.includes('unterminated'))).toBe(true)
  })

  it('should report error when file cannot be read', async () => {
    mockShellExec.mockRejectedValue(new Error('command not found'))
    mockFsReadFile.mockRejectedValue(new Error('ENOENT'))

    const ctx = createCtx(['missing.ts'])
    const result = await syntaxValidator.run(ctx)

    expect(result.passed).toBe(false)
    expect(result.errors[0]).toContain('Cannot read file')
  })
})

// ============================================================================
// Validator Properties
// ============================================================================

describe('validator properties', () => {
  it('should be named "syntax"', () => {
    expect(syntaxValidator.name).toBe('syntax')
  })

  it('should be critical', () => {
    expect(syntaxValidator.critical).toBe(true)
  })
})
