import { createRoot } from 'solid-js'
import { beforeEach, describe, expect, it, vi } from 'vitest'

const openModelBrowserMock = vi.fn()
const agentRunMock = vi.fn(async (_message: string, _options?: unknown) => undefined)

vi.mock('../../../contexts/notification', () => ({
  useNotification: () => ({
    info: vi.fn(),
    error: vi.fn(),
  }),
}))

vi.mock('../../../hooks/useAgent', () => ({
  useAgent: () => ({
    isRunning: () => false,
    isPlanMode: () => false,
    streamingStartedAt: () => null,
    followUp: vi.fn(async () => undefined),
    postComplete: vi.fn(async () => undefined),
    messageQueue: () => [],
    run: (message: string, options?: unknown) => agentRunMock(message, options),
    clearQueue: vi.fn(),
    clearError: vi.fn(),
    cancel: vi.fn(),
    steer: vi.fn(),
  }),
}))

vi.mock('../../../hooks/useElapsedTimer', () => ({
  useElapsedTimer: () => () => 0,
}))

vi.mock('../../../lib/ids', () => ({
  generateMessageId: () => 'msg-id',
}))

vi.mock('../../../services/context-compaction', () => ({
  applyCompactionResult: vi.fn(),
  parseCompactFocus: vi.fn(),
  requestConversationCompaction: vi.fn(),
}))

vi.mock('../../../services/ide-integration', () => ({
  openInExternalEditor: vi.fn(async (text: string) => text),
}))

vi.mock('../../../services/prompt-stash', () => ({
  getStash: () => [],
  popStash: () => '',
  pushStash: vi.fn(),
}))

vi.mock('../../../stores/layout', () => ({
  useLayout: () => ({
    openModelBrowser: () => openModelBrowserMock(),
    toggleSessionSwitcher: vi.fn(),
    openSettings: vi.fn(),
  }),
}))

vi.mock('../../../stores/session', () => ({
  useSession: () => ({
    selectedModel: () => 'gpt-5.4',
    messages: () => [],
    currentSession: () => ({ id: 'session-1' }),
    addMessage: vi.fn(),
    setMessages: vi.fn(),
    createNewSession: vi.fn(async () => undefined),
  }),
}))

vi.mock('../../../stores/settings', () => ({
  useSettings: () => ({
    settings: () => ({
      behavior: { sendKey: 'enter' },
      generation: { thinkingEnabled: false, reasoningEffort: 'medium' },
      providers: [],
    }),
    updateSettings: vi.fn(),
  }),
}))

vi.mock('./attachment-bar', () => ({
  createAttachmentState: () => ({
    pendingPastes: () => [],
    pendingImages: () => [],
    clearAll: () => ({ files: [], pastes: [] }),
  }),
}))

vi.mock('./attachments', () => ({
  buildFullMessage: (message: string) => message,
}))

vi.mock('./use-mention-state', () => ({
  useMentionState: () => ({
    mentionOpen: () => false,
    mentionFiltered: () => [],
    mentionIndex: () => 0,
    setMentionOpen: vi.fn(),
    setMentionIndex: vi.fn(),
    checkMention: vi.fn(),
    handleMentionSelect: vi.fn(),
  }),
}))

vi.mock('./use-model-state', () => ({
  useModelState: () => ({
    enabledProviders: () => [],
    currentModelDisplay: () => 'OpenAI | GPT-5.4',
    activeProviderId: () => 'openai',
    modelSupportsReasoning: () => true,
    handleCycleReasoning: vi.fn(),
  }),
}))

vi.mock('./use-slash-state', () => ({
  useSlashState: () => ({
    slashOpen: () => false,
    slashCommands: () => [],
    slashIndex: () => 0,
    setSlashOpen: vi.fn(),
    setSlashIndex: vi.fn(),
    checkSlash: vi.fn(),
    handleSlashSelect: vi.fn(),
  }),
}))

import { useInputState } from './use-input-state'

function runInRoot(assertions: (state: ReturnType<typeof useInputState>) => Promise<void>) {
  return new Promise<void>((resolve, reject) => {
    createRoot((dispose) => {
      const state = useInputState()
      assertions(state)
        .then(() => {
          dispose()
          resolve()
        })
        .catch((error) => {
          dispose()
          reject(error)
        })
    })
  })
}

describe('useInputState /model routing', () => {
  beforeEach(() => {
    openModelBrowserMock.mockReset()
    agentRunMock.mockReset()
    agentRunMock.mockResolvedValue(undefined)
  })

  it('opens the full model browser for bare /model', async () => {
    await runInRoot(async (state) => {
      state.setInput('/model')
      await state.handleSubmit(new Event('submit'))

      expect(openModelBrowserMock).toHaveBeenCalledTimes(1)
      expect(agentRunMock).not.toHaveBeenCalled()
      expect(state.input()).toBe('')
    })
  })

  it('keeps /model <args> on the agent path', async () => {
    await runInRoot(async (state) => {
      state.setInput('/model claude-sonnet-4')
      await state.handleSubmit(new Event('submit'))

      expect(openModelBrowserMock).not.toHaveBeenCalled()
      expect(agentRunMock).toHaveBeenCalledWith('/model claude-sonnet-4', {
        model: 'gpt-5.4',
      })
      expect(state.input()).toBe('')
    })
  })
})
