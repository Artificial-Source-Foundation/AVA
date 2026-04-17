import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const mockListModels = vi.fn()
const fetchMock = vi.fn()

vi.mock('../rust-bridge', () => ({
  rustBackend: {
    listModels: () => mockListModels(),
  },
}))

import { _resetCatalogCache, syncModelsCatalog } from './curated-model-catalog'
import { fetchModels } from './model-fetcher'

describe('fetchModels', () => {
  beforeEach(async () => {
    _resetCatalogCache()
    mockListModels.mockResolvedValue([
      {
        id: 'gpt-5.4',
        provider: 'openai',
        name: 'GPT-5.4',
        toolCall: true,
        vision: false,
        reasoning: true,
        capabilities: ['tools', 'reasoning'],
        contextWindow: 400000,
        maxOutput: 128000,
        costInput: 1.25,
        costOutput: 10,
      },
    ])
    await syncModelsCatalog()
    fetchMock.mockReset()
    vi.stubGlobal('fetch', fetchMock)
  })

  afterEach(() => {
    vi.unstubAllGlobals()
    vi.restoreAllMocks()
    _resetCatalogCache()
  })

  it('uses the curated catalog for OAuth-backed OpenAI credentials instead of the API-key models endpoint', async () => {
    const models = await fetchModels('openai', {
      apiKey: 'oauth-token',
      authType: 'oauth-token',
    })

    expect(fetchMock).not.toHaveBeenCalled()
    expect(models).toEqual([
      expect.objectContaining({
        id: 'gpt-5.4',
        name: 'GPT-5.4',
        contextWindow: 400000,
      }),
    ])
  })
})
