import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('lsp extension', () => {
  it('activates and listens for session:opened', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('session:opened')).toBe(true)
  })

  it('listens for lsp:diagnostics events', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('lsp:diagnostics')).toBe(true)
  })

  it('emits lsp:ready on session open', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)

    api.emit('session:opened', { sessionId: 'test', workingDirectory: '/project' })
    await new Promise((r) => setTimeout(r, 50))

    const ready = emittedEvents.find((e) => e.event === 'lsp:ready')
    expect(ready).toBeDefined()
  })

  it('responds to lsp:diagnostics with empty result', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)
    api.emit('lsp:diagnostics', { file: '/test.ts' })

    const result = emittedEvents.find((e) => e.event === 'lsp:diagnostics-result')
    expect(result).toBeDefined()
    expect((result!.data as { diagnostics: unknown[] }).diagnostics).toEqual([])
  })

  it('cleans up on dispose', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activate(api)
    disposable.dispose()
    expect(eventHandlers.has('session:opened')).toBe(false)
    expect(eventHandlers.has('lsp:diagnostics')).toBe(false)
  })
})
