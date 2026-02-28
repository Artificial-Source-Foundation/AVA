import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

const ctx = { sessionId: 's', workingDirectory: '/', signal: new AbortController().signal }

describe('deploy-command plugin', () => {
  it('registers the /deploy command', () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)
    expect(registeredCommands).toHaveLength(1)
    expect(registeredCommands[0].name).toBe('deploy')
  })

  it('deploys to staging by default', async () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)

    const result = await registeredCommands[0].execute('', ctx)
    expect(result).toContain('Deploying to staging')
    expect(result).toContain('Deployment to staging successful!')
  })

  it('deploys to production when specified', async () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)

    const result = await registeredCommands[0].execute('production', ctx)
    expect(result).toContain('Deploying to production')
    expect(result).toContain('Deployment to production successful!')
  })

  it('supports dry-run mode', async () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)

    const result = await registeredCommands[0].execute('staging --dry-run', ctx)
    expect(result).toContain('(dry run)')
    expect(result).toContain('Dry run complete')
    expect(result).not.toContain('successful!')
  })

  it('rejects invalid deploy targets', async () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)

    const result = await registeredCommands[0].execute('invalid-target', ctx)
    expect(result).toContain('Unknown deploy target')
    expect(result).toContain('staging, production, preview')
  })

  it('cleans up on dispose', () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(registeredCommands).toHaveLength(1)
    disposable.dispose()
    expect(registeredCommands).toHaveLength(0)
  })
})
