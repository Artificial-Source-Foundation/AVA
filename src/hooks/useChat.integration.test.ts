import { createRoot } from 'solid-js'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const h = vi.hoisted(() => {
  const sessionMessages: Array<Record<string, unknown>> = []
  const currentSession = { id: 'session-1', name: 'New Chat' }
  const settingsState = {
    generation: {
      customInstructions: '',
    },
    behavior: {
      autoFixLint: false,
      sessionAutoTitle: true,
    },
  }
  const tracker = {
    addMessage: vi.fn(),
    clear: vi.fn(),
    getStats: vi.fn(() => ({ total: 0, limit: 100_000, remaining: 100_000, percentUsed: 0 })),
  }

  const sessionMock = {
    messages: () => sessionMessages,
    selectedModel: () => 'openai/gpt-4o',
    currentSession: () => currentSession,
    createSession: vi.fn(async () => ({ id: 'session-2' })),
    setCurrentSession: vi.fn(),
    renameSession: vi.fn(),
    addMessage: vi.fn((message: Record<string, unknown>) => {
      sessionMessages.push(message)
    }),
    updateMessageContent: vi.fn(),
    updateMessage: vi.fn(),
    setMessageError: vi.fn(),
    setMessages: vi.fn(),
    deleteMessage: vi.fn(),
    deleteMessagesAfter: vi.fn(),
    stopEditing: vi.fn(),
    setRetryingMessageId: vi.fn(),
    addFileOperation: vi.fn(),
  }

  // AgentExecutor mock — creates a pending promise that never resolves
  // so the test can check streaming/queue state
  let pendingResolve: (() => void) | null = null
  const agentRunMock = vi.fn(
    () =>
      new Promise<{
        success: boolean
        output: string
        turns: number
        tokensUsed: { input: number; output: number }
        terminateMode: string
        durationMs: number
      }>((resolve) => {
        pendingResolve = () =>
          resolve({
            success: true,
            output: 'done',
            turns: 1,
            tokensUsed: { input: 100, output: 50 },
            terminateMode: 'GOAL',
            durationMs: 100,
          })
      })
  )

  return {
    sessionMessages,
    currentSession,
    settingsState,
    tracker,
    sessionMock,
    agentRunMock,
    getPendingResolve: () => pendingResolve,
    resolveProvider: vi.fn(() => 'openai'),
    saveMessage: vi.fn(),
    updateMessage: vi.fn().mockResolvedValue(undefined),
  }
})

vi.mock('@ava/core-v2/agent', () => ({
  AgentExecutor: class {
    run = h.agentRunMock
  },
}))

vi.mock('@ava/core-v2/extensions', () => ({
  addToolMiddleware: vi.fn(() => ({ dispose: vi.fn() })),
  onEvent: vi.fn(() => ({ dispose: vi.fn() })),
}))

vi.mock('@ava/core-v2/tools', () => ({
  executeTool: vi.fn(),
  getToolDefinitions: vi.fn(() => []),
}))

vi.mock('../lib/cost', () => ({
  estimateCost: vi.fn(() => 0),
  formatCost: vi.fn(() => '$0.00'),
}))

vi.mock('../lib/tool-approval', () => ({
  checkAutoApproval: vi.fn(() => false),
  createApprovalGate: vi.fn(() => ({
    pendingApproval: () => null,
    resolveApproval: vi.fn(),
  })),
}))

vi.mock('../services/core-bridge', () => ({
  getCoreBudget: vi.fn(() => h.tracker),
}))

vi.mock('../services/database', () => ({
  deleteMessageFromDb: vi.fn(async () => undefined),
  saveMessage: h.saveMessage,
  updateMessage: h.updateMessage,
}))

vi.mock('../services/llm/bridge', () => ({
  resolveProvider: h.resolveProvider,
  createClient: vi.fn(),
  getProviderForModel: h.resolveProvider,
}))

vi.mock('../services/file-browser', () => ({
  readFileContent: vi.fn(),
}))

vi.mock('../services/file-versions', () => ({
  recordFileChange: vi.fn(),
}))

vi.mock('../services/logger', () => ({
  logDebug: vi.fn(),
  logInfo: vi.fn(),
  logWarn: vi.fn(),
  logError: vi.fn(),
}))

vi.mock('../services/notifications', () => ({
  notifyCompletion: vi.fn(),
}))

vi.mock('../stores/project', () => ({
  useProject: () => ({
    currentProject: () => ({ directory: '/tmp/project' }),
  }),
}))

vi.mock('../stores/session', () => ({
  useSession: () => h.sessionMock,
}))

vi.mock('../stores/settings', () => ({
  useSettings: () => ({
    settings: () => h.settingsState,
    isToolAutoApproved: () => false,
  }),
}))

import { useChat } from './useChat'

describe('useChat integration queue/steer/cancel', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    h.sessionMessages.length = 0

    h.saveMessage.mockImplementation(async (input: Record<string, unknown>) => ({
      id: `msg-${Math.random().toString(36).slice(2, 8)}`,
      sessionId: input.sessionId,
      role: input.role,
      content: input.content,
      createdAt: Date.now(),
      metadata: input.metadata,
    }))

    h.currentSession.id = 'session-1'
    h.currentSession.name = 'New Chat'
    h.settingsState.behavior.sessionAutoTitle = true
  })

  afterEach(() => {
    vi.clearAllMocks()
    h.sessionMessages.length = 0
  })

  it('queues follow-up messages while streaming and clears queue on cancel', async () => {
    const ctx = createRoot((dispose) => ({ chat: useChat(), dispose }))

    void ctx.chat.sendMessage('first message')
    await Promise.resolve()

    expect(ctx.chat.isStreaming()).toBe(true)

    void ctx.chat.sendMessage('queued follow-up')
    expect(ctx.chat.queuedCount()).toBe(1)

    ctx.chat.cancel()
    expect(ctx.chat.isStreaming()).toBe(false)
    expect(ctx.chat.queuedCount()).toBe(0)

    ctx.dispose()
  })

  it('steer replaces queue with a single priority message', async () => {
    const ctx = createRoot((dispose) => ({ chat: useChat(), dispose }))

    void ctx.chat.sendMessage('stream in progress')
    await Promise.resolve()

    void ctx.chat.sendMessage('queued message')
    expect(ctx.chat.queuedCount()).toBe(1)

    ctx.chat.steer('priority steer')
    expect(ctx.chat.queuedCount()).toBe(1)

    ctx.chat.clearQueue()
    expect(ctx.chat.queuedCount()).toBe(0)

    ctx.dispose()
  })

  it('auto-titles a new chat from first user message', async () => {
    const ctx = createRoot((dispose) => ({ chat: useChat(), dispose }))

    void ctx.chat.sendMessage('Build OAuth flow for OpenAI codex endpoint')
    await Promise.resolve()

    expect(h.sessionMock.renameSession).toHaveBeenCalledWith(
      'session-1',
      'Build OAuth flow for OpenAI codex endpoint'
    )

    ctx.chat.cancel()
    ctx.dispose()
  })
})
