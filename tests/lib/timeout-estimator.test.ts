/**
 * Timeout Estimator Tests
 */

import { describe, it, expect } from 'vitest'
import {
  estimateTimeout,
  quickEstimate,
  getTimeoutCategory,
  getBaseTimeout,
  formatTimeout,
  parseTimeoutString,
  getAllCategories,
  BASE_TIMEOUTS,
  DEFAULT_MIN_TIMEOUT,
  DEFAULT_MAX_TIMEOUT,
} from '../../src/lib/timeout-estimator.js'

describe('timeout-estimator', () => {
  describe('getTimeoutCategory', () => {
    it('should map RECON to scout', () => {
      expect(getTimeoutCategory('RECON')).toBe('scout')
      expect(getTimeoutCategory('recon')).toBe('scout')
    })

    it('should map SIGINT to intel', () => {
      expect(getTimeoutCategory('SIGINT')).toBe('intel')
    })

    it('should map oracles to oracle', () => {
      expect(getTimeoutCategory('CIPHER')).toBe('oracle')
      expect(getTimeoutCategory('VECTOR')).toBe('oracle')
      expect(getTimeoutCategory('PRISM')).toBe('oracle')
      expect(getTimeoutCategory('APEX')).toBe('oracle')
    })

    it('should map SENTINEL to validator', () => {
      expect(getTimeoutCategory('SENTINEL')).toBe('validator')
    })

    it('should default unknown agents to operator', () => {
      expect(getTimeoutCategory('UNKNOWN')).toBe('operator')
      expect(getTimeoutCategory('custom-agent')).toBe('operator')
    })
  })

  describe('getBaseTimeout', () => {
    it('should return correct base timeout for categories', () => {
      expect(getBaseTimeout('scout')).toBe(60_000)
      expect(getBaseTimeout('intel')).toBe(180_000)
      expect(getBaseTimeout('operator')).toBe(300_000)
      expect(getBaseTimeout('validator')).toBe(120_000)
      expect(getBaseTimeout('oracle')).toBe(90_000)
    })

    it('should work with agent names', () => {
      expect(getBaseTimeout('RECON')).toBe(60_000)
      expect(getBaseTimeout('SIGINT')).toBe(180_000)
    })
  })

  describe('estimateTimeout', () => {
    it('should return base timeout for simple prompts', () => {
      const estimate = estimateTimeout('operator', 'Fix the bug')
      expect(estimate.baseMs).toBe(300_000)
      expect(estimate.multiplier).toBe(1.0)
      expect(estimate.finalMs).toBe(300_000)
      expect(estimate.factors).toHaveLength(0)
    })

    it('should increase timeout for long prompts', () => {
      const longPrompt = 'word '.repeat(600) // >500 words
      const estimate = estimateTimeout('operator', longPrompt)
      expect(estimate.multiplier).toBeGreaterThan(1.0)
      expect(estimate.factors.some((f) => f.includes('long prompt'))).toBe(true)
    })

    it('should increase timeout for medium prompts', () => {
      const mediumPrompt = 'word '.repeat(300) // >200 words
      const estimate = estimateTimeout('operator', mediumPrompt)
      expect(estimate.multiplier).toBeGreaterThan(1.0)
      expect(estimate.factors.some((f) => f.includes('medium prompt'))).toBe(true)
    })

    it('should increase timeout for prompts with code blocks', () => {
      const promptWithCode = `Fix this:
\`\`\`typescript
const x = 1
\`\`\`
`
      const estimate = estimateTimeout('operator', promptWithCode)
      expect(estimate.factors.some((f) => f.includes('code block'))).toBe(true)
    })

    it('should increase timeout for prompts with multiple tasks', () => {
      const multiTaskPrompt = `
        1. First task
        2. Second task
        3. Third task
        4. Fourth task
      `
      const estimate = estimateTimeout('operator', multiTaskPrompt)
      expect(estimate.factors.some((f) => f.includes('numbered tasks'))).toBe(true)
    })

    it('should detect file operations', () => {
      const filePrompt = 'Update the file src/lib/config.ts'
      const estimate = estimateTimeout('operator', filePrompt)
      expect(estimate.factors).toContain('file operations')
    })

    it('should combine multiple complexity factors', () => {
      const complexPrompt = `
        Implement the following:
        1. Add authentication with JWT
        2. Add database integration
        3. Add API endpoints
        4. Add security middleware

        \`\`\`typescript
        // Example
        class AuthService {
          async validate(token: string) {
            // implementation
          }
        }
        \`\`\`
      `
      const estimate = estimateTimeout('operator', complexPrompt)
      expect(estimate.multiplier).toBeGreaterThan(1.5)
      expect(estimate.factors.length).toBeGreaterThan(1)
    })

    it('should respect max timeout bound', () => {
      const hugePrompt = 'word '.repeat(2000)
      const estimate = estimateTimeout('operator', hugePrompt, { maxTimeout: 400_000 })
      expect(estimate.finalMs).toBeLessThanOrEqual(400_000)
    })

    it('should respect min timeout bound', () => {
      const estimate = estimateTimeout('scout', 'Hi', { minTimeout: 100_000 })
      expect(estimate.finalMs).toBeGreaterThanOrEqual(100_000)
    })

    it('should allow base timeout override', () => {
      const estimate = estimateTimeout('operator', 'Fix bug', { baseTimeoutOverride: 500_000 })
      expect(estimate.baseMs).toBe(500_000)
    })

    it('should work with agent names', () => {
      const estimate = estimateTimeout('CIPHER', 'Analyze architecture')
      expect(estimate.baseMs).toBe(90_000) // oracle base
    })
  })

  describe('quickEstimate', () => {
    it('should return timeout in milliseconds', () => {
      const timeout = quickEstimate('RECON', 'Search codebase')
      expect(timeout).toBe(60_000) // scout base
    })
  })

  describe('formatTimeout', () => {
    it('should format seconds only', () => {
      expect(formatTimeout(30_000)).toBe('30s')
    })

    it('should format minutes only', () => {
      expect(formatTimeout(120_000)).toBe('2m')
    })

    it('should format minutes and seconds', () => {
      expect(formatTimeout(150_000)).toBe('2m 30s')
    })
  })

  describe('parseTimeoutString', () => {
    it('should parse seconds', () => {
      expect(parseTimeoutString('30s')).toBe(30_000)
      expect(parseTimeoutString('30S')).toBe(30_000)
    })

    it('should parse minutes', () => {
      expect(parseTimeoutString('5m')).toBe(300_000)
      expect(parseTimeoutString('5M')).toBe(300_000)
    })

    it('should parse combined format', () => {
      expect(parseTimeoutString('2m30s')).toBe(150_000)
      expect(parseTimeoutString('2m 30s')).toBe(150_000)
    })

    it('should return undefined for invalid strings', () => {
      expect(parseTimeoutString('invalid')).toBeUndefined()
      expect(parseTimeoutString('')).toBeUndefined()
    })
  })

  describe('getAllCategories', () => {
    it('should return all categories with info', () => {
      const categories = getAllCategories()
      expect(categories).toHaveLength(5)
      expect(categories.map((c) => c.category)).toContain('scout')
      expect(categories.map((c) => c.category)).toContain('operator')
      expect(categories[0]).toHaveProperty('baseMs')
      expect(categories[0]).toHaveProperty('formatted')
    })
  })

  describe('constants', () => {
    it('should have expected base timeouts', () => {
      expect(BASE_TIMEOUTS.scout).toBe(60_000)
      expect(BASE_TIMEOUTS.intel).toBe(180_000)
      expect(BASE_TIMEOUTS.operator).toBe(300_000)
      expect(BASE_TIMEOUTS.validator).toBe(120_000)
      expect(BASE_TIMEOUTS.oracle).toBe(90_000)
    })

    it('should have expected bounds', () => {
      expect(DEFAULT_MIN_TIMEOUT).toBe(30_000)
      expect(DEFAULT_MAX_TIMEOUT).toBe(600_000)
    })
  })
})
