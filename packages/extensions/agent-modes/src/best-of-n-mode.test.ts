import type { ExtensionAPI, ToolMiddleware, ToolResult } from '@ava/core-v2/extensions'
import { describe, expect, it, vi } from 'vitest'
import { bestOfNAgentMode, registerBestOfNMode } from './best-of-n-mode.js'

function createApi(bestOfN = 1): {
  api: ExtensionAPI
  middleware: ToolMiddleware[]
  hookCallbacks: Array<(value: unknown) => unknown>
} {
  const middleware: ToolMiddleware[] = []
  const hookCallbacks: Array<(value: unknown) => unknown> = []

  const api = {
    registerTool: vi.fn(),
    unregisterTool: vi.fn(),
    registerHook: vi.fn((_name: string, cb: (value: unknown) => unknown) => {
      hookCallbacks.push(cb)
      return { dispose: vi.fn() }
    }),
    callHook: vi.fn(),
    addToolMiddleware: vi.fn((mw: ToolMiddleware) => {
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

  return { api: api as unknown as ExtensionAPI, middleware, hookCallbacks }
}

describe('bestOfNAgentMode', () => {
  it('has expected metadata', () => {
    expect(bestOfNAgentMode.name).toBe('best-of-n')
    expect(bestOfNAgentMode.description.toLowerCase()).toContain('candidate')
  })

  it('generates a system prompt', () => {
    const prompt = bestOfNAgentMode.systemPrompt?.('')
    expect(prompt).toContain('best-of-N sampling mode')
  })
})

describe('registerBestOfNMode', () => {
  it('registers mode, middleware, and hook', () => {
    const { api } = createApi(3)
    registerBestOfNMode(api)

    expect(api.registerAgentMode).toHaveBeenCalledTimes(1)
    expect(api.addToolMiddleware).toHaveBeenCalledTimes(1)
    expect(api.registerHook).toHaveBeenCalledWith('tool:beforeExecute', expect.any(Function))
  })

  it('middleware scores tool results when N > 1', async () => {
    const { api, middleware } = createApi(3)
    registerBestOfNMode(api)

    const mw = middleware[0]
    expect(mw).toBeDefined()

    const result: ToolResult = { success: true, output: 'hello world' }
    const context = {
      toolName: 'read_file',
      args: { path: '/test.ts' },
      ctx: { sessionId: 's1', workingDirectory: '/tmp' },
      definition: { name: 'read_file', description: '', input_schema: {} },
    }

    const mwResult = await mw!.after?.(context as never, result)
    expect(mwResult).toBeDefined()
    expect(mwResult!.result).toBeDefined()

    const meta = mwResult!.result!.metadata as Record<string, unknown>
    const bestOfN = meta.bestOfN as Record<string, unknown>
    expect(bestOfN.enabled).toBe(true)
    expect(bestOfN.configuredN).toBe(3)
    expect(typeof bestOfN.score).toBe('number')
  })

  it('middleware skips scoring when N = 1', async () => {
    const { api, middleware } = createApi(1)
    registerBestOfNMode(api)

    const mw = middleware[0]
    const result: ToolResult = { success: true, output: 'hello' }
    const context = {
      toolName: 'read_file',
      args: {},
      ctx: { sessionId: 's1', workingDirectory: '/tmp' },
      definition: { name: 'read_file', description: '', input_schema: {} },
    }

    const mwResult = await mw!.after?.(context as never, result)
    expect(mwResult).toBeUndefined()
  })

  it('hook selects best candidate when loop provides multiple', async () => {
    const { api, hookCallbacks } = createApi(3)
    registerBestOfNMode(api)

    const hook = hookCallbacks[0]!
    const payload = {
      toolName: 'read_file',
      candidates: [
        { id: 'c0', success: false, output: '', estimatedCost: 0 },
        { id: 'c1', success: true, output: 'good result with detail', estimatedCost: 0 },
        { id: 'c2', success: true, output: 'ok', estimatedCost: 50 },
      ],
    }

    const result = (await hook(payload)) as Record<string, unknown>
    expect(result.selectedCandidate).toBeDefined()
    const selected = result.selectedCandidate as { id: string; success: boolean }
    expect(selected.id).toBe('c1') // highest scorer: success + longest output
  })
})
