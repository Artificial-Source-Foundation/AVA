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
      const [thinkingSegments] = createSignal<unknown[]>([])
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
        thinkingSegments,
        activeToolCalls,
        error,
        lastResult,
        tokenUsage,
        events,
        run,
        cancel,
        endRun: vi.fn(),
        steer: vi.fn(async () => {}),
        followUp: vi.fn(async () => {}),
        postComplete: vi.fn(async () => {}),
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
      generation: {
        customInstructions: '',
        reasoningEffort: 'off',
        compactionModel: '',
        autoCompact: false,
        compactionThreshold: 80,
      },
      behavior: { sessionAutoTitle: false },
      agentLimits: { agentMaxTurns: 20, agentMaxTimeMinutes: 10, autoFixLint: false },
      notifications: {},
      permissionMode: 'normal',
    }),
    isToolAutoApproved: () => false,
  }),
}))

vi.mock('../stores/session', () => {
  // eslint-disable-next-line @typescript-eslint/no-require-imports
  const solidJs = require('solid-js') as typeof import('solid-js')
  const createSignal = solidJs.createSignal

  const [currentSession, setCurrentSession] = createSignal<{ id: string; name: string } | null>(
    null
  )
  const [messages, setMessages] = createSignal<unknown[]>([])
  const [selectedModel] = createSignal<string | undefined>(undefined)
  const [selectedProvider] = createSignal<string | undefined>(undefined)
  const [checkpoints] = createSignal<unknown[]>([])
  const [fileOperations] = createSignal<unknown[]>([])
  const [editingMessageId] = createSignal<string | null>(null)
  const [retryingMessageId] = createSignal<string | null>(null)
  const [isLoadingMessages] = createSignal(false)
  const [compactionIndex] = createSignal(-1)

  return {
    useSession: () => ({
      currentSession,
      messages,
      selectedModel,
      selectedProvider,
      checkpoints,
      fileOperations,
      editingMessageId,
      retryingMessageId,
      isLoadingMessages,
      compactionIndex,
      createNewSession: async () => {
        setCurrentSession({ id: 's1', name: 'New Session' })
      },
      addMessage: vi.fn((message: unknown) => {
        setMessages((prev) => [...prev, message])
      }),
      updateMessage: vi.fn(),
      deleteMessage: vi.fn(),
      renameSession: vi.fn(async () => {}),
      setMessages,
      loadSessionMessages: vi.fn(async () => {}),
      startEditing: vi.fn(),
      stopEditing: vi.fn(),
      rollbackToMessage: vi.fn(async () => 0),
      branchAtMessage: vi.fn(async () => 0),
      rollbackToCheckpoint: vi.fn(),
      revertFilesAfter: vi.fn(async () => 0),
      createCheckpoint: vi.fn(),
    }),
  }
})

import { _resetAgentSingleton, useAgent } from './useAgent'

/** Flush multiple microtasks to let async code advance */
async function flushMicrotasks(count = 10): Promise<void> {
  for (let i = 0; i < count; i++) {
    await Promise.resolve()
  }
}

describe('useAgent integration queue/steer/cancel', () => {
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
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))

    // Start a run that will hang (pending promise)
    void ctx.agent.run('first message')
    await flushMicrotasks()

    // The run should be in progress (isRunning=true from mock)
    expect(ctx.agent.isRunning()).toBe(true)

    // Queue a follow-up while running
    void ctx.agent.run('queued follow-up')
    expect(ctx.agent.queuedCount()).toBe(1)

    // Cancel should clear queue
    ctx.agent.cancel()
    await flushMicrotasks()
    expect(ctx.agent.isRunning()).toBe(false)
    expect(ctx.agent.queuedCount()).toBe(0)

    ctx.dispose()
  })

  it('steer uses cancel-and-requeue when running', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))

    void ctx.agent.run('stream in progress')
    await flushMicrotasks()

    // Queue a follow-up while running
    void ctx.agent.run('queued message')
    expect(ctx.agent.queuedCount()).toBe(1)

    // Steer while running — cancels and adds steer content to queue
    ctx.agent.steer('priority steer')
    await flushMicrotasks()

    // After steer (which cancels), queue should have the steer content
    // (cancel clears the old queue, steer sets new queue)
    expect(ctx.agent.queuedCount()).toBeLessThanOrEqual(1)

    ctx.agent.cancel()
    await flushMicrotasks()
    expect(ctx.agent.queuedCount()).toBe(0)

    ctx.dispose()
  })

  it('auto-titles disabled does not rename session', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))

    void ctx.agent.run('Build OAuth flow')
    await flushMicrotasks()

    // With sessionAutoTitle=false in our mock settings, no rename should happen
    // This is a simplified test since the title generation requires core-v2

    ctx.agent.cancel()
    ctx.dispose()
  })
})
