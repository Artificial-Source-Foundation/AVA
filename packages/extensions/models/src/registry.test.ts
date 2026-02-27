import { describe, expect, it } from 'vitest'
import { addModelsToRegistry, createModelRegistry } from './registry.js'
import type { ModelInfo } from './types.js'

const mockModel: ModelInfo = {
  id: 'claude-sonnet',
  provider: 'anthropic',
  displayName: 'Claude Sonnet',
  contextWindow: 200_000,
  maxOutput: 8192,
  supportsTools: true,
  supportsVision: true,
}

describe('createModelRegistry', () => {
  it('creates an empty registry', () => {
    const registry = createModelRegistry()
    expect(registry.models.size).toBe(0)
  })

  it('getModel returns undefined for missing model', () => {
    const registry = createModelRegistry()
    expect(registry.getModel('nonexistent')).toBeUndefined()
  })

  it('getModelsForProvider returns empty for unknown provider', () => {
    const registry = createModelRegistry()
    expect(registry.getModelsForProvider('anthropic')).toEqual([])
  })
})

describe('addModelsToRegistry', () => {
  it('adds models to the registry', () => {
    const registry = createModelRegistry()
    addModelsToRegistry(registry, [mockModel])
    expect(registry.models.size).toBe(1)
    expect(registry.getModel('claude-sonnet')).toEqual(mockModel)
  })

  it('supports multiple models from same provider', () => {
    const registry = createModelRegistry()
    const model2: ModelInfo = { ...mockModel, id: 'claude-opus', displayName: 'Claude Opus' }
    addModelsToRegistry(registry, [mockModel, model2])
    expect(registry.getModelsForProvider('anthropic')).toHaveLength(2)
  })

  it('overwrites duplicate model IDs', () => {
    const registry = createModelRegistry()
    addModelsToRegistry(registry, [mockModel])
    const updated = { ...mockModel, displayName: 'Updated' }
    addModelsToRegistry(registry, [updated])
    expect(registry.models.size).toBe(1)
    expect(registry.getModel('claude-sonnet')?.displayName).toBe('Updated')
  })
})
