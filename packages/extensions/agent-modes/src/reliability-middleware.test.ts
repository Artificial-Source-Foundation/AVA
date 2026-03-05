import type { ToolMiddlewareContext } from '@ava/core-v2/extensions'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { createMockExtensionAPI } from '../../../core-v2/src/__test-utils__/mock-extension-api.js'
import { createReliabilityMiddleware } from './reliability-middleware.js'

const { dispatchComputeMock } = vi.hoisted(() => ({ dispatchComputeMock: vi.fn() }))

vi.mock('@ava/core-v2', () => ({
  dispatchCompute: dispatchComputeMock,
}))

function makeCtx(sessionId: string, toolName = 'edit'): ToolMiddlewareContext {
  return {
    toolName,
    args: { filePath: '/workspace/src/app.ts', oldString: 'a', newString: 'b' },
    ctx: {
      sessionId,
      workingDirectory: '/workspace',
      signal: new AbortController().signal,
    },
    definition: {
      name: toolName,
      description: '',
      input_schema: { type: 'object', properties: {} },
    },
  }
}

describe('createReliabilityMiddleware', () => {
  afterEach(() => {
    dispatchComputeMock.mockReset()
  })

  it('has priority 5', () => {
    const { api } = createMockExtensionAPI()
    const { middleware } = createReliabilityMiddleware(api as never)
    expect(middleware.priority).toBe(5)
  })

  it('escalates repeated calls and blocks on second stuck hit', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    const { middleware } = createReliabilityMiddleware(api as never)
    const ctx = makeCtx('s1')

    await middleware.before?.(ctx)
    await middleware.before?.(ctx)
    const blocked = await middleware.before?.(ctx)

    expect(blocked?.blocked).toBe(true)
    expect(emittedEvents.some((event) => event.event === 'stuck:detected')).toBe(true)
  })

  it('detects no-file-progress turns and blocks after steering', async () => {
    const { api } = createMockExtensionAPI()
    const { middleware } = createReliabilityMiddleware(api as never)

    for (let turn = 1; turn <= 5; turn += 1) {
      api.emit('turn:end', { agentId: 's2', turn, toolCalls: [] })
    }

    await middleware.before?.(makeCtx('s2', 'grep'))
    const blocked = await middleware.before?.(makeCtx('s2', 'grep'))
    expect(blocked?.blocked).toBe(true)
  })

  it('detects token budget over 90%', async () => {
    const { api } = createMockExtensionAPI()
    const { middleware } = createReliabilityMiddleware(api as never)
    api.emit('llm:usage', { sessionId: 's3', inputTokens: 120000, outputTokens: 5000 })
    api.emit('llm:usage', { sessionId: 's3', inputTokens: 70000, outputTokens: 10000 })

    await middleware.before?.(makeCtx('s3', 'glob'))
    const blocked = await middleware.before?.(makeCtx('s3', 'glob'))
    expect(blocked?.blocked).toBe(true)
  })

  it('auto-completes when summary-like output validates modified files', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    dispatchComputeMock.mockResolvedValue({ valid: true })
    await api.platform.fs.mkdir('/workspace/src')
    await api.platform.fs.writeFile('/workspace/src/app.ts', 'const a = 1\n')

    const registration = createReliabilityMiddleware(api as never)
    void registration

    api.emit('turn:end', {
      agentId: 's4',
      turn: 1,
      toolCalls: [
        {
          name: 'edit',
          args: { filePath: '/workspace/src/app.ts' },
          success: true,
        },
      ],
    })
    api.emit('turn:end', { agentId: 's4', turn: 2, toolCalls: [] })
    api.emit('agent:finish', {
      agentId: 's4',
      result: { output: 'Summary: completed requested changes and finished validation.' },
    })

    await new Promise((resolve) => setTimeout(resolve, 0))
    expect(emittedEvents.some((event) => event.event === 'agent:auto-completed')).toBe(true)
  })

  it('emits stuck signal when completion validation fails', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    dispatchComputeMock.mockResolvedValue({ valid: false })
    await api.platform.fs.mkdir('/workspace/src')
    await api.platform.fs.writeFile('/workspace/src/app.ts', 'const a = 1\n')

    createReliabilityMiddleware(api as never)

    api.emit('turn:end', {
      agentId: 's5',
      turn: 1,
      toolCalls: [
        {
          name: 'edit',
          args: { filePath: '/workspace/src/app.ts' },
          success: true,
        },
      ],
    })
    api.emit('turn:end', { agentId: 's5', turn: 2, toolCalls: [] })
    api.emit('agent:finish', {
      agentId: 's5',
      result: { output: 'Summary: completed requested changes and finished validation.' },
    })

    await new Promise((resolve) => setTimeout(resolve, 0))
    expect(emittedEvents.some((event) => event.event === 'stuck:detected')).toBe(true)
  })
})
