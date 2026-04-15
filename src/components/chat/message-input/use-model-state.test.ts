import { describe, expect, it } from 'vitest'
import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'
import { resolveModelSelection } from './use-model-state'

const DummyIcon = () => null

function provider(
  id: string,
  models: string[],
  options?: { defaultModel?: string }
): LLMProviderConfig {
  return {
    id,
    name: id,
    icon: DummyIcon,
    description: `${id} provider`,
    enabled: true,
    defaultModel: options?.defaultModel,
    models: models.map((modelId) => ({
      id: modelId,
      name: modelId,
      contextWindow: 128000,
    })),
    status: 'connected',
  }
}

describe('resolveModelSelection', () => {
  it('backfills the provider for a valid restored model selection', () => {
    const resolved = resolveModelSelection(
      [provider('openai', ['gpt-5.4']), provider('anthropic', ['claude-sonnet-4-6'])],
      'gpt-5.4',
      null
    )

    expect(resolved).toEqual({
      modelId: 'gpt-5.4',
      providerId: 'openai',
    })
  })

  it('falls back to the first enabled provider default model when selection is invalid', () => {
    const resolved = resolveModelSelection(
      [
        provider('openai', ['gpt-5.4', 'gpt-5.4-mini'], { defaultModel: 'gpt-5.4-mini' }),
        provider('anthropic', ['claude-sonnet-4-6']),
      ],
      'missing-model',
      'stale-provider'
    )

    expect(resolved).toEqual({
      modelId: 'gpt-5.4-mini',
      providerId: 'openai',
    })
  })
})
