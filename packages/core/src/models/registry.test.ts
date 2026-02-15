/**
 * Model Registry Tests
 */

import { describe, expect, it } from 'vitest'
import {
  estimateCost,
  findModels,
  formatCost,
  getContextLimit,
  getMaxOutputTokens,
  getModel,
  getModelByApiId,
  getModelIds,
  getModelsForProvider,
  getSuggestedModel,
  hasCapability,
  isValidModel,
  MODEL_REGISTRY,
} from './registry.js'

// ============================================================================
// Lookup Functions
// ============================================================================

describe('getModel', () => {
  it('returns model config by short ID', () => {
    const model = getModel('claude-sonnet-4')
    expect(model).toBeDefined()
    expect(model!.provider).toBe('anthropic')
    expect(model!.displayName).toBe('Claude Sonnet 4')
  })

  it('returns undefined for unknown ID', () => {
    expect(getModel('nonexistent')).toBeUndefined()
  })
})

describe('getModelByApiId', () => {
  it('finds model by API ID', () => {
    const model = getModelByApiId('gpt-4o')
    expect(model).toBeDefined()
    expect(model!.displayName).toBe('GPT-4o')
  })

  it('returns undefined for unknown API ID', () => {
    expect(getModelByApiId('fake-model-123')).toBeUndefined()
  })
})

describe('getContextLimit', () => {
  it('returns context window for known model', () => {
    expect(getContextLimit('claude-opus-4')).toBe(200000)
  })

  it('returns default 128000 for unknown model', () => {
    expect(getContextLimit('nonexistent')).toBe(128000)
  })

  it('returns correct value for large context models', () => {
    expect(getContextLimit('gemini-2.0-flash')).toBe(1048576)
  })
})

describe('getMaxOutputTokens', () => {
  it('returns max output tokens for known model', () => {
    expect(getMaxOutputTokens('claude-opus-4')).toBe(32000)
  })

  it('returns default 4096 for unknown model', () => {
    expect(getMaxOutputTokens('nonexistent')).toBe(4096)
  })
})

describe('hasCapability', () => {
  it('returns true for capability the model has', () => {
    expect(hasCapability('claude-opus-4', 'thinking')).toBe(true)
    expect(hasCapability('claude-opus-4', 'tools')).toBe(true)
    expect(hasCapability('claude-opus-4', 'vision')).toBe(true)
  })

  it('returns false for capability the model lacks', () => {
    expect(hasCapability('o3-mini', 'vision')).toBe(false)
  })

  it('returns false for unknown model', () => {
    expect(hasCapability('nonexistent', 'tools')).toBe(false)
  })
})

// ============================================================================
// Query Functions
// ============================================================================

describe('findModels', () => {
  it('returns all models with no filter', () => {
    const models = findModels()
    expect(models.length).toBe(Object.keys(MODEL_REGISTRY).length)
  })

  it('filters by provider', () => {
    const models = findModels({ provider: 'anthropic' })
    expect(models.length).toBeGreaterThan(0)
    for (const m of models) {
      expect(m.provider).toBe('anthropic')
    }
  })

  it('filters by capability', () => {
    const models = findModels({ capability: 'thinking' })
    expect(models.length).toBeGreaterThan(0)
    for (const m of models) {
      expect(m.capabilities.thinking).toBe(true)
    }
  })

  it('filters by minimum context window', () => {
    const models = findModels({ minContext: 1000000 })
    expect(models.length).toBeGreaterThan(0)
    for (const m of models) {
      expect(m.contextWindow).toBeGreaterThanOrEqual(1000000)
    }
  })

  it('filters by max output price', () => {
    const models = findModels({ maxOutputPrice: 0.001 })
    for (const m of models) {
      expect(m.pricing?.outputPer1k ?? 0).toBeLessThanOrEqual(0.001)
    }
  })

  it('combines multiple filters', () => {
    const models = findModels({ provider: 'anthropic', capability: 'thinking' })
    expect(models.length).toBeGreaterThan(0)
    for (const m of models) {
      expect(m.provider).toBe('anthropic')
      expect(m.capabilities.thinking).toBe(true)
    }
  })
})

describe('getModelsForProvider', () => {
  it('returns all Anthropic models', () => {
    const models = getModelsForProvider('anthropic')
    expect(models.length).toBe(3)
  })

  it('returns empty for provider with no models', () => {
    const models = getModelsForProvider('mistral' as 'anthropic')
    expect(models).toHaveLength(0)
  })
})

describe('getModelIds', () => {
  it('returns all model short IDs', () => {
    const ids = getModelIds()
    expect(ids).toContain('claude-opus-4')
    expect(ids).toContain('gpt-4o')
    expect(ids).toContain('gemini-2.0-flash')
    expect(ids.length).toBe(Object.keys(MODEL_REGISTRY).length)
  })
})

// ============================================================================
// Pricing Functions
// ============================================================================

describe('estimateCost', () => {
  it('calculates cost for known model', () => {
    const cost = estimateCost('claude-sonnet-4', 1000, 1000)
    expect(cost).not.toBeNull()
    // input: 1k * 0.003 = 0.003, output: 1k * 0.015 = 0.015, total = 0.018
    expect(cost).toBeCloseTo(0.018, 4)
  })

  it('returns null for model without pricing', () => {
    // gemini-2.0-flash-thinking has no pricing
    expect(estimateCost('gemini-2.0-flash-thinking', 1000, 1000)).toBeNull()
  })

  it('returns null for unknown model', () => {
    expect(estimateCost('nonexistent', 1000, 1000)).toBeNull()
  })

  it('scales linearly with token count', () => {
    const cost1k = estimateCost('gpt-4o', 1000, 0)!
    const cost2k = estimateCost('gpt-4o', 2000, 0)!
    expect(cost2k).toBeCloseTo(cost1k * 2, 6)
  })
})

describe('formatCost', () => {
  it('formats small costs in millicents', () => {
    const formatted = formatCost(0.005)
    expect(formatted).toBe('$5.00m')
  })

  it('formats larger costs in dollars', () => {
    const formatted = formatCost(1.2345)
    expect(formatted).toBe('$1.2345')
  })

  it('formats cost at the boundary', () => {
    expect(formatCost(0.01)).toBe('$0.0100')
  })

  it('formats zero cost', () => {
    expect(formatCost(0)).toBe('$0.00m')
  })
})

// ============================================================================
// Validation
// ============================================================================

describe('isValidModel', () => {
  it('returns true for valid model', () => {
    expect(isValidModel('claude-opus-4')).toBe(true)
  })

  it('returns false for invalid model', () => {
    expect(isValidModel('nonexistent')).toBe(false)
  })
})

describe('getSuggestedModel', () => {
  it('returns suggested model for provider', () => {
    const suggested = getSuggestedModel('anthropic')
    expect(suggested).toBeDefined()
    expect(typeof suggested).toBe('string')
  })

  it('returns model with largest context window', () => {
    const suggested = getSuggestedModel('google')
    // gemini-1.5-pro has 2M context, largest
    expect(suggested).toBe('gemini-1.5-pro')
  })

  it('returns undefined for provider with no models', () => {
    expect(getSuggestedModel('mistral' as 'anthropic')).toBeUndefined()
  })
})
