import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('sandbox extension', () => {
  it('activates successfully', () => {
    const { api } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(disposable).toBeDefined()
    expect(disposable.dispose).toBeTypeOf('function')
  })

  it('logs activation message', () => {
    const { api } = createMockExtensionAPI()
    activate(api)
    expect(api.log.debug).toHaveBeenCalledWith('Sandbox extension activated')
  })

  it('cleans up on dispose', () => {
    const { api } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(() => disposable.dispose()).not.toThrow()
  })
})
