import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('polite-middleware plugin', () => {
  it('registers a tool middleware', () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    activate(api)
    expect(registeredMiddleware).toHaveLength(1)
    expect(registeredMiddleware[0].name).toBe('polite-reminder')
    expect(registeredMiddleware[0].priority).toBe(100)
  })

  it('middleware has an after hook', () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    activate(api)
    expect(registeredMiddleware[0].after).toBeTypeOf('function')
  })

  it('cleans up on dispose', () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(registeredMiddleware).toHaveLength(1)
    disposable.dispose()
    expect(registeredMiddleware).toHaveLength(0)
  })
})
