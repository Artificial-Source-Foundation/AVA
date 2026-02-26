/**
 * Agent loop — AgentExecutor and runAgent.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { installMockPlatform } from '../__test-utils__/mock-platform.js'
import { resetRegistries } from '../extensions/api.js'
import { resetProviders } from '../llm/client.js'
import type { LLMClient, StreamDelta, ToolDefinition } from '../llm/types.js'
import { resetLogger } from '../logger/logger.js'
import { resetTools } from '../tools/registry.js'
import { AgentExecutor, runAgent } from './loop.js'
import { AgentTerminateMode, COMPLETE_TASK_TOOL, DEFAULT_AGENT_CONFIG } from './types.js'

// ─── Helpers ────────────────────────────────────────────────────────────────

/** Create a mock LLM client that yields predetermined deltas per turn. */
function createMockClient(turns: StreamDelta[][]): LLMClient {
  let turnIndex = 0
  return {
    async *stream(): AsyncGenerator<StreamDelta, void, unknown> {
      const deltas = turns[turnIndex] ?? [{ content: 'fallback', done: true }]
      turnIndex++
      for (const delta of deltas) {
        yield delta
      }
    },
  }
}

/** Create a mock tool definition. */
function mockToolDef(name: string): ToolDefinition {
  return {
    name,
    description: `Mock ${name}`,
    input_schema: { type: 'object' as const, properties: {} },
  }
}

// ─── Setup / Teardown ───────────────────────────────────────────────────────

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

// ─── Constructor ────────────────────────────────────────────────────────────

describe('AgentExecutor constructor', () => {
  it('merges defaults with provided config', () => {
    const exec = new AgentExecutor({ maxTurns: 10 })
    expect(exec.config.maxTurns).toBe(10)
    expect(exec.config.provider).toBe(DEFAULT_AGENT_CONFIG.provider)
    expect(exec.config.model).toBe(DEFAULT_AGENT_CONFIG.model)
  })

  it('uses provided maxTimeMinutes and maxTurns', () => {
    const exec = new AgentExecutor({ maxTimeMinutes: 5, maxTurns: 3 })
    expect(exec.config.maxTimeMinutes).toBe(5)
    expect(exec.config.maxTurns).toBe(3)
  })

  it('defaults maxTimeMinutes to 30 and maxTurns to 50', () => {
    const exec = new AgentExecutor({})
    expect(exec.config.maxTimeMinutes).toBe(30)
    expect(exec.config.maxTurns).toBe(50)
  })

  it('uses provided id', () => {
    const exec = new AgentExecutor({ id: 'my-agent' })
    expect(exec.agentId).toBe('my-agent')
  })

  it('auto-generates agentId when no id provided', () => {
    const exec = new AgentExecutor({})
    expect(exec.agentId).toBeDefined()
    expect(typeof exec.agentId).toBe('string')
    expect(exec.agentId.length).toBeGreaterThan(0)
  })
})

// ─── buildSystemPrompt (via run behavior) ──────────────────────────────────

