/**
 * Steering interrupt behavior in agent loop.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { installMockPlatform } from '../__test-utils__/mock-platform.js'
import { resetRegistries } from '../extensions/api.js'
import { resetProviders } from '../llm/client.js'
import type { ChatMessage, LLMClient, StreamDelta, ToolDefinition } from '../llm/types.js'
import { resetLogger } from '../logger/logger.js'
import { resetTools } from '../tools/registry.js'
import { AgentExecutor } from './loop.js'

function createMockClient(
  turns: StreamDelta[][],
  onStream?: (messages: ChatMessage[]) => void
): LLMClient {
  let turnIndex = 0
  return {
    async *stream(messages): AsyncGenerator<StreamDelta, void, unknown> {
      onStream?.(messages as ChatMessage[])
      const deltas = turns[turnIndex] ?? [{ content: 'fallback', done: true }]
      turnIndex++
      for (const delta of deltas) {
        yield delta
      }
    },
  }
}

function mockToolDef(name: string): ToolDefinition {
  return {
    name,
    description: `Mock ${name}`,
    input_schema: { type: 'object' as const, properties: {} },
  }
}

beforeEach(() => {
  installMockPlatform()
})

afterEach(() => {
  resetTools()
  resetRegistries()
  resetProviders()
  resetLogger()
  vi.restoreAllMocks()
})

describe('AgentExecutor steering interrupts', () => {
  it('steer during active tool call completes current and skips remaining', async () => {
    const events: Array<Record<string, unknown>> = []
    let streamCalls = 0

    const client = createMockClient([
      [
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-1',
            name: 'read_file',
            input: { path: '/a.txt' },
          },
        },
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-2',
            name: 'grep',
            input: { pattern: 'a' },
          },
        },
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-3',
            name: 'ls',
            input: {},
          },
        },
      ],
      [{ content: 'Acknowledged steering', done: true }],
    ])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
      mockToolDef('grep'),
      mockToolDef('ls'),
    ])

    const executeToolSpy = vi
      .spyOn(await import('../tools/registry.js'), 'executeTool')
      .mockImplementation(async (name: string) => {
        streamCalls++
        if (name === 'read_file') {
          await new Promise((resolve) => setTimeout(resolve, 40))
        }
        return { success: true, output: `ok:${name}` }
      })

    const executor = new AgentExecutor({ maxTurns: 3, parallelToolExecution: false }, (event) => {
      events.push(event as Record<string, unknown>)
    })

    const runPromise = executor.run(
      { goal: 'Do three tools', cwd: '/tmp' },
      AbortSignal.timeout(5000)
    )
    setTimeout(() => executor.steer('Please stop tooling and summarize'), 10)

    await runPromise

    expect(streamCalls).toBeGreaterThan(0)
    expect(executeToolSpy).toHaveBeenCalledTimes(1)
    const skippedEvent = events.find((event) => event.type === 'agent:tools-skipped')
    expect(skippedEvent).toBeDefined()
    expect(skippedEvent?.skippedTools).toEqual(['grep', 'ls'])
  })

  it('does not emit skipped event when steering queue is empty', async () => {
    const events: Array<Record<string, unknown>> = []
    const client = createMockClient([
      [
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-1',
            name: 'read_file',
            input: { path: '/a.txt' },
          },
        },
      ],
      [{ content: 'done', done: true }],
    ])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
    ])
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockResolvedValue({
      success: true,
      output: 'ok',
    })

    const executor = new AgentExecutor({ maxTurns: 3, parallelToolExecution: false }, (event) => {
      events.push(event as Record<string, unknown>)
    })
    await executor.run({ goal: 'One tool', cwd: '/tmp' }, AbortSignal.timeout(5000))

    const skippedEvent = events.find((event) => event.type === 'agent:tools-skipped')
    expect(skippedEvent).toBeUndefined()
  })

  it('reports skipped tool names in event payload', async () => {
    const events: Array<Record<string, unknown>> = []
    const client = createMockClient([
      [
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-1',
            name: 'read_file',
            input: { path: '/a.txt' },
          },
        },
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-2',
            name: 'glob',
            input: { path: '/tmp', pattern: '*' },
          },
        },
      ],
      [{ content: 'done', done: true }],
    ])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
      mockToolDef('glob'),
    ])
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockImplementation(
      async (name: string) => {
        if (name === 'read_file') {
          await new Promise((resolve) => setTimeout(resolve, 35))
        }
        return { success: true, output: 'ok' }
      }
    )

    const executor = new AgentExecutor({ maxTurns: 3, parallelToolExecution: false }, (event) => {
      events.push(event as Record<string, unknown>)
    })
    const runPromise = executor.run({ goal: 'Two tools', cwd: '/tmp' }, AbortSignal.timeout(5000))
    setTimeout(() => executor.steer('Change direction'), 10)
    await runPromise

    const skippedEvent = events.find((event) => event.type === 'agent:tools-skipped')
    expect(skippedEvent).toBeDefined()
    expect(skippedEvent?.reason).toBe('steering')
    expect(skippedEvent?.skippedTools).toEqual(['glob'])
  })

  it('includes steering skip notice and steered message in next turn context', async () => {
    const streamMessageSnapshots: ChatMessage[][] = []
    const client = createMockClient(
      [
        [
          {
            toolUse: {
              type: 'tool_use',
              id: 'call-1',
              name: 'read_file',
              input: { path: '/a.txt' },
            },
          },
          {
            toolUse: {
              type: 'tool_use',
              id: 'call-2',
              name: 'grep',
              input: { pattern: 'a' },
            },
          },
        ],
        [{ content: 'done', done: true }],
      ],
      (messages) => {
        streamMessageSnapshots.push(messages)
      }
    )

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
      mockToolDef('grep'),
    ])
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockImplementation(
      async (name: string) => {
        if (name === 'read_file') {
          await new Promise((resolve) => setTimeout(resolve, 35))
        }
        return { success: true, output: 'ok' }
      }
    )

    const executor = new AgentExecutor({ maxTurns: 3, parallelToolExecution: false })
    const runPromise = executor.run(
      { goal: 'Do two tools', cwd: '/tmp' },
      AbortSignal.timeout(5000)
    )
    setTimeout(() => executor.steer('New user direction'), 10)
    await runPromise

    expect(streamMessageSnapshots.length).toBeGreaterThanOrEqual(2)
    const secondTurnMessages = streamMessageSnapshots[1] ?? []
    const serialized = JSON.stringify(secondTurnMessages)
    expect(serialized).toContain('Steering interrupt: 1 pending tool calls skipped')
    expect(serialized).toContain('New user direction')
  })
})
