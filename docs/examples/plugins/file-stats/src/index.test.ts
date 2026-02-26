import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('file-stats plugin', () => {
  it('registers the file_stats tool', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    activate(api)
    expect(registeredTools).toHaveLength(1)
    expect(registeredTools[0].definition.name).toBe('file_stats')
  })

  it('logs activation message', () => {
    const { api } = createMockExtensionAPI()
    activate(api)
    expect(api.log.info).toHaveBeenCalledWith('File stats tool registered')
  })

  it('cleans up on dispose', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(registeredTools).toHaveLength(1)
    disposable.dispose()
    expect(registeredTools).toHaveLength(0)
  })
})
