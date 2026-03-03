import type { ExtensionAPI, ToolMiddleware } from '@ava/core-v2/extensions'
import { describe, expect, it, vi } from 'vitest'
import { bestOfNAgentMode, registerBestOfNMode } from './best-of-n-mode.js'

function createApi(bestOfN = 1): ExtensionAPI {
  const middleware: ToolMiddleware[] = []
  const hookCallbacks: Array<(value: unknown) => unknown> = []

  return {
    registerTool: vi.fn(),
    unregisterTool: vi.fn(),
    registerHook: vi.fn((_name, cb) => {
      hookCallbacks.push(cb)
      return { dispose: vi.fn() }
    }),
    callHook: vi.fn(),
    addToolMiddleware: vi.fn((mw) => {
      middleware.push(mw)
      return { dispose: vi.fn() }
    }),
    registerCommand: vi.fn(),
    registerAgentMode: vi.fn(() => ({ dispose: vi.fn() })),
    addTemplate: vi.fn(),
    getSettings: vi.fn(() => ({ bestOfN })),
    onSettingsChanged: vi.fn(() => ({ dispose: vi.fn() })),
    getProjectInstructions: vi.fn(),
    session: {} as never,
    permissions: {} as never,
    platform: {} as never,
    storage: {} as never,
    log: {
      debug: vi.fn(),
      info: vi.fn(),
      warn: vi.fn(),
      error: vi.fn(),
    },
    emit: vi.fn(),
    on: vi.fn(() => ({ dispose: vi.fn() })),
  }
}

describe('bestOfNAgentMode', () => {
  it('has expected metadata', () => {
    expect(bestOfNAgentMode.name).toBe('best-of-n')
    expect(bestOfNAgentMode.description.toLowerCase()).toContain('candidate')
  })

  it('generates a quality prompt when requested', () => {
    const prompt = bestOfNAgentMode.systemPrompt?.('quality')
    expect(prompt).toContain('Generate 3 candidate actions')
  })
})

describe('registerBestOfNMode', () => {
  it('registers mode, middleware, and hook', async () => {
    const api = createApi(3)
    registerBestOfNMode(api)

    expect(api.registerAgentMode).toHaveBeenCalledTimes(1)
    expect(api.addToolMiddleware).toHaveBeenCalledTimes(1)
    expect(api.registerHook).toHaveBeenCalledWith('tool:beforeExecute', expect.any(Function))
  })
})
