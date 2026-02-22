/**
 * Agent Provider Switch Tests
 * Verifies mid-session provider switching
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { AgentExecutor } from '../loop.js'
import type { AgentEvent } from '../types.js'
import { createMockLLMClient, type MockLLMTurn } from './mock-llm.js'

// ============================================================================
// Mocks
// ============================================================================

let mockTurns: MockLLMTurn[] = []
let _lastProviderUsed: string | undefined

vi.mock('../../llm/client.js', () => ({
  createClient: vi.fn(async (provider: string) => {
    _lastProviderUsed = provider
    return createMockLLMClient(mockTurns)
  }),
  getAuth: vi.fn(async () => ({ type: 'api-key', token: 'test-key' })),
  getEditorModelConfig: vi.fn(() => ({
    model: 'test-model',
    provider: 'anthropic',
  })),
}))

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

vi.mock('../../git/auto-commit.js', () => ({
  autoCommitIfEnabled: vi.fn(async () => {}),
}))

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

const { resetDoomLoopDetector } = await import('../../session/doom-loop.js')
const { resetToolCallCount, registerTool } = await import('../../tools/registry.js')
const { resetMessageBus } = await import('../../bus/message-bus.js')
const { createClient } = await import('../../llm/client.js')

// ============================================================================
// Test Setup
// ============================================================================

beforeEach(() => {
  mockTurns = []
  _lastProviderUsed = undefined
  resetDoomLoopDetector()
  resetToolCallCount()
  resetMessageBus()
  vi.clearAllMocks()

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
      return { success: true, output: 'file1.ts' }
    },
  })
})

afterEach(() => {
  vi.restoreAllMocks()
})

// ============================================================================
// Tests
// ============================================================================

describe('Agent Provider Switching', () => {
  it('switches provider between turns via requestProviderSwitch', async () => {
    mockTurns = [
      {
        content: 'Working with anthropic...',
        toolCalls: [{ id: 'tc-1', name: 'glob', input: { pattern: '*.ts' } }],
      },
      {
        toolCalls: [
          { id: 'tc-2', name: 'attempt_completion', input: { result: 'Done with openai' } },
        ],
      },
    ]

    const events: AgentEvent[] = []
    const executor = new AgentExecutor(
      { maxTurns: 10, maxTimeMinutes: 5, provider: 'anthropic' },
      (e) => events.push(e)
    )

    // Request switch after first turn
    // We schedule this via a one-shot event listener
    const originalEmit = (executor as unknown as { emit: (e: AgentEvent) => void }).emit
    const boundEmit = originalEmit.bind(executor)
    let switchRequested = false
    ;(executor as unknown as { emit: (e: AgentEvent) => void }).emit = (e: AgentEvent) => {
      boundEmit(e)
      if (e.type === 'turn:finish' && !switchRequested) {
        switchRequested = true
        executor.requestProviderSwitch('openai', 'gpt-4o')
      }
    }

    const controller = new AbortController()
    const result = await executor.run(
      { goal: 'Test provider switch', cwd: '/tmp/test' },
      controller.signal
    )

    expect(result.success).toBe(true)

    // createClient should have been called with 'openai' for the switch
    const mockedCreateClient = vi.mocked(createClient)
    const calls = mockedCreateClient.mock.calls
    expect(calls.some(([p]) => p === 'openai')).toBe(true)

    // Should emit provider:switch event
    const switchEvents = events.filter((e) => e.type === 'provider:switch')
    expect(switchEvents).toHaveLength(1)
    const switchEvent = switchEvents[0] as {
      type: 'provider:switch'
      provider: string
      model: string
    }
    expect(switchEvent.provider).toBe('openai')
    expect(switchEvent.model).toBe('gpt-4o')
  })

  it('preserves conversation history across provider switch', async () => {
    // The mock client always returns from the shared queue regardless of provider
    // This test verifies the agent continues working (history not cleared)
    mockTurns = [
      {
        content: 'Turn 1 with anthropic.',
        toolCalls: [{ id: 'tc-1', name: 'glob', input: { pattern: '*.ts' } }],
      },
      {
        content: 'Turn 2 with openai after switch.',
        toolCalls: [
          { id: 'tc-2', name: 'attempt_completion', input: { result: 'Completed after switch' } },
        ],
      },
    ]

    const executor = new AgentExecutor({
      maxTurns: 10,
      maxTimeMinutes: 5,
      provider: 'anthropic',
    })

    // Request switch immediately
    let switchDone = false
    const origEmit = (executor as unknown as { emit: (e: AgentEvent) => void }).emit
    const bound = origEmit.bind(executor)
    ;(executor as unknown as { emit: (e: AgentEvent) => void }).emit = (e: AgentEvent) => {
      bound(e)
      if (e.type === 'turn:finish' && !switchDone) {
        switchDone = true
        executor.requestProviderSwitch('openai')
      }
    }

    const controller = new AbortController()
    const result = await executor.run(
      { goal: 'Test history preservation', cwd: '/tmp/test' },
      controller.signal
    )

    // Agent should complete successfully despite provider change
    expect(result.success).toBe(true)
    expect(result.output).toContain('Completed after switch')
    expect(result.turns).toBeGreaterThanOrEqual(2)
  })

  it('emits provider:switch event on successful switch', async () => {
    mockTurns = [
      {
        toolCalls: [{ id: 'tc-1', name: 'glob', input: { pattern: '*.ts' } }],
      },
      {
        toolCalls: [{ id: 'tc-2', name: 'attempt_completion', input: { result: 'Done' } }],
      },
    ]

    const events: AgentEvent[] = []
    const executor = new AgentExecutor({ maxTurns: 10, maxTimeMinutes: 5 }, (e) => events.push(e))

    let switched = false
    const origEmit = (executor as unknown as { emit: (e: AgentEvent) => void }).emit
    const bound = origEmit.bind(executor)
    ;(executor as unknown as { emit: (e: AgentEvent) => void }).emit = (e: AgentEvent) => {
      bound(e)
      if (e.type === 'turn:finish' && !switched) {
        switched = true
        executor.requestProviderSwitch('google', 'gemini-2.5-pro')
      }
    }

    const controller = new AbortController()
    await executor.run({ goal: 'Switch test', cwd: '/tmp/test' }, controller.signal)

    const switchEvents = events.filter((e) => e.type === 'provider:switch')
    expect(switchEvents).toHaveLength(1)
  })

  it('handles switch with invalid provider gracefully', async () => {
    // Mock createClient to throw for unknown provider
    const mockedCreateClient = vi.mocked(createClient)
    const _originalImpl = mockedCreateClient.getMockImplementation()!
    let callCount = 0
    mockedCreateClient.mockImplementation(async (provider: string) => {
      callCount++
      if (callCount > 1 && provider === 'invalid-provider') {
        throw new Error('Unknown provider: invalid-provider')
      }
      return createMockLLMClient(mockTurns)
    })

    mockTurns = [
      {
        toolCalls: [{ id: 'tc-1', name: 'glob', input: { pattern: '*.ts' } }],
      },
      {
        toolCalls: [{ id: 'tc-2', name: 'attempt_completion', input: { result: 'Done anyway' } }],
      },
    ]

    const executor = new AgentExecutor({ maxTurns: 10, maxTimeMinutes: 5 })

    let switched = false
    const origEmit = (executor as unknown as { emit: (e: AgentEvent) => void }).emit
    const bound = origEmit.bind(executor)
    ;(executor as unknown as { emit: (e: AgentEvent) => void }).emit = (e: AgentEvent) => {
      bound(e)
      if (e.type === 'turn:finish' && !switched) {
        switched = true
        // Use a cast to bypass type safety for testing invalid provider
        executor.requestProviderSwitch('invalid-provider' as 'openai')
      }
    }

    const controller = new AbortController()
    const result = await executor.run(
      { goal: 'Invalid switch test', cwd: '/tmp/test' },
      controller.signal
    )

    // Should still succeed — keeps existing client on failure
    expect(result.success).toBe(true)
  })
})
