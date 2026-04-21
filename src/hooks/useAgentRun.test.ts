import { beforeEach, describe, expect, it, vi } from 'vitest'

let isTauriRuntime = true
const markActiveSessionSyncedMock = vi.fn()
const markSessionNeedsAuthoritativeRecoveryMock = vi.fn()
const registerBackendSessionIdMock = vi.fn()
const persistAssistantPayloadToBackendSessionMock = vi.fn()

vi.mock('@tauri-apps/api/core', () => ({
  isTauri: () => isTauriRuntime,
}))

vi.mock('../services/core-bridge', () => ({
  getCoreBudget: () => null,
  markActiveSessionSynced: (...args: unknown[]) => markActiveSessionSyncedMock(...args),
  markSessionNeedsAuthoritativeRecovery: (...args: unknown[]) =>
    markSessionNeedsAuthoritativeRecoveryMock(...args),
}))

vi.mock('../services/context-compaction', () => ({
  decodeCompactionModel: vi.fn(() => null),
}))

vi.mock('../services/agent-settlement', async (importOriginal) => {
  const actual = await importOriginal<typeof import('../services/agent-settlement')>()
  return {
    ...actual,
    persistAssistantPayloadToBackendSession: (...args: unknown[]) =>
      persistAssistantPayloadToBackendSessionMock(...args),
  }
})

vi.mock('../services/web-session-identity', () => ({
  registerBackendSessionId: (...args: unknown[]) => registerBackendSessionIdMock(...args),
}))

import { createAgentRun } from './useAgentRun'

