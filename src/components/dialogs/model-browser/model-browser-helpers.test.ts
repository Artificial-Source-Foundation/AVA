import { describe, expect, it } from 'vitest'
import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'
import { aggregateModels, formatContextWindow, sortModels } from './model-browser-helpers'
import type { BrowsableModel } from './model-browser-types'

describe('formatContextWindow', () => {
  it('returns a placeholder when context window is missing', () => {
    expect(formatContextWindow(undefined)).toBe('N/A')
  })

  it('keeps existing numeric formatting behavior', () => {
    expect(formatContextWindow(200_000)).toBe('200K')
  })
})

describe('context window normalization', () => {
  it('normalizes missing contextWindow from provider model data', () => {
    const provider = {
      id: 'test-provider',
      name: 'Test Provider',
      icon: () => null,
      description: 'test',
      enabled: true,
      status: 'connected',
      models: [{ id: 'm1', name: 'Model One', contextWindow: undefined }],
    } as unknown as LLMProviderConfig

    const models = aggregateModels([provider])
    expect(models[0].contextWindow).toBeNull()
  })

  it('sorts by context safely when some models have missing context', () => {
    const models: BrowsableModel[] = [
      {
        id: 'high',
        name: 'High',
        providerId: 'p',
        providerName: 'Provider',
        contextWindow: 200_000,
        capabilities: [],
      },
      {
        id: 'missing',
        name: 'Missing',
        providerId: 'p',
        providerName: 'Provider',
        contextWindow: null,
        capabilities: [],
      },
      {
        id: 'mid',
        name: 'Mid',
        providerId: 'p',
        providerName: 'Provider',
        contextWindow: 32_000,
        capabilities: [],
      },
    ]

    const sorted = sortModels(models, 'context')
    expect(sorted.map((m) => m.id)).toEqual(['high', 'mid', 'missing'])
  })
})
