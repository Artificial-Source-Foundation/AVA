import type { ToolMiddlewareContext } from '@ava/core-v2/extensions'
import { describe, expect, it } from 'vitest'
import { createSmartApproveMiddleware, READ_ONLY_TOOLS } from './smart-approve.js'

function makeCtx(toolName: string): ToolMiddlewareContext {
  return {
    toolName,
    args: {},
    ctx: {
      sessionId: 's1',
      workingDirectory: '/workspace',
      signal: new AbortController().signal,
    },
    definition: {
      name: toolName,
      description: 'test',
      input_schema: { type: 'object', properties: {} },
    },
  }
}

describe('smart approve middleware', () => {
  it('uses priority 2', () => {
    expect(createSmartApproveMiddleware().priority).toBe(2)
  })

  it('marks read-only tools as approved', async () => {
    const middleware = createSmartApproveMiddleware()
    const result = await middleware.before?.(makeCtx('read_file'))
    expect((result?.args as { approved?: boolean })?.approved).toBe(true)
  })

  it('does not approve write tools', async () => {
    const middleware = createSmartApproveMiddleware()
    const result = await middleware.before?.(makeCtx('write_file'))
    expect(result).toBeUndefined()
  })

  it('includes expected read-only tools', () => {
    expect(READ_ONLY_TOOLS.has('question')).toBe(true)
    expect(READ_ONLY_TOOLS.has('lsp_definition')).toBe(true)
    expect(READ_ONLY_TOOLS.has('write_file')).toBe(false)
  })
})
