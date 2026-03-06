import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import type { ToolMiddlewareContext } from '@ava/core-v2/extensions'
import type { ToolResult } from '@ava/core-v2/tools'
import { describe, expect, it } from 'vitest'
import { createProgressiveEscalationMiddleware } from './progressive-escalation.js'

function makeCtx(sessionId = 's1'): ToolMiddlewareContext {
  return {
    toolName: 'edit',
    definition: {
      name: 'edit',
      description: 'edit file',
      input_schema: { type: 'object', properties: {} },
    },
    args: { path: '/tmp/a.ts' },
    ctx: {
      workingDirectory: '/tmp',
      sessionId,
      signal: AbortSignal.abort(),
    },
  }
}

function failedResult(message: string): ToolResult {
  return {
    success: false,
    output: message,
    error: message,
  }
}

describe('progressive escalation middleware', () => {
  it('single failure emits level 1 message', async () => {
    const { api } = createMockExtensionAPI()
    const middleware = createProgressiveEscalationMiddleware(api, api.log)

    const response = await middleware.after?.(makeCtx(), failedResult('replace failed'))
    const output = response?.result?.output ?? ''

    expect(output).toContain('[Escalation L1]')
    expect(output).toContain('Please retry with a different approach')
  })

  it('two consecutive failures suggests strategy switch', async () => {
    const { api } = createMockExtensionAPI()
    const middleware = createProgressiveEscalationMiddleware(api, api.log)

    await middleware.after?.(makeCtx(), failedResult('first'))
    const response = await middleware.after?.(makeCtx(), failedResult('second'))
    const escalation = response?.result?.metadata?.escalation as Record<string, unknown>

    expect(response?.result?.output).toContain('[Escalation L2]')
    expect(response?.result?.output).toContain('fundamentally different strategy')
    expect(escalation.forceStrategy).toBe('write_file')
  })

  it('three consecutive failures triggers context compression signal', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    const middleware = createProgressiveEscalationMiddleware(api, api.log)

    await middleware.after?.(makeCtx(), failedResult('one'))
    await middleware.after?.(makeCtx(), failedResult('two'))
    const response = await middleware.after?.(makeCtx(), failedResult('three'))

    expect(response?.result?.output).toContain('[Escalation L3]')
    expect(response?.result?.output).toContain('deeper issue')
    expect(emittedEvents.some((event) => event.event === 'escalation:max-reached')).toBe(true)
    expect(emittedEvents.some((event) => event.event === 'context:compacting')).toBe(true)
  })

  it('success resets counter to zero', async () => {
    const { api } = createMockExtensionAPI()
    const middleware = createProgressiveEscalationMiddleware(api, api.log)

    await middleware.after?.(makeCtx(), failedResult('one'))
    await middleware.after?.(makeCtx(), failedResult('two'))
    await middleware.after?.(makeCtx(), { success: true, output: 'ok' })
    const response = await middleware.after?.(makeCtx(), failedResult('after reset'))

    expect(response?.result?.output).toContain('[Escalation L1]')
  })

  it('five failures emits stuck detected event', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    const middleware = createProgressiveEscalationMiddleware(api, api.log)

    for (let i = 0; i < 5; i++) {
      await middleware.after?.(makeCtx(), failedResult(`fail-${i}`))
    }

    const stuck = emittedEvents.find((event) => event.event === 'stuck:detected')
    expect(stuck).toBeDefined()
    expect(stuck?.data).toMatchObject({ scenario: 'error-escalation', count: 5 })
  })
})
