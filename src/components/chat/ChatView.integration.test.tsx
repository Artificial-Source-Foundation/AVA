import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { ChatView } from './ChatView'

const mockSendMessage = vi.fn()
const mockStartFileWatcher = vi.fn()
const mockStopFileWatcher = vi.fn()

vi.mock('../../hooks/useAgent', () => ({
  useAgent: () => ({
    pendingApproval: () => null,
    resolveApproval: vi.fn(),
  }),
}))

vi.mock('../../contexts/notification', () => ({
  useNotification: () => ({
    info: vi.fn(),
  }),
}))

vi.mock('../../hooks/useChat', () => ({
  useChat: () => ({
    pendingApproval: () => null,
    resolveApproval: vi.fn(),
    sendMessage: (...args: unknown[]) => mockSendMessage(...args),
  }),
}))

vi.mock('../../services/file-watcher', () => ({
  startFileWatcher: (...args: unknown[]) => mockStartFileWatcher(...args),
  stopFileWatcher: (...args: unknown[]) => mockStopFileWatcher(...args),
}))

vi.mock('../../services/logger', () => ({
  logInfo: vi.fn(),
  logError: vi.fn(),
}))

vi.mock('../../stores/project', () => ({
  useProject: () => ({
    currentProject: () => ({ directory: '/tmp/project' }),
  }),
}))

vi.mock('../../stores/settings', () => ({
  useSettings: () => ({
    settings: () => ({
      behavior: {
        fileWatcher: true,
      },
    }),
    addAutoApprovedTool: vi.fn(),
  }),
}))

vi.mock('../../stores/team', () => ({
  useTeam: () => ({
    selectedMemberId: () => null,
  }),
}))

vi.mock('../../services/clipboard-watcher', () => ({
  createClipboardWatcher: () => ({ start: vi.fn(), stop: vi.fn() }),
  looksLikeCode: () => false,
}))

vi.mock('./ApprovalDock', () => ({
  ApprovalDock: () => null,
}))

vi.mock('./ContextBar', () => ({
  ContextBar: () => null,
}))

vi.mock('./MessageInput', () => ({
  MessageInput: () => null,
}))

vi.mock('./MessageList', () => ({
  MessageList: () => null,
}))

vi.mock('./TeamChatView', () => ({
  TeamChatView: () => null,
}))

vi.mock('./TeamStatusStrip', () => ({
  TeamStatusStrip: () => null,
}))

describe('ChatView integration', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  afterEach(() => {
    vi.clearAllMocks()
  })

  it('starts watcher and forwards AI question comment to chat.sendMessage', async () => {
    const container = document.createElement('div')
    const dispose = render(() => <ChatView />, container)

    await Promise.resolve()

    expect(mockStartFileWatcher).toHaveBeenCalledOnce()
    const callback = mockStartFileWatcher.mock.calls[0]?.[1] as
      | ((comment: {
          type: 'question' | 'execute'
          content: string
          filePath: string
          lineNumber: number
          context: string
        }) => void)
      | undefined

    expect(callback).toBeDefined()

    callback?.({
      type: 'question',
      content: 'Should we rename this function?',
      filePath: 'src/file.ts',
      lineNumber: 42,
      context: 'function oldName() {}',
    })

    expect(mockSendMessage).toHaveBeenCalledOnce()
    expect(mockSendMessage.mock.calls[0]?.[0]).toContain(
      '[Question] Should we rename this function?'
    )
    expect(mockSendMessage.mock.calls[0]?.[0]).toContain('// File: src/file.ts:42')

    dispose()
    expect(mockStopFileWatcher).toHaveBeenCalled()
  })
})
