import { describe, expect, it } from 'vitest'
import { BUILTIN_PACKS, getModelPack, listModelPacks, resolveModelForTier } from './packs.js'

describe('BUILTIN_PACKS', () => {
  it('has three built-in packs', () => {
    expect(BUILTIN_PACKS).toHaveLength(3)
  })

  it('includes budget, balanced, and premium', () => {
    const names = BUILTIN_PACKS.map((p) => p.name)
    expect(names).toContain('budget')
    expect(names).toContain('balanced')
    expect(names).toContain('premium')
  })

  it('each pack has commander, lead, and worker models', () => {
    for (const pack of BUILTIN_PACKS) {
      expect(pack.models).toHaveProperty('commander')
      expect(pack.models).toHaveProperty('lead')
      expect(pack.models).toHaveProperty('worker')
    }
  })

  it('each model entry has provider and model fields', () => {
    for (const pack of BUILTIN_PACKS) {
      for (const [, entry] of Object.entries(pack.models)) {
        expect(entry).toHaveProperty('provider')
        expect(entry).toHaveProperty('model')
        expect(typeof entry.provider).toBe('string')
        expect(typeof entry.model).toBe('string')
      }
    }
  })

  it('each pack has a description', () => {
    for (const pack of BUILTIN_PACKS) {
      expect(pack.description.length).toBeGreaterThan(0)
    }
  })
})

describe('getModelPack', () => {
  it('returns the budget pack', () => {
    const pack = getModelPack('budget')
    expect(pack).toBeDefined()
    expect(pack!.name).toBe('budget')
  })

  it('returns the balanced pack', () => {
    const pack = getModelPack('balanced')
    expect(pack).toBeDefined()
    expect(pack!.name).toBe('balanced')
  })

  it('returns the premium pack', () => {
    const pack = getModelPack('premium')
    expect(pack).toBeDefined()
    expect(pack!.name).toBe('premium')
  })

  it('returns undefined for unknown pack', () => {
    expect(getModelPack('nonexistent')).toBeUndefined()
  })

  it('budget uses haiku for commander', () => {
    const pack = getModelPack('budget')!
    expect(pack.models.commander.model).toBe('claude-haiku-4-5')
  })

  it('premium uses opus for commander', () => {
    const pack = getModelPack('premium')!
    expect(pack.models.commander.model).toBe('claude-opus-4')
  })
})

describe('listModelPacks', () => {
  it('returns all pack names', () => {
    const names = listModelPacks()
    expect(names).toEqual(['budget', 'balanced', 'premium'])
  })
})

describe('resolveModelForTier', () => {
  it('resolves commander tier', () => {
    const pack = getModelPack('balanced')!
    const result = resolveModelForTier(pack, 'commander')
    expect(result).toEqual({ provider: 'anthropic', model: 'claude-sonnet-4' })
  })

  it('resolves lead tier', () => {
    const pack = getModelPack('premium')!
    const result = resolveModelForTier(pack, 'lead')
    expect(result).toEqual({ provider: 'anthropic', model: 'claude-sonnet-4' })
  })

  it('resolves worker tier', () => {
    const pack = getModelPack('budget')!
    const result = resolveModelForTier(pack, 'worker')
    expect(result).toEqual({ provider: 'anthropic', model: 'claude-haiku-4-5' })
  })

  it('falls back to worker for unknown tier', () => {
    const pack = getModelPack('balanced')!
    const result = resolveModelForTier(pack, 'unknown-tier')
    expect(result).toEqual({ provider: 'anthropic', model: 'claude-haiku-4-5' })
  })
})
