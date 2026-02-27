import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('codebase extension', () => {
  it('activates and registers /files command', () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)
    const cmd = registeredCommands.find((c) => c.name === 'files')
    expect(cmd).toBeDefined()
    expect(cmd!.description).toContain('indexed files')
  })

  it('listens for session:opened', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('session:opened')).toBe(true)
  })

  it('/files returns message before indexing', async () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)
    const cmd = registeredCommands.find((c) => c.name === 'files')!
    const result = await cmd.execute('', {
      sessionId: 'test',
      workingDirectory: '/tmp',
      signal: new AbortController().signal,
    })
    expect(result).toContain('not indexed yet')
  })

  it('cleans up on dispose', () => {
    const { api, registeredCommands, eventHandlers } = createMockExtensionAPI()
    const disposable = activate(api)
    disposable.dispose()
    expect(registeredCommands).toHaveLength(0)
    expect(eventHandlers.has('session:opened')).toBe(false)
  })
})
