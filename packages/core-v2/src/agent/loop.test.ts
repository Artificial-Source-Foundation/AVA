/**
 * Agent loop — AgentExecutor and runAgent.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { installMockPlatform } from '../__test-utils__/mock-platform.js'
import { resetRegistries } from '../extensions/api.js'
import { resetProviders } from '../llm/client.js'
import type {
  ContentBlock,
  LLMClient,
  StreamDelta,
  ToolDefinition,
  ToolResultBlock,
} from '../llm/types.js'
import { resetLogger } from '../logger/logger.js'
import { resetTools } from '../tools/registry.js'
import { AgentExecutor, runAgent, truncateToolResults } from './loop.js'
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

  it('LLM stream error triggers ERROR terminate (non-retryable)', async () => {
    const client = createMockClient([[{ error: { type: 'auth', message: 'Invalid API key' } }]])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([])

    const exec = new AgentExecutor({ maxTurns: 5 })
    const result = await exec.run({ goal: 'Do something', cwd: '/tmp' }, AbortSignal.timeout(5000))

    expect(result.terminateMode).toBe(AgentTerminateMode.ERROR)
    expect(result.success).toBe(false)
    expect(result.output).toBe('Invalid API key')
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
    const client = createMockClient([[{ error: { type: 'auth', message: 'Unauthorized' } }]])
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

  it('builds structured ContentBlock[] in assistant messages and ToolResultBlock[] in user messages', async () => {
    const capturedHistory: Array<{ role: string; content: unknown }> = []

    const client: LLMClient = {
      async *stream(messages) {
        // Capture history on turn 2 to inspect structured messages
        if (messages.length > 2) {
          for (const m of messages) {
            capturedHistory.push({ role: m.role, content: m.content })
          }
        }
        // Turn 1: return a tool call
        if (messages.length <= 2) {
          yield { content: 'Let me check' }
          yield {
            toolUse: {
              type: 'tool_use',
              id: 'tc-1',
              name: 'read_file',
              input: { path: '/test.txt' },
            },
          }
        } else {
          // Turn 2: done
          yield { content: 'All done.' }
        }
        yield { done: true }
      },
    }

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
    ])
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockResolvedValue({
      success: true,
      output: 'file contents here',
    })

    const exec = new AgentExecutor({ maxTurns: 5 })
    await exec.run({ goal: 'Read test.txt', cwd: '/tmp' }, AbortSignal.timeout(5000))

    // Find the assistant message with structured blocks
    const assistantMsg = capturedHistory.find((m) => m.role === 'assistant')
    expect(assistantMsg).toBeDefined()
    expect(Array.isArray(assistantMsg!.content)).toBe(true)

    const blocks = assistantMsg!.content as ContentBlock[]
    expect(blocks[0]).toEqual({ type: 'text', text: 'Let me check' })
    expect(blocks[1]).toMatchObject({ type: 'tool_use', id: 'tc-1', name: 'read_file' })

    // Find the user message with tool_result blocks
    const toolResultMsg = capturedHistory.find((m) => m.role === 'user' && Array.isArray(m.content))
    expect(toolResultMsg).toBeDefined()

    const resultBlocks = toolResultMsg!.content as ToolResultBlock[]
    expect(resultBlocks[0]).toEqual({
      type: 'tool_result',
      tool_use_id: 'tc-1',
      content: 'file contents here',
      is_error: false,
    })
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

// ─── allowedTools Filtering ─────────────────────────────────────────────────

describe('allowedTools filtering', () => {
  it('filters available tools when allowedTools is set', async () => {
    const client = createMockClient([[{ content: 'Done', done: true }]])
    const streamSpy = vi.spyOn(client, 'stream')

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
      mockToolDef('write_file'),
      mockToolDef('bash'),
      mockToolDef('glob'),
    ])

    const exec = new AgentExecutor({
      maxTurns: 5,
      allowedTools: ['read_file', 'glob'],
    })
    await exec.run({ goal: 'Read', cwd: '/tmp' }, AbortSignal.timeout(5000))

    const tools = streamSpy.mock.calls[0]![1].tools
    expect(tools).toHaveLength(2)
    expect(tools!.map((t) => t.name).sort()).toEqual(['glob', 'read_file'])
  })

  it('does not filter when allowedTools is not set', async () => {
    const client = createMockClient([[{ content: 'Done', done: true }]])
    const streamSpy = vi.spyOn(client, 'stream')

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
      mockToolDef('write_file'),
      mockToolDef('bash'),
    ])

    const exec = new AgentExecutor({ maxTurns: 5 })
    await exec.run({ goal: 'Read', cwd: '/tmp' }, AbortSignal.timeout(5000))

    const tools = streamSpy.mock.calls[0]![1].tools
    expect(tools).toHaveLength(3)
  })
})

// ─── Retry Logic ────────────────────────────────────────────────────────────

describe('Retry logic', () => {
  it('retries on rate limit error and succeeds on second attempt', async () => {
    let callCount = 0
    const client: LLMClient = {
      async *stream() {
        callCount++
        if (callCount === 1) {
          yield { error: { type: 'rate_limit' as const, message: 'Rate limit exceeded (429)' } }
        } else {
          yield { content: 'Success after retry' }
          yield { done: true }
        }
      },
    }

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([])

    const events: unknown[] = []
    const exec = new AgentExecutor({ maxTurns: 5, maxRetries: 2 }, (e) => events.push(e))
    const result = await exec.run({ goal: 'Test retry', cwd: '/tmp' }, AbortSignal.timeout(10000))

    expect(result.terminateMode).toBe(AgentTerminateMode.GOAL)
    expect(result.success).toBe(true)
    expect(result.output).toBe('Success after retry')

    const retryEvents = (events as Array<{ type: string }>).filter((e) => e.type === 'retry')
    expect(retryEvents).toHaveLength(1)
  })

  it('does not retry on auth error', async () => {
    const client = createMockClient([[{ error: { type: 'auth', message: 'Invalid API key' } }]])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([])

    const events: unknown[] = []
    const exec = new AgentExecutor({ maxTurns: 5, maxRetries: 3 }, (e) => events.push(e))
    const result = await exec.run({ goal: 'Test no retry', cwd: '/tmp' }, AbortSignal.timeout(5000))

    expect(result.terminateMode).toBe(AgentTerminateMode.ERROR)
    expect(result.success).toBe(false)

    const retryEvents = (events as Array<{ type: string }>).filter((e) => e.type === 'retry')
    expect(retryEvents).toHaveLength(0)
  })
})

// ─── Sequential Tool Execution (with Steering Interrupt Support) ────────────

describe('Sequential tool execution', () => {
  it('executes multiple tools sequentially (A finishes before B starts)', async () => {
    const executionLog: string[] = []

    const client = createMockClient([
      // Turn 1: LLM returns two tool calls
      [
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-a',
            name: 'read_file',
            input: { path: '/a.txt' },
          },
        },
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-b',
            name: 'grep',
            input: { pattern: 'foo' },
          },
        },
      ],
      // Turn 2: LLM done
      [{ content: 'Done', done: true }],
    ])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
      mockToolDef('grep'),
    ])

    // Tool A takes 50ms, Tool B takes 10ms — if sequential, A finishes before B starts
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockImplementation(
      async (name: string) => {
        executionLog.push(`start:${name}`)
        const delay = name === 'read_file' ? 50 : 10
        await new Promise((r) => setTimeout(r, delay))
        executionLog.push(`end:${name}`)
        return { success: true, output: `result-${name}` }
      }
    )

    const exec = new AgentExecutor({ maxTurns: 5, parallelToolExecution: false })
    await exec.run({ goal: 'Read and grep', cwd: '/tmp' }, AbortSignal.timeout(5000))

    // Tools execute sequentially (for steering interrupt support)
    const startA = executionLog.indexOf('start:read_file')
    const endA = executionLog.indexOf('end:read_file')
    const startB = executionLog.indexOf('start:grep')
    const endB = executionLog.indexOf('end:grep')

    expect(startA).toBeLessThan(endA)
    expect(startB).toBeLessThan(endB)
    // Sequential: A finishes before B starts
    expect(endA).toBeLessThan(startB)
    expect(endB).toBeGreaterThan(startB)
  })

  it('preserves result ordering when tool B finishes before tool A', async () => {
    const client = createMockClient([
      [
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-a',
            name: 'read_file',
            input: { path: '/a.txt' },
          },
        },
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-b',
            name: 'grep',
            input: { pattern: 'bar' },
          },
        },
      ],
      [{ content: 'Done', done: true }],
    ])

    const capturedHistory: Array<{ role: string; content: unknown }> = []
    const realClient: LLMClient = {
      async *stream(messages) {
        // Capture on second call
        if (messages.length > 2) {
          for (const m of messages) capturedHistory.push({ role: m.role, content: m.content })
          yield { content: 'Done' }
          yield { done: true }
          return
        }
        // First call: return tool calls
        yield* client.stream(messages, {} as never, new AbortController().signal)
      },
    }

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(realClient)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
      mockToolDef('grep'),
    ])

    // Tool A takes longer — B finishes first
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockImplementation(
      async (name: string) => {
        const delay = name === 'read_file' ? 40 : 5
        await new Promise((r) => setTimeout(r, delay))
        return { success: true, output: `result-${name}` }
      }
    )

    const exec = new AgentExecutor({ maxTurns: 5, parallelToolExecution: false })
    await exec.run({ goal: 'Sequential ordering', cwd: '/tmp' }, AbortSignal.timeout(5000))

    // Find tool results message
    const toolResultMsg = capturedHistory.find((m) => m.role === 'user' && Array.isArray(m.content))
    expect(toolResultMsg).toBeDefined()
    const results = toolResultMsg!.content as ToolResultBlock[]

    // Order must match tool call order (A first, B second), not completion order
    expect(results[0].tool_use_id).toBe('call-a')
    expect(results[1].tool_use_id).toBe('call-b')
  })

  it('attempt_completion among other tools stops immediately without executing others', async () => {
    const client = createMockClient([
      [
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-a',
            name: 'read_file',
            input: { path: '/a.txt' },
          },
        },
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-completion',
            name: COMPLETE_TASK_TOOL,
            input: { result: 'All done!' },
          },
        },
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-b',
            name: 'grep',
            input: { pattern: 'baz' },
          },
        },
      ],
    ])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
      mockToolDef('grep'),
    ])
    const executeToolSpy = vi
      .spyOn(await import('../tools/registry.js'), 'executeTool')
      .mockResolvedValue({ success: true, output: 'content' })

    const exec = new AgentExecutor({ maxTurns: 5 })
    const result = await exec.run({ goal: 'Complete', cwd: '/tmp' }, AbortSignal.timeout(5000))

    expect(result.terminateMode).toBe(AgentTerminateMode.GOAL)
    expect(result.output).toBe('All done!')
    // Neither read_file nor grep should have been executed
    expect(executeToolSpy).not.toHaveBeenCalled()
  })
})

// ─── Parallel Tool Execution ─────────────────────────────────────────────────

describe('Parallel tool execution', () => {
  it('defaults to parallel execution (parallelToolExecution=true)', () => {
    const exec = new AgentExecutor({ maxTurns: 5 })
    // Default is undefined which the loop treats as true
    expect(exec.config.parallelToolExecution).toBeUndefined()
  })

  it('executes multiple tools in parallel when enabled (default)', async () => {
    const executionLog: string[] = []

    const client = createMockClient([
      // Turn 1: LLM returns two tool calls
      [
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-a',
            name: 'read_file',
            input: { path: '/a.txt' },
          },
        },
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-b',
            name: 'grep',
            input: { pattern: 'foo' },
          },
        },
      ],
      // Turn 2: LLM done
      [{ content: 'Done', done: true }],
    ])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
      mockToolDef('grep'),
    ])

    // Tool A takes 50ms, Tool B takes 10ms — if parallel, B finishes before A
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockImplementation(
      async (name: string) => {
        executionLog.push(`start:${name}`)
        const delay = name === 'read_file' ? 50 : 10
        await new Promise((r) => setTimeout(r, delay))
        executionLog.push(`end:${name}`)
        return { success: true, output: `result-${name}` }
      }
    )

    const exec = new AgentExecutor({ maxTurns: 5 })
    await exec.run({ goal: 'Read and grep', cwd: '/tmp' }, AbortSignal.timeout(5000))

    // Both should have started
    expect(executionLog.filter((e) => e.startsWith('start:'))).toHaveLength(2)
    expect(executionLog.filter((e) => e.startsWith('end:'))).toHaveLength(2)

    // Parallel: B should finish before A (B takes 10ms, A takes 50ms)
    const endB = executionLog.indexOf('end:grep')
    const endA = executionLog.indexOf('end:read_file')
    expect(endB).toBeLessThan(endA)
  })

  it('executes tools sequentially when parallelToolExecution=false', async () => {
    const executionLog: string[] = []

    const client = createMockClient([
      [
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-a',
            name: 'read_file',
            input: { path: '/a.txt' },
          },
        },
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-b',
            name: 'grep',
            input: { pattern: 'foo' },
          },
        },
      ],
      [{ content: 'Done', done: true }],
    ])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
      mockToolDef('grep'),
    ])

    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockImplementation(
      async (name: string) => {
        executionLog.push(`start:${name}`)
        const delay = name === 'read_file' ? 50 : 10
        await new Promise((r) => setTimeout(r, delay))
        executionLog.push(`end:${name}`)
        return { success: true, output: `result-${name}` }
      }
    )

    const exec = new AgentExecutor({ maxTurns: 5, parallelToolExecution: false })
    await exec.run({ goal: 'Read and grep', cwd: '/tmp' }, AbortSignal.timeout(5000))

    // Sequential: A finishes before B starts
    const startA = executionLog.indexOf('start:read_file')
    const endA = executionLog.indexOf('end:read_file')
    const startB = executionLog.indexOf('start:grep')
    const endB = executionLog.indexOf('end:grep')

    expect(startA).toBeLessThan(endA)
    expect(endA).toBeLessThan(startB)
    expect(startB).toBeLessThan(endB)
  })

  it('preserves result ordering in parallel mode (matches tool call order)', async () => {
    const client = createMockClient([
      [
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-a',
            name: 'read_file',
            input: { path: '/a.txt' },
          },
        },
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-b',
            name: 'grep',
            input: { pattern: 'bar' },
          },
        },
      ],
      [{ content: 'Done', done: true }],
    ])

    const capturedHistory: Array<{ role: string; content: unknown }> = []
    const realClient: LLMClient = {
      async *stream(messages) {
        // Capture on second call
        if (messages.length > 2) {
          for (const m of messages) capturedHistory.push({ role: m.role, content: m.content })
          yield { content: 'Done' }
          yield { done: true }
          return
        }
        // First call: return tool calls
        yield* client.stream(messages, {} as never, new AbortController().signal)
      },
    }

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(realClient)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
      mockToolDef('grep'),
    ])

    // Tool A takes longer — B finishes first in parallel
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockImplementation(
      async (name: string) => {
        const delay = name === 'read_file' ? 40 : 5
        await new Promise((r) => setTimeout(r, delay))
        return { success: true, output: `result-${name}` }
      }
    )

    const exec = new AgentExecutor({ maxTurns: 5 })
    await exec.run({ goal: 'Parallel', cwd: '/tmp' }, AbortSignal.timeout(5000))

    // Find tool results message
    const toolResultMsg = capturedHistory.find((m) => m.role === 'user' && Array.isArray(m.content))
    expect(toolResultMsg).toBeDefined()
    const results = toolResultMsg!.content as ToolResultBlock[]

    // Order must match tool call order (A first, B second), not completion order
    expect(results[0].tool_use_id).toBe('call-a')
    expect(results[1].tool_use_id).toBe('call-b')
  })

  it('parallel execution is faster than sequential for independent tools', async () => {
    const makeClient = () =>
      createMockClient([
        [
          {
            toolUse: {
              type: 'tool_use',
              id: 'call-a',
              name: 'read_file',
              input: { path: '/a.txt' },
            },
          },
          {
            toolUse: {
              type: 'tool_use',
              id: 'call-b',
              name: 'grep',
              input: { pattern: 'x' },
            },
          },
          {
            toolUse: {
              type: 'tool_use',
              id: 'call-c',
              name: 'glob',
              input: { pattern: '*.ts' },
            },
          },
        ],
        [{ content: 'Done', done: true }],
      ])

    const toolDefs = [mockToolDef('read_file'), mockToolDef('grep'), mockToolDef('glob')]

    const createClientMod = await import('../llm/client.js')
    const registryMod = await import('../tools/registry.js')

    // Each tool takes 30ms
    const mockExecute = async () => {
      await new Promise((r) => setTimeout(r, 30))
      return { success: true, output: 'ok' }
    }

    // Sequential run
    vi.spyOn(createClientMod, 'createClient').mockReturnValue(makeClient())
    vi.spyOn(registryMod, 'getToolDefinitions').mockReturnValue(toolDefs)
    vi.spyOn(registryMod, 'executeTool').mockImplementation(mockExecute)

    const seqStart = Date.now()
    const seqExec = new AgentExecutor({ maxTurns: 5, parallelToolExecution: false })
    await seqExec.run({ goal: 'Test', cwd: '/tmp' }, AbortSignal.timeout(5000))
    const seqDuration = Date.now() - seqStart

    // Parallel run
    vi.spyOn(createClientMod, 'createClient').mockReturnValue(makeClient())
    vi.spyOn(registryMod, 'getToolDefinitions').mockReturnValue(toolDefs)
    vi.spyOn(registryMod, 'executeTool').mockImplementation(mockExecute)

    const parStart = Date.now()
    const parExec = new AgentExecutor({ maxTurns: 5, parallelToolExecution: true })
    await parExec.run({ goal: 'Test', cwd: '/tmp' }, AbortSignal.timeout(5000))
    const parDuration = Date.now() - parStart

    // Parallel should be significantly faster (3 tools x 30ms = 90ms seq vs ~30ms par)
    // Allow some margin for test infrastructure overhead
    expect(parDuration).toBeLessThan(seqDuration)
  })

  it('parallel mode still respects abort signal', async () => {
    const controller = new AbortController()

    const client = createMockClient([
      [
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-a',
            name: 'read_file',
            input: { path: '/a.txt' },
          },
        },
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-b',
            name: 'grep',
            input: { pattern: 'foo' },
          },
        },
      ],
      [{ content: 'Done', done: true }],
    ])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
      mockToolDef('grep'),
    ])

    // Abort during first tool execution
    let toolCallCount = 0
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockImplementation(async () => {
      toolCallCount++
      // Abort after the first tool starts (both start simultaneously in parallel,
      // but the second will check signal before executing)
      if (toolCallCount === 1) {
        controller.abort()
      }
      return { success: true, output: 'result' }
    })

    const exec = new AgentExecutor({ maxTurns: 5, parallelToolExecution: true })
    const result = await exec.run({ goal: 'Test abort', cwd: '/tmp' }, controller.signal)

    expect(result.terminateMode).toBe(AgentTerminateMode.ABORTED)
    expect(result.success).toBe(false)
  })

  it('emits tool:start and tool:finish events for all parallel tools', async () => {
    const events: unknown[] = []
    const onEvent = vi.fn((e: unknown) => events.push(e))

    const client = createMockClient([
      [
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-a',
            name: 'read_file',
            input: { path: '/a.txt' },
          },
        },
        {
          toolUse: {
            type: 'tool_use',
            id: 'call-b',
            name: 'grep',
            input: { pattern: 'foo' },
          },
        },
      ],
      [{ content: 'Done', done: true }],
    ])

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
      mockToolDef('grep'),
    ])
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockResolvedValue({
      success: true,
      output: 'content',
    })

    const exec = new AgentExecutor({ maxTurns: 5, parallelToolExecution: true }, onEvent)
    await exec.run({ goal: 'Read and grep', cwd: '/tmp' }, AbortSignal.timeout(5000))

    const toolStartEvents = (events as Array<{ type: string; toolName?: string }>).filter(
      (e) => e.type === 'tool:start'
    )
    const toolFinishEvents = (events as Array<{ type: string; toolName?: string }>).filter(
      (e) => e.type === 'tool:finish'
    )

    expect(toolStartEvents).toHaveLength(2)
    expect(toolFinishEvents).toHaveLength(2)
    expect(toolStartEvents.map((e) => e.toolName).sort()).toEqual(['grep', 'read_file'])
    expect(toolFinishEvents.map((e) => e.toolName).sort()).toEqual(['grep', 'read_file'])
  })
})

// ─── Doom Loop Detection ────────────────────────────────────────────────────

describe('Doom loop detection', () => {
  it('injects corrective message after 3 identical tool calls', async () => {
    const capturedHistory: Array<{ role: string; content: unknown }> = []
    let turnCount = 0

    const client: LLMClient = {
      async *stream(messages) {
        turnCount++
        // Capture history on the last turn
        if (turnCount === 5) {
          for (const m of messages) {
            capturedHistory.push({ role: m.role, content: m.content })
          }
          yield { content: 'Stopping now.' }
          yield { done: true }
          return
        }
        // Always make the same tool call
        yield {
          toolUse: {
            type: 'tool_use',
            id: `call-${turnCount}`,
            name: 'read_file',
            input: { path: '/same.txt' },
          },
        }
        yield { done: true }
      },
    }

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
    ])
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockResolvedValue({
      success: true,
      output: 'content',
    })

    const events: unknown[] = []
    const exec = new AgentExecutor({ maxTurns: 10 }, (e) => events.push(e))
    await exec.run({ goal: 'Read file', cwd: '/tmp' }, AbortSignal.timeout(5000))

    // Check that doom-loop event was emitted
    const doomEvents = (events as Array<{ type: string }>).filter((e) => e.type === 'doom-loop')
    expect(doomEvents.length).toBeGreaterThanOrEqual(1)

    // Check that corrective message was injected into history
    const corrections = capturedHistory.filter(
      (m) =>
        m.role === 'user' &&
        typeof m.content === 'string' &&
        (m.content as string).includes('Try a different approach')
    )
    expect(corrections.length).toBeGreaterThanOrEqual(1)
  })
})

// ─── Tool Result Truncation ──────────────────────────────────────────────────

describe('truncateToolResults', () => {
  it('does not truncate small results', async () => {
    const blocks: ToolResultBlock[] = [
      { type: 'tool_result', tool_use_id: 'tc-1', content: 'small result', is_error: false },
      { type: 'tool_result', tool_use_id: 'tc-2', content: 'another small', is_error: false },
    ]
    await truncateToolResults(blocks)
    expect(blocks[0].content).toBe('small result')
    expect(blocks[1].content).toBe('another small')
  })

  it('truncates a single result exceeding MAX_RESULT_BYTES (50KB)', async () => {
    const largeContent = 'x'.repeat(60 * 1024) // 60KB
    const blocks: ToolResultBlock[] = [
      { type: 'tool_result', tool_use_id: 'tc-1', content: largeContent, is_error: false },
    ]
    await truncateToolResults(blocks)
    expect(blocks[0].content.length).toBeLessThan(largeContent.length)
    expect(blocks[0].content).toContain('[...truncated')
    // Should start with the first 50KB of content
    expect(blocks[0].content.startsWith('x'.repeat(50 * 1024))).toBe(true)
  })

  it('adds truncation marker with byte count', async () => {
    const size = 60 * 1024
    const largeContent = 'a'.repeat(size)
    const blocks: ToolResultBlock[] = [
      { type: 'tool_result', tool_use_id: 'tc-1', content: largeContent, is_error: false },
    ]
    await truncateToolResults(blocks)
    const expectedTruncated = size - 50 * 1024
    expect(blocks[0].content).toContain(`[...truncated ${expectedTruncated} bytes]`)
  })

  it('proportionally truncates when total exceeds MAX_TOTAL_RESULT_BYTES (200KB)', async () => {
    // Create 5 results, each 50KB = 250KB total, exceeds 200KB limit
    const blocks: ToolResultBlock[] = []
    for (let i = 0; i < 5; i++) {
      blocks.push({
        type: 'tool_result',
        tool_use_id: `tc-${i}`,
        content: 'y'.repeat(50 * 1024), // each is exactly 50KB (at the per-result limit)
        is_error: false,
      })
    }
    await truncateToolResults(blocks)
    // Each block should be proportionally smaller
    const totalAfter = blocks.reduce((sum, b) => sum + Buffer.byteLength(b.content, 'utf8'), 0)
    // Total should be around 200KB (may exceed slightly due to truncation markers)
    expect(totalAfter).toBeLessThan(250 * 1024)
    // All blocks should have truncation markers
    for (const block of blocks) {
      expect(block.content).toContain('[...truncated')
    }
  })

  it('does not truncate results <= 1KB during proportional truncation', async () => {
    // One large result + one tiny result
    const blocks: ToolResultBlock[] = [
      {
        type: 'tool_result',
        tool_use_id: 'tc-big',
        content: 'z'.repeat(210 * 1024), // 210KB — will be per-result truncated to ~50KB first
        is_error: false,
      },
      {
        type: 'tool_result',
        tool_use_id: 'tc-tiny',
        content: 'small', // 5 bytes — should never be truncated
        is_error: false,
      },
    ]
    await truncateToolResults(blocks)
    // The tiny result should be preserved
    expect(blocks[1].content).toBe('small')
  })

  it('handles empty blocks array', async () => {
    const blocks: ToolResultBlock[] = []
    await truncateToolResults(blocks)
    expect(blocks).toEqual([])
  })

  it('handles blocks with empty content', async () => {
    const blocks: ToolResultBlock[] = [
      { type: 'tool_result', tool_use_id: 'tc-1', content: '', is_error: false },
    ]
    await truncateToolResults(blocks)
    expect(blocks[0].content).toBe('')
  })

  it('truncation is applied before adding to history in executeTurn', async () => {
    const capturedHistory: Array<{ role: string; content: unknown }> = []
    const largeOutput = 'x'.repeat(60 * 1024)

    const client: LLMClient = {
      async *stream(messages) {
        // Capture on second call
        if (messages.length > 2) {
          for (const m of messages) capturedHistory.push({ role: m.role, content: m.content })
          yield { content: 'Done' }
          yield { done: true }
          return
        }
        yield {
          toolUse: {
            type: 'tool_use',
            id: 'tc-1',
            name: 'read_file',
            input: { path: '/big.txt' },
          },
        }
        yield { done: true }
      },
    }

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
    ])
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockResolvedValue({
      success: true,
      output: largeOutput,
    })

    const exec = new AgentExecutor({ maxTurns: 5 })
    await exec.run({ goal: 'Read big file', cwd: '/tmp' }, AbortSignal.timeout(5000))

    // Find the tool result in history
    const toolResultMsg = capturedHistory.find((m) => m.role === 'user' && Array.isArray(m.content))
    expect(toolResultMsg).toBeDefined()
    const results = toolResultMsg!.content as ToolResultBlock[]
    expect(results[0].content.length).toBeLessThan(largeOutput.length)
    expect(results[0].content).toContain('[...truncated')
  })
})

// ─── Steering Interrupt ──────────────────────────────────────────────────────

describe('Steering interrupt', () => {
  it('steer() injects a user message into the next turn', async () => {
    const capturedHistory: Array<{ role: string; content: unknown }> = []
    let turnCount = 0
    let executor: AgentExecutor | null = null

    const client: LLMClient = {
      async *stream(messages) {
        turnCount++
        if (turnCount === 1) {
          // Turn 1: tool call
          yield {
            toolUse: {
              type: 'tool_use',
              id: 'call-1',
              name: 'read_file',
              input: { path: '/test.txt' },
            },
          }
          yield { done: true }
        } else {
          // Turn 2+: capture history, then done
          for (const m of messages) {
            capturedHistory.push({ role: m.role, content: m.content })
          }
          yield { content: 'Steered response' }
          yield { done: true }
        }
      },
    }

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
    ])
    // Steer during tool execution to ensure it happens before the loop checks
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockImplementation(async () => {
      executor!.steer('Change direction, do something else')
      return { success: true, output: 'file content' }
    })

    const events: unknown[] = []
    executor = new AgentExecutor({ maxTurns: 5 }, (e) => events.push(e))

    const result = await executor.run({ goal: 'Read file', cwd: '/tmp' }, AbortSignal.timeout(5000))

    expect(result.success).toBe(true)

    // Verify that the steering message was injected into history
    const steeringMsg = capturedHistory.find(
      (m) => m.role === 'user' && m.content === 'Change direction, do something else'
    )
    expect(steeringMsg).toBeDefined()

    // Verify agent:steered event was emitted
    const steeredEvent = events.find((e) => (e as Record<string, unknown>).type === 'agent:steered')
    expect(steeredEvent).toBeDefined()
    expect((steeredEvent as Record<string, unknown>).message).toBe(
      'Change direction, do something else'
    )
  })

  it('steer() emits agent:steered event with the message', async () => {
    const events: unknown[] = []

    const client: LLMClient = {
      async *stream() {
        yield {
          toolUse: {
            type: 'tool_use',
            id: 'call-1',
            name: 'read_file',
            input: { path: '/test.txt' },
          },
        }
        yield { done: true }
      },
    }

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
    ])
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockResolvedValue({
      success: true,
      output: 'content',
    })

    const exec = new AgentExecutor({ maxTurns: 2 }, (e) => events.push(e))

    // Set steering before run so it gets picked up on second iteration
    exec.steer('Focus on tests only')

    await exec.run({ goal: 'Do task', cwd: '/tmp' }, AbortSignal.timeout(5000))

    const steeredEvents = (events as Array<Record<string, unknown>>).filter(
      (e) => e.type === 'agent:steered'
    )
    expect(steeredEvents).toHaveLength(1)
    expect(steeredEvents[0].message).toBe('Focus on tests only')
  })

  it('steer() with no active run sets message for next loop iteration', () => {
    const exec = new AgentExecutor({ maxTurns: 5 })
    // Calling steer before run just sets the message — no error
    exec.steer('test message')
    // The message will be consumed on next run iteration
  })
})

// ─── Compaction Strategy Selection ───────────────────────────────────────────

describe('Compaction strategy selection', () => {
  it('emits context:compacted event with before/after token counts', async () => {
    const apiModule = await import('../extensions/api.js')
    const emitSpy = vi.spyOn(apiModule, 'emitEvent')

    // Register a truncate strategy that just returns the last 2 messages
    const strategies = apiModule.getContextStrategies() as Map<string, unknown>
    strategies.set('truncate', {
      name: 'truncate',
      compact(messages: Array<{ role: string; content: string }>) {
        return messages.slice(-2)
      },
    })

    // Create a client that produces many tool calls to fill context
    let turnCount = 0
    const client: LLMClient = {
      async *stream() {
        turnCount++
        if (turnCount <= 3) {
          yield {
            toolUse: {
              type: 'tool_use',
              id: `call-${turnCount}`,
              name: 'read_file',
              input: { path: `/file${turnCount}.txt` },
            },
          }
          yield { done: true }
        } else {
          yield { content: 'Done' }
          yield { done: true }
        }
      },
    }

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
    ])
    // Return huge results to trigger compaction
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockResolvedValue({
      success: true,
      output: 'x'.repeat(500_000), // large output to blow up context
    })

    const exec = new AgentExecutor({
      maxTurns: 10,
      compactionThreshold: 0.001, // very low threshold to force compaction
    })
    await exec.run({ goal: 'Read files', cwd: '/tmp' }, AbortSignal.timeout(5000))

    // Check that context:compacted was emitted
    const compactedCalls = emitSpy.mock.calls.filter(([event]) => event === 'context:compacted')
    expect(compactedCalls.length).toBeGreaterThanOrEqual(1)

    const compactedData = compactedCalls[0]![1] as Record<string, unknown>
    expect(compactedData.tokensBefore).toBeDefined()
    expect(compactedData.tokensAfter).toBeDefined()
    expect(compactedData.messagesBefore).toBeDefined()
    expect(compactedData.messagesAfter).toBeDefined()
    expect(compactedData.strategy).toBeDefined()
  })

  it('uses truncate strategy for short sessions (<= 20 messages)', async () => {
    const apiModule = await import('../extensions/api.js')
    const emitSpy = vi.spyOn(apiModule, 'emitEvent')

    // Register both strategies
    const strategies = apiModule.getContextStrategies() as Map<string, unknown>
    strategies.set('truncate', {
      name: 'truncate',
      compact(messages: Array<{ role: string; content: string }>) {
        return messages.slice(-2)
      },
    })
    strategies.set('summarize', {
      name: 'summarize',
      compact(messages: Array<{ role: string; content: string }>) {
        return messages.slice(-2)
      },
    })

    // Short session — only a few messages
    let turnCount = 0
    const client: LLMClient = {
      async *stream() {
        turnCount++
        if (turnCount <= 2) {
          yield {
            toolUse: {
              type: 'tool_use',
              id: `call-${turnCount}`,
              name: 'read_file',
              input: { path: `/file${turnCount}.txt` },
            },
          }
        } else {
          yield { content: 'Done' }
        }
        yield { done: true }
      },
    }

    vi.spyOn(await import('../llm/client.js'), 'createClient').mockReturnValue(client)
    vi.spyOn(await import('../tools/registry.js'), 'getToolDefinitions').mockReturnValue([
      mockToolDef('read_file'),
    ])
    vi.spyOn(await import('../tools/registry.js'), 'executeTool').mockResolvedValue({
      success: true,
      output: 'x'.repeat(500_000),
    })

    const exec = new AgentExecutor({
      maxTurns: 10,
      compactionThreshold: 0.001,
    })
    await exec.run({ goal: 'Read files', cwd: '/tmp' }, AbortSignal.timeout(5000))

    const compactedCalls = emitSpy.mock.calls.filter(([event]) => event === 'context:compacted')
    if (compactedCalls.length > 0) {
      const data = compactedCalls[0]![1] as Record<string, unknown>
      // Short session should use truncate
      expect(data.strategy).toBe('truncate')
    }
  })
})
