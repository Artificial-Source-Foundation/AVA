import { describe, expect, it } from 'vitest'

import { mergeWithDefaults } from './settings-hydration'

describe('settings-hydration', () => {
  it('drops deprecated generation.delegationEnabled from persisted settings', () => {
    const merged = mergeWithDefaults({
      generation: {
        customInstructions: 'Keep this',
        reasoningEffort: 'off',
        delegationEnabled: true,
      },
    } as unknown as Parameters<typeof mergeWithDefaults>[0])

    expect(merged.generation.customInstructions).toBe('Keep this')
    expect('delegationEnabled' in merged.generation).toBe(false)
  })
})
