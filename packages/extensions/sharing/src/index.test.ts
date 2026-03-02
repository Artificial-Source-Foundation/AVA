import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('sharing extension', () => {
  it('activates and registers /share command', () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)
    expect(registeredCommands).toHaveLength(1)
    expect(registeredCommands[0]!.name).toBe('share')
  })

  it('returns stub message when no endpoint is configured', async () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)

    const cmd = registeredCommands[0]!
    const result = await cmd.execute('', {
      sessionId: 'test',
      workingDirectory: '/tmp',
      signal: new AbortController().signal,
    })

    expect(result).toContain('No sharing endpoint configured')
  })

  it('cleans up on dispose', () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(registeredCommands).toHaveLength(1)

    disposable.dispose()
    expect(registeredCommands).toHaveLength(0)
  })

  it('has correct command description', () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)
    expect(registeredCommands[0]!.description).toContain('Share')
  })
})
