import { beforeEach, describe, expect, it, vi } from 'vitest'

let isTauriRuntime = true
const ensureActiveSessionSyncedMock = vi.fn()
const markActiveSessionSyncedMock = vi.fn()
const resolveApprovalBridgeMock = vi.fn()
const resolveQuestionBridgeMock = vi.fn()
const resolvePlanBridgeMock = vi.fn()

vi.mock('@tauri-apps/api/core', () => ({
  isTauri: () => isTauriRuntime,
}))

vi.mock('../services/core-bridge', () => ({
  ensureActiveSessionSynced: (...args: unknown[]) => ensureActiveSessionSyncedMock(...args),
  getCoreBudget: () => null,
  markActiveSessionSynced: (...args: unknown[]) => markActiveSessionSyncedMock(...args),
}))

vi.mock('../services/rust-bridge', () => ({
  rustAgent: {
    cancel: vi.fn().mockResolvedValue(undefined),
    resolveApproval: (...args: unknown[]) => resolveApprovalBridgeMock(...args),
    resolveQuestion: (...args: unknown[]) => resolveQuestionBridgeMock(...args),
    resolvePlan: (...args: unknown[]) => resolvePlanBridgeMock(...args),
  },
  rustBackend: { undoLastEdit: vi.fn().mockResolvedValue({ success: true, message: 'ok' }) },
}))

import { createAgentActions } from './useAgentActions'

function createDeferred<T>() {
  let resolve!: (value: T) => void
  let reject!: (reason?: unknown) => void
  const promise = new Promise<T>((resolvePromise, rejectPromise) => {
    resolve = resolvePromise
    reject = rejectPromise
  })
  return { promise, resolve, reject }
}

function createHarness() {
  const setMessageError = vi.fn()
  const rustAgent = {
    isRunning: () => false,
  }
  const session = {
    currentSession: () => ({ id: 'session-123' }),
    setMessageError,
  }

  const actions = createAgentActions({
    rustAgent,
    session,
    settingsRef: { settings: () => ({}) },
    isPlanMode: () => false,
    setIsPlanMode: vi.fn(),
    currentTurn: () => 0,
    tokensUsed: () => 0,
    currentThought: () => '',
    setCurrentThought: vi.fn(),
    toolActivity: () => [],
    setToolActivity: vi.fn(),
    pendingApproval: () => null,
    setPendingApproval: vi.fn(),
    pendingQuestion: () => null,
    setPendingQuestion: vi.fn(),
    pendingPlan: () => null,
    setPendingPlan: vi.fn(),
    doomLoopDetected: () => false,
    setDoomLoopDetected: vi.fn(),
    streamingTokenEstimate: () => 0,
    setStreamingTokenEstimate: vi.fn(),
    streamingStartedAt: () => null,
    setStreamingStartedAt: vi.fn(),
    messageQueue: () => [],
    setMessageQueue: vi.fn(),
    liveMessageId: () => null,
    setLiveMessageId: vi.fn(),
    streaming: {
      streamingContentOffset: () => 0,
      setStreamingContentOffset: vi.fn(),
      toolCallsOffset: () => 0,
      setToolCallsOffset: vi.fn(),
      thinkingSegmentsOffset: () => 0,
      setThinkingSegmentsOffset: vi.fn(),
    },
    runOwnership: {
      beginRun: (() => {
        let token = 0
        return () => ++token
      })(),
      isCurrentRun: () => true,
    },
  } as unknown as Parameters<typeof createAgentActions>[0])

  return { actions, setMessageError }
}

