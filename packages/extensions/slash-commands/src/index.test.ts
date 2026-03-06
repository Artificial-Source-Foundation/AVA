import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('slash-commands extension', () => {
  it('activates and registers 13 commands', () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)
    expect(registeredCommands).toHaveLength(13)
  })

  it('registers expected command names', () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)
    const names = registeredCommands.map((c) => c.name)
    expect(names).toContain('help')
    expect(names).toContain('clear')
    expect(names).toContain('mode')
    expect(names).toContain('architect')
    expect(names).toContain('model')
    expect(names).toContain('compact')
    expect(names).toContain('undo')
    expect(names).toContain('settings')
    expect(names).toContain('status')
    expect(names).toContain('recipe')
  })

  it('logs activation with command count', () => {
    const { api } = createMockExtensionAPI()
    activate(api)
    expect(api.log.debug).toHaveBeenCalledWith('Slash commands extension activated (13 commands)')
  })

  it('cleans up all commands on dispose', () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(registeredCommands).toHaveLength(13)
    disposable.dispose()
    expect(registeredCommands).toHaveLength(0)
  })
})