describe('AgentExecutor.run', () => {
  it('single turn no tool calls — returns GOAL', async () => {
    const client = createMockClient([[{ content: 'Done!', done: true }]])

    // Mock createClient to return our mock
    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([])

    const exec = new AgentExecutor({ maxTurns: 5 })
    const result = await exec.run({ goal: 'Say hello', cwd: '/tmp' }, AbortSignal.timeout(5000))

    expect(result.terminateMode).toBe(AgentTerminateMode.GOAL)
    expect(result.success).toBe(true)
    expect(result.output).toBe('Done!')
    expect(result.turns).toBe(1)
  })

  it('single turn with tool calls that succeed', async () => {
    const client = createMockClient([
      // Turn 1: LLM returns a tool call
      [
        { content: 'Let me read the file' },
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-1',
            name: 'read_file',
            input: { path: '/test.txt' },
          },
        },
      ],
      // Turn 2: LLM done after seeing result
      [{ content: 'The file contains hello.', done: true }],
    ])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
    ])
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockResolvedValue({
      success: true,
      output: 'hello world',
    })

    const exec = new AgentExecutor({ maxTurns: 5 })
    const result = await exec.run({ goal: 'Read test.txt', cwd: '/tmp' }, AbortSignal.timeout(5000))

    expect(result.terminateMode).toBe(AgentTerminateMode.GOAL)
    expect(result.success).toBe(true)
    expect(result.turns).toBe(2)
  })

  it('attempt_completion tool triggers GOAL terminate', async () => {
    const client = createMockClient([
      [
        { content: 'Task complete.' },
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-1',
            name: COMPLETE_TASK_TOOL,
            input: { result: 'All done!' },
          },
        },
      ],
    ])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([])

    const exec = new AgentExecutor({ maxTurns: 5 })
    const result = await exec.run({ goal: 'Do the task', cwd: '/tmp' }, AbortSignal.timeout(5000))

    expect(result.terminateMode).toBe(AgentTerminateMode.GOAL)
    expect(result.output).toBe('All done!')
  })

  it('LLM stream error triggers ERROR terminate', async () => {
    const client = createMockClient([
      [{ error: { type: 'server', message: 'Internal server error' } }],
    ])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([])

    const exec = new AgentExecutor({ maxTurns: 5 })
    const result = await exec.run({ goal: 'Do something', cwd: '/tmp' }, AbortSignal.timeout(5000))

    expect(result.terminateMode).toBe(AgentTerminateMode.ERROR)
    expect(result.success).toBe(false)
    expect(result.output).toBe('Internal server error')
  })

  it('abort signal triggers ABORTED terminate', async () => {
    const controller = new AbortController()
    // Abort immediately
    controller.abort()

    const client = createMockClient([[{ content: 'should not run' }]])
    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([])

    const exec = new AgentExecutor({ maxTurns: 5 })
    const result = await exec.run({ goal: 'Do something', cwd: '/tmp' }, controller.signal)

    expect(result.terminateMode).toBe(AgentTerminateMode.ABORTED)
    expect(result.success).toBe(false)
  })

  it('max turns triggers MAX_TURNS terminate', async () => {
    // Each turn returns a tool call so the loop continues
    const turns: StreamDelta[][] = []
    for (let i = 0; i < 5; i++) {
      turns.push([
        {
          toolUse: {
            type: 'tool_use',
            id: `call-${i}`,
            name: 'read_file',
            input: { path: '/test.txt' },
          },
        },
      ])
    }

    const client = createMockClient(turns)
    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
    ])
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockResolvedValue({
      success: true,
      output: 'content',
    })

    const exec = new AgentExecutor({ maxTurns: 3 })
    const result = await exec.run({ goal: 'Keep reading', cwd: '/tmp' }, AbortSignal.timeout(5000))

    expect(result.terminateMode).toBe(AgentTerminateMode.MAX_TURNS)
    expect(result.success).toBe(false)
    expect(result.turns).toBe(3)
  })

  it('emits agent:start event', async () => {
    const events: unknown[] = []
    const onEvent = vi.fn((e: unknown) => events.push(e))

    const client = createMockClient([[{ content: 'Hi', done: true }]])
    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([])

    const exec = new AgentExecutor({ maxTurns: 5 }, onEvent)
    await exec.run({ goal: 'Hello', cwd: '/tmp' }, AbortSignal.timeout(5000))

    const startEvent = events.find(
      (e: unknown) => (e as Record<string, unknown>).type === 'agent:start'
    )
    expect(startEvent).toBeDefined()
    expect((startEvent as Record<string, unknown>).goal).toBe('Hello')
  })

  it('emits turn:start and turn:end events', async () => {
    const events: unknown[] = []
    const onEvent = vi.fn((e: unknown) => events.push(e))

    const client = createMockClient([[{ content: 'Done', done: true }]])
    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([])

    const exec = new AgentExecutor({ maxTurns: 5 }, onEvent)
    await exec.run({ goal: 'Test', cwd: '/tmp' }, AbortSignal.timeout(5000))

    const turnStart = events.find(
      (e: unknown) => (e as Record<string, unknown>).type === 'turn:start'
    )
    const turnEnd = events.find((e: unknown) => (e as Record<string, unknown>).type === 'turn:end')
    expect(turnStart).toBeDefined()
    expect(turnEnd).toBeDefined()
    expect((turnStart as Record<string, unknown>).turn).toBe(1)
  })

  it('emits thought event for assistant content', async () => {
    const events: unknown[] = []
    const onEvent = vi.fn((e: unknown) => events.push(e))

    const client = createMockClient([[{ content: 'I am thinking...', done: true }]])
    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([])

    const exec = new AgentExecutor({ maxTurns: 5 }, onEvent)
    await exec.run({ goal: 'Think', cwd: '/tmp' }, AbortSignal.timeout(5000))

    const thoughtEvent = events.find(
      (e: unknown) => (e as Record<string, unknown>).type === 'thought'
    )
    expect(thoughtEvent).toBeDefined()
    expect((thoughtEvent as Record<string, unknown>).content).toBe('I am thinking...')
  })

  it('emits tool:start and tool:finish events', async () => {
    const events: unknown[] = []
    const onEvent = vi.fn((e: unknown) => events.push(e))

    const client = createMockClient([
      [
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-1',
            name: 'read_file',
            input: { path: '/test.txt' },
          },
        },
      ],
      [{ content: 'Done', done: true }],
    ])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
    ])
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockResolvedValue({
      success: true,
      output: 'content',
    })

    const exec = new AgentExecutor({ maxTurns: 5 }, onEvent)
    await exec.run({ goal: 'Read', cwd: '/tmp' }, AbortSignal.timeout(5000))

    const toolStart = events.find(
      (e: unknown) => (e as Record<string, unknown>).type === 'tool:start'
    )
    const toolFinish = events.find(
      (e: unknown) => (e as Record<string, unknown>).type === 'tool:finish'
    )
    expect(toolStart).toBeDefined()
    expect((toolStart as Record<string, unknown>).toolName).toBe('read_file')
    expect(toolFinish).toBeDefined()
    expect((toolFinish as Record<string, unknown>).success).toBe(true)
  })

  it('emits agent:finish event', async () => {
    const events: unknown[] = []
    const onEvent = vi.fn((e: unknown) => events.push(e))

    const client = createMockClient([[{ content: 'Done', done: true }]])
    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([])

    const exec = new AgentExecutor({ maxTurns: 5 }, onEvent)
    await exec.run({ goal: 'Finish', cwd: '/tmp' }, AbortSignal.timeout(5000))

    const finishEvent = events.find(
      (e: unknown) => (e as Record<string, unknown>).type === 'agent:finish'
    )
    expect(finishEvent).toBeDefined()
    const agentResult = (finishEvent as Record<string, unknown>).result as Record<string, unknown>
    expect(agentResult.success).toBe(true)
  })

  it('finish — success=true for GOAL mode', async () => {
    const client = createMockClient([[{ content: 'Done', done: true }]])
    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([])

    const exec = new AgentExecutor({ maxTurns: 5 })
    const result = await exec.run({ goal: 'Complete', cwd: '/tmp' }, AbortSignal.timeout(5000))

    expect(result.success).toBe(true)
    expect(result.durationMs).toBeGreaterThanOrEqual(0)
    expect(result.tokensUsed).toEqual({ input: 0, output: 0 })
  })

  it('finish — success=false for non-GOAL modes', async () => {
    const client = createMockClient([[{ error: { type: 'server', message: 'Error' } }]])
    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([])

    const exec = new AgentExecutor({ maxTurns: 5 })
    const result = await exec.run({ goal: 'Fail', cwd: '/tmp' }, AbortSignal.timeout(5000))

    expect(result.success).toBe(false)
  })

  it('multi-turn loop: tool calls → results → more tool calls → completion', async () => {
    const client = createMockClient([
      // Turn 1: tool call
      [
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-1',
            name: 'grep',
            input: { pattern: 'foo' },
          },
        },
      ],
      // Turn 2: another tool call
      [
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-2',
            name: 'read_file',
            input: { path: '/found.ts' },
          },
        },
      ],
      // Turn 3: completion
      [{ content: 'Found and read the file.', done: true }],
    ])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('grep'),
      mockToolDef('read_file'),
    ])
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockResolvedValue({
      success: true,
      output: 'result',
    })

    const exec = new AgentExecutor({ maxTurns: 10 })
    const result = await exec.run({ goal: 'Find foo', cwd: '/tmp' }, AbortSignal.timeout(5000))

    expect(result.terminateMode).toBe(AgentTerminateMode.GOAL)
    expect(result.turns).toBe(3)
    expect(result.output).toBe('Found and read the file.')
  })

  it('uses agent mode to filter tools when toolMode is set', async () => {
    const { getAgentModes } = await import('../extensions/api.js')
    const modes = getAgentModes() as Map<string, unknown>
    modes.set('minimal', {
      name: 'minimal',
      filterTools(tools: ToolDefinition[]) {
        return tools.filter((t) => t.name === 'read_file')
      },
      systemPrompt(base: string) {
        return `${base}\nMINIMAL MODE`
      },
    })

    const client = createMockClient([[{ content: 'Done', done: true }]])
    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
      mockToolDef('write_file'),
      mockToolDef('bash'),
    ])

    const streamSpy = vi.spyOn(client, 'stream')

    const exec = new AgentExecutor({ maxTurns: 5, toolMode: 'minimal' })
    await exec.run({ goal: 'Do something', cwd: '/tmp' }, AbortSignal.timeout(5000))

    // Verify tools were filtered — only read_file should be in the call
    const callArgs = streamSpy.mock.calls[0]
    const tools = callArgs![1].tools
    expect(tools).toHaveLength(1)
    expect(tools![0].name).toBe('read_file')
  })
})