describe('createAgentActions session sync preflight', () => {
  beforeEach(() => {
    isTauriRuntime = true
    ensureActiveSessionSyncedMock.mockReset()
    markActiveSessionSyncedMock.mockReset()
    resolveApprovalBridgeMock.mockReset()
    resolveApprovalBridgeMock.mockResolvedValue(undefined)
    resolveQuestionBridgeMock.mockReset()
    resolveQuestionBridgeMock.mockResolvedValue(undefined)
    resolvePlanBridgeMock.mockReset()
    resolvePlanBridgeMock.mockResolvedValue(undefined)
  })

  it('blocks retry when the restored desktop session is missing in the backend', async () => {
    ensureActiveSessionSyncedMock.mockRejectedValueOnce(new Error('backend session missing'))
    const { actions, setMessageError } = createHarness()

    await actions.retryMessage('assistant-1')

    expect(ensureActiveSessionSyncedMock).toHaveBeenCalledWith('session-123')
    expect(setMessageError).toHaveBeenCalledWith(
      'assistant-1',
      expect.objectContaining({ message: 'backend session missing' })
    )
  })

  it('blocks edit-and-resend when the restored desktop session is missing in the backend', async () => {
    ensureActiveSessionSyncedMock.mockRejectedValueOnce(new Error('backend session missing'))
    const { actions, setMessageError } = createHarness()

    await actions.editAndResend('user-1', 'retry this')

    expect(setMessageError).toHaveBeenCalledWith(
      'user-1',
      expect.objectContaining({ message: 'backend session missing' })
    )
  })

  it('blocks regenerate when the restored desktop session is missing in the backend', async () => {
    ensureActiveSessionSyncedMock.mockRejectedValueOnce(new Error('backend session missing'))
    const { actions, setMessageError } = createHarness()

    await actions.regenerateResponse('assistant-2')

    expect(setMessageError).toHaveBeenCalledWith(
      'assistant-2',
      expect.objectContaining({ message: 'backend session missing' })
    )
  })

  it('waits for desktop session sync before retry continues on the happy path', async () => {
    ensureActiveSessionSyncedMock.mockResolvedValueOnce({
      sessionId: 'session-123',
      exists: true,
      messageCount: 2,
    })
    const retryRun = vi
      .fn()
      .mockResolvedValue({ success: true, turns: 1, sessionId: 'session-123' })
    const actions = createAgentActions({
      rustAgent: {
        isRunning: () => false,
        retryRun,
        error: () => null,
        streamingContent: () => 'final answer',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        tokenUsage: () => ({ output: 0, cost: 0 }),
        activeToolCalls: () => [],
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: 'session-123' }),
        setMessageError: vi.fn(),
        selectedModel: () => 'gpt-5.4',
        selectedProvider: () => 'openai',
        updateMessage: vi.fn(),
        deleteMessage: vi.fn(),
      },
      settingsRef: { settings: () => ({}) },
      isPlanMode: () => false,
      setIsPlanMode: vi.fn(),
      currentTurn: () => 0,
      tokensUsed: () => 0,
      currentThought: () => '',
      setCurrentThought: vi.fn(),
      toolActivity: () => [],
      setToolActivity: vi.fn(),
      pendingApproval: () => null,
      setPendingApproval: vi.fn(),
      pendingQuestion: () => null,
      setPendingQuestion: vi.fn(),
      pendingPlan: () => null,
      setPendingPlan: vi.fn(),
      doomLoopDetected: () => false,
      setDoomLoopDetected: vi.fn(),
      streamingTokenEstimate: () => 0,
      setStreamingTokenEstimate: vi.fn(),
      streamingStartedAt: () => null,
      setStreamingStartedAt: vi.fn(),
      messageQueue: () => [],
      setMessageQueue: vi.fn(),
      liveMessageId: () => null,
      setLiveMessageId: vi.fn(),
      streaming: {
        streamingContentOffset: () => 0,
        setStreamingContentOffset: vi.fn(),
        toolCallsOffset: () => 0,
        setToolCallsOffset: vi.fn(),
        thinkingSegmentsOffset: () => 0,
        setThinkingSegmentsOffset: vi.fn(),
      },
      runOwnership: {
        beginRun: (() => {
          let token = 0
          return () => ++token
        })(),
        isCurrentRun: () => true,
      },
    } as unknown as Parameters<typeof createAgentActions>[0])

    await actions.retryMessage('assistant-1')

    expect(ensureActiveSessionSyncedMock).toHaveBeenCalledWith('session-123')
    expect(retryRun).toHaveBeenCalledTimes(1)
    expect(retryRun).toHaveBeenCalledWith('session-123')
    expect(ensureActiveSessionSyncedMock.mock.invocationCallOrder[0]).toBeLessThan(
      retryRun.mock.invocationCallOrder[0]
    )
    expect(markActiveSessionSyncedMock).toHaveBeenCalledWith('session-123')
  })

  it('preserves tool-only retry completions instead of deleting the assistant message', async () => {
    ensureActiveSessionSyncedMock.mockResolvedValueOnce({
      sessionId: 'session-123',
      exists: true,
      messageCount: 2,
    })
    const updateMessage = vi.fn()
    const deleteMessage = vi.fn()
    const retryRun = vi
      .fn()
      .mockResolvedValue({ success: true, turns: 1, sessionId: 'session-123' })
    const actions = createAgentActions({
      rustAgent: {
        isRunning: () => false,
        retryRun,
        error: () => null,
        streamingContent: () => '',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        tokenUsage: () => ({ output: 0, cost: 0 }),
        activeToolCalls: () => [
          {
            id: 'tool-1',
            name: 'bash',
            args: { command: 'pwd' },
            status: 'success',
            startedAt: 10,
            completedAt: 20,
            output: '/workspace',
          },
        ],
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: 'session-123' }),
        setMessageError: vi.fn(),
        selectedModel: () => 'gpt-5.4',
        selectedProvider: () => 'openai',
        updateMessage,
        deleteMessage,
      },
      settingsRef: { settings: () => ({}) },
      isPlanMode: () => false,
      setIsPlanMode: vi.fn(),
      currentTurn: () => 0,
      tokensUsed: () => 0,
      currentThought: () => '',
      setCurrentThought: vi.fn(),
      toolActivity: () => [],
      setToolActivity: vi.fn(),
      pendingApproval: () => null,
      setPendingApproval: vi.fn(),
      pendingQuestion: () => null,
      setPendingQuestion: vi.fn(),
      pendingPlan: () => null,
      setPendingPlan: vi.fn(),
      doomLoopDetected: () => false,
      setDoomLoopDetected: vi.fn(),
      streamingTokenEstimate: () => 0,
      setStreamingTokenEstimate: vi.fn(),
      streamingStartedAt: () => null,
      setStreamingStartedAt: vi.fn(),
      messageQueue: () => [],
      setMessageQueue: vi.fn(),
      liveMessageId: () => 'assistant-1',
      setLiveMessageId: vi.fn(),
      streaming: {
        streamingContentOffset: () => 0,
        setStreamingContentOffset: vi.fn(),
        toolCallsOffset: () => 0,
        setToolCallsOffset: vi.fn(),
        thinkingSegmentsOffset: () => 0,
        setThinkingSegmentsOffset: vi.fn(),
      },
      runOwnership: {
        beginRun: (() => {
          let token = 0
          return () => ++token
        })(),
        isCurrentRun: () => true,
      },
    } as unknown as Parameters<typeof createAgentActions>[0])

    await actions.retryMessage('assistant-1')

    expect(updateMessage).toHaveBeenCalledWith('assistant-1', {
      content: '',
      tokensUsed: 0,
      costUSD: undefined,
      toolCalls: [expect.objectContaining({ id: 'tool-1', name: 'bash', output: '/workspace' })],
      metadata: {
        provider: 'openai',
        model: 'gpt-5.4',
        mode: 'code',
        elapsedMs: expect.any(Number),
      },
    })
    expect(deleteMessage).not.toHaveBeenCalled()
  })

  it('forwards current web session id on retry replays', async () => {
    isTauriRuntime = false
    const retryRun = vi
      .fn()
      .mockResolvedValue({ success: true, turns: 1, sessionId: 'session-123' })
    const actions = createAgentActions({
      rustAgent: {
        isRunning: () => false,
        retryRun,
        error: () => null,
        streamingContent: () => 'final answer',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        tokenUsage: () => ({ output: 0, cost: 0 }),
        activeToolCalls: () => [],
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: 'session-web' }),
        setMessageError: vi.fn(),
        selectedModel: () => 'gpt-5.4',
        selectedProvider: () => 'openai',
        updateMessage: vi.fn(),
        deleteMessage: vi.fn(),
      },
      settingsRef: { settings: () => ({}) },
      isPlanMode: () => false,
      setIsPlanMode: vi.fn(),
      currentTurn: () => 0,
      tokensUsed: () => 0,
      currentThought: () => '',
      setCurrentThought: vi.fn(),
      toolActivity: () => [],
      setToolActivity: vi.fn(),
      pendingApproval: () => null,
      setPendingApproval: vi.fn(),
      pendingQuestion: () => null,
      setPendingQuestion: vi.fn(),
      pendingPlan: () => null,
      setPendingPlan: vi.fn(),
      doomLoopDetected: () => false,
      setDoomLoopDetected: vi.fn(),
      streamingTokenEstimate: () => 0,
      setStreamingTokenEstimate: vi.fn(),
      streamingStartedAt: () => null,
      setStreamingStartedAt: vi.fn(),
      messageQueue: () => [],
      setMessageQueue: vi.fn(),
      liveMessageId: () => null,
      setLiveMessageId: vi.fn(),
      streaming: {
        streamingContentOffset: () => 0,
        setStreamingContentOffset: vi.fn(),
        toolCallsOffset: () => 0,
        setToolCallsOffset: vi.fn(),
        thinkingSegmentsOffset: () => 0,
        setThinkingSegmentsOffset: vi.fn(),
      },
      runOwnership: {
        beginRun: (() => {
          let token = 0
          return () => ++token
        })(),
        isCurrentRun: () => true,
      },
    } as unknown as Parameters<typeof createAgentActions>[0])

    await actions.retryMessage('assistant-1')

    expect(retryRun).toHaveBeenCalledTimes(1)
    expect(retryRun).toHaveBeenCalledWith('session-web')
  })

  it('allows retry in the visible session while another session runs off-screen', async () => {
    isTauriRuntime = false
    const retryRun = vi
      .fn()
      .mockResolvedValue({ success: true, turns: 1, sessionId: 'session-visible' })
    const actions = createAgentActions({
      rustAgent: {
        isRunning: () => true,
        trackedSessionId: () => 'session-hidden',
        currentRunId: () => 'run-hidden',
        retryRun,
        error: () => null,
        streamingContent: () => 'visible retry answer',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        tokenUsage: () => ({ output: 0, cost: 0 }),
        activeToolCalls: () => [],
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: 'session-visible' }),
        setMessageError: vi.fn(),
        selectedModel: () => 'gpt-5.4',
        selectedProvider: () => 'openai',
        updateMessage: vi.fn(),
        deleteMessage: vi.fn(),
      },
      settingsRef: { settings: () => ({}) },
      sessionHasActiveRun: () => false,
      isPlanMode: () => false,
      setIsPlanMode: vi.fn(),
      currentTurn: () => 0,
      tokensUsed: () => 0,
      currentThought: () => '',
      setCurrentThought: vi.fn(),
      toolActivity: () => [],
      setToolActivity: vi.fn(),
      pendingApproval: () => null,
      setPendingApproval: vi.fn(),
      pendingQuestion: () => null,
      setPendingQuestion: vi.fn(),
      pendingPlan: () => null,
      setPendingPlan: vi.fn(),
      doomLoopDetected: () => false,
      setDoomLoopDetected: vi.fn(),
      streamingTokenEstimate: () => 0,
      setStreamingTokenEstimate: vi.fn(),
      streamingStartedAt: () => null,
      setStreamingStartedAt: vi.fn(),
      messageQueue: () => [],
      setMessageQueue: vi.fn(),
      liveMessageId: () => null,
      setLiveMessageId: vi.fn(),
      streaming: {
        streamingContentOffset: () => 0,
        setStreamingContentOffset: vi.fn(),
        toolCallsOffset: () => 0,
        setToolCallsOffset: vi.fn(),
        thinkingSegmentsOffset: () => 0,
        setThinkingSegmentsOffset: vi.fn(),
      },
      runOwnership: {
        beginRun: (() => {
          let token = 0
          return () => ++token
        })(),
        isCurrentRun: () => true,
      },
    } as unknown as Parameters<typeof createAgentActions>[0])

    await actions.retryMessage('assistant-visible')

    expect(retryRun).toHaveBeenCalledTimes(1)
    expect(retryRun).toHaveBeenCalledWith('session-visible')
  })

  it('waits for desktop session sync before regenerate continues on the happy path', async () => {
    ensureActiveSessionSyncedMock.mockResolvedValueOnce({
      sessionId: 'session-123',
      exists: true,
      messageCount: 2,
    })
    const regenerateRun = vi
      .fn()
      .mockResolvedValue({ success: true, turns: 1, sessionId: 'session-123' })
    const actions = createAgentActions({
      rustAgent: {
        isRunning: () => false,
        regenerateRun,
        error: () => null,
        streamingContent: () => 'final answer',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        tokenUsage: () => ({ output: 0, cost: 0 }),
        activeToolCalls: () => [],
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: 'session-123' }),
        setMessageError: vi.fn(),
        selectedModel: () => 'gpt-5.4',
        selectedProvider: () => 'openai',
        updateMessage: vi.fn(),
        deleteMessage: vi.fn(),
      },
      settingsRef: { settings: () => ({}) },
      isPlanMode: () => false,
      setIsPlanMode: vi.fn(),
      currentTurn: () => 0,
      tokensUsed: () => 0,
      currentThought: () => '',
      setCurrentThought: vi.fn(),
      toolActivity: () => [],
      setToolActivity: vi.fn(),
      pendingApproval: () => null,
      setPendingApproval: vi.fn(),
      pendingQuestion: () => null,
      setPendingQuestion: vi.fn(),
      pendingPlan: () => null,
      setPendingPlan: vi.fn(),
      doomLoopDetected: () => false,
      setDoomLoopDetected: vi.fn(),
      streamingTokenEstimate: () => 0,
      setStreamingTokenEstimate: vi.fn(),
      streamingStartedAt: () => null,
      setStreamingStartedAt: vi.fn(),
      messageQueue: () => [],
      setMessageQueue: vi.fn(),
      liveMessageId: () => null,
      setLiveMessageId: vi.fn(),
      streaming: {
        streamingContentOffset: () => 0,
        setStreamingContentOffset: vi.fn(),
        toolCallsOffset: () => 0,
        setToolCallsOffset: vi.fn(),
        thinkingSegmentsOffset: () => 0,
        setThinkingSegmentsOffset: vi.fn(),
      },
      runOwnership: {
        beginRun: (() => {
          let token = 0
          return () => ++token
        })(),
        isCurrentRun: () => true,
      },
    } as unknown as Parameters<typeof createAgentActions>[0])

    await actions.regenerateResponse('assistant-2')

    expect(ensureActiveSessionSyncedMock).toHaveBeenCalledWith('session-123')
    expect(regenerateRun).toHaveBeenCalledTimes(1)
    expect(regenerateRun).toHaveBeenCalledWith('session-123')
    expect(ensureActiveSessionSyncedMock.mock.invocationCallOrder[0]).toBeLessThan(
      regenerateRun.mock.invocationCallOrder[0]
    )
    expect(markActiveSessionSyncedMock).toHaveBeenCalledWith('session-123')
  })

  it('forwards current web session id on regenerate replays', async () => {
    isTauriRuntime = false
    const regenerateRun = vi
      .fn()
      .mockResolvedValue({ success: true, turns: 1, sessionId: 'session-web' })
    const actions = createAgentActions({
      rustAgent: {
        isRunning: () => false,
        regenerateRun,
        error: () => null,
        streamingContent: () => 'final answer',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        tokenUsage: () => ({ output: 0, cost: 0 }),
        activeToolCalls: () => [],
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: 'session-web' }),
        setMessageError: vi.fn(),
        selectedModel: () => 'gpt-5.4',
        selectedProvider: () => 'openai',
        updateMessage: vi.fn(),
        deleteMessage: vi.fn(),
      },
      settingsRef: { settings: () => ({}) },
      isPlanMode: () => false,
      setIsPlanMode: vi.fn(),
      currentTurn: () => 0,
      tokensUsed: () => 0,
      currentThought: () => '',
      setCurrentThought: vi.fn(),
      toolActivity: () => [],
      setToolActivity: vi.fn(),
      pendingApproval: () => null,
      setPendingApproval: vi.fn(),
      pendingQuestion: () => null,
      setPendingQuestion: vi.fn(),
      pendingPlan: () => null,
      setPendingPlan: vi.fn(),
      doomLoopDetected: () => false,
      setDoomLoopDetected: vi.fn(),
      streamingTokenEstimate: () => 0,
      setStreamingTokenEstimate: vi.fn(),
      streamingStartedAt: () => null,
      setStreamingStartedAt: vi.fn(),
      messageQueue: () => [],
      setMessageQueue: vi.fn(),
      liveMessageId: () => null,
      setLiveMessageId: vi.fn(),
      streaming: {
        streamingContentOffset: () => 0,
        setStreamingContentOffset: vi.fn(),
        toolCallsOffset: () => 0,
        setToolCallsOffset: vi.fn(),
        thinkingSegmentsOffset: () => 0,
        setThinkingSegmentsOffset: vi.fn(),
      },
      runOwnership: {
        beginRun: (() => {
          let token = 0
          return () => ++token
        })(),
        isCurrentRun: () => true,
      },
    } as unknown as Parameters<typeof createAgentActions>[0])

    await actions.regenerateResponse('assistant-2')

    expect(regenerateRun).toHaveBeenCalledTimes(1)
    expect(regenerateRun).toHaveBeenCalledWith('session-web')
  })

  it('abandons retry after preflight if the initiating session is no longer current', async () => {
    const syncDeferred = createDeferred<{
      sessionId: string
      exists: boolean
      messageCount: number
    }>()
    ensureActiveSessionSyncedMock.mockImplementationOnce(() => syncDeferred.promise)

    let activeSessionId = 'session-123'
    const retryRun = vi.fn()
    const updateMessage = vi.fn()
    const deleteMessage = vi.fn()
    const actions = createAgentActions({
      rustAgent: {
        isRunning: () => false,
        retryRun,
      },
      session: {
        currentSession: () => ({ id: activeSessionId }),
        setMessageError: vi.fn(),
        updateMessage,
        deleteMessage,
      },
      settingsRef: { settings: () => ({}) },
      isPlanMode: () => false,
      setIsPlanMode: vi.fn(),
      currentTurn: () => 0,
      tokensUsed: () => 0,
      currentThought: () => '',
      setCurrentThought: vi.fn(),
      toolActivity: () => [],
      setToolActivity: vi.fn(),
      pendingApproval: () => null,
      setPendingApproval: vi.fn(),
      pendingQuestion: () => null,
      setPendingQuestion: vi.fn(),
      pendingPlan: () => null,
      setPendingPlan: vi.fn(),
      doomLoopDetected: () => false,
      setDoomLoopDetected: vi.fn(),
      streamingTokenEstimate: () => 0,
      setStreamingTokenEstimate: vi.fn(),
      streamingStartedAt: () => null,
      setStreamingStartedAt: vi.fn(),
      messageQueue: () => [],
      setMessageQueue: vi.fn(),
      liveMessageId: () => null,
      setLiveMessageId: vi.fn(),
      streaming: {
        streamingContentOffset: () => 0,
        setStreamingContentOffset: vi.fn(),
        toolCallsOffset: () => 0,
        setToolCallsOffset: vi.fn(),
        thinkingSegmentsOffset: () => 0,
        setThinkingSegmentsOffset: vi.fn(),
      },
      runOwnership: {
        beginRun: (() => {
          let token = 0
          return () => ++token
        })(),
        isCurrentRun: () => true,
      },
    } as unknown as Parameters<typeof createAgentActions>[0])

    const retryPromise = actions.retryMessage('assistant-1')
    activeSessionId = 'session-456'
    syncDeferred.resolve({ sessionId: 'session-123', exists: true, messageCount: 2 })

    await retryPromise

    expect(retryRun).not.toHaveBeenCalled()
    expect(updateMessage).not.toHaveBeenCalled()
    expect(deleteMessage).not.toHaveBeenCalled()
  })

  it('abandons edit-and-resend after preflight if the initiating session is no longer current', async () => {
    const syncDeferred = createDeferred<{
      sessionId: string
      exists: boolean
      messageCount: number
    }>()
    ensureActiveSessionSyncedMock.mockImplementationOnce(() => syncDeferred.promise)

    let activeSessionId = 'session-123'
    const editAndResendRun = vi.fn()
    const deleteMessagesAfter = vi.fn()
    const deleteMessage = vi.fn()
    const stopEditing = vi.fn()
    const actions = createAgentActions({
      rustAgent: {
        isRunning: () => false,
        editAndResendRun,
      },
      session: {
        currentSession: () => ({ id: activeSessionId }),
        setMessageError: vi.fn(),
        deleteMessagesAfter,
        deleteMessage,
        stopEditing,
      },
      settingsRef: { settings: () => ({}) },
      isPlanMode: () => false,
      setIsPlanMode: vi.fn(),
      currentTurn: () => 0,
      tokensUsed: () => 0,
      currentThought: () => '',
      setCurrentThought: vi.fn(),
      toolActivity: () => [],
      setToolActivity: vi.fn(),
      pendingApproval: () => null,
      setPendingApproval: vi.fn(),
      pendingQuestion: () => null,
      setPendingQuestion: vi.fn(),
      pendingPlan: () => null,
      setPendingPlan: vi.fn(),
      doomLoopDetected: () => false,
      setDoomLoopDetected: vi.fn(),
      streamingTokenEstimate: () => 0,
      setStreamingTokenEstimate: vi.fn(),
      streamingStartedAt: () => null,
      setStreamingStartedAt: vi.fn(),
      messageQueue: () => [],
      setMessageQueue: vi.fn(),
      liveMessageId: () => null,
      setLiveMessageId: vi.fn(),
      streaming: {
        streamingContentOffset: () => 0,
        setStreamingContentOffset: vi.fn(),
        toolCallsOffset: () => 0,
        setToolCallsOffset: vi.fn(),
        thinkingSegmentsOffset: () => 0,
        setThinkingSegmentsOffset: vi.fn(),
      },
      runOwnership: {
        beginRun: (() => {
          let token = 0
          return () => ++token
        })(),
        isCurrentRun: () => true,
      },
    } as unknown as Parameters<typeof createAgentActions>[0])

    const editPromise = actions.editAndResend('user-1', 'retry this')
    activeSessionId = 'session-456'
    syncDeferred.resolve({ sessionId: 'session-123', exists: true, messageCount: 2 })

    await editPromise

    expect(editAndResendRun).not.toHaveBeenCalled()
    expect(deleteMessagesAfter).not.toHaveBeenCalled()
    expect(deleteMessage).not.toHaveBeenCalled()
    expect(stopEditing).not.toHaveBeenCalled()
  })

  it('restores browser messages when edit-and-resend is rejected by the backend', async () => {
    isTauriRuntime = false
    const originalMessages = [
      {
        id: 'user-1',
        sessionId: 'session-123',
        role: 'user' as const,
        content: 'original',
        createdAt: 1,
      },
      {
        id: 'assistant-1',
        sessionId: 'session-123',
        role: 'assistant' as const,
        content: 'reply',
        createdAt: 2,
      },
    ]
    const setMessages = vi.fn()
    const setMessageError = vi.fn()
    const editAndResendRun = vi
      .fn()
      .mockRejectedValue(new Error('Invalid message ID for edit-resend'))
    const actions = createAgentActions({
      rustAgent: {
        isRunning: () => false,
        editAndResendRun,
        error: () => null,
        streamingContent: () => '',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        tokenUsage: () => ({ output: 0, cost: 0 }),
        activeToolCalls: () => [],
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: 'session-123' }),
        messages: () => originalMessages,
        setMessages,
        setMessageError,
        deleteMessagesAfter: vi.fn(),
        deleteMessage: vi.fn(),
        stopEditing: vi.fn(),
        addMessage: vi.fn(),
        selectedModel: () => 'gpt-5.4',
        selectedProvider: () => 'openai',
        updateMessage: vi.fn(),
      },
      settingsRef: { settings: () => ({}) },
      isPlanMode: () => false,
      setIsPlanMode: vi.fn(),
      currentTurn: () => 0,
      tokensUsed: () => 0,
      currentThought: () => '',
      setCurrentThought: vi.fn(),
      toolActivity: () => [],
      setToolActivity: vi.fn(),
      pendingApproval: () => null,
      setPendingApproval: vi.fn(),
      pendingQuestion: () => null,
      setPendingQuestion: vi.fn(),
      pendingPlan: () => null,
      setPendingPlan: vi.fn(),
      doomLoopDetected: () => false,
      setDoomLoopDetected: vi.fn(),
      streamingTokenEstimate: () => 0,
      setStreamingTokenEstimate: vi.fn(),
      streamingStartedAt: () => null,
      setStreamingStartedAt: vi.fn(),
      messageQueue: () => [],
      setMessageQueue: vi.fn(),
      liveMessageId: () => null,
      setLiveMessageId: vi.fn(),
      streaming: {
        streamingContentOffset: () => 0,
        setStreamingContentOffset: vi.fn(),
        toolCallsOffset: () => 0,
        setToolCallsOffset: vi.fn(),
        thinkingSegmentsOffset: () => 0,
        setThinkingSegmentsOffset: vi.fn(),
      },
      runOwnership: {
        beginRun: (() => {
          let token = 0
          return () => ++token
        })(),
        isCurrentRun: () => true,
      },
    } as unknown as Parameters<typeof createAgentActions>[0])

    await actions.editAndResend('user-1', 'retry this')

    expect(editAndResendRun).toHaveBeenCalledWith('user-1', 'retry this', 'session-123')
    expect(setMessages).toHaveBeenCalledWith(originalMessages)
    expect(setMessageError).toHaveBeenCalledWith(
      'user-1',
      expect.objectContaining({ message: 'Invalid message ID for edit-resend' })
    )
  })

  it('abandons regenerate after preflight if the initiating session is no longer current', async () => {
    const syncDeferred = createDeferred<{
      sessionId: string
      exists: boolean
      messageCount: number
    }>()
    ensureActiveSessionSyncedMock.mockImplementationOnce(() => syncDeferred.promise)

    let activeSessionId = 'session-123'
    const regenerateRun = vi.fn()
    const updateMessage = vi.fn()
    const deleteMessage = vi.fn()
    const actions = createAgentActions({
      rustAgent: {
        isRunning: () => false,
        regenerateRun,
      },
      session: {
        currentSession: () => ({ id: activeSessionId }),
        setMessageError: vi.fn(),
        updateMessage,
        deleteMessage,
      },
      settingsRef: { settings: () => ({}) },
      isPlanMode: () => false,
      setIsPlanMode: vi.fn(),
      currentTurn: () => 0,
      tokensUsed: () => 0,
      currentThought: () => '',
      setCurrentThought: vi.fn(),
      toolActivity: () => [],
      setToolActivity: vi.fn(),
      pendingApproval: () => null,
      setPendingApproval: vi.fn(),
      pendingQuestion: () => null,
      setPendingQuestion: vi.fn(),
      pendingPlan: () => null,
      setPendingPlan: vi.fn(),
      doomLoopDetected: () => false,
      setDoomLoopDetected: vi.fn(),
      streamingTokenEstimate: () => 0,
      setStreamingTokenEstimate: vi.fn(),
      streamingStartedAt: () => null,
      setStreamingStartedAt: vi.fn(),
      messageQueue: () => [],
      setMessageQueue: vi.fn(),
      liveMessageId: () => null,
      setLiveMessageId: vi.fn(),
      streaming: {
        streamingContentOffset: () => 0,
        setStreamingContentOffset: vi.fn(),
        toolCallsOffset: () => 0,
        setToolCallsOffset: vi.fn(),
        thinkingSegmentsOffset: () => 0,
        setThinkingSegmentsOffset: vi.fn(),
      },
      runOwnership: {
        beginRun: (() => {
          let token = 0
          return () => ++token
        })(),
        isCurrentRun: () => true,
      },
    } as unknown as Parameters<typeof createAgentActions>[0])

    const regeneratePromise = actions.regenerateResponse('assistant-2')
    activeSessionId = 'session-456'
    syncDeferred.resolve({ sessionId: 'session-123', exists: true, messageCount: 2 })

    await regeneratePromise

    expect(regenerateRun).not.toHaveBeenCalled()
    expect(updateMessage).not.toHaveBeenCalled()
    expect(deleteMessage).not.toHaveBeenCalled()
  })

  it.each([
    {
      label: 'approval',
      setup: () => {
        const deferred = createDeferred<void>()
        resolveApprovalBridgeMock.mockReturnValueOnce(deferred.promise)
        const current = {
          id: 'approval-1',
          toolCallId: 'tool-1',
          type: 'command' as const,
          toolName: 'bash',
          args: { command: 'pwd' },
          description: 'Need approval',
          riskLevel: 'medium' as const,
          resolve: () => {},
        }
        let pending: typeof current | null = current
        const setPendingApproval = vi.fn((value: typeof current | null) => {
          pending = value
          return pending
        })
        const markToolApproval = vi.fn()
        const actions = createAgentActions({
          rustAgent: {
            isRunning: () => false,
            markToolApproval,
          },
          session: {
            currentSession: () => ({ id: 'session-123' }),
          },
          settingsRef: { settings: () => ({}) },
          isPlanMode: () => false,
          setIsPlanMode: vi.fn(),
          currentTurn: () => 0,
          tokensUsed: () => 0,
          currentThought: () => '',
          setCurrentThought: vi.fn(),
          toolActivity: () => [],
          setToolActivity: vi.fn(),
          pendingApproval: () => pending,
          setPendingApproval,
          pendingQuestion: () => null,
          setPendingQuestion: vi.fn(),
          pendingPlan: () => null,
          setPendingPlan: vi.fn(),
          doomLoopDetected: () => false,
          setDoomLoopDetected: vi.fn(),
          streamingTokenEstimate: () => 0,
          setStreamingTokenEstimate: vi.fn(),
          streamingStartedAt: () => null,
          setStreamingStartedAt: vi.fn(),
          messageQueue: () => [],
          setMessageQueue: vi.fn(),
          liveMessageId: () => null,
          setLiveMessageId: vi.fn(),
          streaming: {
            streamingContentOffset: () => 0,
            setStreamingContentOffset: vi.fn(),
            toolCallsOffset: () => 0,
            setToolCallsOffset: vi.fn(),
            thinkingSegmentsOffset: () => 0,
            setThinkingSegmentsOffset: vi.fn(),
          },
          runOwnership: {
            beginRun: (() => {
              let token = 0
              return () => ++token
            })(),
            isCurrentRun: () => true,
          },
        } as unknown as Parameters<typeof createAgentActions>[0])

        const actPromise = actions.resolveApproval(true)

        return {
          actPromise,
          reject: () => deferred.reject(new Error('approval failed')),
          pending: () => pending,
          setPending: setPendingApproval,
          verify: (): void => {
            expect(markToolApproval).not.toHaveBeenCalled()
          },
        }
      },
    },
    {
      label: 'question',
      setup: () => {
        const deferred = createDeferred<void>()
        resolveQuestionBridgeMock.mockReturnValueOnce(deferred.promise)
        const current = {
          id: 'question-1',
          question: 'Ship now?',
          options: ['yes', 'no'],
        }
        let pending: typeof current | null = current
        const setPendingQuestion = vi.fn((value: typeof current | null) => {
          pending = value
          return pending
        })
        const actions = createAgentActions({
          rustAgent: {
            isRunning: () => false,
          },
          session: {
            currentSession: () => ({ id: 'session-123' }),
          },
          settingsRef: { settings: () => ({}) },
          isPlanMode: () => false,
          setIsPlanMode: vi.fn(),
          currentTurn: () => 0,
          tokensUsed: () => 0,
          currentThought: () => '',
          setCurrentThought: vi.fn(),
          toolActivity: () => [],
          setToolActivity: vi.fn(),
          pendingApproval: () => null,
          setPendingApproval: vi.fn(),
          pendingQuestion: () => pending,
          setPendingQuestion,
          pendingPlan: () => null,
          setPendingPlan: vi.fn(),
          doomLoopDetected: () => false,
          setDoomLoopDetected: vi.fn(),
          streamingTokenEstimate: () => 0,
          setStreamingTokenEstimate: vi.fn(),
          streamingStartedAt: () => null,
          setStreamingStartedAt: vi.fn(),
          messageQueue: () => [],
          setMessageQueue: vi.fn(),
          liveMessageId: () => null,
          setLiveMessageId: vi.fn(),
          streaming: {
            streamingContentOffset: () => 0,
            setStreamingContentOffset: vi.fn(),
            toolCallsOffset: () => 0,
            setToolCallsOffset: vi.fn(),
            thinkingSegmentsOffset: () => 0,
            setThinkingSegmentsOffset: vi.fn(),
          },
          runOwnership: {
            beginRun: (() => {
              let token = 0
              return () => ++token
            })(),
            isCurrentRun: () => true,
          },
        } as unknown as Parameters<typeof createAgentActions>[0])

        const actPromise = actions.resolveQuestion('yes')

        return {
          actPromise,
          reject: () => deferred.reject(new Error('question failed')),
          pending: () => pending,
          setPending: setPendingQuestion,
          verify: (): void => undefined,
        }
      },
    },
    {
      label: 'plan',
      setup: () => {
        const deferred = createDeferred<void>()
        resolvePlanBridgeMock.mockReturnValueOnce(deferred.promise)
        const current = {
          requestId: 'plan-1',
          summary: 'Ship polish',
          steps: [],
          estimatedTurns: 1,
        }
        let pending: typeof current | null = current
        const setPendingPlan = vi.fn((value: typeof current | null) => {
          pending = value
          return pending
        })
        const actions = createAgentActions({
          rustAgent: {
            isRunning: () => false,
          },
          session: {
            currentSession: () => ({ id: 'session-123' }),
          },
          settingsRef: { settings: () => ({}) },
          isPlanMode: () => false,
          setIsPlanMode: vi.fn(),
          currentTurn: () => 0,
          tokensUsed: () => 0,
          currentThought: () => '',
          setCurrentThought: vi.fn(),
          toolActivity: () => [],
          setToolActivity: vi.fn(),
          pendingApproval: () => null,
          setPendingApproval: vi.fn(),
          pendingQuestion: () => null,
          setPendingQuestion: vi.fn(),
          pendingPlan: () => pending,
          setPendingPlan,
          doomLoopDetected: () => false,
          setDoomLoopDetected: vi.fn(),
          streamingTokenEstimate: () => 0,
          setStreamingTokenEstimate: vi.fn(),
          streamingStartedAt: () => null,
          setStreamingStartedAt: vi.fn(),
          messageQueue: () => [],
          setMessageQueue: vi.fn(),
          liveMessageId: () => null,
          setLiveMessageId: vi.fn(),
          streaming: {
            streamingContentOffset: () => 0,
            setStreamingContentOffset: vi.fn(),
            toolCallsOffset: () => 0,
            setToolCallsOffset: vi.fn(),
            thinkingSegmentsOffset: () => 0,
            setThinkingSegmentsOffset: vi.fn(),
          },
          runOwnership: {
            beginRun: (() => {
              let token = 0
              return () => ++token
            })(),
            isCurrentRun: () => true,
          },
        } as unknown as Parameters<typeof createAgentActions>[0])

        const actPromise = actions.resolvePlan('approved', current)

        return {
          actPromise,
          reject: () => deferred.reject(new Error('plan failed')),
          pending: () => pending,
          setPending: setPendingPlan,
          verify: (): void => undefined,
        }
      },
    },
  ])('keeps pending $label UI visible when resolve IPC rejects', async ({ setup }) => {
    const scenario = setup()

    // Act starts the promise but we don't await it yet
    expect(scenario.pending()).not.toBeNull()
    expect(scenario.setPending).not.toHaveBeenCalledWith(null)

    // Reject the IPC call
    scenario.reject()

    // Now await and catch the rejection to prevent unhandled rejection
    await scenario.actPromise.catch(() => {
      /* expected rejection, test continues */
    })

    expect(scenario.pending()).not.toBeNull()
    expect(scenario.setPending).not.toHaveBeenCalledWith(null)
    scenario.verify()
  })

  it('rejects when the pending plan is missing requestId', async () => {
    const current = {
      summary: 'Ship polish',
      steps: [],
      estimatedTurns: 1,
    }
    let pending: typeof current | null = current
    const setPendingPlan = vi.fn((value: typeof current | null) => {
      pending = value
      return pending
    })
    const actions = createAgentActions({
      rustAgent: {
        isRunning: () => false,
      },
      session: {
        currentSession: () => ({ id: 'session-123' }),
      },
      settingsRef: { settings: () => ({}) },
      isPlanMode: () => false,
      setIsPlanMode: vi.fn(),
      currentTurn: () => 0,
      tokensUsed: () => 0,
      currentThought: () => '',
      setCurrentThought: vi.fn(),
      toolActivity: () => [],
      setToolActivity: vi.fn(),
      pendingApproval: () => null,
      setPendingApproval: vi.fn(),
      pendingQuestion: () => null,
      setPendingQuestion: vi.fn(),
      pendingPlan: () => pending,
      setPendingPlan,
      doomLoopDetected: () => false,
      setDoomLoopDetected: vi.fn(),
      streamingTokenEstimate: () => 0,
      setStreamingTokenEstimate: vi.fn(),
      streamingStartedAt: () => null,
      setStreamingStartedAt: vi.fn(),
      messageQueue: () => [],
      setMessageQueue: vi.fn(),
      liveMessageId: () => null,
      setLiveMessageId: vi.fn(),
      streaming: {
        streamingContentOffset: () => 0,
        setStreamingContentOffset: vi.fn(),
        toolCallsOffset: () => 0,
        setToolCallsOffset: vi.fn(),
        thinkingSegmentsOffset: () => 0,
        setThinkingSegmentsOffset: vi.fn(),
      },
      runOwnership: {
        beginRun: (() => {
          let token = 0
          return () => ++token
        })(),
        isCurrentRun: () => true,
      },
    } as unknown as Parameters<typeof createAgentActions>[0])

    // Should reject since there's no requestId
    await expect(actions.resolvePlan('approved', current)).rejects.toThrow(
      'Cannot resolve plan without requestId'
    )

    expect(resolvePlanBridgeMock).not.toHaveBeenCalled()
    expect(setPendingPlan).not.toHaveBeenCalledWith(null)
  })

  it('syncs messages from backend and marks session synced after successful web retry', async () => {
    isTauriRuntime = false
    markActiveSessionSyncedMock.mockReset()
    const replaceMessagesFromBackend = vi.fn()
    const updateMessage = vi.fn()

    // Mock successful backend response
    const mockFetch = vi.fn().mockResolvedValue({
      ok: true,
      json: vi.fn().mockResolvedValue([
        {
          id: 'backend-msg-1',
          role: 'user',
          content: 'Hello',
          created_at: 1000,
          tokens_used: 10,
          cost_usd: 0.001,
          model: 'gpt-5.4',
          metadata: null,
        },
        {
          id: 'backend-msg-2',
          role: 'assistant',
          content: 'Hi there',
          created_at: 2000,
          tokens_used: 20,
          cost_usd: 0.002,
          model: 'gpt-5.4',
          metadata: JSON.stringify({ thinking: 'test' }),
        },
      ]),
    })
    global.fetch = mockFetch

    const retryRun = vi
      .fn()
      .mockResolvedValue({ success: true, turns: 1, sessionId: 'backend-session-web' })

    const actions = createAgentActions({
      rustAgent: {
        isRunning: () => false,
        retryRun,
        error: () => null,
        streamingContent: () => 'retry answer',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        tokenUsage: () => ({ output: 50, cost: 0.005 }),
        activeToolCalls: () => [],
        endRun: vi.fn(),
        detachedSessionId: () => null,
        clearDetachedSessionId: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: 'session-web' }),
        setMessageError: vi.fn(),
        selectedModel: () => 'gpt-5.4',
        selectedProvider: () => 'openai',
        updateMessage,
        deleteMessage: vi.fn(),
        replaceMessagesFromBackend,
      },
      settingsRef: { settings: () => ({}) },
      isPlanMode: () => false,
      setIsPlanMode: vi.fn(),
      currentTurn: () => 0,
      tokensUsed: () => 0,
      currentThought: () => '',
      setCurrentThought: vi.fn(),
      toolActivity: () => [],
      setToolActivity: vi.fn(),
      pendingApproval: () => null,
      setPendingApproval: vi.fn(),
      pendingQuestion: () => null,
      setPendingQuestion: vi.fn(),
      pendingPlan: () => null,
      setPendingPlan: vi.fn(),
      doomLoopDetected: () => false,
      setDoomLoopDetected: vi.fn(),
      streamingTokenEstimate: () => 0,
      setStreamingTokenEstimate: vi.fn(),
      streamingStartedAt: () => null,
      setStreamingStartedAt: vi.fn(),
      messageQueue: () => [],
      setMessageQueue: vi.fn(),
      liveMessageId: () => 'assistant-1',
      setLiveMessageId: vi.fn(),
      streaming: {
        streamingContentOffset: () => 0,
        setStreamingContentOffset: vi.fn(),
        toolCallsOffset: () => 0,
        setToolCallsOffset: vi.fn(),
        thinkingSegmentsOffset: () => 0,
        setThinkingSegmentsOffset: vi.fn(),
      },
      runOwnership: {
        beginRun: (() => {
          let token = 0
          return () => ++token
        })(),
        isCurrentRun: () => true,
      },
    } as unknown as Parameters<typeof createAgentActions>[0])

    await actions.retryMessage('assistant-1')

    // Verify backend sync was called
    expect(mockFetch).toHaveBeenCalledWith(
      expect.stringContaining('/api/sessions/backend-session-web/messages')
    )

    // Verify messages were replaced from backend
    expect(replaceMessagesFromBackend).toHaveBeenCalledWith(
      expect.arrayContaining([
        expect.objectContaining({
          id: 'backend-msg-1',
          sessionId: 'session-web',
          role: 'user',
          content: 'Hello',
        }),
        expect.objectContaining({
          id: 'backend-msg-2',
          sessionId: 'session-web',
          role: 'assistant',
          content: 'Hi there',
          metadata: expect.objectContaining({
            provider: 'openai',
            model: 'gpt-5.4',
            mode: 'code',
          }),
          tokensUsed: 50,
        }),
      ])
    )

    // Verify active session was marked as synced (called by settlement service)
    expect(markActiveSessionSyncedMock).toHaveBeenCalledWith('session-web', 2)

    // Restore fetch
    mockFetch.mockRestore()
  })
})
