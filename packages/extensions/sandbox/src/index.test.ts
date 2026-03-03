import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('sandbox extension', () => {
  it('activates and logs', () => {
    const { api } = createMockExtensionAPI()
    activate(api)
    expect(api.log.debug).toHaveBeenCalledWith('Sandbox extension activated')
  })

  it('registers sandbox_run tool when native linux runtime is available', async () => {
    const { api, registeredTools } = createMockExtensionAPI()
    api.platform.shell.setResult('bwrap --version', {
      stdout: 'bwrap 0.8',
      stderr: '',
      exitCode: 0,
    })
    activate(api)

    await new Promise((r) => setTimeout(r, 50))

    expect(registeredTools).toHaveLength(1)
    expect(registeredTools[0].definition.name).toBe('sandbox_run')
  })

  it('emits sandbox:ready with runtime details', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    api.platform.shell.setResult('bwrap --version', {
      stdout: 'bwrap 0.8',
      stderr: '',
      exitCode: 0,
    })
    activate(api)

    await new Promise((r) => setTimeout(r, 50))

    const ready = emittedEvents.find((e) => e.event === 'sandbox:ready')
    expect(ready).toBeDefined()
    expect((ready!.data as { available: boolean }).available).toBe(true)
    expect((ready!.data as { runtime: string }).runtime).toBe('native')
  })

  it('registers tool with noop runtime when native and docker are unavailable', async () => {
    const { api, registeredTools } = createMockExtensionAPI()
    api.platform.shell.setResult('bwrap --version', {
      stdout: '',
      stderr: 'not found',
      exitCode: 127,
    })
    api.platform.shell.setResult('docker --version', {
      stdout: '',
      stderr: 'not found',
      exitCode: 127,
    })
    activate(api)

    await new Promise((r) => setTimeout(r, 50))

    expect(registeredTools).toHaveLength(1)
    expect(registeredTools[0].definition.name).toBe('sandbox_run')

    const result = await registeredTools[0].execute({ code: 'echo hi' })
    expect(result.success).toBe(false)
    expect(result.metadata?.runtime).toBe('noop')
  })

  it('cleans up on dispose', async () => {
    const { api, registeredTools } = createMockExtensionAPI()
    api.platform.shell.setResult('bwrap --version', {
      stdout: 'bwrap 0.8',
      stderr: '',
      exitCode: 0,
    })
    const disposable = activate(api)

    await new Promise((r) => setTimeout(r, 50))

    disposable.dispose()
    expect(registeredTools).toHaveLength(0)
  })
})
