import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

const ctx = { sessionId: 's', workingDirectory: '/', signal: new AbortController().signal }

describe('session-notes plugin', () => {
  it('registers the /notes command', () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)
    expect(registeredCommands).toHaveLength(1)
    expect(registeredCommands[0].name).toBe('notes')
  })

  it('lists empty notes initially', async () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)
    const result = await registeredCommands[0].execute('list', ctx)
    expect(result).toContain('No notes yet')
  })

  it('adds and lists notes', async () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)
    const cmd = registeredCommands[0]

    const addResult = await cmd.execute('add Remember to test edge cases', ctx)
    expect(addResult).toContain('Note added')

    const listResult = await cmd.execute('list', ctx)
    expect(listResult).toContain('Remember to test edge cases')
  })

  it('clears notes', async () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)
    const cmd = registeredCommands[0]

    await cmd.execute('add A note', ctx)
    const clearResult = await cmd.execute('clear', ctx)
    expect(clearResult).toContain('cleared')

    const listResult = await cmd.execute('list', ctx)
    expect(listResult).toContain('No notes yet')
  })

  it('cleans up on dispose', () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(registeredCommands).toHaveLength(1)
    disposable.dispose()
    expect(registeredCommands).toHaveLength(0)
  })
})
