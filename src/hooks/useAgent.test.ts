import { createRoot } from 'solid-js'
import { beforeEach, describe, expect, it, vi } from 'vitest'

import type { Message } from '../types'
import type { AgentEvent } from '../types/rust-ipc'

let isTauriRuntime = false
const replaceMessagesFromBackendMock = vi.fn()
const recoverDetachedDesktopSessionIfNeededMock = vi.fn(async () => false)
const testMocks = vi.hoisted(() => ({
  getMessagesMock: vi.fn<(sessionId: string) => Promise<Message[]>>(async () => []),
}))

vi.mock('@tauri-apps/api/core', () => ({
  isTauri: () => isTauriRuntime,
}))

const h = vi.hoisted(() => {
  type RehydrateResultLike = {
    sessionId: string | null
    running: boolean
    runId: string | null
    pendingApproval: null
    pendingQuestion: {
      type: 'question_request'
      id: string
      question: string
      options: string[]
      run_id: string
    } | null
    pendingPlan: null
  }
  let appendEvent: ((event: AgentEvent) => void) | null = null
  let resetEvents: (() => void) | null = null
  let setCurrentRunId: ((runId: string | null) => void) | null = null
  let setTrackedSessionId: ((sessionId: string | null) => void) | null = null
  let setIsRunningState: ((value: boolean) => void) | null = null
  let setCurrentSessionId: ((sessionId: string | null) => void) | null = null
  const rehydrateStatus = vi.fn<(sessionId?: string | null) => Promise<RehydrateResultLike>>(
    async (sessionId?: string | null) => ({
      sessionId: sessionId ?? null,
      running: false,
      runId: null,
      pendingApproval: null,
      pendingQuestion: null,
      pendingPlan: null,
    })
  )
  const resetState = vi.fn()

  return {
    bind(
      nextAppendEvent: (event: AgentEvent) => void,
      nextResetEvents: () => void,
      nextSetCurrentRunId: (runId: string | null) => void,
      nextSetTrackedSessionId: (sessionId: string | null) => void,
      nextSetIsRunningState: (value: boolean) => void
    ): void {
      appendEvent = nextAppendEvent
      resetEvents = nextResetEvents
      setCurrentRunId = nextSetCurrentRunId
      setTrackedSessionId = nextSetTrackedSessionId
      setIsRunningState = nextSetIsRunningState
    },
    bindSession(nextSetCurrentSessionId: (sessionId: string | null) => void): void {
      setCurrentSessionId = nextSetCurrentSessionId
    },
    emit(event: AgentEvent): void {
      appendEvent?.(event)
    },
    setRunId(runId: string | null): void {
      setCurrentRunId?.(runId)
    },
    setTrackedRunSessionId(sessionId: string | null): void {
      setTrackedSessionId?.(sessionId)
    },
    setIsRunning(value: boolean): void {
      setIsRunningState?.(value)
    },
    setSessionId(sessionId: string | null): void {
      setCurrentSessionId?.(sessionId)
    },
    reset(): void {
      resetEvents?.()
      setIsRunningState?.(false)
      setCurrentRunId?.(null)
      setTrackedSessionId?.(null)
      setCurrentSessionId?.('session-1')
      rehydrateStatus.mockClear()
      resetState.mockClear()
    },
    rehydrateStatus,
    resetState,
  }
})

