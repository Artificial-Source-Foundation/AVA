import { createRoot } from 'solid-js'
import { beforeEach, describe, expect, it, vi } from 'vitest'

import type { PendingFile, PendingImage, PendingPaste } from './types'

const openModelBrowserMock = vi.fn()
const agentRunMock = vi.fn(async (_message: string, _options?: unknown) => undefined)
const followUpMock = vi.fn(async (_message: string, _sessionId?: string) => undefined)
const clearAllMock = vi.fn<
  () => {
    images: PendingImage[]
    files: PendingFile[]
    pastes: PendingPaste[]
  }
>(() => ({ images: [], files: [], pastes: [] }))
const notifyInfoMock = vi.fn()
const notifyErrorMock = vi.fn()

let isRunningState = false
let pendingPastesState: Array<{ content: string; lineCount: number }> = []
let pendingImagesState: Array<{
  data: string
  mimeType: 'image/png' | 'image/jpeg' | 'image/gif' | 'image/webp'
  name?: string
}> = []

vi.mock('../../../contexts/notification', () => ({
  useNotification: () => ({
    info: notifyInfoMock,
    error: notifyErrorMock,
  }),
}))

vi.mock('../../../hooks/useAgent', () => ({
  useAgent: () => ({
    isRunning: () => isRunningState,
    isPlanMode: () => false,
    streamingStartedAt: () => null,
    followUp: (message: string, sessionId?: string) => followUpMock(message, sessionId),
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
    pendingPastes: () => pendingPastesState,
    pendingImages: () => pendingImagesState,
    clearAll: () => clearAllMock(),
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
    followUpMock.mockReset()
    followUpMock.mockResolvedValue(undefined)
    clearAllMock.mockReset()
    clearAllMock.mockReturnValue({ images: [], files: [], pastes: [] })
    notifyInfoMock.mockReset()
    notifyErrorMock.mockReset()
    isRunningState = false
    pendingPastesState = []
    pendingImagesState = []
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

  it('forwards pending images to the initial run payload', async () => {
    clearAllMock.mockReturnValue({
      images: [{ data: 'base64-image', mimeType: 'image/png', name: 'screen.png' }],
      files: [],
      pastes: [],
    })

    await runInRoot(async (state) => {
      state.setInput('describe this screenshot')
      await state.handleSubmit(new Event('submit'))

      expect(agentRunMock).toHaveBeenCalledWith('describe this screenshot', {
        model: 'gpt-5.4',
        images: [{ data: 'base64-image', mediaType: 'image/png' }],
      })
    })
  })

  it('blocks image-bearing submit while a run is active with a clear error', async () => {
    isRunningState = true
    pendingImagesState = [{ data: 'base64-image', mimeType: 'image/png', name: 'screen.png' }]

    await runInRoot(async (state) => {
      state.setInput('queue this screenshot')
      await state.handleSubmit(new Event('submit'))

      expect(followUpMock).not.toHaveBeenCalled()
      expect(agentRunMock).not.toHaveBeenCalled()
      expect(clearAllMock).not.toHaveBeenCalled()
      expect(notifyErrorMock).toHaveBeenCalledWith(
        'Images unavailable while agent is running',
        'Wait for the current response to finish before sending images.'
      )
      expect(state.input()).toBe('queue this screenshot')
      expect(state.attachments.pendingImages()).toEqual([
        { data: 'base64-image', mimeType: 'image/png', name: 'screen.png' },
      ])
    })
  })
})
