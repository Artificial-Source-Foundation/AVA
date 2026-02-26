import { afterEach, describe, expect, it } from 'vitest'
import {
  createFailedResult,
  createPassedResult,
  formatReport,
  getAllValidators,
  registerValidator,
  resetValidators,
  runPipeline,
} from './pipeline.js'
import type { Validator, ValidatorName } from './types.js'
import { DEFAULT_VALIDATOR_CONFIG } from './types.js'

function makeValidator(
  name: string,
  critical: boolean,
  passed: boolean,
  durationMs = 10
): Validator {
  return {
    name: name as Validator['name'],
    description: `Test ${name} validator`,
    critical,
    async run(): Promise<ReturnType<Validator['run']>> {
      return {
        validator: name,
        passed,
        errors: passed ? [] : [`${name} failed`],
        warnings: [],
        durationMs,
      }
    },
  }
}

describe('Validator Pipeline', () => {
  afterEach(() => resetValidators())

  describe('Registry', () => {
    it('registers and retrieves validators', () => {
      registerValidator(makeValidator('syntax', true, true))
      expect(getAllValidators()).toHaveLength(1)
    })

    it('resets registry', () => {
      registerValidator(makeValidator('syntax', true, true))
      resetValidators()
      expect(getAllValidators()).toHaveLength(0)
    })
  })

  describe('runPipeline', () => {
    it('runs all passing validators', async () => {
      registerValidator(makeValidator('syntax', true, true))
      registerValidator(makeValidator('typescript', true, true))

      const result = await runPipeline(
        ['/tmp/a.ts'],
        {
          ...DEFAULT_VALIDATOR_CONFIG,
          enabledValidators: ['syntax', 'typescript'] as ValidatorName[],
        },
        AbortSignal.timeout(5000),
        '/tmp'
      )

      expect(result.passed).toBe(true)
      expect(result.results).toHaveLength(2)
      expect(result.summary.passed).toBe(2)
      expect(result.summary.failed).toBe(0)
    })

    it('stops on critical failure with failFast', async () => {
      registerValidator(makeValidator('syntax', true, false)) // critical, failing
      registerValidator(makeValidator('lint', false, true))

      const result = await runPipeline(
        ['/tmp/a.ts'],
        {
          ...DEFAULT_VALIDATOR_CONFIG,
          enabledValidators: ['syntax', 'lint'] as ValidatorName[],
          failFast: true,
        },
        AbortSignal.timeout(5000),
        '/tmp'
      )

      expect(result.passed).toBe(false)
      expect(result.blockedBy).toBe('syntax')
      expect(result.results).toHaveLength(1) // lint never ran
    })

    it('continues on non-critical failure', async () => {
      registerValidator(makeValidator('lint', false, false)) // non-critical
      registerValidator(makeValidator('syntax', true, true))

      const result = await runPipeline(
        ['/tmp/a.ts'],
        { ...DEFAULT_VALIDATOR_CONFIG, enabledValidators: ['lint', 'syntax'] as ValidatorName[] },
        AbortSignal.timeout(5000),
        '/tmp'
      )

      expect(result.results).toHaveLength(2) // both ran
    })

    it('skips unregistered validators', async () => {
      const result = await runPipeline(
        ['/tmp/a.ts'],
        { ...DEFAULT_VALIDATOR_CONFIG, enabledValidators: ['nonexistent'] as ValidatorName[] },
        AbortSignal.timeout(5000),
        '/tmp'
      )

      expect(result.results).toHaveLength(1)
      expect(result.results[0]?.warnings[0]).toContain('not registered')
    })

    it('respects canRun check', async () => {
      const v = makeValidator('typescript', true, true)
      v.canRun = async () => false
      registerValidator(v)

      const result = await runPipeline(
        ['/tmp/a.ts'],
        { ...DEFAULT_VALIDATOR_CONFIG, enabledValidators: ['typescript'] as ValidatorName[] },
        AbortSignal.timeout(5000),
        '/tmp'
      )

      expect(result.results[0]?.warnings[0]).toContain('skipped')
    })

    it('handles abort signal', async () => {
      const controller = new AbortController()
      controller.abort()

      registerValidator(makeValidator('syntax', true, true))

      const result = await runPipeline(
        ['/tmp/a.ts'],
        { ...DEFAULT_VALIDATOR_CONFIG, enabledValidators: ['syntax'] as ValidatorName[] },
        controller.signal,
        '/tmp'
      )

      expect(result.aborted).toBe(true)
      expect(result.results).toHaveLength(0)
    })
  })

  describe('Helpers', () => {
    it('creates passed result', () => {
      const r = createPassedResult('syntax', 50, ['minor issue'])
      expect(r.passed).toBe(true)
      expect(r.warnings).toEqual(['minor issue'])
    })

    it('creates failed result', () => {
      const r = createFailedResult('syntax', 50, ['fatal error'])
      expect(r.passed).toBe(false)
      expect(r.errors).toEqual(['fatal error'])
    })
  })

  describe('formatReport', () => {
    it('formats passing report', () => {
      const report = formatReport({
        passed: true,
        results: [{ validator: 'syntax', passed: true, errors: [], warnings: [], durationMs: 10 }],
        totalDurationMs: 10,
        summary: { total: 1, passed: 1, failed: 0, totalErrors: 0, totalWarnings: 0 },
      })
      expect(report).toContain('PASSED')
      expect(report).toContain('[PASS] syntax')
    })

    it('formats failing report', () => {
      const report = formatReport({
        passed: false,
        results: [
          {
            validator: 'syntax',
            passed: false,
            errors: ['bad code'],
            warnings: [],
            durationMs: 10,
          },
        ],
        totalDurationMs: 10,
        blockedBy: 'syntax',
        summary: { total: 1, passed: 0, failed: 1, totalErrors: 1, totalWarnings: 0 },
      })
      expect(report).toContain('FAILED')
      expect(report).toContain('[FAIL] syntax')
      expect(report).toContain('bad code')
      expect(report).toContain('Blocked by: syntax')
    })
  })
})