vi.mock('./use-rust-agent', async () => {
  const { createSignal } = await vi.importActual<typeof import('solid-js')>('solid-js')

  return {
    useRustAgent: () => {
      const [events, setEvents] = createSignal<AgentEvent[]>([])
      const [isRunning, setIsRunning] = createSignal(false)
      const [streamingContent] = createSignal('')
      const [thinkingContent] = createSignal('')
      const [thinkingSegments] = createSignal([])
      const [activeToolCalls] = createSignal([])
      const [error, setError] = createSignal<string | null>(null)
      const [lastResult, setLastResult] = createSignal(null)
      const [currentRunId, setCurrentRunId] = createSignal<string | null>(null)
      const [trackedSessionId, setTrackedSessionId] = createSignal<string | null>(null)
      const [detachedSessionId, setDetachedSessionId] = createSignal<string | null>(null)
      const [tokenUsage] = createSignal({ input: 0, output: 0, cost: 0 })
      const [progressMessage, setProgressMessage] = createSignal<string | null>(null)
      const [budgetWarning, setBudgetWarning] = createSignal<{
        thresholdPercent: number
        currentCostUsd: number
        maxBudgetUsd: number
      } | null>(null)
      const [pendingPlan, setPendingPlan] = createSignal(null)
      const [todos, setTodos] = createSignal([])

      h.bind(
        (event) => setEvents((prev) => [...prev, event]),
        () => setEvents([]),
        setCurrentRunId,
        setTrackedSessionId,
        setIsRunning
      )

      return {
        isRunning,
        streamingContent,
        thinkingContent,
        thinkingSegments,
        activeToolCalls,
        error,
        lastResult,
        currentRunId,
        trackedSessionId,
        detachedSessionId,
        tokenUsage,
        events,
        eventVersion: () => events().length,
        eventCursor: () => events().length,
        readEventsSince: (cursor: number) => {
          const snapshot = events()
          const safeCursor = Math.max(0, Math.min(cursor, snapshot.length))
          return {
            cursor: snapshot.length,
            events: snapshot.slice(safeCursor),
          }
        },
        progressMessage,
        budgetWarning,
        pendingPlan,
        todos,
        run: vi.fn(async () => null),
        editAndResendRun: vi.fn(async () => null),
        retryRun: vi.fn(async () => null),
        regenerateRun: vi.fn(async () => null),
        cancel: vi.fn(async () => {}),
        clearError: () => setError(null),
        endRun: vi.fn(),
        steer: vi.fn(async () => {}),
        followUp: vi.fn(async () => {}),
        postComplete: vi.fn(async () => {}),
        rehydrateStatus: h.rehydrateStatus,
        markToolApproval: vi.fn(),
        stop: vi.fn(async () => {}),
        isStreaming: isRunning,
        currentTokens: streamingContent,
        session: lastResult,
        resetState: h.resetState,
        captureRuntimeSnapshot: () => ({
          isRunning: isRunning(),
          streamingContent: streamingContent(),
          thinkingContent: thinkingContent(),
          activeToolCalls: activeToolCalls(),
          error: error(),
          lastResult: lastResult(),
          currentRunId: currentRunId(),
          trackedSessionId: trackedSessionId(),
          detachedSessionId: detachedSessionId(),
          tokenUsage: tokenUsage(),
          events: events(),
          progressMessage: progressMessage(),
          budgetWarning: budgetWarning(),
          pendingPlan: pendingPlan(),
          thinkingSegments: thinkingSegments(),
          todos: todos(),
          binding: {
            activeRunId: currentRunId(),
            attachedSessionId: trackedSessionId(),
          },
        }),
        restoreRuntimeSnapshot: (snapshot: Record<string, unknown> | null) => {
          if (!snapshot) {
            setEvents([])
            setError(null)
            setLastResult(null)
            setCurrentRunId(null)
            setTrackedSessionId(null)
            setDetachedSessionId(null)
            setProgressMessage(null)
            setBudgetWarning(null)
            setPendingPlan(null)
            setTodos([])
            setIsRunning(false)
            return
          }
          setEvents((snapshot.events as AgentEvent[]) ?? [])
          setError((snapshot.error as string | null) ?? null)
          setLastResult((snapshot.lastResult as null) ?? null)
          setCurrentRunId((snapshot.currentRunId as string | null) ?? null)
          setTrackedSessionId((snapshot.trackedSessionId as string | null) ?? null)
          setDetachedSessionId((snapshot.detachedSessionId as string | null) ?? null)
          setProgressMessage((snapshot.progressMessage as string | null) ?? null)
          setBudgetWarning(
            (snapshot.budgetWarning as {
              thresholdPercent: number
              currentCostUsd: number
              maxBudgetUsd: number
            } | null) ?? null
          )
          setPendingPlan((snapshot.pendingPlan as null) ?? null)
          setTodos((snapshot.todos as never[]) ?? [])
          setIsRunning(Boolean(snapshot.isRunning))
        },
      }
    },
  }
})

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

