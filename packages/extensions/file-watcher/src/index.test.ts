/**
 * Activation test for file-watcher extension.
 */

import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('file-watcher extension', () => {
  it('activates and listens for session:opened', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activate(api)

    expect(eventHandlers.has('session:opened')).toBe(true)
    disposable.dispose()
  })

  it('cleans up on dispose', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activate(api)

    expect(eventHandlers.has('session:opened')).toBe(true)
    disposable.dispose()
    expect(eventHandlers.has('session:opened')).toBe(false)
  })

  it('creates watcher on session:opened event', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activate(api)

    // Simulate session:opened event
    const handlers = eventHandlers.get('session:opened')
    expect(handlers).toBeDefined()
    expect(handlers!.size).toBe(1)

    // Trigger the handler — it should not throw
    const handler = [...handlers!][0]!
    expect(() =>
      handler({ sessionId: 'test', workingDirectory: '/tmp/test-project' })
    ).not.toThrow()

    disposable.dispose()
  })

  it('logs activation message', () => {
    const { api } = createMockExtensionAPI()
    const disposable = activate(api)

    expect(api.log.debug).toHaveBeenCalledWith('File watcher extension activated')
    disposable.dispose()
  })
})
