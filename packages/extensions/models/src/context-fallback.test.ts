import { afterEach, describe, expect, it } from 'vitest'
import { getContextFallback, resetAvailability } from './availability.js'

afterEach(() => {
  resetAvailability()
})

describe('getContextFallback', () => {
  it('returns larger model when overflow requires more context', () => {
    const fallback = getContextFallback('openrouter', 'anthropic/claude-sonnet-4-6', 300_000)
    expect(fallback).toEqual({
      provider: 'openrouter',
      model: 'google/gemini-2.5-pro',
      contextWindow: 1_000_000,
    })
  })

  it('returns null when no larger model can satisfy required tokens', () => {
    const fallback = getContextFallback('anthropic', 'claude-sonnet-4-6', 300_000)
    expect(fallback).toBeNull()
  })

  it('returns null when already on largest model in chain', () => {
    const fallback = getContextFallback('openrouter', 'google/gemini-2.5-pro', 400_000)
    expect(fallback).toBeNull()
  })

  it('selects fallback by context window ordering', () => {
    const fallback = getContextFallback('openrouter', 'anthropic/claude-sonnet-4-6', 220_000)
    expect(fallback?.contextWindow).toBe(1_000_000)
    expect(fallback?.model).toBe('google/gemini-2.5-pro')
  })
})
