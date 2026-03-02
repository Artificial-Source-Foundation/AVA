import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  getAllModelStatuses,
  getFallbackModel,
  getModelStatus,
  isModelAvailable,
  recordFailure,
  recordSuccess,
  resetAvailability,
} from './availability.js'

// Mock emitEvent to avoid needing global extension registries
vi.mock('@ava/core-v2/extensions', () => ({
  emitEvent: vi.fn(),
}))

// Mock createLogger to avoid needing global logger
vi.mock('@ava/core-v2/logger', () => ({
  createLogger: () => ({
    debug: vi.fn(),
    info: vi.fn(),
    warn: vi.fn(),
    error: vi.fn(),
  }),
}))

describe('Model Availability', () => {
  afterEach(() => resetAvailability())

  describe('recordSuccess', () => {
    it('marks model as available', () => {
      recordSuccess('anthropic', 'claude-sonnet', 200)
      const status = getModelStatus('anthropic', 'claude-sonnet')
      expect(status).toBeDefined()
      expect(status!.status).toBe('available')
      expect(status!.consecutiveFailures).toBe(0)
    })

    it('tracks average latency', () => {
      recordSuccess('anthropic', 'claude-sonnet', 100)
      recordSuccess('anthropic', 'claude-sonnet', 200)
      const status = getModelStatus('anthropic', 'claude-sonnet')
      // EMA: 100 * 0.8 + 200 * 0.2 = 120
      expect(status!.avgLatencyMs).toBe(120)
    })

    it('resets consecutive failures on success', () => {
      recordFailure('anthropic', 'claude-sonnet', 'timeout')
      recordFailure('anthropic', 'claude-sonnet', 'timeout')
      recordSuccess('anthropic', 'claude-sonnet', 150)
      const status = getModelStatus('anthropic', 'claude-sonnet')
      expect(status!.consecutiveFailures).toBe(0)
      expect(status!.status).toBe('available')
    })

    it('sets initial latency on first success', () => {
      recordSuccess('openai', 'gpt-4o', 300)
      const status = getModelStatus('openai', 'gpt-4o')
      expect(status!.avgLatencyMs).toBe(300)
    })
  })

  describe('recordFailure', () => {
    it('marks model as degraded after 1 failure', () => {
      recordFailure('anthropic', 'claude-sonnet', 'rate limit')
      const status = getModelStatus('anthropic', 'claude-sonnet')
      expect(status!.status).toBe('degraded')
      expect(status!.consecutiveFailures).toBe(1)
      expect(status!.lastError).toBe('rate limit')
    })

    it('marks model as degraded after 2 failures', () => {
      recordFailure('anthropic', 'claude-sonnet', 'error 1')
      recordFailure('anthropic', 'claude-sonnet', 'error 2')
      const status = getModelStatus('anthropic', 'claude-sonnet')
      expect(status!.status).toBe('degraded')
      expect(status!.consecutiveFailures).toBe(2)
    })

    it('marks model as unavailable after 3 failures', () => {
      recordFailure('anthropic', 'claude-sonnet', 'error 1')
      recordFailure('anthropic', 'claude-sonnet', 'error 2')
      recordFailure('anthropic', 'claude-sonnet', 'error 3')
      const status = getModelStatus('anthropic', 'claude-sonnet')
      expect(status!.status).toBe('unavailable')
      expect(status!.consecutiveFailures).toBe(3)
    })

    it('preserves average latency from prior successes', () => {
      recordSuccess('anthropic', 'claude-sonnet', 200)
      recordFailure('anthropic', 'claude-sonnet', 'timeout')
      const status = getModelStatus('anthropic', 'claude-sonnet')
      expect(status!.avgLatencyMs).toBe(200)
    })
  })

  describe('isModelAvailable', () => {
    it('returns true for unknown models', () => {
      expect(isModelAvailable('anthropic', 'unknown-model')).toBe(true)
    })

    it('returns true for available models', () => {
      recordSuccess('anthropic', 'claude-sonnet', 100)
      expect(isModelAvailable('anthropic', 'claude-sonnet')).toBe(true)
    })

    it('returns true for degraded models', () => {
      recordFailure('anthropic', 'claude-sonnet', 'error')
      expect(isModelAvailable('anthropic', 'claude-sonnet')).toBe(true)
    })

    it('returns false for unavailable models', () => {
      recordFailure('anthropic', 'claude-sonnet', 'e1')
      recordFailure('anthropic', 'claude-sonnet', 'e2')
      recordFailure('anthropic', 'claude-sonnet', 'e3')
      expect(isModelAvailable('anthropic', 'claude-sonnet')).toBe(false)
    })
  })

  describe('getFallbackModel', () => {
    it('returns fallback for unavailable anthropic claude-opus', () => {
      // Mark claude-opus as unavailable
      recordFailure('anthropic', 'claude-opus-4', 'e1')
      recordFailure('anthropic', 'claude-opus-4', 'e2')
      recordFailure('anthropic', 'claude-opus-4', 'e3')

      const fallback = getFallbackModel('anthropic', 'claude-opus-4')
      expect(fallback).toBeDefined()
      expect(fallback!.model).toBe('claude-sonnet-4-20250514')
    })

    it('skips unavailable fallbacks', () => {
      // Mark claude-opus and first fallback as unavailable
      recordFailure('anthropic', 'claude-opus-4', 'e1')
      recordFailure('anthropic', 'claude-opus-4', 'e2')
      recordFailure('anthropic', 'claude-opus-4', 'e3')
      recordFailure('anthropic', 'claude-sonnet-4-20250514', 'e1')
      recordFailure('anthropic', 'claude-sonnet-4-20250514', 'e2')
      recordFailure('anthropic', 'claude-sonnet-4-20250514', 'e3')

      const fallback = getFallbackModel('anthropic', 'claude-opus-4')
      expect(fallback).toBeDefined()
      expect(fallback!.model).toBe('claude-haiku-4-5-20251001')
    })

    it('returns undefined when all fallbacks unavailable', () => {
      recordFailure('anthropic', 'claude-opus-4', 'e1')
      recordFailure('anthropic', 'claude-opus-4', 'e2')
      recordFailure('anthropic', 'claude-opus-4', 'e3')
      recordFailure('anthropic', 'claude-sonnet-4-20250514', 'e1')
      recordFailure('anthropic', 'claude-sonnet-4-20250514', 'e2')
      recordFailure('anthropic', 'claude-sonnet-4-20250514', 'e3')
      recordFailure('anthropic', 'claude-haiku-4-5-20251001', 'e1')
      recordFailure('anthropic', 'claude-haiku-4-5-20251001', 'e2')
      recordFailure('anthropic', 'claude-haiku-4-5-20251001', 'e3')

      const fallback = getFallbackModel('anthropic', 'claude-opus-4')
      expect(fallback).toBeUndefined()
    })

    it('returns undefined for unknown provider/model combo', () => {
      const fallback = getFallbackModel('unknown-provider', 'unknown-model')
      expect(fallback).toBeUndefined()
    })

    it('returns fallback for openai gpt-4o', () => {
      recordFailure('openai', 'gpt-4o', 'e1')
      recordFailure('openai', 'gpt-4o', 'e2')
      recordFailure('openai', 'gpt-4o', 'e3')

      const fallback = getFallbackModel('openai', 'gpt-4o')
      expect(fallback).toBeDefined()
      expect(fallback!.model).toBe('gpt-4o-mini')
    })

    it('returns fallback for openrouter anthropic/claude-opus', () => {
      recordFailure('openrouter', 'anthropic/claude-opus-4', 'e1')
      recordFailure('openrouter', 'anthropic/claude-opus-4', 'e2')
      recordFailure('openrouter', 'anthropic/claude-opus-4', 'e3')

      const fallback = getFallbackModel('openrouter', 'anthropic/claude-opus-4')
      expect(fallback).toBeDefined()
      expect(fallback!.model).toBe('anthropic/claude-sonnet-4')
    })
  })

  describe('getAllModelStatuses', () => {
    it('returns empty array when no records', () => {
      expect(getAllModelStatuses()).toEqual([])
    })

    it('returns all tracked models', () => {
      recordSuccess('anthropic', 'claude-sonnet', 100)
      recordFailure('openai', 'gpt-4o', 'error')
      const statuses = getAllModelStatuses()
      expect(statuses).toHaveLength(2)
      const providers = statuses.map((s) => s.provider)
      expect(providers).toContain('anthropic')
      expect(providers).toContain('openai')
    })
  })

  describe('resetAvailability', () => {
    it('clears all health records', () => {
      recordSuccess('anthropic', 'claude-sonnet', 100)
      recordFailure('openai', 'gpt-4o', 'error')
      resetAvailability()
      expect(getAllModelStatuses()).toEqual([])
      expect(getModelStatus('anthropic', 'claude-sonnet')).toBeUndefined()
    })
  })
})
