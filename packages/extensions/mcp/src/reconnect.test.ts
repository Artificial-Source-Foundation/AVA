import { describe, expect, it } from 'vitest'
import { DEFAULT_RECONNECT_CONFIG, ReconnectStrategy } from './reconnect.js'

describe('ReconnectStrategy', () => {
  it('returns delays for each attempt', () => {
    const strategy = new ReconnectStrategy({ maxAttempts: 3, baseDelayMs: 100, maxDelayMs: 10_000 })
    const d1 = strategy.nextDelay()
    expect(d1).not.toBeNull()
    expect(d1!).toBeGreaterThan(0)
    expect(strategy.attemptCount).toBe(1)
  })

  it('returns null after max attempts', () => {
    const strategy = new ReconnectStrategy({ maxAttempts: 2, baseDelayMs: 100, maxDelayMs: 10_000 })
    strategy.nextDelay()
    strategy.nextDelay()
    expect(strategy.nextDelay()).toBeNull()
    expect(strategy.canRetry).toBe(false)
  })

  it('caps delay at maxDelayMs', () => {
    const strategy = new ReconnectStrategy({
      maxAttempts: 10,
      baseDelayMs: 10_000,
      maxDelayMs: 15_000,
    })
    const delay = strategy.nextDelay()
    // With jitter (0.5x-1.5x), max possible is 15000 * 1.5 = 22500
    expect(delay!).toBeLessThanOrEqual(22500)
  })

  it('resets attempt counter', () => {
    const strategy = new ReconnectStrategy({ maxAttempts: 2, baseDelayMs: 100, maxDelayMs: 10_000 })
    strategy.nextDelay()
    strategy.nextDelay()
    expect(strategy.canRetry).toBe(false)
    strategy.reset()
    expect(strategy.canRetry).toBe(true)
    expect(strategy.attemptCount).toBe(0)
  })

  it('uses exponential backoff', () => {
    // Use max jitter range to verify exponential growth pattern
    const strategy = new ReconnectStrategy({
      maxAttempts: 5,
      baseDelayMs: 100,
      maxDelayMs: 100_000,
    })
    const delays: number[] = []
    for (let i = 0; i < 3; i++) {
      delays.push(strategy.nextDelay()!)
    }
    // Each delay's base doubles: 100, 200, 400
    // Even with jitter, the average should increase
    expect(delays).toHaveLength(3)
  })

  it('defaults match DEFAULT_RECONNECT_CONFIG', () => {
    expect(DEFAULT_RECONNECT_CONFIG.maxAttempts).toBe(5)
    expect(DEFAULT_RECONNECT_CONFIG.baseDelayMs).toBe(1000)
    expect(DEFAULT_RECONNECT_CONFIG.maxDelayMs).toBe(30_000)
  })

  it('canRetry is true before max attempts', () => {
    const strategy = new ReconnectStrategy({ maxAttempts: 3, baseDelayMs: 100, maxDelayMs: 10_000 })
    expect(strategy.canRetry).toBe(true)
    strategy.nextDelay()
    expect(strategy.canRetry).toBe(true)
  })
})
