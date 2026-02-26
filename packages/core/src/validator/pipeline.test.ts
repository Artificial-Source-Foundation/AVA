import { beforeEach, describe, expect, it, vi } from 'vitest'
import { SimpleValidatorRegistry, ValidationPipeline } from './pipeline.js'
import type { Validator, ValidatorName } from './types.js'

// ============================================================================
// Helpers
// ============================================================================

function createMockValidator(overrides: Partial<Validator> & { name: string }): Validator {
  return {
    description: `Mock ${overrides.name} validator`,
    critical: false,
    run: vi.fn().mockResolvedValue({
      validator: overrides.name,
      passed: true,
      errors: [],
      warnings: [],
      durationMs: 10,
    }),
    ...overrides,
  }
}

function passResult(name: string, durationMs = 10): ValidationResult {
  return { validator: name, passed: true, errors: [], warnings: [], durationMs }
}

function failResult(name: string, errors: string[], durationMs = 10): ValidationResult {
  return { validator: name, passed: false, errors, warnings: [], durationMs }
}

// ============================================================================
// SimpleValidatorRegistry
// ============================================================================

describe('SimpleValidatorRegistry', () => {
  let registry: SimpleValidatorRegistry

  beforeEach(() => {
    registry = new SimpleValidatorRegistry()
  })

  it('should register and retrieve a validator', () => {
    const v = createMockValidator({ name: 'syntax' })
    registry.register(v)
    expect(registry.get('syntax')).toBe(v)
  })

  it('should return undefined for unregistered validator', () => {
    expect(registry.get('syntax')).toBeUndefined()
  })

  it('should report has() correctly', () => {
    const v = createMockValidator({ name: 'syntax' })
    expect(registry.has('syntax')).toBe(false)
    registry.register(v)
    expect(registry.has('syntax')).toBe(true)
  })

  it('should return all registered validators via getAll()', () => {
    const a = createMockValidator({ name: 'syntax' })
    const b = createMockValidator({ name: 'typescript' })
    registry.register(a)
    registry.register(b)

    const all = registry.getAll()
    expect(all).toHaveLength(2)
    expect(all).toContain(a)
    expect(all).toContain(b)
  })

  it('should overwrite a validator with same name', () => {
    const v1 = createMockValidator({ name: 'syntax' })
    const v2 = createMockValidator({ name: 'syntax', description: 'updated' })
    registry.register(v1)
    registry.register(v2)
    expect(registry.get('syntax')).toBe(v2)
    expect(registry.getAll()).toHaveLength(1)
  })
})

// ============================================================================
// ValidationPipeline.run()
// ============================================================================

describe('ValidationPipeline.run()', () => {
  let registry: SimpleValidatorRegistry
  let pipeline: ValidationPipeline
  let signal: AbortSignal

  beforeEach(() => {
    registry = new SimpleValidatorRegistry()
    pipeline = new ValidationPipeline(registry)
    signal = AbortSignal.abort() // Will be overridden per test
    signal = new AbortController().signal
  })

  it('should pass when all validators pass', async () => {
    registry.register(
      createMockValidator({
        name: 'syntax',
        run: vi.fn().mockResolvedValue(passResult('syntax')),
      })
    )
    registry.register(
      createMockValidator({
        name: 'typescript',
        run: vi.fn().mockResolvedValue(passResult('typescript')),
      })
    )

    const result = await pipeline.run(
      ['file.ts'],
      { enabledValidators: ['syntax', 'typescript'] as ValidatorName[] },
      signal,
      '/tmp'
    )

    expect(result.passed).toBe(true)
    expect(result.results).toHaveLength(2)
    expect(result.summary.passed).toBe(2)
    expect(result.summary.failed).toBe(0)
  })

  it('should fail when any validator fails', async () => {
    registry.register(
      createMockValidator({
        name: 'syntax',
        run: vi.fn().mockResolvedValue(passResult('syntax')),
      })
    )
    registry.register(
      createMockValidator({
        name: 'typescript',
        run: vi.fn().mockResolvedValue(failResult('typescript', ['TS2304: Cannot find name'])),
      })
    )

    const result = await pipeline.run(
      ['file.ts'],
      { enabledValidators: ['syntax', 'typescript'] as ValidatorName[] },
      signal,
      '/tmp'
    )

    expect(result.passed).toBe(false)
    expect(result.summary.failed).toBe(1)
    expect(result.summary.totalErrors).toBe(1)
  })

  it('should stop on critical failure with failFast', async () => {
    const syntaxRun = vi.fn().mockResolvedValue(failResult('syntax', ['parse error']))
    const tsRun = vi.fn().mockResolvedValue(passResult('typescript'))

    registry.register(
      createMockValidator({
        name: 'syntax',
        critical: true,
        run: syntaxRun,
      })
    )
    registry.register(
      createMockValidator({
        name: 'typescript',
        run: tsRun,
      })
    )

    const result = await pipeline.run(
      ['file.ts'],
      {
        enabledValidators: ['syntax', 'typescript'] as ValidatorName[],
        failFast: true,
      },
      signal,
      '/tmp'
    )

    expect(result.passed).toBe(false)
    expect(result.blockedBy).toBe('syntax')
    // typescript should NOT have been called because syntax is critical + failFast
    expect(tsRun).not.toHaveBeenCalled()
    expect(result.results).toHaveLength(1)
  })

  it('should skip validator when canRun returns false', async () => {
    const tsRun = vi.fn().mockResolvedValue(passResult('typescript'))

    registry.register(
      createMockValidator({
        name: 'typescript',
        canRun: vi.fn().mockResolvedValue(false),
        run: tsRun,
      })
    )

    const result = await pipeline.run(
      ['file.ts'],
      { enabledValidators: ['typescript'] as ValidatorName[] },
      signal,
      '/tmp'
    )

    expect(result.passed).toBe(true)
    expect(tsRun).not.toHaveBeenCalled()
    expect(result.results[0].warnings).toContainEqual(expect.stringContaining('skipped'))
  })

  it('should abort early when signal is aborted', async () => {
    const abortController = new AbortController()
    abortController.abort()

    registry.register(
      createMockValidator({
        name: 'syntax',
        run: vi.fn().mockResolvedValue(passResult('syntax')),
      })
    )

    const result = await pipeline.run(
      ['file.ts'],
      { enabledValidators: ['syntax'] as ValidatorName[] },
      abortController.signal,
      '/tmp'
    )

    expect(result.aborted).toBe(true)
    expect(result.results).toHaveLength(0)
  })

  it('should handle validator timeout', async () => {
    registry.register(
      createMockValidator({
        name: 'syntax',
        run: vi.fn().mockImplementation(() => new Promise((resolve) => setTimeout(resolve, 60000))),
      })
    )

    const result = await pipeline.run(
      ['file.ts'],
      {
        enabledValidators: ['syntax'] as ValidatorName[],
        timeout: 50, // 50ms timeout
      },
      signal,
      '/tmp'
    )

    expect(result.passed).toBe(false)
    expect(result.results[0].errors[0]).toContain('timed out')
  })

  it('should skip unknown validators with a warning', async () => {
    const result = await pipeline.run(
      ['file.ts'],
      { enabledValidators: ['nonexistent' as ValidatorName] },
      signal,
      '/tmp'
    )

    expect(result.passed).toBe(true)
    expect(result.results[0].warnings[0]).toContain('not found')
  })
})

