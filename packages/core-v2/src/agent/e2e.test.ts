/**
 * End-to-end agent test — mock provider + real tool execution.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { installMockPlatform } from '../__test-utils__/mock-platform.js'
import { resetRegistries } from '../extensions/api.js'
import { registerProvider, resetProviders } from '../llm/client.js'
import type { StreamDelta } from '../llm/types.js'
import { resetLogger } from '../logger/logger.js'
import { registerCoreTools } from '../tools/index.js'
import { getToolDefinitions, resetTools } from '../tools/registry.js'
import { AgentExecutor } from './loop.js'
import { AgentTerminateMode } from './types.js'

beforeEach(() => {
  installMockPlatform()
  registerCoreTools()
})

afterEach(() => {
  resetTools()
  resetRegistries()
  resetProviders()
  resetLogger()
  vi.restoreAllMocks()
})

describe('E2E: Agent with real core tools', () => {
  it('registers all 6 core tools', () => {
    const tools = getToolDefinitions()
    const names = tools.map((t) => t.name).sort()
    expect(names).toEqual(['bash', 'edit', 'glob', 'grep', 'read_file', 'write_file'])
  })

  it('executes glob tool via agent loop', async () => {
    let turn = 0
    registerProvider('test', () => ({
      async *stream(): AsyncGenerator<StreamDelta, void, unknown> {
        turn++
        if (turn === 1) {
          yield {
            toolUse: {
              type: 'tool_use',
              id: 'c1',
              name: 'glob',
              input: { pattern: '*.nonexistent-ext-xyz', path: '/tmp' },
            },
          }
        } else {
          yield { content: 'Glob completed. No matching files found.' }
        }
        yield { done: true }
      },
    }))

    const events: Array<{ type: string; toolName?: string; success?: boolean }> = []
    const exec = new AgentExecutor({ provider: 'test' as unknown, maxTurns: 5 }, (e) =>
      events.push(e as unknown)
    )

    const result = await exec.run({ goal: 'Find files', cwd: '/tmp' }, AbortSignal.timeout(10000))

    expect(result.success).toBe(true)
    expect(result.terminateMode).toBe(AgentTerminateMode.GOAL)
    expect(result.turns).toBe(2)

    // Verify tool events were emitted
    const toolStart = events.find((e) => e.type === 'tool:start' && e.toolName === 'glob')
    const toolFinish = events.find((e) => e.type === 'tool:finish' && e.toolName === 'glob')
    expect(toolStart).toBeDefined()
    expect(toolFinish).toBeDefined()
    expect(toolFinish!.success).toBe(true)
  })

  it('allowedTools restricts available tools', async () => {
    let receivedTools: string[] = []
    registerProvider('test', () => ({
      async *stream(_msgs, config): AsyncGenerator<StreamDelta, void, unknown> {
        receivedTools = (config.tools ?? []).map((t) => t.name)
        yield { content: 'Done' }
        yield { done: true }
      },
    }))

    const exec = new AgentExecutor({
      provider: 'test' as unknown,
      maxTurns: 5,
      allowedTools: ['read_file', 'grep'],
    })

    await exec.run({ goal: 'Search', cwd: '/tmp' }, AbortSignal.timeout(5000))

    expect(receivedTools.sort()).toEqual(['grep', 'read_file'])
  })
})
