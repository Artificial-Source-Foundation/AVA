import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('lsp extension', () => {
  it('activates and listens for session:opened', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('session:opened')).toBe(true)
  })

  it('registers lsp_diagnostics tool', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    activate(api)
    const names = registeredTools.map((t) => t.definition.name)
    expect(names).toContain('lsp_diagnostics')
  })

  it('registers lsp_hover tool', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    activate(api)
    const names = registeredTools.map((t) => t.definition.name)
    expect(names).toContain('lsp_hover')
  })

  it('registers lsp_definition tool', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    activate(api)
    const names = registeredTools.map((t) => t.definition.name)
    expect(names).toContain('lsp_definition')
  })

  it('registers diagnostics append middleware with priority 20', () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    activate(api)
    const middleware = registeredMiddleware.find((m) => m.name === 'lsp-diagnostics-after-write')
    expect(middleware).toBeDefined()
    expect(middleware?.priority).toBe(20)
  })

  it('emits lsp:ready on session open', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)

    api.emit('session:opened', { sessionId: 'test', workingDirectory: '/project' })
    await new Promise((r) => setTimeout(r, 50))

    const ready = emittedEvents.find((e) => e.event === 'lsp:ready')
    expect(ready).toBeDefined()
  })

  it('cleans up on dispose', () => {
    const { api, eventHandlers, registeredTools } = createMockExtensionAPI()
    const disposable = activate(api)
    disposable.dispose()
    expect(eventHandlers.has('session:opened')).toBe(false)
    expect(registeredTools).toHaveLength(0)
  })
})
