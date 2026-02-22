/**
 * Tests for the validate command
 */

import { SimpleValidatorRegistry, ValidationPipeline } from '@ava/core'
import { describe, expect, it } from 'vitest'

describe('validate command', () => {
  describe('validator registry', () => {
    it('should create a registry and register validators', () => {
      const registry = new SimpleValidatorRegistry()
      const makeValidator = (name: string) => ({
        name,
        description: `${name} validator`,
        critical: true,
        async run() {
          return { validator: name, passed: true, errors: [], warnings: [], durationMs: 0 }
        },
      })

      registry.register(makeValidator('syntax'))
      registry.register(makeValidator('typescript'))
      registry.register(makeValidator('lint'))

      expect(registry.has('syntax')).toBe(true)
      expect(registry.has('typescript')).toBe(true)
      expect(registry.has('lint')).toBe(true)
      expect(registry.getAll().length).toBe(3)
    })

    it('should create a pipeline from registry', () => {
      const registry = new SimpleValidatorRegistry()
      registry.register({
        name: 'syntax',
        description: 'Syntax check',
        critical: true,
        async run() {
          return { validator: 'syntax', passed: true, errors: [], warnings: [], durationMs: 0 }
        },
      })

      const pipeline = new ValidationPipeline(registry)
      expect(pipeline).toBeDefined()
    })
  })

  describe('validation pipeline', () => {
    it('should run a custom validator through the pipeline', async () => {
      const registry = new SimpleValidatorRegistry()

      // Register a simple always-pass validator
      registry.register({
        name: 'syntax',
        description: 'Test validator',
        critical: true,
        async run() {
          return {
            validator: 'syntax',
            passed: true,
            errors: [],
            warnings: [],
            durationMs: 1,
          }
        },
      })

      const pipeline = new ValidationPipeline(registry)
      const ac = new AbortController()

      const result = await pipeline.run(
        ['test-file.ts'],
        { enabledValidators: ['syntax'] },
        ac.signal,
        process.cwd()
      )

      expect(result.passed).toBe(true)
      expect(result.results.length).toBe(1)
      expect(result.results[0].validator).toBe('syntax')
    })

    it('should report failures from validators', async () => {
      const registry = new SimpleValidatorRegistry()

      registry.register({
        name: 'syntax',
        description: 'Always-fail validator',
        critical: true,
        async run() {
          return {
            validator: 'syntax',
            passed: false,
            errors: ['Syntax error on line 1'],
            warnings: [],
            durationMs: 1,
          }
        },
      })

      const pipeline = new ValidationPipeline(registry)
      const ac = new AbortController()

      const result = await pipeline.run(
        ['bad-file.ts'],
        { enabledValidators: ['syntax'] },
        ac.signal,
        process.cwd()
      )

      expect(result.passed).toBe(false)
      expect(result.summary.failed).toBe(1)
      expect(result.results[0].errors).toContain('Syntax error on line 1')
    })

    it('should format a report from results', () => {
      const registry = new SimpleValidatorRegistry()
      const pipeline = new ValidationPipeline(registry)

      const report = pipeline.formatReport({
        passed: true,
        results: [
          {
            validator: 'syntax',
            passed: true,
            errors: [],
            warnings: [],
            durationMs: 42,
          },
        ],
        totalDurationMs: 42,
        summary: {
          total: 1,
          passed: 1,
          failed: 0,
          totalErrors: 0,
          totalWarnings: 0,
        },
      })

      expect(report).toContain('PASSED')
      expect(report).toContain('syntax')
    })

    it('should handle missing validators gracefully', async () => {
      const registry = new SimpleValidatorRegistry()
      const pipeline = new ValidationPipeline(registry)
      const ac = new AbortController()

      const result = await pipeline.run(
        ['/home/xn6/Projects/ASF/AVA/packages/core/src/agent/types.ts'],
        { enabledValidators: ['syntax'] },
        ac.signal,
        '/home/xn6/Projects/ASF/AVA'
      )

      // Should pass with a warning about missing validator
      expect(result.passed).toBe(true)
      expect(result.results[0].warnings.length).toBeGreaterThan(0)
    })
  })
})
