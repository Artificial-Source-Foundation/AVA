/**
 * Agent Pipeline Integration Tests
 * Verifies the full agent loop → LLM → tool dispatch → result pipeline
 *
 * Uses a mock LLM client to script tool calls and verify end-to-end behavior
 * without requiring a real LLM provider.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { AgentExecutor } from '../loop.js'
import type { AgentEvent } from '../types.js'
import { AgentTerminateMode } from '../types.js'
import { createMockLLMClient, type MockLLMTurn } from './mock-llm.js'

// ============================================================================
// Mocks
// ============================================================================

// Mock LLM client module
let mockTurns: MockLLMTurn[] = []

vi.mock('../../llm/client.js', () => ({
  createClient: vi.fn(async () => createMockLLMClient(mockTurns)),
  getAuth: vi.fn(async () => ({ type: 'api-key', token: 'test-key' })),
  getEditorModelConfig: vi.fn(() => ({
    model: 'test-model',
    provider: 'anthropic',
  })),
}))

// Mock hooks — no-op runner
vi.mock('../../hooks/index.js', () => ({
  getHookRunner: vi.fn(() => ({
    run: vi.fn(async () => ({})),
  })),
  createTaskStartContext: vi.fn((p: unknown) => p),
  createTaskCompleteContext: vi.fn((p: unknown) => p),
  createTaskCancelContext: vi.fn((p: unknown) => p),
  createPreToolUseContext: vi.fn((p: unknown) => p),
  createPostToolUseContext: vi.fn((p: unknown) => p),
}))

// Mock git auto-commit
vi.mock('../../git/auto-commit.js', () => ({
  autoCommitIfEnabled: vi.fn(async () => {}),
}))

// Mock system prompt builder (variants/types.js uses require() which doesn't resolve in test)
vi.mock('../prompts/variants/index.js', () => ({
  buildSystemPromptForModel: vi.fn(() => 'You are a test agent.'),
  buildWorkerPromptForModel: vi.fn(() => 'You are a test worker.'),
  getVariant: vi.fn(() => ({
    buildSystemPrompt: () => 'You are a test agent.',
    buildWorkerPrompt: () => 'You are a test worker.',
  })),
  getVariantForModel: vi.fn(() => ({
    buildSystemPrompt: () => 'You are a test agent.',
    buildWorkerPrompt: () => 'You are a test worker.',
  })),
  detectPromptModelFamily: vi.fn(() => 'generic'),
  genericVariant: {
    buildSystemPrompt: () => 'You are a test agent.',
    buildWorkerPrompt: () => 'You are a test worker.',
  },
}))

// ============================================================================
// Imports that depend on mocks
// ============================================================================

// Import after mocks so they use mocked versions
const { resetDoomLoopDetector } = await import('../../session/doom-loop.js')
const { resetToolCallCount, registerTool, getToolDefinitions } = await import(
  '../../tools/registry.js'
)
const { resetMessageBus } = await import('../../bus/message-bus.js')
const { executeWorker } = await import('../../commander/executor.js')

// ============================================================================
// Test Setup
// ============================================================================

beforeEach(() => {
  mockTurns = []
  resetDoomLoopDetector()
  resetToolCallCount()
  resetMessageBus()
  vi.clearAllMocks()

  // Register minimal tools for testing
  registerTool({
    definition: {
      name: 'glob',
      description: 'Find files by pattern',
      input_schema: {
        type: 'object',
        properties: { pattern: { type: 'string' } },
        required: ['pattern'],
      },
    },
    async execute() {
      return { success: true, output: 'file1.ts\nfile2.ts' }
    },
  })

  registerTool({
    definition: {
      name: 'read_file',
      description: 'Read a file',
      input_schema: {
        type: 'object',
        properties: { path: { type: 'string' } },
        required: ['path'],
      },
    },
    async execute() {
      return { success: true, output: 'const x = 1;' }
    },
  })

  registerTool({
    definition: {
      name: 'grep',
      description: 'Search file contents',
      input_schema: {
        type: 'object',
        properties: { pattern: { type: 'string' } },
        required: ['pattern'],
      },
    },
    async execute() {
      return { success: true, output: 'match found in line 42' }
    },
  })

  registerTool({
    definition: {
      name: 'ls',
      description: 'List directory',
      input_schema: {
        type: 'object',
        properties: { path: { type: 'string' } },
      },
    },
    async execute() {
      return { success: true, output: 'src/\npackage.json' }
    },
  })
})

afterEach(() => {
  vi.restoreAllMocks()
})

// ============================================================================
// Test Helpers
// ============================================================================

function createAbortController(): AbortController {
  return new AbortController()
}

function makeExecutor(overrides: Record<string, unknown> = {}) {
  return new AgentExecutor({
    maxTurns: 10,
    maxTimeMinutes: 5,
    ...overrides,
  })
}

// ============================================================================
// Test 1: Tool dispatch → history → GOAL termination
// ============================================================================

describe('Agent Pipeline Integration', () => {
  it('dispatches tool call and completes via complete_task', async () => {
    mockTurns = [
      {
        content: 'Let me search for files.',
        toolCalls: [{ id: 'tc-1', name: 'glob', input: { pattern: '**/*.ts' } }],
      },
      {
        content: 'Found the files. Task complete.',
        toolCalls: [
          { id: 'tc-2', name: 'complete_task', input: { result: 'Found 2 TypeScript files' } },
        ],
      },
    ]

    const executor = makeExecutor()
    const controller = createAbortController()
    const result = await executor.run(
      { goal: 'Find TypeScript files', cwd: '/tmp/test' },
      controller.signal
    )

    expect(result.success).toBe(true)
    expect(result.terminateMode).toBe(AgentTerminateMode.GOAL)
    expect(result.output).toBe('Found 2 TypeScript files')
    expect(result.turns).toBe(2)
    expect(result.steps).toHaveLength(2)
    expect(result.steps[0].toolsCalled[0].name).toBe('glob')
    expect(result.steps[0].toolsCalled[0].success).toBe(true)
  })

  // ============================================================================
  // Test 2: MAX_TURNS termination + recovery attempt
  // ============================================================================

  it('terminates at maxTurns and attempts recovery', async () => {
    // Script 5 turns of tool calls (more than maxTurns: 3)
    mockTurns = [
      { toolCalls: [{ id: 'tc-1', name: 'glob', input: { pattern: '*.ts' } }] },
      { toolCalls: [{ id: 'tc-2', name: 'read_file', input: { path: '/a.ts' } }] },
      { toolCalls: [{ id: 'tc-3', name: 'grep', input: { pattern: 'todo' } }] },
      // Recovery turn — auto-complete from mock (queue empty)
    ]

    const executor = makeExecutor({ maxTurns: 3 })
    const controller = createAbortController()
    const result = await executor.run(
      { goal: 'Explore codebase', cwd: '/tmp/test' },
      controller.signal
    )

    // Recovery should kick in — either succeeds with auto-complete or fails
    // The mock auto-completes when queue is empty, so recovery should succeed
    expect(result.turns).toBeGreaterThanOrEqual(3)
    // MAX_TURNS is recoverable; with auto-complete in recovery, it should succeed
    expect(result.terminateMode).toBe(AgentTerminateMode.GOAL)
    expect(result.success).toBe(true)
  })

  // ============================================================================
  // Test 3: NO_COMPLETE_TASK detection
  // ============================================================================

  it('detects NO_COMPLETE_TASK when LLM returns text only', async () => {
    // LLM returns content but no tool calls — triggers NO_COMPLETE_TASK
    mockTurns = [
      { content: 'I think the answer is 42. No tools needed.' },
      // Recovery turn — auto-complete from mock
    ]

    const executor = makeExecutor()
    const controller = createAbortController()
    const result = await executor.run(
      { goal: 'What is the answer?', cwd: '/tmp/test' },
      controller.signal
    )

    // NO_COMPLETE_TASK is recoverable, and mock auto-completes on empty queue
    expect(result.success).toBe(true)
    expect(result.terminateMode).toBe(AgentTerminateMode.GOAL)
  })

  // ============================================================================
  // Test 4: Doom loop detection + recovery message
  // ============================================================================

  it('detects doom loop when same tool called 3x identically', async () => {
    const sameCall = { id: 'tc-dup', name: 'glob', input: { pattern: '*.ts' } }

    mockTurns = [
      { toolCalls: [sameCall] },
      { toolCalls: [{ ...sameCall, id: 'tc-dup-2' }] },
      { toolCalls: [{ ...sameCall, id: 'tc-dup-3' }] },
      // After doom loop detection, agent gets recovery message and auto-completes
    ]

    const events: AgentEvent[] = []
    const executor = makeExecutor({
      maxTurns: 10,
      onEvent: undefined,
    })

    // Capture events via the constructor
    const trackedExecutor = new AgentExecutor({ maxTurns: 10, maxTimeMinutes: 5 }, (event) => {
      events.push(event)
    })

    const controller = createAbortController()
    const result = await trackedExecutor.run(
      { goal: 'Find files', cwd: '/tmp/test' },
      controller.signal
    )

    // The doom loop should have been detected
    const errorEvents = events.filter((e) => e.type === 'error')
    const hasDoomLoopError = errorEvents.some(
      (e) => e.type === 'error' && e.error.includes('Doom loop')
    )
    expect(hasDoomLoopError).toBe(true)

    // Agent should still complete (auto-complete on empty queue)
    expect(result.success).toBe(true)
  })

  // ============================================================================
  // Test 5: External abort signal
  // ============================================================================

  it('terminates on external abort signal', async () => {
    // Abort before starting — guarantees the signal is checked
    const controller = createAbortController()
    controller.abort()

    mockTurns = [{ toolCalls: [{ id: 'tc-1', name: 'glob', input: { pattern: '**/*' } }] }]

    const executor = makeExecutor()

    const result = await executor.run(
      { goal: 'Explore everything', cwd: '/tmp/test' },
      controller.signal
    )

    expect(result.success).toBe(false)
    expect(result.terminateMode).toBe(AgentTerminateMode.ABORTED)
  })

  // ============================================================================
  // Test 6: Events emitted in correct order
  // ============================================================================

  it('emits events in correct order', async () => {
    mockTurns = [
      {
        toolCalls: [{ id: 'tc-1', name: 'glob', input: { pattern: '*.ts' } }],
      },
      {
        toolCalls: [{ id: 'tc-2', name: 'complete_task', input: { result: 'Done' } }],
      },
    ]

    const eventTypes: string[] = []
    const executor = new AgentExecutor({ maxTurns: 10, maxTimeMinutes: 5 }, (event) => {
      eventTypes.push(event.type)
    })

    const controller = createAbortController()
    await executor.run({ goal: 'Test events', cwd: '/tmp/test' }, controller.signal)

    // Verify essential event ordering
    const startIdx = eventTypes.indexOf('agent:start')
    const firstTurnStart = eventTypes.indexOf('turn:start')
    const firstToolStart = eventTypes.indexOf('tool:start')
    const firstToolFinish = eventTypes.indexOf('tool:finish')
    const firstTurnFinish = eventTypes.indexOf('turn:finish')
    const finishIdx = eventTypes.indexOf('agent:finish')

    expect(startIdx).toBeLessThan(firstTurnStart)
    expect(firstTurnStart).toBeLessThan(firstToolStart)
    expect(firstToolStart).toBeLessThan(firstToolFinish)
    expect(firstToolFinish).toBeLessThan(firstTurnFinish)
    expect(firstTurnFinish).toBeLessThan(finishIdx)

    // agent:start should be first, agent:finish should be last
    expect(startIdx).toBe(0)
    expect(finishIdx).toBe(eventTypes.length - 1)
  })

  // ============================================================================
  // Test 7: Filtered tools config
  // ============================================================================

  it('only exposes allowed tools when configured', async () => {
    mockTurns = [
      {
        toolCalls: [{ id: 'tc-1', name: 'complete_task', input: { result: 'Checked tools' } }],
      },
    ]

    // Track what tools the LLM sees
    const { createClient } = await import('../../llm/client.js')
    const mockedCreateClient = vi.mocked(createClient)

    let toolsSentToLLM: string[] = []
    mockedCreateClient.mockImplementation(async () => {
      const client = createMockLLMClient(mockTurns)
      return {
        async *stream(messages, config) {
          toolsSentToLLM = (config.tools ?? []).map((t) => t.name)
          yield* client.stream(messages, config)
        },
      }
    })

    const executor = new AgentExecutor({
      maxTurns: 5,
      maxTimeMinutes: 5,
      tools: ['glob', 'grep'],
    })

    const controller = createAbortController()
    await executor.run({ goal: 'Check tools', cwd: '/tmp/test' }, controller.signal)

    // Should only have glob, grep, and the auto-added complete_task
    expect(toolsSentToLLM).toContain('glob')
    expect(toolsSentToLLM).toContain('grep')
    expect(toolsSentToLLM).toContain('complete_task')
    expect(toolsSentToLLM).not.toContain('read_file')
    expect(toolsSentToLLM).not.toContain('ls')
  })

  // ============================================================================
  // Test 8: executeWorker with mock LLM
  // ============================================================================

  it('executeWorker runs worker with isolated result', async () => {
    mockTurns = [
      {
        content: 'Exploring the frontend code.',
        toolCalls: [{ id: 'tc-1', name: 'glob', input: { pattern: 'src/**/*.tsx' } }],
      },
      {
        toolCalls: [
          { id: 'tc-2', name: 'complete_task', input: { result: 'Found 5 React components' } },
        ],
      },
    ]

    const controller = createAbortController()
    const result = await executeWorker(
      {
        name: 'frontend',
        displayName: 'Senior Frontend Lead',
        tools: ['glob', 'grep', 'read_file', 'ls'],
        systemPrompt: 'You are the frontend specialist.',
        maxTurns: 10,
      },
      {
        task: 'Find all React components',
        cwd: '/tmp/test',
      },
      controller.signal
    )

    expect(result.success).toBe(true)
    expect(result.output).toBe('Found 5 React components')
    expect(result.terminateMode).toBe(AgentTerminateMode.GOAL)
    expect(result.turns).toBe(2)
  })

  // ============================================================================
  // Test 9: Task tool spawns subagent
  // ============================================================================

  it('task tool spawns explore subagent with real execution', async () => {
    // This turn is for the subagent spawned by the task tool
    mockTurns = [
      {
        content: 'Exploring the codebase.',
        toolCalls: [{ id: 'tc-1', name: 'glob', input: { pattern: '**/*.ts' } }],
      },
      {
        toolCalls: [
          { id: 'tc-2', name: 'complete_task', input: { result: 'Found TypeScript files' } },
        ],
      },
    ]

    // Import the task tool
    const { taskTool } = await import('../../tools/task.js')

    const controller = createAbortController()
    const ctx = {
      sessionId: 'test-session',
      workingDirectory: '/tmp/test',
      signal: controller.signal,
    }

    const result = await taskTool.execute(
      {
        description: 'Explore codebase',
        prompt: 'Find all TypeScript files',
        agentType: 'explore' as const,
      },
      ctx
    )

    expect(result.success).toBe(true)
    expect(result.output).toContain('Completed')
    expect(result.metadata?.turns).toBe(2)
    expect(result.metadata?.terminationReason).toBe('completed')
  })

  // ============================================================================
  // Test 10: Task tool filters 'task' from subagent tools (recursion prevention)
  // ============================================================================

  it('task tool filters task from subagent tools (recursion prevention)', async () => {
    // The 'execute' type gets all tools — but 'task' should be filtered out
    mockTurns = [
      {
        toolCalls: [{ id: 'tc-1', name: 'complete_task', input: { result: 'Done' } }],
      },
    ]

    // Track the tools the subagent executor gets
    const { createClient } = await import('../../llm/client.js')
    const mockedCreateClient = vi.mocked(createClient)

    let subagentTools: string[] = []
    mockedCreateClient.mockImplementation(async () => {
      const client = createMockLLMClient(mockTurns)
      return {
        async *stream(messages, config) {
          subagentTools = (config.tools ?? []).map((t) => t.name)
          yield* client.stream(messages, config)
        },
      }
    })

    const { taskTool } = await import('../../tools/task.js')

    // Register the task tool itself so getToolDefinitions includes it
    registerTool(taskTool)

    const controller = createAbortController()
    const ctx = {
      sessionId: 'test-session',
      workingDirectory: '/tmp/test',
      signal: controller.signal,
    }

    await taskTool.execute(
      {
        description: 'Execute task',
        prompt: 'Do something',
        agentType: 'execute' as const,
      },
      ctx
    )

    // 'task' should NOT be in the subagent's tools (recursion prevention)
    expect(subagentTools).not.toContain('task')
    // But other tools should be present
    expect(subagentTools).toContain('glob')
    // complete_task is always added by AgentExecutor
    expect(subagentTools).toContain('complete_task')
  })
})
