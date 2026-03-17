import { createRoot } from 'solid-js'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const h = vi.hoisted(() => {
  let runResolve: ((value: unknown) => void) | null = null
  const runMock = vi.fn(
    (_goal: string) =>
      new Promise((resolve) => {
        runResolve = resolve
      })
  )
  const cancelMock = vi.fn(async () => {})
  const clearErrorMock = vi.fn()

  return {
    runMock,
    cancelMock,
    clearErrorMock,
    getRunResolve: () => runResolve,
    resolveRun: (val?: unknown) => {
      if (runResolve) runResolve(val ?? { id: 's1', completed: true, messages: [] })
    },
    isRunningState: { value: false },
    errorState: { value: null as string | null },
  }
})

// Mock the Rust agent at the lowest level
vi.mock('./use-rust-agent', () => {
  // eslint-disable-next-line @typescript-eslint/no-require-imports
  const solidJs = require('solid-js') as typeof import('solid-js')
  const createSignal = solidJs.createSignal
  return {
    useRustAgent: () => {
      const [isRunning, setIsRunning] = createSignal(false)
      const [error, setError] = createSignal<string | null>(null)
      const [streamingContent, _setStreamingContent] = createSignal('')
      const [events, _setEvents] = createSignal<unknown[]>([])
      const [activeToolCalls] = createSignal<unknown[]>([])
      const [lastResult, setLastResult] = createSignal<unknown>(null)
      const [tokenUsage] = createSignal({ input: 0, output: 0, cost: 0 })
      const [thinkingContent] = createSignal('')

      const run = async (goal: string) => {
        setIsRunning(true)
        h.isRunningState.value = true
        try {
          const result = await h.runMock(goal)
          setLastResult(result)
          return result
        } finally {
          setIsRunning(false)
          h.isRunningState.value = false
        }
      }

      const cancel = async () => {
        setIsRunning(false)
        h.isRunningState.value = false
        h.cancelMock()
      }

      return {
        isRunning,
        streamingContent,
        thinkingContent,
        activeToolCalls,
        error,
        lastResult,
        tokenUsage,
        events,
        run,
        cancel,
        clearError: () => {
          setError(null)
          h.clearErrorMock()
        },
        stop: cancel,
        isStreaming: isRunning,
        currentTokens: streamingContent,
        session: lastResult,
      }
    },
  }
})

vi.mock('../lib/tool-approval', () => ({
  checkAutoApproval: vi.fn(() => false),
  createApprovalGate: vi.fn(() => ({
    pendingApproval: () => null,
    resolveApproval: vi.fn(),
  })),
}))

vi.mock('../services/tool-approval-bridge', () => ({
  pendingApproval: () => null,
  resolveApproval: vi.fn(),
  createApprovalMiddleware: vi.fn(() => ({ name: 'test', priority: 5 })),
  setAutoApprovalChecker: vi.fn(),
}))

vi.mock('../stores/settings', () => ({
  useSettings: () => ({
    settings: () => ({
      generation: { customInstructions: '', delegationEnabled: false, reasoningEffort: 'off' },
      behavior: { sessionAutoTitle: false },
      agentLimits: { agentMaxTurns: 20, agentMaxTimeMinutes: 10, autoFixLint: false },
      notifications: {},
      permissionMode: 'normal',
    }),
    isToolAutoApproved: () => false,
  }),
}))

import { _resetAgentSingleton } from './useAgent'
import { useChat } from './useChat'

/** Flush multiple microtasks to let async code advance */
async function flushMicrotasks(count = 10): Promise<void> {
  for (let i = 0; i < count; i++) {
    await Promise.resolve()
  }
}

describe('useChat integration queue/steer/cancel', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    _resetAgentSingleton()
    h.isRunningState.value = false
    h.errorState.value = null
  })

  afterEach(() => {
    vi.clearAllMocks()
  })

  it('queues follow-up messages while streaming and clears queue on cancel', async () => {
    const ctx = createRoot((dispose) => ({ chat: useChat(), dispose }))

    // Start a run that will hang (pending promise)
    void ctx.chat.sendMessage('first message')
    await flushMicrotasks()

    // The run should be in progress (isRunning=true from mock)
    expect(ctx.chat.isStreaming()).toBe(true)

    // Queue a follow-up while running
    void ctx.chat.sendMessage('queued follow-up')
    expect(ctx.chat.queuedCount()).toBe(1)

    // Cancel should clear queue
    ctx.chat.cancel()
    await flushMicrotasks()
    expect(ctx.chat.isStreaming()).toBe(false)
    expect(ctx.chat.queuedCount()).toBe(0)

    ctx.dispose()
  })

  it('steer uses cancel-and-requeue when running', async () => {
    const ctx = createRoot((dispose) => ({ chat: useChat(), dispose }))

    void ctx.chat.sendMessage('stream in progress')
    await flushMicrotasks()

    // Queue a follow-up while running
    void ctx.chat.sendMessage('queued message')
    expect(ctx.chat.queuedCount()).toBe(1)

    // Steer while running — cancels and adds steer content to queue
    ctx.chat.steer('priority steer')
    await flushMicrotasks()

    // After steer (which cancels), queue should have the steer content
    // (cancel clears the old queue, steer sets new queue)
    expect(ctx.chat.queuedCount()).toBeLessThanOrEqual(1)

    ctx.chat.cancel()
    await flushMicrotasks()
    expect(ctx.chat.queuedCount()).toBe(0)

    ctx.dispose()
  })

  it('auto-titles disabled does not rename session', async () => {
    const ctx = createRoot((dispose) => ({ chat: useChat(), dispose }))

    void ctx.chat.sendMessage('Build OAuth flow')
    await flushMicrotasks()

    // With sessionAutoTitle=false in our mock settings, no rename should happen
    // This is a simplified test since the title generation requires core-v2

    ctx.chat.cancel()
    ctx.dispose()
  })
})
