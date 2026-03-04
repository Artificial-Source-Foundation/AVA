import { createRoot } from 'solid-js'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const h = vi.hoisted(() => {
  const sessionMessages: Array<Record<string, unknown>> = []
  const currentSession = { id: 'session-1', name: 'New Chat' }
  const settingsState = {
    generation: {
      customInstructions: '',
      delegationEnabled: false,
      reasoningEffort: 'off' as const,
    },
    behavior: {
      sessionAutoTitle: true,
    },
    agentLimits: {
      agentMaxTurns: 20,
      agentMaxTimeMinutes: 10,
      autoFixLint: false,
    },
    notifications: {},
    permissionMode: 'normal',
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
    createNewSession: vi.fn(async () => ({ id: 'session-2' })),
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
    steer = vi.fn()
  },
  abortExecutor: vi.fn(),
  registerExecutor: vi.fn(),
  unregisterExecutor: vi.fn(),
  generateTitle: vi.fn(async () => 'OAuth Flow Implementation'),
}))

vi.mock('@ava/core-v2/extensions', () => ({
  addToolMiddleware: vi.fn(() => ({ dispose: vi.fn() })),
  getAgentModes: vi.fn(() => new Map()),
  onEvent: vi.fn(() => ({ dispose: vi.fn() })),
}))

vi.mock('@ava/core-v2/tools', () => ({
  executeTool: vi.fn(),
  getToolDefinitions: vi.fn(() => []),
}))

vi.mock('@ava/core-v2/platform', () => ({
  getPlatform: vi.fn(() => ({
    shell: { exec: vi.fn(async () => ({ exitCode: 1, stdout: '', stderr: '' })) },
  })),
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
  flushLogs: vi.fn(),
}))

vi.mock('../services/notifications', () => ({
  notifyCompletion: vi.fn(),
}))

vi.mock('../services/tool-approval-bridge', () => ({
  pendingApproval: () => null,
  resolveApproval: vi.fn(),
  createApprovalMiddleware: vi.fn(() => ({ name: 'test', priority: 5 })),
  setAutoApprovalChecker: vi.fn(),
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

vi.mock('../stores/team', () => ({
  useTeam: () => ({
    clearTeam: vi.fn(),
    teamMembers: vi.fn(() => new Map()),
    teamLead: vi.fn(() => null),
    seniorLeads: vi.fn(() => []),
    selectedMemberId: vi.fn(() => null),
    addMember: vi.fn(),
    updateMemberStatus: vi.fn(),
    updateMember: vi.fn(),
    addToolCall: vi.fn(),
    updateToolCall: vi.fn(),
    addMessage: vi.fn(),
    updateMessage: vi.fn(),
    agentTypeToRole: vi.fn(() => 'team-lead'),
    inferDomain: vi.fn(() => 'general'),
    generateName: vi.fn(() => 'Test Agent'),
  }),
}))

// Mock the prompt builder to avoid the 1.5s timer
vi.mock('@ava/core-v2/config', () => ({
  getSettingsManager: vi.fn(() => ({
    get: vi.fn((key: string) => {
      if (key === 'provider') {
        return {
          defaultProvider: 'openai',
          defaultModel: 'gpt-4o',
        }
      }
      return undefined
    }),
  })),
}))

vi.mock('@ava/core-v2/llm', () => ({
  createClient: vi.fn(() => ({
    stream: vi.fn(async function* () {
      // Simulate streaming title generation
      yield { content: 'OAuth Flow Implementation' }
    }),
  })),
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
    await flushMicrotasks()

    expect(ctx.chat.isStreaming()).toBe(true)

    void ctx.chat.sendMessage('queued follow-up')
    expect(ctx.chat.queuedCount()).toBe(1)

    ctx.chat.cancel()
    expect(ctx.chat.isStreaming()).toBe(false)
    expect(ctx.chat.queuedCount()).toBe(0)

    ctx.dispose()
  })

  it('steer uses executor when running, falls back to queue when idle', async () => {
    const ctx = createRoot((dispose) => ({ chat: useChat(), dispose }))

    void ctx.chat.sendMessage('stream in progress')
    await flushMicrotasks()

    // Queue a follow-up while running
    void ctx.chat.sendMessage('queued message')
    expect(ctx.chat.queuedCount()).toBe(1)

    // Steer while executor is running — uses executor.steer(), doesn't touch queue
    ctx.chat.steer('priority steer')
    // Queue still has the queued message (steer went via executor)
    expect(ctx.chat.queuedCount()).toBe(1)

    ctx.chat.cancel()
    expect(ctx.chat.queuedCount()).toBe(0)

    ctx.dispose()
  })

  it('auto-titles a new chat from first user message using AI', async () => {
    const ctx = createRoot((dispose) => ({ chat: useChat(), dispose }))

    void ctx.chat.sendMessage('Build OAuth flow for OpenAI codex endpoint')
    await flushMicrotasks()

    // The title should be AI-generated (mocked to return 'OAuth Flow Implementation')
    expect(h.sessionMock.renameSession).toHaveBeenCalledWith(
      'session-1',
      'OAuth Flow Implementation'
    )

    ctx.chat.cancel()
    ctx.dispose()
  })
})
