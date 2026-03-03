import { describe, expect, it } from 'vitest'
import { createMockExtensionAPI } from '../../../core-v2/src/__test-utils__/mock-extension-api.js'
import type { ToolMiddlewareContext } from '../../../core-v2/src/extensions/types.js'

import { activate } from './index.js'

function wait(ms = 40): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

function middlewareContext(toolName: string, args: Record<string, unknown>): ToolMiddlewareContext {
  return {
    toolName,
    args,
    ctx: {
      cwd: '/project',
      signal: undefined,
      emitProgress: () => {},
      executeTool: async () => ({ content: [] }),
      getContextWindow: async () => [],
      getSessionId: () => 's1',
      askQuestion: async () => ({ type: 'text', text: '' }),
    },
    definition: { description: 'test tool' },
  }
}

describe('instructions extension', () => {
  it('activates and listens for session:opened', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('session:opened')).toBe(true)
  })

  it('registers middleware for subdirectory discovery', () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    activate(api)
    expect(registeredMiddleware.some((m) => m.name === 'instructions-subdirectory')).toBe(true)
  })

  it('loads instructions when session opens', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    api.platform.fs.addFile('/project/CLAUDE.md', '# My Instructions')
    activate(api)

    api.emit('session:opened', { sessionId: 'test', workingDirectory: '/project' })
    await wait()

    const loaded = emittedEvents.find((e) => e.event === 'instructions:loaded')
    expect(loaded).toBeDefined()
    expect((loaded!.data as { count: number }).count).toBe(1)
  })

  it('discovers subdirectory instructions on glob tool access', async () => {
    const { api, emittedEvents, registeredMiddleware } = createMockExtensionAPI()
    api.platform.fs.addFile('/project/src/components/AGENTS.md', '# Component rules')
    activate(api)

    api.emit('session:opened', { sessionId: 's1', workingDirectory: '/project' })
    await wait()

    const middleware = registeredMiddleware.find((m) => m.name === 'instructions-subdirectory')
    if (!middleware?.after) throw new Error('expected middleware after hook')

    await middleware.after(middlewareContext('glob', { path: '/project/src/components' }), {
      content: [],
    })

    const event = emittedEvents.find((e) => e.event === 'instructions:subdirectory-loaded')
    expect(event).toBeDefined()
    expect((event!.data as { count: number }).count).toBe(1)
  })

  it('discovers instructions from bash path extraction', async () => {
    const { api, emittedEvents, registeredMiddleware } = createMockExtensionAPI()
    api.platform.fs.addFile('/project/pkg/AGENTS.md', '# Package rules')
    activate(api)

    api.emit('session:opened', { sessionId: 's1', workingDirectory: '/project' })
    await wait()

    const middleware = registeredMiddleware.find((m) => m.name === 'instructions-subdirectory')
    if (!middleware?.after) throw new Error('expected middleware after hook')

    await middleware.after(
      middlewareContext('bash', { command: 'ls /project/pkg && cat /project/pkg/file.ts' }),
      {
        content: [],
      }
    )

    const event = emittedEvents.find((e) => e.event === 'instructions:subdirectory-loaded')
    expect(event).toBeDefined()
    expect((event!.data as { merged: string }).merged).toContain('Package rules')
  })

  it('deduplicates repeated directory discovery via cache', async () => {
    const { api, emittedEvents, registeredMiddleware } = createMockExtensionAPI()
    api.platform.fs.addFile('/project/lib/AGENTS.md', '# Lib rules')
    activate(api)

    api.emit('session:opened', { sessionId: 's1', workingDirectory: '/project' })
    await wait()

    const middleware = registeredMiddleware.find((m) => m.name === 'instructions-subdirectory')
    if (!middleware?.after) throw new Error('expected middleware after hook')

    const ctx = middlewareContext('read_file', { path: '/project/lib/file.ts' })
    await middleware.after(ctx, { content: [] })
    await middleware.after(ctx, { content: [] })

    const discoveredEvents = emittedEvents.filter(
      (e) => e.event === 'instructions:subdirectory-loaded'
    )
    expect(discoveredEvents).toHaveLength(1)
  })

  it('cleans up on dispose', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activate(api)
    disposable.dispose()
    expect(eventHandlers.has('session:opened')).toBe(false)
  })
})
