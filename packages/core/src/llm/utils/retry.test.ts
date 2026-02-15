import { describe, expect, it, vi } from 'vitest'
import { calculateDelay, shouldRetry, withRetry } from './retry.js'

describe('shouldRetry', () => {
  it('retries on rate_limit', () => {
    expect(shouldRetry('rate_limit', 0, 3)).toBe(true)
  })

  it('retries on server error', () => {
    expect(shouldRetry('server', 0, 3)).toBe(true)
  })

  it('retries on network error', () => {
    expect(shouldRetry('network', 1, 3)).toBe(true)
  })

  it('does not retry on auth error', () => {
    expect(shouldRetry('auth', 0, 3)).toBe(false)
  })

  it('does not retry on unknown error', () => {
    expect(shouldRetry('unknown', 0, 3)).toBe(false)
  })

  it('does not retry on api error', () => {
    expect(shouldRetry('api', 0, 3)).toBe(false)
  })

  it('does not retry when max retries reached', () => {
    expect(shouldRetry('rate_limit', 3, 3)).toBe(false)
  })

  it('does not retry when past max retries', () => {
    expect(shouldRetry('server', 5, 3)).toBe(false)
  })
})

describe('calculateDelay', () => {
  it('honors Retry-After header', () => {
    const delay = calculateDelay(0, 30)
    expect(delay).toBe(30_000)
  })

  it('caps Retry-After at maxDelayMs', () => {
    const delay = calculateDelay(0, 120, { maxDelayMs: 60_000 })
    expect(delay).toBe(60_000)
  })

  it('uses exponential backoff for attempt 0', () => {
    const delay = calculateDelay(0, undefined, { baseDelayMs: 1000, maxDelayMs: 60_000 })
    // base * 2^0 = 1000, jitter adds 0-500, so range is [1000, 1500]
    expect(delay).toBeGreaterThanOrEqual(1000)
    expect(delay).toBeLessThanOrEqual(1500)
  })

  it('uses exponential backoff for attempt 1', () => {
    const delay = calculateDelay(1, undefined, { baseDelayMs: 1000, maxDelayMs: 60_000 })
    // base * 2^1 = 2000, jitter adds 0-1000, so range is [2000, 3000]
    expect(delay).toBeGreaterThanOrEqual(2000)
    expect(delay).toBeLessThanOrEqual(3000)
  })

  it('caps exponential backoff at maxDelayMs', () => {
    const delay = calculateDelay(10, undefined, { baseDelayMs: 1000, maxDelayMs: 5000 })
    expect(delay).toBeLessThanOrEqual(5000)
  })

  it('ignores zero Retry-After and uses backoff', () => {
    const delay = calculateDelay(0, 0, { baseDelayMs: 1000, maxDelayMs: 60_000 })
    // Retry-After of 0 should not trigger the retryAfter path
    expect(delay).toBeGreaterThanOrEqual(1000)
  })
})

describe('withRetry', () => {
  it('returns result on first success', async () => {
    const fn = vi.fn().mockResolvedValue('ok')
    const result = await withRetry(fn, { maxRetries: 3 })
    expect(result).toBe('ok')
    expect(fn).toHaveBeenCalledTimes(1)
  })

  it('retries on failure then succeeds', async () => {
    const fn = vi.fn().mockRejectedValueOnce(new Error('fail')).mockResolvedValue('ok')

    const result = await withRetry(fn, { maxRetries: 3, baseDelayMs: 1 })
    expect(result).toBe('ok')
    expect(fn).toHaveBeenCalledTimes(2)
  })

  it('throws after exhausting retries', async () => {
    const fn = vi.fn().mockRejectedValue(new Error('always fails'))

    await expect(withRetry(fn, { maxRetries: 2, baseDelayMs: 1 })).rejects.toThrow('always fails')
    // 1 initial + 2 retries = 3 calls
    expect(fn).toHaveBeenCalledTimes(3)
  })

  it('wraps non-Error throws', async () => {
    const fn = vi.fn().mockRejectedValue('string error')

    await expect(withRetry(fn, { maxRetries: 0 })).rejects.toThrow('string error')
  })
})
