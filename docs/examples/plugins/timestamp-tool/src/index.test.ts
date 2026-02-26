import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('timestamp-tool plugin', () => {
  it('registers the timestamp tool', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    activate(api)
    expect(registeredTools).toHaveLength(1)
    expect(registeredTools[0].definition.name).toBe('timestamp')
  })

  it('executes with all format', async () => {
    const { api, registeredTools } = createMockExtensionAPI()
    activate(api)
    const tool = registeredTools[0]
    const result = await tool.execute(
      { format: 'all' },
      { sessionId: 's', workingDirectory: '/', signal: new AbortController().signal }
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('iso:')
    expect(result.output).toContain('unix:')
    expect(result.output).toContain('human:')
  })

  it('executes with iso format', async () => {
    const { api, registeredTools } = createMockExtensionAPI()
    activate(api)
    const result = await registeredTools[0].execute(
      { format: 'iso' },
      { sessionId: 's', workingDirectory: '/', signal: new AbortController().signal }
    )
    expect(result.success).toBe(true)
    // ISO format: YYYY-MM-DDTHH:mm:ss.sssZ
    expect(result.output).toMatch(/^\d{4}-\d{2}-\d{2}T/)
  })

  it('cleans up on dispose', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(registeredTools).toHaveLength(1)
    disposable.dispose()
    expect(registeredTools).toHaveLength(0)
  })
})
