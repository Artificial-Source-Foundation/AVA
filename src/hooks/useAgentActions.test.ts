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

async function flushMicrotasks(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
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

        return {
          act: () => actions.resolveApproval(true),
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

        return {
          act: () => actions.resolveQuestion('yes'),
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

        return {
          act: () => actions.resolvePlan('approved', current),
          reject: () => deferred.reject(new Error('plan failed')),
          pending: () => pending,
          setPending: setPendingPlan,
          verify: (): void => undefined,
        }
      },
    },
  ])('keeps pending $label UI visible when resolve IPC rejects', async ({ setup }) => {
    const scenario = setup()

    scenario.act()

    expect(scenario.pending()).not.toBeNull()
    expect(scenario.setPending).not.toHaveBeenCalledWith(null)

    scenario.reject()
    await flushMicrotasks()

    expect(scenario.pending()).not.toBeNull()
    expect(scenario.setPending).not.toHaveBeenCalledWith(null)
    scenario.verify()
  })

  it('does not call resolvePlan when the pending plan is missing requestId', () => {
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

    actions.resolvePlan('approved', current)

    expect(resolvePlanBridgeMock).not.toHaveBeenCalled()
    expect(setPendingPlan).not.toHaveBeenCalledWith(null)
  })
})