// ============================================================================
// ValidationPipeline.runSingle()
// ============================================================================

describe('ValidationPipeline.runSingle()', () => {
  let registry: SimpleValidatorRegistry
  let pipeline: ValidationPipeline
  let signal: AbortSignal

  beforeEach(() => {
    registry = new SimpleValidatorRegistry()
    pipeline = new ValidationPipeline(registry)
    signal = new AbortController().signal
  })

  it('should run a single validator', async () => {
    registry.register(
      createMockValidator({
        name: 'syntax',
        run: vi.fn().mockResolvedValue(passResult('syntax')),
      })
    )

    const result = await pipeline.runSingle('syntax', ['file.ts'], {}, signal, '/tmp')
    expect(result.passed).toBe(true)
    expect(result.validator).toBe('syntax')
  })

  it('should return failure for unknown validator', async () => {
    const result = await pipeline.runSingle(
      'nonexistent' as ValidatorName,
      ['file.ts'],
      {},
      signal,
      '/tmp'
    )
    expect(result.passed).toBe(false)
    expect(result.errors[0]).toContain('not found')
  })

  it('should handle validator throwing an error', async () => {
    registry.register(
      createMockValidator({
        name: 'syntax',
        run: vi.fn().mockImplementation(() => {
          throw new Error('boom')
        }),
      })
    )

    const result = await pipeline.runSingle('syntax', ['file.ts'], {}, signal, '/tmp')
    expect(result.passed).toBe(false)
    expect(result.errors[0]).toContain('boom')
  })
})

// ============================================================================
// ValidationPipeline.formatReport()
// ============================================================================

describe('ValidationPipeline.formatReport()', () => {
  let pipeline: ValidationPipeline

  beforeEach(() => {
    pipeline = new ValidationPipeline(new SimpleValidatorRegistry())
  })

  it('should format a passing report', () => {
    const report = pipeline.formatReport({
      passed: true,
      results: [passResult('syntax', 15)],
      totalDurationMs: 15,
      summary: { total: 1, passed: 1, failed: 0, totalErrors: 0, totalWarnings: 0 },
    })

    expect(report).toContain('PASSED')
    expect(report).toContain('1/1 validators passed')
    expect(report).toContain('syntax')
  })

  it('should format a failing report with errors', () => {
    const report = pipeline.formatReport({
      passed: false,
      results: [failResult('typescript', ['TS2304: x', 'TS2305: y'])],
      totalDurationMs: 20,
      blockedBy: 'typescript',
      summary: { total: 1, passed: 0, failed: 1, totalErrors: 2, totalWarnings: 0 },
    })

    expect(report).toContain('FAILED')
    expect(report).toContain('Errors: 2')
    expect(report).toContain('TS2304')
    expect(report).toContain('blocked by: typescript')
  })
})
