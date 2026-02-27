import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('diff extension', () => {
  it('activates and registers middleware', () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    activate(api)
    expect(registeredMiddleware).toHaveLength(1)
    expect(registeredMiddleware[0].name).toBe('ava-diff-tracker')
    expect(registeredMiddleware[0].priority).toBe(20)
  })

  it('middleware has before and after hooks', () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    activate(api)
    expect(registeredMiddleware[0].before).toBeTypeOf('function')
    expect(registeredMiddleware[0].after).toBeTypeOf('function')
  })

  it('before hook snapshots file content for write tools', async () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    api.platform.fs.addFile('/test.ts', 'original content')
    activate(api)

    const mw = registeredMiddleware[0]
    const result = await mw.before!({
      toolName: 'write_file',
      args: { path: '/test.ts' },
      ctx: { sessionId: 'test', workingDirectory: '/tmp', signal: new AbortController().signal },
      definition: { name: 'write_file', description: '', parameters: {} },
    })

    // Should not block
    expect(result).toBeUndefined()
  })

  it('before hook skips non-write tools', async () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    activate(api)

    const mw = registeredMiddleware[0]
    const result = await mw.before!({
      toolName: 'read_file',
      args: { path: '/test.ts' },
      ctx: { sessionId: 'test', workingDirectory: '/tmp', signal: new AbortController().signal },
      definition: { name: 'read_file', description: '', parameters: {} },
    })

    expect(result).toBeUndefined()
  })

  it('cleans up on dispose', () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(registeredMiddleware).toHaveLength(1)
    disposable.dispose()
    expect(registeredMiddleware).toHaveLength(0)
  })
})
