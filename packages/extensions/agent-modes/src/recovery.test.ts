import { describe, expect, it } from 'vitest'
import {
  calculateBackoff,
  classifyError,
  isRetryable,
  retryWithBackoff,
  suggestStrategy,
} from './recovery.js'

describe('Recovery', () => {
  describe('classifyError', () => {
    it('classifies permission errors', () => {
      expect(classifyError('EACCES: permission denied')).toBe('permission')
    })

    it('classifies not found errors', () => {
      expect(classifyError('ENOENT: no such file')).toBe('not_found')
    })

    it('classifies timeout errors', () => {
      expect(classifyError('Operation timed out')).toBe('timeout')
    })

    it('classifies network errors', () => {
      expect(classifyError('ECONNRESET')).toBe('network')
    })

    it('classifies rate limit errors', () => {
      expect(classifyError('429 too many requests')).toBe('rate_limit')
    })

    it('classifies syntax errors', () => {
      expect(classifyError('SyntaxError: unexpected token')).toBe('syntax')
    })

    it('classifies resource errors', () => {
      expect(classifyError('out of memory')).toBe('resource')
    })

    it('returns unknown for unrecognized errors', () => {
      expect(classifyError('something weird happened')).toBe('unknown')
    })
  })

  describe('suggestStrategy', () => {
    it('suggests retry for transient errors', () => {
      expect(suggestStrategy('ECONNRESET')).toBe('retry')
      expect(suggestStrategy('timed out')).toBe('retry')
      expect(suggestStrategy('429 rate limit')).toBe('retry')
    })

    it('suggests alternate for permission errors', () => {
      expect(suggestStrategy('EACCES')).toBe('alternate')
    })

    it('suggests abort for fatal errors', () => {
      expect(suggestStrategy('SyntaxError')).toBe('abort')
      expect(suggestStrategy('out of memory')).toBe('abort')
    })
  })

  describe('isRetryable', () => {
    it('returns true for transient errors', () => {
      expect(isRetryable('network error')).toBe(true)
      expect(isRetryable('timeout')).toBe(true)
    })

    it('returns false for fatal errors', () => {
      expect(isRetryable('SyntaxError')).toBe(false)
      expect(isRetryable('EACCES')).toBe(false)
    })
  })

  describe('calculateBackoff', () => {
    it('increases exponentially', () => {
      const d1 = calculateBackoff(1, { jitterFactor: 0 })
      const d2 = calculateBackoff(2, { jitterFactor: 0 })
      const d3 = calculateBackoff(3, { jitterFactor: 0 })
      expect(d1).toBe(1000)
      expect(d2).toBe(2000)
      expect(d3).toBe(4000)
    })

    it('caps at maxDelay', () => {
      const d = calculateBackoff(20, { jitterFactor: 0, maxDelayMs: 5000 })
      expect(d).toBe(5000)
    })
  })

  describe('retryWithBackoff', () => {
    it('returns on first success', async () => {
      let calls = 0
      const result = await retryWithBackoff(async () => {
        calls++
        return 'ok'
      })
      expect(result).toBe('ok')
      expect(calls).toBe(1)
    })

    it('retries on failure then succeeds', async () => {
      let calls = 0
      const result = await retryWithBackoff(
        async () => {
          calls++
          if (calls < 3) throw new Error('fail')
          return 'ok'
        },
        { maxAttempts: 3, initialDelayMs: 1 }
      )
      expect(result).toBe('ok')
      expect(calls).toBe(3)
    })

    it('throws after max attempts', async () => {
      await expect(
        retryWithBackoff(
          async () => {
            throw new Error('always fails')
          },
          { maxAttempts: 2, initialDelayMs: 1 }
        )
      ).rejects.toThrow('always fails')
    })
  })
})
