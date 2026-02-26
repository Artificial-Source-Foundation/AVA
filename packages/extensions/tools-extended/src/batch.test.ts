/**
 * batch tool — execute multiple tools in parallel.
 */

import { installMockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { resetRegistries } from '@ava/core-v2/extensions'
import { resetLogger } from '@ava/core-v2/logger'
import { defineTool, registerTool, resetTools } from '@ava/core-v2/tools'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import * as z from 'zod'
import { batchTool } from './batch.js'

function makeCtx() {
  return {
    sessionId: 'test',
    workingDirectory: '/tmp',
    signal: AbortSignal.timeout(5000),
  }
}

beforeEach(() => {
  installMockPlatform()
  resetTools()
  resetRegistries()

  // Register the batch tool itself
  registerTool(batchTool)

  // Register a simple echo tool for testing
  const echoTool = defineTool({
    name: 'echo',
    description: 'Echo input',
    schema: z.object({ message: z.string() }),
    async execute(input) {
      return { success: true, output: input.message }
    },
  })
  registerTool(echoTool)

  // Register a failing tool
  const failTool = defineTool({
    name: 'fail',
    description: 'Always fails',
    schema: z.object({}),
    async execute() {
      return { success: false, output: '', error: 'Intentional failure' }
    },
  })
  registerTool(failTool)
})

afterEach(() => {
  resetTools()
  resetRegistries()
  resetLogger()
})

describe('batchTool', () => {
  it('has correct name', () => {
    expect(batchTool.definition.name).toBe('batch')
  })

  it('returns error for empty tool_calls', async () => {
    const result = await batchTool.execute({ tool_calls: [] }, makeCtx())
    expect(result.success).toBe(false)
    expect(result.error).toBe('No tool calls provided')
  })

  it('returns error for recursive batch calls', async () => {
    const result = await batchTool.execute(
      {
        tool_calls: [{ tool: 'batch', parameters: { tool_calls: [] } }],
      },
      makeCtx()
    )
    expect(result.success).toBe(false)
    expect(result.error).toBe('Cannot nest batch calls')
  })

  it('executes a single tool call successfully', async () => {
    const result = await batchTool.execute(
      {
        tool_calls: [{ tool: 'echo', parameters: { message: 'hello' } }],
      },
      makeCtx()
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('[OK] echo: hello')
  })

  it('executes multiple tool calls in parallel', async () => {
    const result = await batchTool.execute(
      {
        tool_calls: [
          { tool: 'echo', parameters: { message: 'first' } },
          { tool: 'echo', parameters: { message: 'second' } },
        ],
      },
      makeCtx()
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('[OK] echo: first')
    expect(result.output).toContain('[OK] echo: second')
  })

  it('reports allSuccess=false when one tool fails', async () => {
    const result = await batchTool.execute(
      {
        tool_calls: [
          { tool: 'echo', parameters: { message: 'works' } },
          { tool: 'fail', parameters: {} },
        ],
      },
      makeCtx()
    )
    expect(result.success).toBe(false)
    expect(result.output).toContain('[OK] echo: works')
    expect(result.output).toContain('[ERROR] fail:')
  })

  it('reports allSuccess=true when all tools succeed', async () => {
    const result = await batchTool.execute(
      {
        tool_calls: [
          { tool: 'echo', parameters: { message: 'a' } },
          { tool: 'echo', parameters: { message: 'b' } },
          { tool: 'echo', parameters: { message: 'c' } },
        ],
      },
      makeCtx()
    )
    expect(result.success).toBe(true)
  })

  it('handles unknown tool gracefully', async () => {
    const result = await batchTool.execute(
      {
        tool_calls: [{ tool: 'nonexistent', parameters: {} }],
      },
      makeCtx()
    )
    expect(result.success).toBe(false)
    expect(result.output).toContain('[ERROR] nonexistent:')
  })
})
