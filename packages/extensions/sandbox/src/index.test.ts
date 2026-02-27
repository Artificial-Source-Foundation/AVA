import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('sandbox extension', () => {
  it('activates and logs', () => {
    const { api } = createMockExtensionAPI()
    activate(api)
    expect(api.log.debug).toHaveBeenCalledWith('Sandbox extension activated')
  })

  it('registers sandbox_run tool when docker is available', async () => {
    const { api, registeredTools } = createMockExtensionAPI()
    api.platform.shell.setResult('docker --version', {
      stdout: 'Docker version 24.0.0',
      stderr: '',
      exitCode: 0,
    })
    activate(api)

    await new Promise((r) => setTimeout(r, 50))

    expect(registeredTools).toHaveLength(1)
    expect(registeredTools[0].definition.name).toBe('sandbox_run')
  })

  it('emits sandbox:ready with available=true when docker is available', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    api.platform.shell.setResult('docker --version', {
      stdout: 'Docker version 24.0.0',
      stderr: '',
      exitCode: 0,
    })
    activate(api)

    await new Promise((r) => setTimeout(r, 50))

    const ready = emittedEvents.find((e) => e.event === 'sandbox:ready')
    expect(ready).toBeDefined()
    expect((ready!.data as { available: boolean }).available).toBe(true)
  })

  it('does not register tool when docker is unavailable', async () => {
    const { api, registeredTools } = createMockExtensionAPI()
    api.platform.shell.setResult('docker --version', {
      stdout: '',
      stderr: 'not found',
      exitCode: 127,
    })
    activate(api)

    await new Promise((r) => setTimeout(r, 50))

    expect(registeredTools).toHaveLength(0)
  })

  it('cleans up on dispose', async () => {
    const { api, registeredTools } = createMockExtensionAPI()
    api.platform.shell.setResult('docker --version', {
      stdout: 'Docker version 24.0.0',
      stderr: '',
      exitCode: 0,
    })
    const disposable = activate(api)

    await new Promise((r) => setTimeout(r, 50))

    disposable.dispose()
    expect(registeredTools).toHaveLength(0)
  })
})
