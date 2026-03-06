import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { afterEach, describe, expect, it, vi } from 'vitest'

const { runPipelineMock, reviewAgentOutputMock } = vi.hoisted(() => ({
  runPipelineMock: vi.fn().mockResolvedValue({
    passed: true,
    results: [],
    totalDurationMs: 10,
    summary: { total: 0, passed: 0, failed: 0, totalErrors: 0, totalWarnings: 0 },
  }),
  reviewAgentOutputMock: vi.fn(),
}))

vi.mock('./pipeline.js', async (importOriginal) => {
  const original = await importOriginal<typeof import('./pipeline.js')>()
  return {
    ...original,
    runPipeline: runPipelineMock,
  }
})

vi.mock('./reviewer.js', () => ({
  reviewAgentOutput: reviewAgentOutputMock,
}))

afterEach(() => {
  vi.clearAllMocks()
})

describe('validator reviewer loop', () => {
  it('approved output passes through unchanged', async () => {
    const { activate } = await import('./index.js')
    reviewAgentOutputMock.mockResolvedValue({
      approved: true,
      feedback: 'Looks good',
      confidence: 0.9,
      issues: [],
    })

    const { api } = createMockExtensionAPI()
    api.getSettings = vi.fn().mockReturnValue({
      enabledValidators: ['syntax', 'typescript', 'lint'],
      timeout: 30_000,
      failFast: true,
      reviewEnabled: true,
      reviewProvider: 'openrouter',
      reviewModel: 'anthropic/claude-sonnet-4-6',
    })

    activate(api)
    api.emit('agent:completing', {
      agentId: 'agent-1',
      goal: 'Implement feature',
      result: 'Done',
      filesChanged: ['a.ts'],
      diffs: ['+ const a = 1'],
    })

    await vi.waitFor(() => {
      expect(reviewAgentOutputMock).toHaveBeenCalledTimes(1)
    })
    expect(reviewAgentOutputMock.mock.calls[0]?.[1]).toBe('Done')
  })

  it('rejected output triggers one retry with feedback', async () => {
    const { activate } = await import('./index.js')
    reviewAgentOutputMock
      .mockResolvedValueOnce({
        approved: false,
        feedback: 'Missing edge case coverage',
        confidence: 0.7,
        issues: ['No test for empty input'],
      })
      .mockResolvedValueOnce({
        approved: true,
        feedback: 'Now complete',
        confidence: 0.8,
        issues: [],
      })

    const { api } = createMockExtensionAPI()
    api.getSettings = vi.fn().mockReturnValue({
      enabledValidators: ['syntax', 'typescript', 'lint'],
      timeout: 30_000,
      failFast: true,
      reviewEnabled: true,
      reviewProvider: 'openrouter',
      reviewModel: 'anthropic/claude-sonnet-4-6',
    })

    activate(api)
    api.emit('agent:completing', {
      agentId: 'agent-2',
      goal: 'Ship robust feature',
      result: 'Initial implementation',
      filesChanged: ['b.ts'],
      diffs: ['+ function run() {}'],
    })

    await vi.waitFor(() => {
      expect(reviewAgentOutputMock).toHaveBeenCalledTimes(2)
    })

    const retryOutput = reviewAgentOutputMock.mock.calls[1]?.[1] as string
    expect(retryOutput).toContain('Initial implementation')
    expect(retryOutput).toContain('Missing edge case coverage')
  })

  it('max 1 retry cycle (no infinite loops)', async () => {
    const { activate } = await import('./index.js')
    reviewAgentOutputMock.mockResolvedValue({
      approved: false,
      feedback: 'Still not enough',
      confidence: 0.5,
      issues: ['Gap remains'],
    })

    const { api } = createMockExtensionAPI()
    api.getSettings = vi.fn().mockReturnValue({
      enabledValidators: ['syntax', 'typescript', 'lint'],
      timeout: 30_000,
      failFast: true,
      reviewEnabled: true,
      reviewProvider: 'openrouter',
      reviewModel: 'anthropic/claude-sonnet-4-6',
    })

    activate(api)
    api.emit('agent:completing', {
      agentId: 'agent-3',
      goal: 'Feature',
      result: 'Attempt',
      filesChanged: [],
      diffs: [],
    })

    await vi.waitFor(() => {
      expect(reviewAgentOutputMock).toHaveBeenCalledTimes(2)
    })
  })

  it('reviewer is disabled by default', async () => {
    const { activate } = await import('./index.js')
    const { api, emittedEvents } = createMockExtensionAPI()
    api.getSettings = vi.fn().mockReturnValue({
      enabledValidators: ['syntax', 'typescript', 'lint'],
      timeout: 30_000,
      failFast: true,
    })

    activate(api)
    api.emit('agent:completing', {
      agentId: 'agent-4',
      goal: 'No review',
      result: 'Done',
      filesChanged: [],
      diffs: [],
    })

    await vi.waitFor(() => {
      const result = emittedEvents.find((e) => e.event === 'validation:result')
      expect(result).toBeDefined()
    })
    expect(reviewAgentOutputMock).not.toHaveBeenCalled()
  })

  it('reviewer uses configured provider/model', async () => {
    const { activate } = await import('./index.js')
    reviewAgentOutputMock.mockResolvedValue({
      approved: true,
      feedback: 'OK',
      confidence: 0.9,
      issues: [],
    })

    const { api } = createMockExtensionAPI()
    api.getSettings = vi.fn().mockReturnValue({
      enabledValidators: ['syntax', 'typescript', 'lint'],
      timeout: 30_000,
      failFast: true,
      reviewEnabled: true,
      reviewProvider: 'openrouter',
      reviewModel: 'google/gemini-2.5-pro',
    })

    activate(api)
    api.emit('agent:completing', {
      agentId: 'agent-5',
      goal: 'Use configured reviewer model',
      result: 'Done',
      filesChanged: [],
      diffs: [],
    })

    await vi.waitFor(() => {
      expect(reviewAgentOutputMock).toHaveBeenCalledTimes(1)
    })
    expect(reviewAgentOutputMock.mock.calls[0]?.[4]).toBe('openrouter')
    expect(reviewAgentOutputMock.mock.calls[0]?.[5]).toBe('google/gemini-2.5-pro')
  })
})