vi.mock('../stores/session', async () => {
  const { createSignal } = await vi.importActual<typeof import('solid-js')>('solid-js')
  const [currentSessionId, setCurrentSessionId] = createSignal<string | null>('session-1')
  h.bindSession(setCurrentSessionId)

  return {
    useSession: () => ({
      currentSession: () => {
        const sessionId = currentSessionId()
        return sessionId ? { id: sessionId, name: `Session ${sessionId}` } : null
      },
      messages: () => [],
      selectedModel: () => 'gpt-5.4',
      selectedProvider: () => 'openai',
      createNewSession: vi.fn(async () => ({ id: 'session-1', name: 'Session 1' })),
      addMessage: vi.fn(),
      addMessageToSession: vi.fn(),
      updateMessage: vi.fn(),
      updateMessageInSession: vi.fn(),
      deleteMessage: vi.fn(async () => {}),
      deleteMessageInSession: vi.fn(async () => {}),
      deleteMessagesAfter: vi.fn(),
      replaceMessagesFromBackend: replaceMessagesFromBackendMock,
      recoverDetachedDesktopSessionIfNeeded: recoverDetachedDesktopSessionIfNeededMock,
      renameSession: vi.fn(async () => {}),
      stopEditing: vi.fn(),
      setMessageError: vi.fn(),
    }),
  }
})

vi.mock('../services/core-bridge', () => ({
  ensureActiveSessionSynced: vi.fn(async () => ({
    sessionId: 'session-1',
    exists: true,
    messageCount: 0,
  })),
  getCoreBudget: () => null,
  markActiveSessionSynced: vi.fn(),
}))

vi.mock('../services/context-compaction', () => ({
  applyCompactionResult: vi.fn(),
  decodeCompactionModel: () => null,
}))

vi.mock('../services/database', () => ({
  getMessages: testMocks.getMessagesMock,
}))

vi.mock('../services/web-session-identity', () => ({
  registerBackendSessionId: vi.fn(),
}))

vi.mock('../services/rust-bridge', () => ({
  rustAgent: {
    cancel: vi.fn(async () => {}),
    resolveApproval: vi.fn(async () => {}),
    resolveQuestion: vi.fn(async () => {}),
    resolvePlan: vi.fn(async () => {}),
  },
  rustBackend: { undoLastEdit: vi.fn(async () => ({ success: true, message: 'ok' })) },
}))

import { _resetAgentSingleton, useAgent } from './useAgent'

async function flushEffects(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
  await Promise.resolve()
  await Promise.resolve()
  await new Promise((resolve) => setTimeout(resolve, 0))
}