describe('createAgentRun', () => {
  beforeEach(() => {
    isTauriRuntime = true
    markActiveSessionSyncedMock.mockReset()
    markSessionNeedsAuthoritativeRecoveryMock.mockReset()
    registerBackendSessionIdMock.mockReset()
    persistAssistantPayloadToBackendSessionMock.mockReset()
    persistAssistantPayloadToBackendSessionMock.mockResolvedValue(true)
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

  it('auto-titles legacy default session names on the first user message', async () => {
    let currentRunToken = 0
    const renameSession = vi.fn().mockResolvedValue(undefined)

    const runModule = createAgentRun({
      rustAgent: {
        isRunning: () => false,
        run: vi.fn(async () => ({ success: true, turns: 1, sessionId: 'session-a' })),
        error: () => null,
        streamingContent: () => 'done',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        activeToolCalls: () => [],
        tokenUsage: () => ({ output: 0, cost: 0 }),
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({
          id: 'session-a',
          name: 'New Session',
          metadata: { titlePlaceholder: true },
        }),
        createNewSession: vi.fn(),
        addMessage: vi.fn(),
        updateMessage: vi.fn(),
        updateMessageInSession: vi.fn(),
        addMessageToSession: vi.fn(),
        deleteMessage: vi.fn(),
        deleteMessageInSession: vi.fn(),
        selectedModel: () => 'gpt-5.4',
        selectedProvider: () => 'openai',
        messages: () => [],
        renameSession,
      },
      settingsRef: {
        settings: () => ({
          behavior: { sessionAutoTitle: true },
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
        beginRun: () => ++currentRunToken,
        isCurrentRun: (token: number) => token === currentRunToken,
      },
    } as unknown as Parameters<typeof createAgentRun>[0])

    await expect(runModule.run('Build OAuth flow')).resolves.toEqual(
      expect.objectContaining({ success: true, sessionId: 'session-a' })
    )

    expect(renameSession).toHaveBeenCalledWith('session-a', 'Build OAuth flow')
  })

  it('does not auto-title explicitly named placeholder-looking sessions', async () => {
    let currentRunToken = 0
    const renameSession = vi.fn().mockResolvedValue(undefined)

    const runModule = createAgentRun({
      rustAgent: {
        isRunning: () => false,
        run: vi.fn(async () => ({ success: true, turns: 1, sessionId: 'session-a' })),
        error: () => null,
        streamingContent: () => 'done',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        activeToolCalls: () => [],
        tokenUsage: () => ({ output: 0, cost: 0 }),
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({
          id: 'session-a',
          name: 'New Session',
          metadata: { titlePlaceholder: false },
        }),
        createNewSession: vi.fn(),
        addMessage: vi.fn(),
        updateMessage: vi.fn(),
        updateMessageInSession: vi.fn(),
        addMessageToSession: vi.fn(),
        deleteMessage: vi.fn(),
        deleteMessageInSession: vi.fn(),
        selectedModel: () => 'gpt-5.4',
        selectedProvider: () => 'openai',
        messages: () => [],
        renameSession,
      },
      settingsRef: {
        settings: () => ({
          behavior: { sessionAutoTitle: true },
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
        beginRun: () => ++currentRunToken,
        isCurrentRun: (token: number) => token === currentRunToken,
      },
    } as unknown as Parameters<typeof createAgentRun>[0])

    await expect(runModule.run('Build OAuth flow')).resolves.toEqual(
      expect.objectContaining({ success: true, sessionId: 'session-a' })
    )

    expect(renameSession).not.toHaveBeenCalled()
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

  it('settles hidden-session runtime cache after a successful off-screen completion', async () => {
    let activeSessionId = 'session-a'
    let currentRunToken = 0
    let releaseRun!: (value: { success: boolean; turns: number; sessionId: string }) => void
    const onSessionRuntimeSettled = vi.fn()

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
        addMessage: vi.fn(),
        updateMessage: vi.fn(),
        updateMessageInSession: vi.fn(),
        deleteMessage: vi.fn(),
        deleteMessageInSession: vi.fn(),
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
      onSessionRuntimeSettled,
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

    await runPromise

    expect(onSessionRuntimeSettled).toHaveBeenCalledWith('session-a')
  })

  it('preserves tool-only assistant completions instead of deleting the placeholder', async () => {
    const updateMessage = vi.fn()
    const deleteMessage = vi.fn()

    const runModule = createAgentRun({
      rustAgent: {
        isRunning: () => false,
        run: vi.fn().mockResolvedValue({ success: true, turns: 1, sessionId: 'session-a' }),
        error: () => null,
        streamingContent: () => '',
        thinkingContent: () => '',
        thinkingSegments: () => [],
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
        tokenUsage: () => ({ output: 0, cost: 0 }),
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: 'session-a', name: 'Session A' }),
        createNewSession: vi.fn(),
        addMessage: vi.fn(),
        updateMessage,
        deleteMessage,
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

    expect(updateMessage).toHaveBeenCalledWith('asst-live', {
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

  it('skips blank placeholder settlement when a run detaches to another session before tokens arrive', async () => {
    let activeSessionId = 'session-a'
    let currentRunToken = 0
    let releaseRun!: (value: {
      detachedSessionId: string
      success: boolean
      turns: number
      sessionId: string
    }) => void
    const addMessage = vi.fn()
    const updateMessage = vi.fn()
    const updateMessageInSession = vi.fn()
    const deleteMessage = vi.fn()
    const deleteMessageInSession = vi.fn()

    const runModule = createAgentRun({
      rustAgent: {
        isRunning: () => false,
        run: vi.fn(
          () =>
            new Promise<{
              detachedSessionId: string
              success: boolean
              turns: number
              sessionId: string
            }>((resolve) => {
              releaseRun = resolve
            })
        ),
        error: () => null,
        streamingContent: () => '',
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
    releaseRun({
      detachedSessionId: 'session-a',
      success: false,
      turns: 0,
      sessionId: 'session-a',
    })

    await expect(runPromise).resolves.toBeNull()
    expect(updateMessage).not.toHaveBeenCalled()
    expect(updateMessageInSession).not.toHaveBeenCalled()
    expect(deleteMessage).not.toHaveBeenCalled()
    expect(deleteMessageInSession).toHaveBeenCalledWith('session-a', 'asst-live')
    expect(markSessionNeedsAuthoritativeRecoveryMock).toHaveBeenCalledWith('session-a')
  })

  it('keeps detached settlement run-scoped when a newer run starts later', async () => {
    let activeSessionId = 'session-a'
    let currentRunToken = 0
    let releaseFirstRun!: (value: {
      detachedSessionId: string
      success: boolean
      turns: number
      sessionId: string
    }) => void
    const runMock = vi
      .fn()
      .mockImplementationOnce(
        () =>
          new Promise<{
            detachedSessionId: string
            success: boolean
            turns: number
            sessionId: string
          }>((resolve) => {
            releaseFirstRun = resolve
          })
      )
      .mockResolvedValueOnce({ success: true, turns: 1, sessionId: 'session-b' })
    const deleteMessageInSession = vi.fn()

    const runModule = createAgentRun({
      rustAgent: {
        isRunning: () => false,
        run: runMock,
        error: () => null,
        streamingContent: () => '',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        activeToolCalls: () => [],
        tokenUsage: () => ({ output: 0, cost: 0 }),
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: activeSessionId, name: activeSessionId }),
        createNewSession: vi.fn(),
        addMessage: vi.fn(),
        updateMessage: vi.fn(),
        updateMessageInSession: vi.fn(),
        deleteMessage: vi.fn(),
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

    const firstRunPromise = runModule.run('ship detached')
    activeSessionId = 'session-b'
    await expect(runModule.run('ship visible')).resolves.toEqual({
      success: true,
      turns: 1,
      sessionId: 'session-b',
    })

    releaseFirstRun({
      detachedSessionId: 'session-a',
      success: false,
      turns: 0,
      sessionId: 'session-a',
    })

    await expect(firstRunPromise).resolves.toBeNull()
    expect(deleteMessageInSession).toHaveBeenCalledWith('session-a', expect.any(String))
    expect(markSessionNeedsAuthoritativeRecoveryMock).toHaveBeenCalledWith('session-a')
  })

  it('preserves partial streamed content when a run detaches after tokens started', async () => {
    let activeSessionId = 'session-a'
    let currentRunToken = 0
    let releaseRun!: (value: {
      detachedSessionId: string
      success: boolean
      turns: number
      sessionId: string
    }) => void
    const updateMessageInSession = vi.fn()
    const deleteMessageInSession = vi.fn()

    const runModule = createAgentRun({
      rustAgent: {
        isRunning: () => false,
        run: vi.fn(
          () =>
            new Promise<{
              detachedSessionId: string
              success: boolean
              turns: number
              sessionId: string
            }>((resolve) => {
              releaseRun = resolve
            })
        ),
        error: () => null,
        streamingContent: () => 'partial answer',
        thinkingContent: () => 'working',
        thinkingSegments: () => [{ thinking: 'working', toolCallIds: [] }],
        activeToolCalls: () => [],
        tokenUsage: () => ({ output: 5, cost: 0.01 }),
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: activeSessionId, name: 'Session A' }),
        createNewSession: vi.fn(),
        addMessage: vi.fn(),
        addMessageToSession: vi.fn(),
        updateMessage: vi.fn(),
        updateMessageInSession,
        deleteMessage: vi.fn(),
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
    releaseRun({
      detachedSessionId: 'session-a',
      success: false,
      turns: 0,
      sessionId: 'session-a',
    })

    await expect(runPromise).resolves.toBeNull()
    expect(updateMessageInSession).toHaveBeenCalledWith('session-a', 'asst-live', {
      content: 'partial answer',
      tokensUsed: 5,
      costUSD: 0.01,
      toolCalls: [],
      metadata: {
        provider: 'openai',
        model: 'gpt-5.4',
        mode: 'code',
        elapsedMs: expect.any(Number),
        thinking: 'working',
      },
    })
    expect(deleteMessageInSession).not.toHaveBeenCalled()
    expect(markSessionNeedsAuthoritativeRecoveryMock).toHaveBeenCalledWith('session-a')
  })

  it('settles hidden-session runtime cache after an off-screen detach', async () => {
    let activeSessionId = 'session-a'
    let currentRunToken = 0
    let releaseRun!: (value: {
      detachedSessionId: string
      success: boolean
      turns: number
      sessionId: string
    }) => void
    const onSessionRuntimeSettled = vi.fn()

    const runModule = createAgentRun({
      rustAgent: {
        isRunning: () => false,
        run: vi.fn(
          () =>
            new Promise<{
              detachedSessionId: string
              success: boolean
              turns: number
              sessionId: string
            }>((resolve) => {
              releaseRun = resolve
            })
        ),
        error: () => null,
        streamingContent: () => '',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        activeToolCalls: () => [],
        tokenUsage: () => ({ output: 0, cost: 0 }),
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: activeSessionId, name: 'Session A' }),
        createNewSession: vi.fn(),
        addMessage: vi.fn(),
        updateMessage: vi.fn(),
        updateMessageInSession: vi.fn(),
        deleteMessage: vi.fn(),
        deleteMessageInSession: vi.fn(),
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
      onSessionRuntimeSettled,
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

    const runPromise = runModule.run('ship detached')
    activeSessionId = 'session-b'
    releaseRun({
      detachedSessionId: 'session-a',
      success: false,
      turns: 0,
      sessionId: 'session-a',
    })

    await runPromise

    expect(onSessionRuntimeSettled).toHaveBeenCalledWith('session-a')
  })

  it('falls back to the detached-session signal when the run result omits it', async () => {
    let activeSessionId = 'session-a'
    let currentRunToken = 0
    const clearDetachedSessionId = vi.fn()
    const updateMessageInSession = vi.fn()

    const runModule = createAgentRun({
      rustAgent: {
        isRunning: () => false,
        run: vi.fn().mockResolvedValue({ success: false, turns: 0, sessionId: 'session-a' }),
        error: () => null,
        streamingContent: () => 'partial answer',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        activeToolCalls: () => [],
        detachedSessionId: () => 'session-a',
        clearDetachedSessionId,
        tokenUsage: () => ({ output: 5, cost: 0.01 }),
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: activeSessionId, name: 'Session A' }),
        createNewSession: vi.fn(),
        addMessage: vi.fn(),
        addMessageToSession: vi.fn(),
        updateMessage: vi.fn(),
        updateMessageInSession,
        deleteMessage: vi.fn(),
        deleteMessageInSession: vi.fn(),
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

    await expect(runPromise).resolves.toBeNull()
    expect(updateMessageInSession).toHaveBeenCalledWith('session-a', 'asst-live', {
      content: 'partial answer',
      tokensUsed: 5,
      costUSD: 0.01,
      toolCalls: [],
      metadata: {
        provider: 'openai',
        model: 'gpt-5.4',
        mode: 'code',
        elapsedMs: expect.any(Number),
      },
    })
    expect(clearDetachedSessionId).toHaveBeenCalledTimes(1)
    expect(markSessionNeedsAuthoritativeRecoveryMock).toHaveBeenCalledWith('session-a')
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
        run: vi.fn().mockResolvedValue({
          success: true,
          turns: 1,
          sessionId: 'backend-session-a',
        }),
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
      sessionId: 'backend-session-a',
    })

    expect(fetchMock).toHaveBeenCalledWith(
      '/api/sessions/backend-session-a/messages',
      expect.objectContaining({
        headers: expect.any(Object),
      })
    )
    expect(replaceMessagesFromBackend).toHaveBeenCalledWith([
      expect.objectContaining({ id: 'backend-user-1', sessionId: 'session-a' }),
    ])
    expect(markActiveSessionSyncedMock).toHaveBeenCalledWith('session-a', 1)
    expect(registerBackendSessionIdMock).toHaveBeenCalledWith('session-a', 'backend-session-a')
  })

  it('marks hidden web completions for authoritative recovery and registers backend session ids', async () => {
    isTauriRuntime = false
    let activeSessionId = 'session-a'
    let currentRunToken = 0
    let releaseRun!: (value: {
      detachedSessionId: string
      success: boolean
      turns: number
      sessionId: string
    }) => void
    const updateMessageInSession = vi.fn()

    const runModule = createAgentRun({
      rustAgent: {
        isRunning: () => false,
        run: vi.fn(
          () =>
            new Promise<{
              detachedSessionId: string
              success: boolean
              turns: number
              sessionId: string
            }>((resolve) => {
              releaseRun = resolve
            })
        ),
        error: () => null,
        streamingContent: () => 'partial detached answer',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        activeToolCalls: () => [
          {
            id: 'tool-1',
            name: 'bash',
            args: { command: 'pwd' },
            status: 'success',
            output: '/workspace',
          },
        ],
        tokenUsage: () => ({ output: 5, cost: 0.01 }),
        endRun: vi.fn(),
        clearDetachedSessionId: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: activeSessionId, name: 'Session A' }),
        createNewSession: vi.fn(),
        addMessage: vi.fn(),
        addMessageToSession: vi.fn(),
        updateMessage: vi.fn(),
        updateMessageInSession,
        deleteMessage: vi.fn(),
        deleteMessageInSession: vi.fn(),
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
    releaseRun({
      detachedSessionId: 'session-a',
      success: true,
      turns: 1,
      sessionId: 'backend-session-a',
    })

    await expect(runPromise).resolves.toBeNull()

    expect(updateMessageInSession).toHaveBeenCalledWith('session-a', 'asst-live', {
      content: 'partial detached answer',
      tokensUsed: 5,
      costUSD: 0.01,
      toolCalls: [expect.objectContaining({ id: 'tool-1', name: 'bash', output: '/workspace' })],
      metadata: {
        provider: 'openai',
        model: 'gpt-5.4',
        mode: 'code',
        elapsedMs: expect.any(Number),
      },
    })
    expect(persistAssistantPayloadToBackendSessionMock).toHaveBeenCalledWith(
      'backend-session-a',
      expect.objectContaining({
        content: 'partial detached answer',
        tokensUsed: 5,
        costUSD: 0.01,
        provider: 'openai',
        model: 'gpt-5.4',
        mode: 'code',
      })
    )
    expect(markSessionNeedsAuthoritativeRecoveryMock).toHaveBeenCalledWith('session-a')
    expect(registerBackendSessionIdMock).toHaveBeenCalledWith('session-a', 'backend-session-a')
  })

  it('does not mark for authoritative recovery when persistence fails in hidden web completion', async () => {
    isTauriRuntime = false
    persistAssistantPayloadToBackendSessionMock.mockResolvedValue(false) // Simulate persistence failure
    let activeSessionId = 'session-a'
    let currentRunToken = 0
    let releaseRun!: (value: {
      detachedSessionId: string
      success: boolean
      turns: number
      sessionId: string
    }) => void
    const updateMessageInSession = vi.fn()

    const runModule = createAgentRun({
      rustAgent: {
        isRunning: () => false,
        run: vi.fn(
          () =>
            new Promise<{
              detachedSessionId: string
              success: boolean
              turns: number
              sessionId: string
            }>((resolve) => {
              releaseRun = resolve
            })
        ),
        error: () => null,
        streamingContent: () => 'partial detached answer',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        activeToolCalls: () => [],
        tokenUsage: () => ({ output: 5, cost: 0.01 }),
        endRun: vi.fn(),
        clearDetachedSessionId: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: activeSessionId, name: 'Session A' }),
        createNewSession: vi.fn(),
        addMessage: vi.fn(),
        addMessageToSession: vi.fn(),
        updateMessage: vi.fn(),
        updateMessageInSession,
        deleteMessage: vi.fn(),
        deleteMessageInSession: vi.fn(),
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
    releaseRun({
      detachedSessionId: 'session-a',
      success: true,
      turns: 1,
      sessionId: 'backend-session-a',
    })

    await expect(runPromise).resolves.toBeNull()

    // When persistence fails, recovery should NOT be marked to avoid recovery from stale state
    expect(persistAssistantPayloadToBackendSessionMock).toHaveBeenCalled()
    expect(markSessionNeedsAuthoritativeRecoveryMock).not.toHaveBeenCalled()
    expect(registerBackendSessionIdMock).toHaveBeenCalledWith('session-a', 'backend-session-a')
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

  it('starts a new visible-session run while another session is running off-screen', async () => {
    const setMessageQueue = vi.fn()
    const runMock = vi.fn().mockResolvedValue({ success: true, turns: 1, sessionId: 'session-b' })

    const runModule = createAgentRun({
      rustAgent: {
        isRunning: () => true,
        trackedSessionId: () => 'session-a',
        run: runMock,
        error: () => null,
        streamingContent: () => 'fresh answer',
        thinkingContent: () => '',
        thinkingSegments: () => [],
        activeToolCalls: () => [],
        tokenUsage: () => ({ output: 0, cost: 0 }),
        endRun: vi.fn(),
      },
      session: {
        currentSession: () => ({ id: 'session-b', name: 'Session B' }),
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
      sessionHasActiveRun: () => false,
      isPlanMode: () => false,
      setCurrentThought: vi.fn(),
      setDoomLoopDetected: vi.fn(),
      setToolActivity: vi.fn(),
      setStreamingTokenEstimate: vi.fn(),
      streamingStartedAt: () => null,
      setStreamingStartedAt: vi.fn(),
      messageQueue: () => [],
      setMessageQueue,
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

    await expect(runModule.run('ship from session-b')).resolves.toEqual({
      success: true,
      turns: 1,
      sessionId: 'session-b',
    })

    expect(runMock).toHaveBeenCalledWith(
      'ship from session-b',
      expect.objectContaining({ sessionId: 'session-b' })
    )
    expect(runMock).toHaveBeenCalledTimes(1)
  })

  it('retains submitted images in optimistic local user message state', async () => {
    const addMessage = vi.fn()
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
        addMessage,
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

    await runModule.run('describe screenshot', {
      images: [{ data: 'base64-image', mediaType: 'image/png' }],
    })

    expect(addMessage).toHaveBeenCalledWith(
      expect.objectContaining({
        role: 'user',
        content: 'describe screenshot',
        images: [{ data: 'base64-image', mimeType: 'image/png' }],
        metadata: {
          images: [{ data: 'base64-image', mimeType: 'image/png' }],
        },
      })
    )
    expect(runMock).toHaveBeenCalledWith(
      'describe screenshot',
      expect.objectContaining({
        images: [{ data: 'base64-image', mediaType: 'image/png' }],
      })
    )
  })

  it('preserves image attachments when submit is queued behind an active run', async () => {
    let queue: Array<{ content: string; sessionId?: string; model?: string; images?: unknown[] }> =
      []
    const setMessageQueue = vi.fn((updater: unknown) => {
      if (typeof updater === 'function') {
        queue = (updater as (prev: typeof queue) => typeof queue)(queue)
      } else {
        queue = updater as typeof queue
      }
      return queue
    })

    const runModule = createAgentRun({
      rustAgent: {
        isRunning: () => true,
        trackedSessionId: () => 'session-a',
        run: vi.fn(),
        error: () => null,
        streamingContent: () => '',
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
      messageQueue: () => queue,
      setMessageQueue,
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
        beginRun: () => 1,
        isCurrentRun: () => true,
      },
    } as unknown as Parameters<typeof createAgentRun>[0])

    await expect(
      runModule.run('queued screenshot prompt', {
        model: 'gpt-5.4',
        images: [{ data: 'base64-image', mediaType: 'image/png' }],
      })
    ).resolves.toBeNull()

    expect(setMessageQueue).toHaveBeenCalledTimes(1)
    expect(queue).toEqual([
      {
        content: 'queued screenshot prompt',
        sessionId: 'session-a',
        model: 'gpt-5.4',
        images: [{ data: 'base64-image', mimeType: 'image/png' }],
      },
    ])
  })
})
