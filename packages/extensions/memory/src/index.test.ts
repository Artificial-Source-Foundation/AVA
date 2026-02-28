import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('memory extension', () => {
  it('activates and registers 4 tools', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    activate(api)
    expect(registeredTools).toHaveLength(4)
    const names = registeredTools.map((t) => t.definition.name)
    expect(names).toContain('memory_write')
    expect(names).toContain('memory_read')
    expect(names).toContain('memory_list')
    expect(names).toContain('memory_delete')
  })

  it('listens for prompt:build events', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('prompt:build')).toBe(true)
  })

  it('cleans up on dispose', () => {
    const { api, registeredTools, eventHandlers } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(registeredTools).toHaveLength(4)
    disposable.dispose()
    expect(registeredTools).toHaveLength(0)
    expect(eventHandlers.has('prompt:build')).toBe(false)
  })
})
