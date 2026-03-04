import { describe, expect, it } from 'vitest'
import { percentile, summarizeSamples } from './benchmark-stats.js'

describe('benchmark stats', () => {
  it('computes percentile with nearest-rank strategy', () => {
    const values = [2, 8, 4, 6, 10]

    expect(percentile(values, 50)).toBe(6)
    expect(percentile(values, 95)).toBe(10)
    expect(percentile(values, 0)).toBe(2)
    expect(percentile(values, 100)).toBe(10)
  })

  it('summarizes samples with average and percentile markers', () => {
    const stats = summarizeSamples([1, 2, 3, 4, 5])

    expect(stats.iterations).toBe(5)
    expect(stats.min).toBe(1)
    expect(stats.max).toBe(5)
    expect(stats.p50).toBe(3)
    expect(stats.p95).toBe(5)
    expect(stats.mean).toBe(3)
  })

  it('throws when summarizing empty sample list', () => {
    expect(() => summarizeSamples([])).toThrow('Cannot summarize empty sample list')
  })
})
