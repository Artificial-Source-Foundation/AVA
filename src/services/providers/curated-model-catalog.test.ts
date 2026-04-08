import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import {
  _resetCatalogCache,
  getModelFromCatalog,
  getModelsDevModels,
  isBlockedModelId,
  syncModelsCatalog,
} from './curated-model-catalog'

const mockListModels = vi.fn()

vi.mock('../rust-bridge', () => ({
  rustBackend: {
    listModels: () => mockListModels(),
  },
}))

const MOCK_MODELS = [
  {
    id: 'claude-sonnet-4-6',
    provider: 'anthropic',
    name: 'Claude Sonnet 4.6',
    toolCall: true,
    vision: true,
    reasoning: true,
    capabilities: ['tools', 'vision', 'reasoning'],
    contextWindow: 200000,
    maxOutput: 64000,
    costInput: 3,
    costOutput: 15,
  },
  {
    id: 'gpt-5.2',
    provider: 'openai',
    name: 'GPT-5.2',
    toolCall: true,
    vision: true,
    reasoning: true,
    capabilities: ['tools', 'vision', 'reasoning'],
    contextWindow: 400000,
    maxOutput: 32768,
    costInput: 1.75,
    costOutput: 14,
  },
  {
    id: 'aurora-alpha',
    provider: 'xai',
    name: 'Aurora Alpha',
    toolCall: true,
    vision: false,
    reasoning: false,
    capabilities: ['tools'],
    contextWindow: 65536,
    maxOutput: 8192,
    costInput: 0,
    costOutput: 0,
  },
]

describe('curated-model-catalog', () => {
  beforeEach(() => {
    _resetCatalogCache()
    mockListModels.mockResolvedValue(MOCK_MODELS)
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('hydrates catalog from backend model metadata', async () => {
    const result = await syncModelsCatalog()
    expect(result).toBeTruthy()
    expect(result?.anthropic?.['claude-sonnet-4-6']?.name).toBe('Claude Sonnet 4.6')
  })

  it('transforms provider models for a known provider', async () => {
    await syncModelsCatalog()
    const models = getModelsDevModels('anthropic')
    expect(models).toHaveLength(1)
    expect(models[0].pricing).toEqual({ input: 3, output: 15 })
    expect(models[0].capabilities).toContain('tools')
    expect(models[0].capabilities).toContain('vision')
    expect(models[0].capabilities).toContain('reasoning')
  })

  it('normalizes legacy provider aliases into canonical catalog buckets', async () => {
    mockListModels.mockResolvedValue([
      ...MOCK_MODELS,
      {
        id: 'glm-4.7',
        provider: 'zhipuai-coding-plan',
        name: 'GLM-4.7',
        toolCall: true,
        vision: false,
        reasoning: true,
        capabilities: ['tools', 'reasoning'],
        contextWindow: 204800,
        maxOutput: 131072,
        costInput: 0,
        costOutput: 0,
      },
    ])
    await syncModelsCatalog()

    expect(getModelsDevModels('zai')).toHaveLength(1)
    expect(getModelsDevModels('zhipuai-coding-plan')).toHaveLength(1)
    expect(getModelFromCatalog('glm-4.7', 'zai')?.name).toBe('GLM-4.7')
  })

  it('filters blocked model ids from provider lists', async () => {
    await syncModelsCatalog()
    const models = getModelsDevModels('xai' as 'openai')
    expect(models).toEqual([])
  })

  it('looks up models by provider or globally', async () => {
    await syncModelsCatalog()
    expect(getModelFromCatalog('claude-sonnet-4-6', 'anthropic')?.reasoning).toBe(true)
    expect(getModelFromCatalog('gpt-5.2')?.name).toBe('GPT-5.2')
    expect(getModelFromCatalog('missing-model')).toBeNull()
  })

  it('recognizes blocked model id patterns', () => {
    expect(isBlockedModelId('aurora-alpha')).toBe(true)
    expect(isBlockedModelId('Aurora-Alpha')).toBe(true)
    expect(isBlockedModelId('claude-sonnet-4-6')).toBe(false)
  })
})
