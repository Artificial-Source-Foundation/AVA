import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('commander extension activate', () => {
  it('registers praxis mode and invoke tools', () => {
    const mock = createMockExtensionAPI()
    const disposable = activate(mock.api)
    expect(mock.registeredModes).toHaveLength(1)
    expect(mock.registeredTools).toHaveLength(2)

    mock.api.emit('praxis:mode-selected', { mode: 'full' })
    expect(mock.emittedEvents.some((entry) => entry.event === 'praxis:progress-updated')).toBe(true)

    disposable.dispose()
    expect(mock.registeredModes).toHaveLength(0)
    expect(mock.registeredTools).toHaveLength(0)
  })
})
