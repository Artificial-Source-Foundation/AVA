import { afterEach, describe, expect, it } from 'vitest'

import {
  getModelPricing,
  getSessionCost,
  registerModelPricing,
  resetPricingRegistry,
  resetSessionCost,
  trackSessionCost,
} from './cost-tracker.js'

describe('cost tracker', () => {
  afterEach(() => {
    resetPricingRegistry()
    resetSessionCost()
  })

  it('registers and reads model pricing', () => {
    registerModelPricing('anthropic', 'claude-sonnet-4', { inputPer1M: 3, outputPer1M: 15 })
    expect(getModelPricing('anthropic', 'claude-sonnet-4')).toEqual({
      inputPer1M: 3,
      outputPer1M: 15,
    })
  })

  it('tracks zero cost when pricing is missing', () => {
    const stats = trackSessionCost('s1', 'anthropic', 'unknown-model', 1000, 2000)
    expect(stats.totalCostUsd).toBe(0)
    expect(stats.totalTokens).toBe(3000)
  })

  it('computes cost using model pricing', () => {
    registerModelPricing('anthropic', 'claude-sonnet-4', { inputPer1M: 3, outputPer1M: 15 })
    const stats = trackSessionCost('s1', 'anthropic', 'claude-sonnet-4', 100_000, 20_000)
    expect(stats.totalCostUsd).toBeCloseTo(0.6, 6)
    expect(stats.totalTurns).toBe(1)
  })

  it('accumulates per-session and per-model cost across turns', () => {
    registerModelPricing('anthropic', 'claude-sonnet-4', { inputPer1M: 3, outputPer1M: 15 })
    trackSessionCost('s1', 'anthropic', 'claude-sonnet-4', 100_000, 20_000)
    const stats = trackSessionCost('s1', 'anthropic', 'claude-sonnet-4', 200_000, 10_000)
    expect(stats.totalTurns).toBe(2)
    expect(stats.totalTokens).toBe(330_000)
    expect(stats.totalCostUsd).toBeCloseTo(1.35, 6)
    expect(stats.byModel['anthropic:claude-sonnet-4']?.turns).toBe(2)
  })

  it('tracks separate sessions independently', () => {
    registerModelPricing('anthropic', 'claude-haiku-4-5', { inputPer1M: 0.8, outputPer1M: 4 })
    trackSessionCost('s1', 'anthropic', 'claude-haiku-4-5', 100_000, 100_000)
    trackSessionCost('s2', 'anthropic', 'claude-haiku-4-5', 100_000, 0)
    expect(getSessionCost('s1')?.totalCostUsd).toBeCloseTo(0.48, 6)
    expect(getSessionCost('s2')?.totalCostUsd).toBeCloseTo(0.08, 6)
  })

  it('resets single session or all sessions', () => {
    registerModelPricing('anthropic', 'claude-haiku-4-5', { inputPer1M: 1, outputPer1M: 1 })
    trackSessionCost('s1', 'anthropic', 'claude-haiku-4-5', 10_000, 10_000)
    trackSessionCost('s2', 'anthropic', 'claude-haiku-4-5', 10_000, 10_000)
    resetSessionCost('s1')
    expect(getSessionCost('s1')).toBeNull()
    expect(getSessionCost('s2')).not.toBeNull()

    resetSessionCost()
    expect(getSessionCost('s2')).toBeNull()
  })
})
