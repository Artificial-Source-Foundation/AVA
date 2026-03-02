import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('instructions extension', () => {
  it('activates and listens for session:opened', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('session:opened')).toBe(true)
  })

  it('logs activation message', () => {
    const { api } = createMockExtensionAPI()
    activate(api)
    expect(api.log.debug).toHaveBeenCalledWith('Instructions extension activated')
  })

  it('loads instructions when session opens', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    api.platform.fs.addFile('/project/CLAUDE.md', '# My Instructions')
    activate(api)

    api.emit('session:opened', { sessionId: 'test', workingDirectory: '/project' })

    // Wait for async loading
    await new Promise((r) => setTimeout(r, 50))

    const loaded = emittedEvents.find((e) => e.event === 'instructions:loaded')
    expect(loaded).toBeDefined()
    expect((loaded!.data as { count: number }).count).toBe(1)
  })

  it('loads AGENTS.md instructions when session opens', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    api.platform.fs.addFile('/project/AGENTS.md', '# Agent Instructions\nAlways be helpful.')
    activate(api)

    api.emit('session:opened', { sessionId: 'test', workingDirectory: '/project' })

    await new Promise((r) => setTimeout(r, 50))

    const loaded = emittedEvents.find((e) => e.event === 'instructions:loaded')
    expect(loaded).toBeDefined()
    expect((loaded!.data as { count: number }).count).toBe(1)
    expect((loaded!.data as { merged: string }).merged).toContain('Agent Instructions')
  })

  it('cleans up on dispose', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activate(api)
    disposable.dispose()
    expect(eventHandlers.has('session:opened')).toBe(false)
  })
})