describe('useAgent session attachment', () => {
  beforeEach(() => {
    isTauriRuntime = false
    vi.clearAllMocks()
    replaceMessagesFromBackendMock.mockReset()
    recoverDetachedDesktopSessionIfNeededMock.mockReset()
    recoverDetachedDesktopSessionIfNeededMock.mockResolvedValue(false)
    testMocks.getMessagesMock.mockReset()
    testMocks.getMessagesMock.mockResolvedValue([])
    h.reset()
    _resetAgentSingleton()
  })

  it('rehydrates backend status only once through the singleton agent store', async () => {
    const first = createRoot((dispose) => ({ agent: useAgent(), dispose }))
    const second = createRoot((dispose) => ({ agent: useAgent(), dispose }))
    await flushEffects()

    expect(first.agent).toBe(second.agent)
    expect(h.rehydrateStatus).toHaveBeenCalledTimes(1)
    expect(h.rehydrateStatus).toHaveBeenCalledWith('session-1')

    first.dispose()
    second.dispose()
  })

  it('preserves persisted tool calls when refreshing session messages after rehydrate', async () => {
    testMocks.getMessagesMock.mockResolvedValueOnce([
      {
        id: 'assistant-1',
        sessionId: 'session-1',
        role: 'assistant' as const,
        content: 'Done',
        createdAt: 1,
        metadata: {
          toolCalls: [
            { id: 'call-1', name: 'bash', arguments: { command: 'pwd' }, status: 'completed' },
          ],
        },
        toolCalls: [
          { id: 'call-1', name: 'bash', args: { command: 'pwd' }, status: 'success', startedAt: 0 },
        ],
      },
    ])

    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))
    await flushEffects()

    expect(replaceMessagesFromBackendMock).toHaveBeenCalledWith([
      expect.objectContaining({
        id: 'assistant-1',
        sessionId: 'session-1',
        toolCalls: [expect.objectContaining({ id: 'call-1', name: 'bash' })],
      }),
    ])

    ctx.dispose()
  })

  it('rehydrates a newly selected session even when another session run is still tracked', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))
    await flushEffects()

    h.setIsRunning(true)
    h.setTrackedRunSessionId('session-1')
    h.setRunId('desktop-run-session-1')
    h.rehydrateStatus.mockClear()
    h.resetState.mockClear()

    h.setSessionId('session-2')
    await flushEffects()

    expect(h.rehydrateStatus).toHaveBeenLastCalledWith('session-2')

    ctx.dispose()
  })

  it('ignores stale correlated interactive events from an older run', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))
    await flushEffects()

    h.setRunId('desktop-run-current')
    h.setTrackedRunSessionId('session-1')
    h.setIsRunning(true)

    h.emit({
      type: 'question_request',
      id: 'question-current',
      question: 'Continue?',
      options: ['Yes', 'No'],
      run_id: 'desktop-run-current',
    })
    await flushEffects()

    expect(ctx.agent.eventTimeline()).toContainEqual(
      expect.objectContaining({ type: 'question_request', id: 'question-current' })
    )

    h.emit({
      type: 'interactive_request_cleared',
      request_id: 'question-current',
      request_kind: 'question',
      timed_out: false,
      run_id: 'desktop-run-old',
    })
    await flushEffects()

    expect(ctx.agent.eventTimeline()).toContainEqual(
      expect.objectContaining({ type: 'question_request', id: 'question-current' })
    )

    ctx.dispose()
  })

  it('ignores uncorrelated interactive events while a web run is active', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))
    await flushEffects()

    h.setRunId('web-run-current')
    h.setTrackedRunSessionId('session-1')
    h.setIsRunning(true)

    h.emit({
      type: 'approval_request',
      id: 'approval-uncorrelated',
      tool_call_id: 'call-1',
      tool_name: 'bash',
      args: { command: 'pwd' },
      risk_level: 'low',
      reason: 'read cwd',
      warnings: [],
    })
    await flushEffects()

    h.emit({
      type: 'question_request',
      id: 'question-current',
      question: 'Continue?',
      options: ['Yes', 'No'],
      run_id: 'web-run-current',
    })
    await flushEffects()

    expect(ctx.agent.eventTimeline()).toContainEqual(
      expect.objectContaining({ type: 'question_request', id: 'question-current' })
    )

    ctx.dispose()
  })

  it('clears stale UI and rehydrates the newly selected session when switching sessions', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))
    await flushEffects()

    h.emit({ type: 'thinking', content: 'session-1 hidden thought' })
    h.setRunId('web-run-current')
    h.setTrackedRunSessionId('session-1')
    h.setIsRunning(true)
    await flushEffects()

    h.rehydrateStatus.mockClear()
    h.resetState.mockClear()
    h.setSessionId('session-2')
    await flushEffects()

    expect(h.rehydrateStatus).toHaveBeenCalledWith('session-2')
    expect(ctx.agent.isRunning()).toBe(false)
    expect(ctx.agent.currentRunId()).toBeNull()
    expect(ctx.agent.currentThought()).toBe('')

    ctx.dispose()
  })

  it('restores cached live runtime state immediately when switching back to a recent session', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))
    await flushEffects()

    h.setRunId('web-run-session-1')
    h.setTrackedRunSessionId('session-1')
    h.setIsRunning(true)
    h.emit({ type: 'thinking', content: 'session-1 hidden thought' })
    h.emit({
      type: 'question_request',
      id: 'question-session-1',
      question: 'Keep going?',
      options: ['Yes', 'No'],
      run_id: 'web-run-session-1',
    })
    await flushEffects()

    h.setSessionId('session-2')
    await flushEffects()

    expect(ctx.agent.currentThought()).toBe('')
    expect(ctx.agent.pendingQuestion()).toBeNull()

    h.rehydrateStatus.mockClear()
    replaceMessagesFromBackendMock.mockClear()
    testMocks.getMessagesMock.mockClear()
    testMocks.getMessagesMock.mockResolvedValueOnce([
      {
        id: 'assistant-stale',
        sessionId: 'session-1',
        role: 'assistant' as const,
        content: 'persisted stale output',
        createdAt: 2,
      },
    ])
    h.rehydrateStatus.mockImplementationOnce(async (sessionId?: string | null) => ({
      sessionId: sessionId ?? null,
      running: true,
      runId: 'web-run-session-1',
      pendingApproval: null,
      pendingQuestion: {
        type: 'question_request',
        id: 'question-session-1',
        question: 'Keep going?',
        options: ['Yes', 'No'],
        run_id: 'web-run-session-1',
      },
      pendingPlan: null,
    }))
    h.setSessionId('session-1')
    await flushEffects()

    expect(h.rehydrateStatus).toHaveBeenCalledWith('session-1')
    expect(ctx.agent.isRunning()).toBe(true)
    expect(ctx.agent.currentRunId()).toBe('web-run-session-1')
    expect(ctx.agent.currentThought()).toBe('session-1 hidden thought')
    expect(ctx.agent.pendingQuestion()).toMatchObject({
      id: 'question-session-1',
      question: 'Keep going?',
    })
    expect(testMocks.getMessagesMock).not.toHaveBeenCalled()
    expect(replaceMessagesFromBackendMock).not.toHaveBeenCalled()

    ctx.dispose()
  })

  it('clears stale cached runtime after authoritative rehydrate reports no active run', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))
    await flushEffects()

    h.setRunId('web-run-stale')
    h.setTrackedRunSessionId('session-1')
    h.setIsRunning(true)
    h.emit({ type: 'thinking', content: 'stale hidden thought' })
    h.emit({
      type: 'question_request',
      id: 'question-stale',
      question: 'Stale question?',
      options: ['Yes'],
      run_id: 'web-run-stale',
    })
    await flushEffects()

    h.setSessionId('session-2')
    await flushEffects()

    h.rehydrateStatus.mockClear()
    h.rehydrateStatus.mockImplementationOnce(async (sessionId?: string | null) => {
      h.setIsRunning(false)
      h.setRunId(null)
      h.setTrackedRunSessionId(null)
      return {
        sessionId: sessionId ?? null,
        running: false,
        runId: null,
        pendingApproval: null,
        pendingQuestion: null,
        pendingPlan: null,
      }
    })

    h.setSessionId('session-1')
    await flushEffects()

    expect(ctx.agent.isRunning()).toBe(false)
    expect(ctx.agent.currentRunId()).toBeNull()
    expect(ctx.agent.currentThought()).toBe('')
    expect(ctx.agent.pendingQuestion()).toBeNull()

    h.rehydrateStatus.mockClear()
    h.setSessionId('session-2')
    await flushEffects()
    h.setSessionId('session-1')
    await flushEffects()

    expect(ctx.agent.isRunning()).toBe(false)
    expect(ctx.agent.currentThought()).toBe('')
    expect(ctx.agent.pendingQuestion()).toBeNull()

    ctx.dispose()
  })

  it('replaces stale cached pending interactive state with authoritative rehydrate state', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))
    await flushEffects()

    h.setRunId('web-run-session-1')
    h.setTrackedRunSessionId('session-1')
    h.setIsRunning(true)
    h.emit({
      type: 'question_request',
      id: 'question-stale',
      question: 'Old question?',
      options: ['Yes'],
      run_id: 'web-run-session-1',
    })
    await flushEffects()

    h.setSessionId('session-2')
    await flushEffects()

    h.rehydrateStatus.mockClear()
    h.rehydrateStatus.mockImplementationOnce(async (sessionId?: string | null) => ({
      sessionId: sessionId ?? null,
      running: true,
      runId: 'web-run-session-1',
      pendingApproval: null,
      pendingQuestion: null,
      pendingPlan: null,
    }))

    h.setSessionId('session-1')
    await flushEffects()

    expect(ctx.agent.isRunning()).toBe(true)
    expect(ctx.agent.currentRunId()).toBe('web-run-session-1')
    expect(ctx.agent.pendingQuestion()).toBeNull()

    ctx.dispose()
  })

  it('keeps hidden run progression and completion out of the visible session', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))
    await flushEffects()

    h.setRunId('web-run-hidden')
    h.setTrackedRunSessionId('session-1')
    h.setIsRunning(true)
    h.setSessionId('session-2')
    await flushEffects()

    h.emit({ type: 'thinking', content: 'hidden session thought' })
    await flushEffects()

    expect(ctx.agent.isRunning()).toBe(false)
    expect(ctx.agent.currentRunId()).toBeNull()
    expect(ctx.agent.currentThought()).toBe('')
    expect(ctx.agent.eventTimeline()).toEqual([])

    ctx.dispose()
  })

  it('clears session-scoped UI state when the active session is cleared', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))
    await flushEffects()

    h.setSessionId(null)
    await flushEffects()

    expect(h.rehydrateStatus).toHaveBeenLastCalledWith(null)
    expect(ctx.agent.currentThought()).toBe('')
    expect(ctx.agent.currentRunId()).toBeNull()
    expect(ctx.agent.isRunning()).toBe(false)

    ctx.dispose()
  })

  it('recovers authoritative desktop output when a reattached session run finishes after switching back', async () => {
    isTauriRuntime = true
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))
    await flushEffects()

    recoverDetachedDesktopSessionIfNeededMock.mockClear()
    h.rehydrateStatus.mockClear()

    h.setRunId('desktop-run-a')
    h.setTrackedRunSessionId('session-1')
    h.setIsRunning(true)

    h.setSessionId('session-2')
    await flushEffects()

    h.setSessionId('session-1')
    await flushEffects()

    expect(h.rehydrateStatus).toHaveBeenLastCalledWith('session-1')
    expect(recoverDetachedDesktopSessionIfNeededMock).toHaveBeenCalledWith('session-2')

    h.setIsRunning(false)
    h.setRunId(null)
    h.setTrackedRunSessionId(null)
    await flushEffects()

    expect(recoverDetachedDesktopSessionIfNeededMock).toHaveBeenCalledWith('session-1')

    ctx.dispose()
  })

  it('ignores uncorrelated interactive events while a desktop run is active', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))
    await flushEffects()

    h.setRunId('desktop-run-current')
    h.setTrackedRunSessionId('session-1')
    h.setIsRunning(true)

    h.emit({
      type: 'approval_request',
      id: 'approval-uncorrelated',
      tool_call_id: 'call-1',
      tool_name: 'bash',
      args: { command: 'pwd' },
      risk_level: 'low',
      reason: 'read cwd',
      warnings: [],
    })
    await flushEffects()

    h.emit({
      type: 'approval_request',
      id: 'approval-current',
      tool_call_id: 'call-2',
      tool_name: 'bash',
      args: { command: 'pwd' },
      risk_level: 'low',
      reason: 'read cwd',
      warnings: [],
      run_id: 'desktop-run-current',
    })
    await flushEffects()

    expect(ctx.agent.eventTimeline()).toContainEqual(
      expect.objectContaining({ type: 'approval_request', id: 'approval-current' })
    )

    ctx.dispose()
  })
})
