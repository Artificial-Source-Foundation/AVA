import { beforeEach, describe, expect, it } from 'vitest'

import {
  registerModelPricing,
  resetPricingRegistry,
  resetSessionCost,
  trackSessionCost,
} from '../../context/src/cost-tracker.js'
import { sessionCostTool } from './session-cost.js'

function ctx(sessionId = 's1') {
  return {
    sessionId,
    workingDirectory: '/repo',
    signal: AbortSignal.timeout(5000),
  }
}

describe('session_cost tool', () => {
  beforeEach(() => {
    resetPricingRegistry()
    resetSessionCost()
  })

  it('returns error when no session cost exists', async () => {
    const result = await sessionCostTool.execute({}, ctx())
    expect(result.success).toBe(false)
    expect(result.error).toContain('No session cost data')
  })

  it('reports current session cost using context session id', async () => {
    registerModelPricing('anthropic', 'claude-sonnet-4', { inputPer1M: 3, outputPer1M: 15 })
    trackSessionCost('s1', 'anthropic', 'claude-sonnet-4', 100_000, 20_000)

    const result = await sessionCostTool.execute({}, ctx('s1'))
    expect(result.success).toBe(true)
    expect(result.output).toContain('Session: s1')
    expect(result.output).toContain('Total cost (USD): 0.600000')
  })

  it('supports querying a specific sessionId', async () => {
    registerModelPricing('anthropic', 'claude-haiku-4-5', { inputPer1M: 1, outputPer1M: 5 })
    trackSessionCost('sA', 'anthropic', 'claude-haiku-4-5', 100_000, 0)
    trackSessionCost('sB', 'anthropic', 'claude-haiku-4-5', 200_000, 0)

    const result = await sessionCostTool.execute({ sessionId: 'sB' }, ctx('sA'))
    expect(result.success).toBe(true)
    expect(result.output).toContain('Session: sB')
    expect(result.output).toContain('Input tokens: 200000')
  })

  it('lists per-model breakdown', async () => {
    registerModelPricing('anthropic', 'claude-sonnet-4', { inputPer1M: 3, outputPer1M: 15 })
    registerModelPricing('openai', 'gpt-4.1-mini', { inputPer1M: 0.8, outputPer1M: 3.2 })
    trackSessionCost('s1', 'anthropic', 'claude-sonnet-4', 100_000, 10_000)
    trackSessionCost('s1', 'openai', 'gpt-4.1-mini', 50_000, 5_000)

    const result = await sessionCostTool.execute({ sessionId: 's1' }, ctx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('anthropic/claude-sonnet-4')
    expect(result.output).toContain('openai/gpt-4.1-mini')
  })

  it('resets session stats when reset=true', async () => {
    registerModelPricing('anthropic', 'claude-sonnet-4', { inputPer1M: 3, outputPer1M: 15 })
    trackSessionCost('s1', 'anthropic', 'claude-sonnet-4', 10_000, 10_000)

    const result = await sessionCostTool.execute({ sessionId: 's1', reset: true }, ctx())
    expect(result.success).toBe(true)

    const after = await sessionCostTool.execute({ sessionId: 's1' }, ctx())
    expect(after.success).toBe(false)
  })
})