// ─── Token Tracking ────────────────────────────────────────────────────────

describe('Token tracking', () => {
  it('accumulates usage from stream deltas into tokensUsed', async () => {
    const client = createMockClient([
      [{ content: 'Hello!', usage: { inputTokens: 100, outputTokens: 50 } }, { done: true }],
    ])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([])

    const exec = new AgentExecutor({ maxTurns: 5 })
    const result = await exec.run({ goal: 'Say hi', cwd: '/tmp' }, AbortSignal.timeout(5000))

    expect(result.tokensUsed).toEqual({ input: 100, output: 50 })
  })

  it('accumulates tokens across multiple turns', async () => {
    const client = createMockClient([
      // Turn 1: tool call with usage
      [
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-1',
            name: 'read_file',
            input: { path: '/test.txt' },
          },
        },
        { usage: { inputTokens: 100, outputTokens: 50 } },
      ],
      // Turn 2: completion with usage
      [{ content: 'Done reading.', usage: { inputTokens: 200, outputTokens: 80 } }, { done: true }],
    ])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
    ])
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockResolvedValue({
      success: true,
      output: 'content',
    })

    const exec = new AgentExecutor({ maxTurns: 5 })
    const result = await exec.run({ goal: 'Read file', cwd: '/tmp' }, AbortSignal.timeout(5000))

    expect(result.tokensUsed).toEqual({ input: 300, output: 130 })
  })

  it('stays at zero when no usage deltas are emitted', async () => {
    const client = createMockClient([[{ content: 'No usage info', done: true }]])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([])

    const exec = new AgentExecutor({ maxTurns: 5 })
    const result = await exec.run({ goal: 'Test', cwd: '/tmp' }, AbortSignal.timeout(5000))

    expect(result.tokensUsed).toEqual({ input: 0, output: 0 })
  })

  it('emits llm:usage event per turn', async () => {
    const usageEvents: unknown[] = []
    const { resetRegistries } = await import('../extensions/api.js')
    const apiModule = await import('../extensions/api.js')

    // Register event handler for llm:usage
    const handlers = new Set<(data: unknown) => void>()
    const handler = (data: unknown) => usageEvents.push(data)
    handlers.add(handler)

    // Use the real emitEvent — spy on it to capture calls
    const emitSpy = vi.spyOn(apiModule, 'emitEvent')

    const client = createMockClient([
      [
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-1',
            name: 'read_file',
            input: { path: '/test.txt' },
          },
        },
        { usage: { inputTokens: 50, outputTokens: 25 } },
      ],
      [{ content: 'Done', usage: { inputTokens: 75, outputTokens: 30 } }, { done: true }],
    ])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
    ])
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockResolvedValue({
      success: true,
      output: 'content',
    })

    const exec = new AgentExecutor({ maxTurns: 5 })
    await exec.run({ goal: 'Read', cwd: '/tmp' }, AbortSignal.timeout(5000))

    // Filter emitEvent calls for 'llm:usage'
    const llmUsageCalls = emitSpy.mock.calls.filter(([event]) => event === 'llm:usage')
    expect(llmUsageCalls).toHaveLength(2)
    expect(llmUsageCalls[0]![1]).toEqual({
      sessionId: exec.agentId,
      inputTokens: 50,
      outputTokens: 25,
    })
    expect(llmUsageCalls[1]![1]).toEqual({
      sessionId: exec.agentId,
      inputTokens: 75,
      outputTokens: 30,
    })
  })
})

// ─── runAgent convenience ──────────────────────────────────────────────────

describe('runAgent', () => {
  it('creates an executor and runs it', async () => {
    const client = createMockClient([[{ content: 'Hi', done: true }]])
    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([])

    const result = await runAgent(
      { goal: 'Hello', cwd: '/tmp' },
      { maxTurns: 5 },
      AbortSignal.timeout(5000)
    )

    expect(result.terminateMode).toBe(AgentTerminateMode.GOAL)
    expect(result.success).toBe(true)
  })

  it('passes onEvent callback', async () => {
    const events: unknown[] = []
    const onEvent = vi.fn((e: unknown) => events.push(e))

    const client = createMockClient([[{ content: 'Hi', done: true }]])
    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([])

    await runAgent(
      { goal: 'Hello', cwd: '/tmp' },
      { maxTurns: 5 },
      AbortSignal.timeout(5000),
      onEvent
    )

    expect(events.length).toBeGreaterThan(0)
  })
})
