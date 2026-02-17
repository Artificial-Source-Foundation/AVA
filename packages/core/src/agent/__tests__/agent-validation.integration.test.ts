/**
 * Agent Validation Integration Tests
 * Verifies the validation pipeline is wired into task completion
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { AgentExecutor } from '../loop.js'
import type { AgentEvent } from '../types.js'
import { AgentTerminateMode } from '../types.js'
import { createMockLLMClient, type MockLLMTurn } from './mock-llm.js'

// ============================================================================
// Mocks
// ============================================================================

let mockTurns: MockLLMTurn[] = []

vi.mock('../../llm/client.js', () => ({
  createClient: vi.fn(async () => createMockLLMClient(mockTurns)),
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

// Mock validation pipeline — control pass/fail from tests
let validationShouldPass = true
vi.mock('../../validator/index.js', () => ({
  SimpleValidatorRegistry: vi.fn().mockImplementation(() => ({
    register: vi.fn(),
  })),
  ValidationPipeline: vi.fn().mockImplementation(() => ({
    run: vi.fn(async () => ({
      passed: validationShouldPass,
      results: [],
      totalDurationMs: 100,
      summary: {
        total: 1,
        passed: validationShouldPass ? 1 : 0,
        failed: validationShouldPass ? 0 : 1,
        totalErrors: 0,
        totalWarnings: 0,
      },
    })),
    formatReport: vi.fn(() =>
      validationShouldPass ? 'All checks passed' : 'Validation failed: syntax error'
    ),
  })),
  syntaxValidator: { name: 'syntax', critical: true },
  typescriptValidator: { name: 'typescript', critical: true },
  lintValidator: { name: 'lint', critical: false },
}))

// ============================================================================
// Imports that depend on mocks
// ============================================================================

const { resetDoomLoopDetector } = await import('../../session/doom-loop.js')
const { resetToolCallCount, registerTool } = await import('../../tools/registry.js')
const { resetMessageBus } = await import('../../bus/message-bus.js')

// ============================================================================
// Test Setup
// ============================================================================

beforeEach(() => {
  mockTurns = []
  validationShouldPass = true
  resetDoomLoopDetector()
  resetToolCallCount()
  resetMessageBus()
  vi.clearAllMocks()

  registerTool({
    definition: {
      name: 'write_file',
      description: 'Write a file',
      input_schema: {
        type: 'object',
        properties: { path: { type: 'string' }, content: { type: 'string' } },
        required: ['path', 'content'],
      },
    },
    async execute() {
      return { success: true, output: 'File written successfully' }
    },
  })

  registerTool({
    definition: {
      name: 'edit',
      description: 'Edit a file',
      input_schema: {
        type: 'object',
        properties: { file_path: { type: 'string' }, content: { type: 'string' } },
        required: ['file_path', 'content'],
      },
    },
    async execute() {
      return { success: true, output: 'File edited successfully' }
    },
  })

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

describe('Agent Validation Integration', () => {
  it('runs validation on completion when enabled and files modified', async () => {
    mockTurns = [
      {
        toolCalls: [
          { id: 'tc-1', name: 'write_file', input: { path: '/tmp/test/foo.ts', content: 'code' } },
        ],
      },
      {
        toolCalls: [{ id: 'tc-2', name: 'complete_task', input: { result: 'Done writing' } }],
      },
    ]

    const events: AgentEvent[] = []
    const executor = new AgentExecutor(
      { maxTurns: 10, maxTimeMinutes: 5, validationEnabled: true },
      (e) => events.push(e)
    )

    const controller = new AbortController()
    const result = await executor.run({ goal: 'Write a file', cwd: '/tmp/test' }, controller.signal)

    expect(result.success).toBe(true)
    expect(result.terminateMode).toBe(AgentTerminateMode.GOAL)

    // Should emit validation events
    const validationEvents = events.filter((e) => e.type.startsWith('validation:'))
    expect(validationEvents.length).toBeGreaterThanOrEqual(2) // start + finish at minimum
  })

  it('sends agent back to loop on validation failure', async () => {
    validationShouldPass = false

    mockTurns = [
      {
        toolCalls: [
          {
            id: 'tc-1',
            name: 'write_file',
            input: { path: '/tmp/test/bar.ts', content: 'bad code' },
          },
        ],
      },
      // First completion attempt — fails validation
      {
        toolCalls: [{ id: 'tc-2', name: 'complete_task', input: { result: 'Done' } }],
      },
      // Agent fixes and tries again — still fails
      {
        toolCalls: [{ id: 'tc-3', name: 'complete_task', input: { result: 'Fixed now' } }],
      },
      // After max retries, validation is skipped and completion proceeds
    ]

    // After 2 failures, make it pass on retry 3 (but max retries is 2 so it won't get a 3rd try)
    const executor = new AgentExecutor({
      maxTurns: 10,
      maxTimeMinutes: 5,
      validationEnabled: true,
      maxValidationRetries: 2,
    })

    const controller = new AbortController()
    const result = await executor.run({ goal: 'Write code', cwd: '/tmp/test' }, controller.signal)

    // After maxValidationRetries, the agent proceeds to completion
    // (validation is skipped because retries exhausted)
    expect(result.turns).toBeGreaterThanOrEqual(2)
  })

  it('skips validation when disabled', async () => {
    mockTurns = [
      {
        toolCalls: [
          { id: 'tc-1', name: 'write_file', input: { path: '/tmp/test/foo.ts', content: 'code' } },
        ],
      },
      {
        toolCalls: [{ id: 'tc-2', name: 'complete_task', input: { result: 'Done' } }],
      },
    ]

    const events: AgentEvent[] = []
    const executor = new AgentExecutor(
      { maxTurns: 10, maxTimeMinutes: 5, validationEnabled: false },
      (e) => events.push(e)
    )

    const controller = new AbortController()
    const result = await executor.run({ goal: 'Write a file', cwd: '/tmp/test' }, controller.signal)

    expect(result.success).toBe(true)

    // No validation events
    const validationEvents = events.filter((e) => e.type.startsWith('validation:'))
    expect(validationEvents).toHaveLength(0)
  })

  it('skips validation when no files modified', async () => {
    mockTurns = [
      {
        toolCalls: [{ id: 'tc-1', name: 'glob', input: { pattern: '*.ts' } }],
      },
      {
        toolCalls: [{ id: 'tc-2', name: 'complete_task', input: { result: 'Found files' } }],
      },
    ]

    const events: AgentEvent[] = []
    const executor = new AgentExecutor(
      { maxTurns: 10, maxTimeMinutes: 5, validationEnabled: true },
      (e) => events.push(e)
    )

    const controller = new AbortController()
    const result = await executor.run({ goal: 'Search files', cwd: '/tmp/test' }, controller.signal)

    expect(result.success).toBe(true)

    // No validation events (no files modified)
    const validationEvents = events.filter((e) => e.type.startsWith('validation:'))
    expect(validationEvents).toHaveLength(0)
  })

  it('respects maxValidationRetries', async () => {
    validationShouldPass = false

    mockTurns = [
      {
        toolCalls: [
          { id: 'tc-1', name: 'write_file', input: { path: '/tmp/test/x.ts', content: 'code' } },
        ],
      },
      // First attempt — fails
      {
        toolCalls: [{ id: 'tc-2', name: 'complete_task', input: { result: 'Try 1' } }],
      },
      // Second attempt — fails (maxValidationRetries: 1)
      {
        toolCalls: [{ id: 'tc-3', name: 'complete_task', input: { result: 'Try 2' } }],
      },
    ]

    const executor = new AgentExecutor({
      maxTurns: 10,
      maxTimeMinutes: 5,
      validationEnabled: true,
      maxValidationRetries: 1,
    })

    const controller = new AbortController()
    const result = await executor.run({ goal: 'Write code', cwd: '/tmp/test' }, controller.signal)

    // With maxValidationRetries: 1, only 1 retry allowed
    // First complete_task fails validation, second bypasses it
    expect(result.success).toBe(true)
  })
})
