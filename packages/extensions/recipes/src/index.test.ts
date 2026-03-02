import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import type { MockFileSystem } from '@ava/core-v2/__test-utils__/mock-platform'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('recipes extension', () => {
  it('activates and registers /recipe command', () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)
    expect(registeredCommands).toHaveLength(1)
    expect(registeredCommands[0]!.name).toBe('recipe')
  })

  it('logs activation', () => {
    const { api } = createMockExtensionAPI()
    activate(api)
    expect(api.log.debug).toHaveBeenCalledWith('Recipes extension activated')
  })

  it('cleans up on dispose', () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(registeredCommands).toHaveLength(1)
    disposable!.dispose()
    expect(registeredCommands).toHaveLength(0)
  })

  it('registers session:opened handler for recipe discovery', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('session:opened')).toBe(true)
  })

  it('discovers recipes from .ava/recipes/ on session open', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    const fs = api.platform.fs as MockFileSystem

    // Add a recipe file
    fs.addFile(
      '/project/.ava/recipes/deploy.json',
      JSON.stringify({
        name: 'deploy',
        description: 'Deploy the app',
        steps: [{ name: 'build', tool: 'bash' }],
      })
    )

    activate(api)

    // Trigger session:opened
    api.emit('session:opened', { sessionId: 'test', workingDirectory: '/project' })

    // Wait for async discovery
    await new Promise((resolve) => setTimeout(resolve, 50))

    const discoveredEvent = emittedEvents.find((e) => e.event === 'recipes:discovered')
    expect(discoveredEvent).toBeDefined()
    expect((discoveredEvent!.data as Record<string, unknown>).count).toBe(1)
  })

  it('/recipe command lists discovered recipes', async () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    const fs = api.platform.fs as MockFileSystem

    fs.addFile(
      '/project/.ava/recipes/ci.json',
      JSON.stringify({
        name: 'ci',
        description: 'Run CI pipeline',
        steps: [{ name: 'test', tool: 'bash' }],
      })
    )

    activate(api)

    // Trigger discovery
    api.emit('session:opened', { sessionId: 'test', workingDirectory: '/project' })
    await new Promise((resolve) => setTimeout(resolve, 50))

    // Execute /recipe with no args
    const cmd = registeredCommands[0]!
    const ctx = {
      sessionId: 'test',
      workingDirectory: '/project',
      signal: new AbortController().signal,
    }
    const output = await cmd.execute('', ctx)
    expect(output).toContain('ci')
    expect(output).toContain('Run CI pipeline')
  })

  it('/recipe command returns message when no recipes found', async () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)

    const cmd = registeredCommands[0]!
    const ctx = {
      sessionId: 'test',
      workingDirectory: '/project',
      signal: new AbortController().signal,
    }
    const output = await cmd.execute('', ctx)
    expect(output).toContain('No recipes found')
  })

  it('/recipe command returns not found for unknown recipe name', async () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)

    const cmd = registeredCommands[0]!
    const ctx = {
      sessionId: 'test',
      workingDirectory: '/project',
      signal: new AbortController().signal,
    }
    const output = await cmd.execute('nonexistent', ctx)
    expect(output).toContain('not found')
  })
})
