import { beforeEach, describe, expect, it, vi } from 'vitest'

let isTauriRuntime = true
const markActiveSessionSyncedMock = vi.fn()
const registerBackendSessionIdMock = vi.fn()

vi.mock('@tauri-apps/api/core', () => ({
  isTauri: () => isTauriRuntime,
}))

vi.mock('../services/core-bridge', () => ({
  getCoreBudget: () => null,
  markActiveSessionSynced: (...args: unknown[]) => markActiveSessionSyncedMock(...args),
}))

vi.mock('../services/context-compaction', () => ({
  decodeCompactionModel: () => null,
}))

vi.mock('../services/db-web-fallback', () => ({
  registerBackendSessionId: (...args: unknown[]) => registerBackendSessionIdMock(...args),
}))

import { createAgentRun } from './useAgentRun'

describe('createAgentRun', () => {
  beforeEach(() => {
    isTauriRuntime = true
    markActiveSessionSyncedMock.mockReset()
    registerBackendSessionIdMock.mockReset()
  })

  it('persists a cancelled run into its originating session after a fast switch', async () => {
    let activeSessionId = 'session-a'
    let currentRunToken = 0
    let releaseRun!: (value: { success: boolean; turns: number; sessionId: string }) => void
    const addMessage = vi.fn()
    const addMessageToSession = vi.fn()
    const updateMessage = vi.fn()
    const updateMessageInSession = vi.fn()
    const deleteMessage = vi.fn()
    const deleteMessageInSession = vi.fn()

    const runModule = createAgentRun({
      rustAgent: {
        isRunning: () => false,
        run: vi.fn(
          () =>
            new Promise<{ success: boolean; turns: number; sessionId: string }>((resolve) => {
              releaseRun = resolve
            })
        ),
        error: () => 'Agent run cancelled by user',
        streamingContent: () => 'partial answer',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        activeToolCalls: () => [],
        tokenUsage: () => ({ output: 0, cost: 0 }),
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: activeSessionId, name: 'Session A' }),
        createNewSession: vi.fn(),
        addMessage,
        addMessageToSession,
        updateMessage,
        updateMessageInSession,
        deleteMessage,
        deleteMessageInSession,
        selectedModel: () => 'gpt-5.4',
        selectedProvider: () => 'openai',
        messages: () => [],
        renameSession: vi.fn(),
      },
      settingsRef: {
        settings: () => ({
          behavior: { sessionAutoTitle: false },
          generation: {
            reasoningEffort: 'off',
            autoCompact: true,
            compactionThreshold: 80,
            compactionModel: null,
          },
        }),
      },
      isPlanMode: () => false,
      setCurrentThought: vi.fn(),
      setDoomLoopDetected: vi.fn(),
      setToolActivity: vi.fn(),
      setStreamingTokenEstimate: vi.fn(),
      streamingStartedAt: () => null,
      setStreamingStartedAt: vi.fn(),
      messageQueue: () => [],
      setMessageQueue: vi.fn(),
      liveMessageId: () => 'asst-live',
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
        beginRun: () => ++currentRunToken,
        isCurrentRun: (token: number) => token === currentRunToken,
      },
    } as unknown as Parameters<typeof createAgentRun>[0])

    const runPromise = runModule.run('ship it')
    activeSessionId = 'session-b'
    releaseRun({ success: true, turns: 1, sessionId: 'session-a' })

    await expect(runPromise).resolves.toBeNull()
    expect(addMessage).toHaveBeenCalledTimes(2)
    expect(updateMessage).not.toHaveBeenCalled()
    expect(deleteMessage).not.toHaveBeenCalled()
    expect(updateMessageInSession).toHaveBeenCalledWith('session-a', 'asst-live', {
      content: 'partial answer',
      tokensUsed: 0,
      costUSD: undefined,
      toolCalls: [],
      metadata: {
        provider: 'openai',
        model: 'gpt-5.4',
        mode: 'code',
        elapsedMs: expect.any(Number),
        cancelled: true,
      },
    })
    expect(addMessageToSession).toHaveBeenCalledWith(
      expect.objectContaining({
        sessionId: 'session-a',
        role: 'assistant',
        metadata: { cancelled: true, system: true },
        error: expect.objectContaining({ type: 'cancelled' }),
      })
    )
    expect(deleteMessageInSession).not.toHaveBeenCalled()
    expect(markActiveSessionSyncedMock).not.toHaveBeenCalled()
  })

  it('persists a successful run into its originating session after a fast switch', async () => {
    let activeSessionId = 'session-a'
    let currentRunToken = 0
    let releaseRun!: (value: { success: boolean; turns: number; sessionId: string }) => void
    const addMessage = vi.fn()
    const updateMessageInSession = vi.fn()
    const updateMessage = vi.fn()
    const deleteMessage = vi.fn()
    const deleteMessageInSession = vi.fn()

    const runModule = createAgentRun({
      rustAgent: {
        isRunning: () => false,
        run: vi.fn(
          () =>
            new Promise<{ success: boolean; turns: number; sessionId: string }>((resolve) => {
              releaseRun = resolve
            })
        ),
        error: () => null,
        streamingContent: () => 'final answer',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        activeToolCalls: () => [],
        tokenUsage: () => ({ output: 0, cost: 0 }),
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: activeSessionId, name: 'Session A' }),
        createNewSession: vi.fn(),
        addMessage,
        updateMessage,
        updateMessageInSession,
        deleteMessage,
        deleteMessageInSession,
        selectedModel: () => 'gpt-5.4',
        selectedProvider: () => 'openai',
        messages: () => [],
        renameSession: vi.fn(),
      },
      settingsRef: {
        settings: () => ({
          behavior: { sessionAutoTitle: false },
          generation: {
            reasoningEffort: 'off',
            autoCompact: true,
            compactionThreshold: 80,
            compactionModel: null,
          },
        }),
      },
      isPlanMode: () => false,
      setCurrentThought: vi.fn(),
      setDoomLoopDetected: vi.fn(),
      setToolActivity: vi.fn(),
      setStreamingTokenEstimate: vi.fn(),
      streamingStartedAt: () => null,
      setStreamingStartedAt: vi.fn(),
      messageQueue: () => [],
      setMessageQueue: vi.fn(),
      liveMessageId: () => 'asst-live',
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
        beginRun: () => ++currentRunToken,
        isCurrentRun: (token: number) => token === currentRunToken,
      },
    } as unknown as Parameters<typeof createAgentRun>[0])

    const runPromise = runModule.run('ship it')
    activeSessionId = 'session-b'
    releaseRun({ success: true, turns: 1, sessionId: 'session-a' })

    await expect(runPromise).resolves.toEqual({ success: true, turns: 1, sessionId: 'session-a' })
    expect(addMessage).toHaveBeenCalledTimes(2)
    expect(updateMessage).not.toHaveBeenCalled()
    expect(deleteMessage).not.toHaveBeenCalled()
    expect(updateMessageInSession).toHaveBeenCalledWith('session-a', 'asst-live', {
      content: 'final answer',
      tokensUsed: 0,
      costUSD: undefined,
      toolCalls: [],
      metadata: {
        provider: 'openai',
        model: 'gpt-5.4',
        mode: 'code',
        elapsedMs: expect.any(Number),
      },
    })
    expect(deleteMessageInSession).not.toHaveBeenCalled()
    expect(markActiveSessionSyncedMock).not.toHaveBeenCalled()
  })

  it('hydrates backend-authored message ids after a successful web run', async () => {
    isTauriRuntime = false
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => [
        {
          id: 'backend-user-1',
          role: 'user',
          content: 'ship it',
          created_at: 1,
          metadata: {},
        },
      ],
    })
    vi.stubGlobal('fetch', fetchMock)

    const replaceMessagesFromBackend = vi.fn()
    const runModule = createAgentRun({
      rustAgent: {
        isRunning: () => false,
        run: vi.fn().mockResolvedValue({ success: true, turns: 1, sessionId: 'session-a' }),
        error: () => null,
        streamingContent: () => 'final answer',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        activeToolCalls: () => [],
        tokenUsage: () => ({ output: 0, cost: 0 }),
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: 'session-a', name: 'Session A' }),
        createNewSession: vi.fn(),
        addMessage: vi.fn(),
        updateMessage: vi.fn(),
        deleteMessage: vi.fn(),
        selectedModel: () => 'gpt-5.4',
        selectedProvider: () => 'openai',
        messages: () => [],
        renameSession: vi.fn(),
        replaceMessagesFromBackend,
      },
      settingsRef: {
        settings: () => ({
          behavior: { sessionAutoTitle: false },
          generation: {
            reasoningEffort: 'off',
            autoCompact: true,
            compactionThreshold: 80,
            compactionModel: null,
          },
        }),
      },
      isPlanMode: () => false,
      setCurrentThought: vi.fn(),
      setDoomLoopDetected: vi.fn(),
      setToolActivity: vi.fn(),
      setStreamingTokenEstimate: vi.fn(),
      streamingStartedAt: () => null,
      setStreamingStartedAt: vi.fn(),
      messageQueue: () => [],
      setMessageQueue: vi.fn(),
      liveMessageId: () => 'asst-live',
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
        beginRun: () => 1,
        isCurrentRun: () => true,
      },
    } as unknown as Parameters<typeof createAgentRun>[0])

    await expect(runModule.run('ship it')).resolves.toEqual({
      success: true,
      turns: 1,
      sessionId: 'session-a',
    })

    expect(fetchMock).toHaveBeenCalledWith('/api/sessions/session-a/messages')
    expect(replaceMessagesFromBackend).toHaveBeenCalledWith([
      expect.objectContaining({ id: 'backend-user-1', sessionId: 'session-a' }),
    ])
    expect(registerBackendSessionIdMock).toHaveBeenCalledWith('session-a', 'session-a')
  })

  it('forwards the selected provider and model into the desktop submit path', async () => {
    const runMock = vi.fn().mockResolvedValue({ success: true, turns: 1, sessionId: 'session-a' })

    const runModule = createAgentRun({
      rustAgent: {
        isRunning: () => false,
        run: runMock,
        error: () => null,
        streamingContent: () => 'final answer',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        activeToolCalls: () => [],
        tokenUsage: () => ({ output: 0, cost: 0 }),
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: 'session-a', name: 'Session A' }),
        createNewSession: vi.fn(),
        addMessage: vi.fn(),
        updateMessage: vi.fn(),
        deleteMessage: vi.fn(),
        selectedModel: () => 'gpt-5.4',
        selectedProvider: () => 'openai',
        messages: () => [],
        renameSession: vi.fn(),
      },
      settingsRef: {
        settings: () => ({
          behavior: { sessionAutoTitle: false },
          generation: {
            reasoningEffort: 'off',
            autoCompact: true,
            compactionThreshold: 80,
            compactionModel: null,
          },
        }),
      },
      isPlanMode: () => false,
      setCurrentThought: vi.fn(),
      setDoomLoopDetected: vi.fn(),
      setToolActivity: vi.fn(),
      setStreamingTokenEstimate: vi.fn(),
      streamingStartedAt: () => null,
      setStreamingStartedAt: vi.fn(),
      messageQueue: () => [],
      setMessageQueue: vi.fn(),
      liveMessageId: () => 'asst-live',
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
        beginRun: () => 1,
        isCurrentRun: () => true,
      },
    } as unknown as Parameters<typeof createAgentRun>[0])

    await expect(runModule.run('ship it')).resolves.toEqual({
      success: true,
      turns: 1,
      sessionId: 'session-a',
    })

    expect(runMock).toHaveBeenCalledWith(
      'ship it',
      expect.objectContaining({
        provider: 'openai',
        model: 'gpt-5.4',
        sessionId: 'session-a',
      })
    )
  })
})
