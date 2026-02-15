/**
 * Tests for Agent Recovery Module
 *
 * Covers: classifyError, isRetryableCategory, getStrategyForCategory,
 * calculateBackoffDelay, retryWithBackoff, RecoveryManager, createRecoveryManager
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { RecoveryPlan } from './planner.js'
import type { ErrorCategory, RetryOptions } from './recovery.js'
import {
  calculateBackoffDelay,
  classifyError,
  createRecoveryManager,
  getStrategyForCategory,
  isRetryableCategory,
  RecoveryManager,
  retryWithBackoff,
} from './recovery.js'
import { createMockStep, createMockToolCallInfo } from './test-helpers.js'

// ============================================================================
// Helpers
// ============================================================================

function makeRetryOptions(overrides?: Partial<RetryOptions>): RetryOptions {
  return {
    maxAttempts: 3,
    initialDelayMs: 1000,
    maxDelayMs: 30000,
    jitterFactor: 0,
    ...overrides,
  }
}

function makeRecoveryPlan(overrides?: Partial<RecoveryPlan>): RecoveryPlan {
  return {
    failedStep: createMockStep({ id: 'step-failed', status: 'failed' }),
    failureAnalysis: 'Test failure',
    strategy: 'retry',
    actions: ['retry the step'],
    goalRecoverable: true,
    ...overrides,
  }
}

// ============================================================================
// classifyError — string errors
// ============================================================================

describe('classifyError', () => {
  describe('string errors', () => {
    it('classifies "permission denied" as permission', () => {
      expect(classifyError('permission denied')).toBe('permission')
    })

    it('classifies "access denied" as permission', () => {
      expect(classifyError('Access Denied to resource')).toBe('permission')
    })

    it('classifies "not found" as not_found', () => {
      expect(classifyError('resource not found')).toBe('not_found')
    })

    it('classifies "no such file" as not_found', () => {
      expect(classifyError('no such file or directory')).toBe('not_found')
    })

    it('classifies "does not exist" as not_found', () => {
      expect(classifyError('path does not exist')).toBe('not_found')
    })

    it('classifies "timeout" as timeout', () => {
      expect(classifyError('request timeout')).toBe('timeout')
    })

    it('classifies "timed out" as timeout', () => {
      expect(classifyError('operation timed out after 30s')).toBe('timeout')
    })

    it('classifies "deadline" as timeout', () => {
      expect(classifyError('deadline exceeded')).toBe('timeout')
    })

    it('classifies "connection" as network', () => {
      expect(classifyError('connection refused')).toBe('network')
    })

    it('classifies "network" as network', () => {
      expect(classifyError('network error occurred')).toBe('network')
    })

    it('classifies "socket" as network', () => {
      expect(classifyError('socket hang up')).toBe('network')
    })

    it('classifies "fetch failed" as network', () => {
      expect(classifyError('fetch failed')).toBe('network')
    })

    it('classifies "invalid" as validation', () => {
      expect(classifyError('invalid input provided')).toBe('validation')
    })

    it('classifies "validation" as validation', () => {
      expect(classifyError('validation error: field required')).toBe('validation')
    })

    it('classifies "expected" as validation', () => {
      expect(classifyError('expected number but got string')).toBe('validation')
    })

    it('classifies "syntax" as syntax', () => {
      expect(classifyError('syntax error on line 42')).toBe('syntax')
    })

    it('classifies "parse" as syntax', () => {
      expect(classifyError('failed to parse JSON')).toBe('syntax')
    })

    it('classifies "unexpected token" as validation (contains "expected" which matches first)', () => {
      // NOTE: "unexpected token" contains "expected", which triggers the
      // validation check before the syntax check. This is a known ordering
      // quirk in classifyError. Use "syntax error" for syntax classification.
      expect(classifyError('unexpected token }')).toBe('validation')
    })

    it('classifies "out of memory" as resource', () => {
      expect(classifyError('out of memory')).toBe('resource')
    })

    it('classifies "disk full" as resource', () => {
      expect(classifyError('disk full')).toBe('resource')
    })

    it('classifies "no space" as resource', () => {
      expect(classifyError('no space left on device')).toBe('resource')
    })

    it('classifies "rate limit" as rate_limit', () => {
      expect(classifyError('rate limit exceeded')).toBe('rate_limit')
    })

    it('classifies "too many requests" as rate_limit', () => {
      expect(classifyError('too many requests')).toBe('rate_limit')
    })

    it('classifies "429" as rate_limit', () => {
      expect(classifyError('HTTP 429 response')).toBe('rate_limit')
    })

    it('classifies unknown strings as unknown', () => {
      expect(classifyError('something completely different happened')).toBe('unknown')
    })
  })

  // ============================================================================
  // classifyError — Error objects with messages
  // ============================================================================

  describe('Error objects with messages', () => {
    it('classifies Error with permission message', () => {
      expect(classifyError(new Error('permission denied'))).toBe('permission')
    })

    it('classifies Error with not found message', () => {
      expect(classifyError(new Error('file not found'))).toBe('not_found')
    })

    it('classifies Error with timeout message', () => {
      expect(classifyError(new Error('request timed out'))).toBe('timeout')
    })

    it('classifies Error with network message', () => {
      expect(classifyError(new Error('network failure'))).toBe('network')
    })

    it('classifies Error with rate limit message', () => {
      expect(classifyError(new Error('too many requests'))).toBe('rate_limit')
    })
  })

  // ============================================================================
  // classifyError — Error objects with code properties
  // ============================================================================

  describe('Error objects with code properties', () => {
    it('classifies EACCES as permission', () => {
      const err = Object.assign(new Error('op failed'), { code: 'EACCES' })
      expect(classifyError(err)).toBe('permission')
    })

    it('classifies EPERM as permission', () => {
      const err = Object.assign(new Error('op failed'), { code: 'EPERM' })
      expect(classifyError(err)).toBe('permission')
    })

    it('classifies ENOENT as not_found', () => {
      const err = Object.assign(new Error('op failed'), { code: 'ENOENT' })
      expect(classifyError(err)).toBe('not_found')
    })

    it('classifies ENOTDIR as not_found', () => {
      const err = Object.assign(new Error('op failed'), { code: 'ENOTDIR' })
      expect(classifyError(err)).toBe('not_found')
    })

    it('classifies ECONNRESET as network', () => {
      const err = Object.assign(new Error('op failed'), { code: 'ECONNRESET' })
      expect(classifyError(err)).toBe('network')
    })

    it('classifies ETIMEDOUT as network', () => {
      const err = Object.assign(new Error('op failed'), { code: 'ETIMEDOUT' })
      expect(classifyError(err)).toBe('network')
    })

    it('classifies EPIPE as network', () => {
      const err = Object.assign(new Error('op failed'), { code: 'EPIPE' })
      expect(classifyError(err)).toBe('network')
    })

    it('classifies ENOTFOUND as network', () => {
      const err = Object.assign(new Error('op failed'), { code: 'ENOTFOUND' })
      expect(classifyError(err)).toBe('network')
    })

    it('classifies EAI_AGAIN as network', () => {
      const err = Object.assign(new Error('op failed'), { code: 'EAI_AGAIN' })
      expect(classifyError(err)).toBe('network')
    })

    it('classifies ECONNREFUSED as network', () => {
      const err = Object.assign(new Error('op failed'), { code: 'ECONNREFUSED' })
      expect(classifyError(err)).toBe('network')
    })

    it('classifies ENETUNREACH as network', () => {
      const err = Object.assign(new Error('op failed'), { code: 'ENETUNREACH' })
      expect(classifyError(err)).toBe('network')
    })

    it('classifies EHOSTUNREACH as network', () => {
      const err = Object.assign(new Error('op failed'), { code: 'EHOSTUNREACH' })
      expect(classifyError(err)).toBe('network')
    })

    it('prioritizes error code over message pattern', () => {
      // Error code says EACCES (permission), message says "timed out" (timeout)
      // Code check runs first, so this should be permission
      const err = Object.assign(new Error('operation timed out'), { code: 'EACCES' })
      expect(classifyError(err)).toBe('permission')
    })
  })

  // ============================================================================
  // classifyError — Error objects with nested cause.code
  // ============================================================================

  describe('Error objects with nested cause.code', () => {
    it('extracts code from cause property', () => {
      const err = new Error('wrapper error', {
        cause: { code: 'ECONNREFUSED' },
      })
      expect(classifyError(err)).toBe('network')
    })

    it('extracts permission code from cause', () => {
      const err = new Error('wrapper error', {
        cause: { code: 'EACCES' },
      })
      expect(classifyError(err)).toBe('permission')
    })

    it('extracts not_found code from cause', () => {
      const err = new Error('wrapper error', {
        cause: { code: 'ENOENT' },
      })
      expect(classifyError(err)).toBe('not_found')
    })
  })

  // ============================================================================
  // classifyError — edge cases
  // ============================================================================

  describe('edge cases', () => {
    it('handles empty string as unknown', () => {
      expect(classifyError('')).toBe('unknown')
    })

    it('handles null as unknown', () => {
      expect(classifyError(null)).toBe('unknown')
    })

    it('handles undefined as unknown', () => {
      expect(classifyError(undefined)).toBe('unknown')
    })

    it('handles numbers via String() coercion', () => {
      // String(429) = "429", which matches rate_limit pattern
      expect(classifyError(429)).toBe('rate_limit')
    })

    it('handles plain objects via String() coercion', () => {
      // String({}) = "[object Object]" -> unknown
      expect(classifyError({})).toBe('unknown')
    })
  })
})

// ============================================================================
// isRetryableCategory
// ============================================================================

describe('isRetryableCategory', () => {
  const retryable: ErrorCategory[] = ['network', 'timeout', 'rate_limit']
  const nonRetryable: ErrorCategory[] = [
    'permission',
    'not_found',
    'validation',
    'syntax',
    'resource',
    'unknown',
  ]

  for (const category of retryable) {
    it(`returns true for ${category}`, () => {
      expect(isRetryableCategory(category)).toBe(true)
    })
  }

  for (const category of nonRetryable) {
    it(`returns false for ${category}`, () => {
      expect(isRetryableCategory(category)).toBe(false)
    })
  }
})

// ============================================================================
// getStrategyForCategory
// ============================================================================

describe('getStrategyForCategory', () => {
  const expected: [ErrorCategory, string][] = [
    ['network', 'retry'],
    ['timeout', 'retry'],
    ['rate_limit', 'retry'],
    ['permission', 'alternate'],
    ['not_found', 'decompose'],
    ['validation', 'alternate'],
    ['syntax', 'abort'],
    ['resource', 'abort'],
    ['unknown', 'retry'],
  ]

  for (const [category, strategy] of expected) {
    it(`maps ${category} to ${strategy}`, () => {
      expect(getStrategyForCategory(category)).toBe(strategy)
    })
  }
})

// ============================================================================
// calculateBackoffDelay
// ============================================================================

describe('calculateBackoffDelay', () => {
  it('returns base delay for attempt 1 with no jitter', () => {
    const options = makeRetryOptions({ initialDelayMs: 1000, jitterFactor: 0 })
    // delay = 1000 * 2^(1-1) = 1000 * 1 = 1000
    expect(calculateBackoffDelay(1, options)).toBe(1000)
  })

  it('doubles delay for each subsequent attempt', () => {
    const options = makeRetryOptions({ initialDelayMs: 1000, jitterFactor: 0 })
    // attempt 1: 1000, attempt 2: 2000, attempt 3: 4000
    expect(calculateBackoffDelay(2, options)).toBe(2000)
    expect(calculateBackoffDelay(3, options)).toBe(4000)
    expect(calculateBackoffDelay(4, options)).toBe(8000)
  })

  it('caps delay at maxDelayMs', () => {
    const options = makeRetryOptions({
      initialDelayMs: 1000,
      maxDelayMs: 5000,
      jitterFactor: 0,
    })
    // attempt 4: 1000 * 2^3 = 8000, capped to 5000
    expect(calculateBackoffDelay(4, options)).toBe(5000)
  })

  it('applies jitter within expected range', () => {
    const options = makeRetryOptions({
      initialDelayMs: 1000,
      jitterFactor: 0.3,
    })
    // base = 1000, jitter range = 1000 * 0.3 * [-1, 1] = [-300, 300]
    // result should be in [700, 1300]
    const results = new Set<number>()
    for (let i = 0; i < 100; i++) {
      const delay = calculateBackoffDelay(1, options)
      results.add(delay)
      expect(delay).toBeGreaterThanOrEqual(700)
      expect(delay).toBeLessThanOrEqual(1300)
    }
    // With 100 samples, we should see variation (not all the same value)
    expect(results.size).toBeGreaterThan(1)
  })

  it('never returns negative values', () => {
    const options = makeRetryOptions({
      initialDelayMs: 10,
      jitterFactor: 1.0,
    })
    // jitter can subtract up to 100% of base, Math.max(0, ...) should prevent negatives
    for (let i = 0; i < 50; i++) {
      expect(calculateBackoffDelay(1, options)).toBeGreaterThanOrEqual(0)
    }
  })
})

// ============================================================================
// retryWithBackoff
// ============================================================================

describe('retryWithBackoff', () => {
  beforeEach(() => {
    vi.useFakeTimers()
  })

  afterEach(() => {
    vi.useRealTimers()
  })

  it('returns result on first success', async () => {
    const fn = vi.fn().mockResolvedValue('ok')
    const promise = retryWithBackoff(fn, { maxAttempts: 3 })
    const result = await promise
    expect(result).toBe('ok')
    expect(fn).toHaveBeenCalledTimes(1)
  })

  it('retries on retryable errors and succeeds', async () => {
    const networkError = Object.assign(new Error('op failed'), { code: 'ECONNRESET' })
    const fn = vi.fn().mockRejectedValueOnce(networkError).mockResolvedValueOnce('recovered')

    const promise = retryWithBackoff(fn, {
      maxAttempts: 3,
      initialDelayMs: 100,
      jitterFactor: 0,
    })

    // Advance past the backoff delay for attempt 1
    await vi.advanceTimersByTimeAsync(200)

    const result = await promise
    expect(result).toBe('recovered')
    expect(fn).toHaveBeenCalledTimes(2)
  })

  it('throws immediately for non-retryable errors', async () => {
    const syntaxError = new Error('syntax error in code')
    const fn = vi.fn().mockRejectedValue(syntaxError)

    await expect(retryWithBackoff(fn, { maxAttempts: 3 })).rejects.toThrow('syntax error in code')
    expect(fn).toHaveBeenCalledTimes(1)
  })

  it('throws after exhausting all attempts', async () => {
    // Use real timers with minimal delay to avoid fake timer + promise rejection race
    vi.useRealTimers()

    const fn = vi.fn(async () => {
      throw new Error('network failure')
    })

    await expect(
      retryWithBackoff(fn, {
        maxAttempts: 2,
        initialDelayMs: 1,
        jitterFactor: 0,
      })
    ).rejects.toThrow('network failure')
    expect(fn).toHaveBeenCalledTimes(2)

    // Restore fake timers for the rest of the describe block
    vi.useFakeTimers()
  })

  it('calls onRetry callback on each retry', async () => {
    const networkError = Object.assign(new Error('fail'), { code: 'ECONNRESET' })
    const fn = vi.fn().mockRejectedValueOnce(networkError).mockResolvedValueOnce('ok')
    const onRetry = vi.fn()

    const promise = retryWithBackoff(fn, {
      maxAttempts: 3,
      initialDelayMs: 100,
      jitterFactor: 0,
      onRetry,
    })

    await vi.advanceTimersByTimeAsync(200)
    await promise

    expect(onRetry).toHaveBeenCalledTimes(1)
    expect(onRetry).toHaveBeenCalledWith(1, networkError, 100)
  })

  it('throws AbortError if signal is already aborted', async () => {
    const controller = new AbortController()
    controller.abort()
    const fn = vi.fn().mockResolvedValue('ok')

    await expect(retryWithBackoff(fn, { signal: controller.signal })).rejects.toThrow('Aborted')
    expect(fn).not.toHaveBeenCalled()
  })

  it('throws AbortError if signal is aborted during retry', async () => {
    // Use real timers to avoid fake timer + abort promise race
    vi.useRealTimers()

    const controller = new AbortController()
    const fn = vi.fn(async () => {
      const err = new Error('fail')
      ;(err as NodeJS.ErrnoException).code = 'ECONNRESET'
      throw err
    })

    const promise = retryWithBackoff(fn, {
      maxAttempts: 5,
      initialDelayMs: 500,
      jitterFactor: 0,
      signal: controller.signal,
    })

    // Let the first attempt fail and enter the sleep, then abort
    // Small wait to ensure we are inside the sleep
    await new Promise((r) => setTimeout(r, 50))
    controller.abort()

    await expect(promise).rejects.toThrow('Aborted')

    // Restore fake timers for the rest of the describe block
    vi.useFakeTimers()
  })
})

// ============================================================================
// RecoveryManager — construction and state
// ============================================================================

describe('RecoveryManager', () => {
  describe('constructor and initial state', () => {
    it('creates with default maxRetries of 3', () => {
      const manager = new RecoveryManager()
      const state = manager.getState()
      expect(state.executedSteps).toEqual([])
      expect(state.rolledBackSteps).toEqual([])
      expect(state.modifiedFiles).toEqual([])
      expect(state.hasSnapshot).toBe(false)
      expect(state.snapshotId).toBeUndefined()
    })

    it('creates with custom maxRetries', () => {
      const manager = new RecoveryManager(5)
      // Can retry up to 5 times
      const stepId = 'test-step'
      for (let i = 0; i < 5; i++) {
        expect(manager.canRetry(stepId)).toBe(true)
        manager.incrementRetry(stepId)
      }
      expect(manager.canRetry(stepId)).toBe(false)
    })
  })

  describe('recordStep', () => {
    it('adds step to executedSteps', () => {
      const manager = new RecoveryManager()
      const step = createMockStep({ id: 'step-1' })
      manager.recordStep(step)
      expect(manager.getState().executedSteps).toHaveLength(1)
      expect(manager.getState().executedSteps[0]).toBe(step)
    })

    it('records multiple steps', () => {
      const manager = new RecoveryManager()
      const step1 = createMockStep({ id: 'step-1' })
      const step2 = createMockStep({ id: 'step-2' })
      manager.recordStep(step1)
      manager.recordStep(step2)
      expect(manager.getState().executedSteps).toHaveLength(2)
    })

    it('extracts modified files from tool calls', () => {
      const manager = new RecoveryManager()
      const step = createMockStep({
        toolsCalled: [
          createMockToolCallInfo({
            name: 'write_file',
            args: { path: '/tmp/test.ts' },
          }),
          createMockToolCallInfo({
            name: 'edit',
            args: { file_path: '/src/index.ts' },
          }),
        ],
      })
      manager.recordStep(step)
      expect(manager.getState().modifiedFiles).toContain('/tmp/test.ts')
      expect(manager.getState().modifiedFiles).toContain('/src/index.ts')
    })

    it('ignores non-absolute file paths in args', () => {
      const manager = new RecoveryManager()
      const step = createMockStep({
        toolsCalled: [
          createMockToolCallInfo({
            name: 'read_file',
            args: { path: 'relative/path.ts' },
          }),
        ],
      })
      manager.recordStep(step)
      expect(manager.getState().modifiedFiles).toHaveLength(0)
    })
  })

  describe('getState', () => {
    it('returns readonly snapshot of state', () => {
      const manager = new RecoveryManager()
      const state1 = manager.getState()
      expect(state1.executedSteps).toEqual([])

      manager.recordStep(createMockStep())
      const state2 = manager.getState()
      expect(state2.executedSteps).toHaveLength(1)
    })
  })

  describe('canRetry and incrementRetry', () => {
    it('canRetry returns true when under maxRetries', () => {
      const manager = new RecoveryManager(2)
      expect(manager.canRetry('step-x')).toBe(true)
    })

    it('canRetry returns false when at maxRetries', () => {
      const manager = new RecoveryManager(2)
      manager.incrementRetry('step-x')
      manager.incrementRetry('step-x')
      expect(manager.canRetry('step-x')).toBe(false)
    })

    it('incrementRetry returns the new count', () => {
      const manager = new RecoveryManager()
      expect(manager.incrementRetry('s1')).toBe(1)
      expect(manager.incrementRetry('s1')).toBe(2)
      expect(manager.incrementRetry('s1')).toBe(3)
    })

    it('tracks retries independently per step id', () => {
      const manager = new RecoveryManager(2)
      manager.incrementRetry('step-a')
      manager.incrementRetry('step-a')
      manager.incrementRetry('step-b')

      expect(manager.canRetry('step-a')).toBe(false)
      expect(manager.canRetry('step-b')).toBe(true)
    })
  })

  describe('setSnapshot', () => {
    it('sets snapshot id and flag', () => {
      const manager = new RecoveryManager()
      manager.setSnapshot('snap-abc123')
      const state = manager.getState()
      expect(state.hasSnapshot).toBe(true)
      expect(state.snapshotId).toBe('snap-abc123')
    })
  })

  describe('reset', () => {
    it('clears all state and retry counters', () => {
      const manager = new RecoveryManager()

      // Build up state
      manager.recordStep(createMockStep({ id: 'step-1' }))
      manager.setSnapshot('snap-1')
      manager.incrementRetry('step-1')
      manager.incrementRetry('step-1')

      // Reset
      manager.reset()

      const state = manager.getState()
      expect(state.executedSteps).toEqual([])
      expect(state.rolledBackSteps).toEqual([])
      expect(state.modifiedFiles).toEqual([])
      expect(state.hasSnapshot).toBe(false)
      expect(state.snapshotId).toBeUndefined()

      // Retry counters should be cleared too
      expect(manager.canRetry('step-1')).toBe(true)
    })
  })

  // ============================================================================
  // RecoveryManager.executeRecovery
  // ============================================================================

  describe('executeRecovery', () => {
    beforeEach(() => {
      vi.useFakeTimers()
    })

    afterEach(() => {
      vi.useRealTimers()
    })

    it('handles retry strategy successfully', async () => {
      const manager = new RecoveryManager(3)
      const plan = makeRecoveryPlan({ strategy: 'retry' })

      const promise = manager.executeRecovery(plan)
      // Advance past backoff delay
      await vi.advanceTimersByTimeAsync(5000)

      const result = await promise
      expect(result.success).toBe(true)
      expect(result.action).toBe('retried')
    })

    it('handles retry strategy failure when max retries exceeded', async () => {
      const manager = new RecoveryManager(1)
      const failedStep = createMockStep({ id: 'step-exhaust' })
      const plan = makeRecoveryPlan({ strategy: 'retry', failedStep })

      // Exhaust the retry quota
      manager.incrementRetry('step-exhaust')

      const result = await manager.executeRecovery(plan)
      expect(result.success).toBe(false)
      expect(result.action).toBe('aborted')
      expect(result.error).toContain('Maximum retries')
    })

    it('handles skip strategy', async () => {
      const manager = new RecoveryManager()
      const failedStep = createMockStep({ id: 'step-skip', status: 'failed' })
      const plan = makeRecoveryPlan({ strategy: 'skip', failedStep })

      const result = await manager.executeRecovery(plan)
      expect(result.success).toBe(true)
      expect(result.action).toBe('skipped')
      // The step's status should be mutated to 'skipped'
      expect(failedStep.status).toBe('skipped')
    })

    it('handles rollback strategy', async () => {
      const manager = new RecoveryManager()
      const failedStep = createMockStep({ id: 'step-rollback' })
      const plan = makeRecoveryPlan({ strategy: 'rollback', failedStep })

      const result = await manager.executeRecovery(plan)
      expect(result.success).toBe(false)
      expect(result.action).toBe('rolled_back')
      expect(result.error).toContain('not yet implemented')
      expect(manager.getState().rolledBackSteps).toContain(failedStep)
    })

    it('handles alternate strategy with alternative steps', async () => {
      const manager = new RecoveryManager()
      const plan = makeRecoveryPlan({
        strategy: 'alternate',
        alternativeSteps: [
          {
            stepNumber: 1,
            description: 'Try a different approach',
            toolsRequired: ['read_file'],
            expectedOutput: 'file contents',
            dependsOn: [],
            complexity: 2,
          },
          {
            stepNumber: 2,
            description: 'Apply the fix',
            toolsRequired: ['edit'],
            expectedOutput: 'file modified',
            dependsOn: [1],
            complexity: 3,
          },
        ],
      })

      const result = await manager.executeRecovery(plan)
      expect(result.success).toBe(true)
      expect(result.action).toBe('alternated')
      expect(result.newSteps).toHaveLength(2)
      expect(result.newSteps![0].description).toBe('Try a different approach')
      expect(result.newSteps![0].id).toContain('alt-')
      expect(result.newSteps![1].status).toBe('pending')
    })

    it('handles alternate strategy without alternative steps', async () => {
      const manager = new RecoveryManager()
      const plan = makeRecoveryPlan({
        strategy: 'alternate',
        alternativeSteps: undefined,
      })

      const result = await manager.executeRecovery(plan)
      expect(result.success).toBe(false)
      expect(result.action).toBe('aborted')
      expect(result.error).toContain('No alternative steps')
    })

    it('handles alternate strategy with empty alternative steps array', async () => {
      const manager = new RecoveryManager()
      const plan = makeRecoveryPlan({
        strategy: 'alternate',
        alternativeSteps: [],
      })

      const result = await manager.executeRecovery(plan)
      expect(result.success).toBe(false)
      expect(result.action).toBe('aborted')
      expect(result.error).toContain('No alternative steps')
    })

    it('handles decompose strategy the same as alternate', async () => {
      const manager = new RecoveryManager()
      const plan = makeRecoveryPlan({
        strategy: 'decompose',
        alternativeSteps: [
          {
            stepNumber: 1,
            description: 'Sub-task A',
            toolsRequired: ['bash'],
            expectedOutput: 'done',
            dependsOn: [],
            complexity: 1,
          },
        ],
      })

      const result = await manager.executeRecovery(plan)
      expect(result.success).toBe(true)
      expect(result.action).toBe('alternated')
      expect(result.newSteps).toHaveLength(1)
    })

    it('handles abort strategy', async () => {
      const manager = new RecoveryManager()
      const plan = makeRecoveryPlan({ strategy: 'abort' })

      const result = await manager.executeRecovery(plan)
      expect(result.success).toBe(false)
      expect(result.action).toBe('aborted')
      expect(result.error).toContain('aborting execution')
    })

    it('handles unknown strategy gracefully', async () => {
      const manager = new RecoveryManager()
      const plan = makeRecoveryPlan({
        strategy: 'nonexistent' as RecoveryPlan['strategy'],
      })

      const result = await manager.executeRecovery(plan)
      expect(result.success).toBe(false)
      expect(result.action).toBe('aborted')
      expect(result.error).toContain('Unknown recovery strategy')
    })
  })
})

// ============================================================================
// createRecoveryManager factory
// ============================================================================

describe('createRecoveryManager', () => {
  it('creates a RecoveryManager instance', () => {
    const manager = createRecoveryManager()
    expect(manager).toBeInstanceOf(RecoveryManager)
  })

  it('creates with default maxRetries', () => {
    const manager = createRecoveryManager()
    // Default is 3 retries
    const id = 'factory-step'
    for (let i = 0; i < 3; i++) {
      expect(manager.canRetry(id)).toBe(true)
      manager.incrementRetry(id)
    }
    expect(manager.canRetry(id)).toBe(false)
  })

  it('creates with custom maxRetries', () => {
    const manager = createRecoveryManager(7)
    const id = 'factory-step'
    for (let i = 0; i < 7; i++) {
      expect(manager.canRetry(id)).toBe(true)
      manager.incrementRetry(id)
    }
    expect(manager.canRetry(id)).toBe(false)
  })
})
