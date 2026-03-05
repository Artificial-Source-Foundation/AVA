import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('integrations extension', () => {
  it('activates and listens for integrations:search', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('integrations:search')).toBe(true)
  })

  it('emits integrations:ready after checking credentials', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)

    await new Promise((r) => setTimeout(r, 50))

    const ready = emittedEvents.find((e) => e.event === 'integrations:ready')
    expect(ready).toBeDefined()
    expect((ready!.data as { providers: string[] }).providers).toEqual([])
  })

  it('responds to search when no provider available', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)
    api.emit('integrations:search', { query: 'test' })

    const result = emittedEvents.find((e) => e.event === 'integrations:search-result')
    expect(result).toBeDefined()
    expect((result!.data as { error: string }).error).toContain('No provider')
  })

  it('cleans up on dispose', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activate(api)
    disposable.dispose()
    expect(eventHandlers.has('integrations:search')).toBe(false)
  })
})
