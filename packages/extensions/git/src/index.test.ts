import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('git extension', () => {
  it('activates and registers middleware', () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    activate(api)
    expect(registeredMiddleware).toHaveLength(1)
    expect(registeredMiddleware[0].name).toBe('ava-git-snapshots')
    expect(registeredMiddleware[0].priority).toBe(30)
  })

  it('registers /snapshot command', () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)
    const cmd = registeredCommands.find((c) => c.name === 'snapshot')
    expect(cmd).toBeDefined()
    expect(cmd!.description).toContain('snapshot')
  })

  it('listens for session:opened', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('session:opened')).toBe(true)
  })

  it('emits git:ready when in a git repo', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    api.platform.shell.setResult('cd "/project" && git rev-parse --is-inside-work-tree', {
      stdout: 'true\n',
      stderr: '',
      exitCode: 0,
    })
    activate(api)

    api.emit('session:opened', { sessionId: 'test', workingDirectory: '/project' })
    await new Promise((r) => setTimeout(r, 50))

    const ready = emittedEvents.find((e) => e.event === 'git:ready')
    expect(ready).toBeDefined()
  })

  it('cleans up on dispose', () => {
    const { api, registeredMiddleware, registeredCommands, eventHandlers } =
      createMockExtensionAPI()
    const disposable = activate(api)
    disposable.dispose()
    expect(registeredMiddleware).toHaveLength(0)
    expect(registeredCommands).toHaveLength(0)
    expect(eventHandlers.has('session:opened')).toBe(false)
  })
})
