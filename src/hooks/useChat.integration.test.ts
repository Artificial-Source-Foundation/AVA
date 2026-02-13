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
  }

  return {
    sessionMessages,
    currentSession,
    settingsState,
    tracker,
    sessionMock,
    createClient: vi.fn(),
    getProviderForModel: vi.fn(() => 'openai'),
    saveMessage: vi.fn(),
    updateMessage: vi.fn().mockResolvedValue(undefined),
  }
})

vi.mock('@estela/core', () => ({
  estimateCost: vi.fn(() => 0),
  executeTool: vi.fn(),
  getToolDefinitions: vi.fn(() => []),
  resetToolCallCount: vi.fn(),
  undoLastAutoCommit: vi.fn(async () => ({ success: true, output: 'ok' })),
}))

vi.mock('../lib/tool-approval', () => ({
  checkAutoApproval: vi.fn(() => false),
  createApprovalGate: vi.fn(() => ({
    pendingApproval: () => null,
    resolveApproval: vi.fn(),
  })),
}))

vi.mock('../services/core-bridge', () => ({
  getCoreCompactor: vi.fn(() => null),
  getCoreMemory: vi.fn(() => ({
    recallSimilar: vi.fn(async () => []),
    recallProcedural: vi.fn(async () => []),
  })),
  getCoreTracker: vi.fn(() => h.tracker),
}))

vi.mock('../services/database', () => ({
  deleteMessageFromDb: vi.fn(async () => undefined),
  saveMessage: h.saveMessage,
  updateMessage: h.updateMessage,
}))

vi.mock('../services/llm/bridge', () => ({
  createClient: h.createClient,
  getProviderForModel: h.getProviderForModel,
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
  }),
}))

import { useChat } from './useChat'

function createPendingClient() {
  return {
    stream: async function* () {
      await new Promise(() => {})
      yield { type: 'content', content: 'never' } as const
    },
  }
}

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

    h.createClient.mockResolvedValue(createPendingClient())
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
