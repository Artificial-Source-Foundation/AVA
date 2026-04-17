import { createRoot } from 'solid-js'
import { beforeEach, describe, expect, it, vi } from 'vitest'

const openModelBrowserMock = vi.fn()
const registerActionMock = vi.fn<(id: string, action: () => void) => void>()
const setupShortcutListenerMock = vi.fn(() => () => {})

vi.mock('../components/chat/message-input/toolbar-buttons', () => ({
  cycleReasoningEffort: vi.fn(() => 'low'),
}))

vi.mock('../contexts/notification', () => ({
  useNotification: () => ({
    info: vi.fn(),
  }),
}))

vi.mock('../stores/layout', () => ({
  useLayout: () => ({
    toggleSidebar: vi.fn(),
    toggleSettings: vi.fn(),
    toggleBottomPanel: vi.fn(),
    toggleModelBrowser: vi.fn(),
    toggleChatSearch: vi.fn(),
    toggleSessionSwitcher: vi.fn(),
    openModelBrowser: () => openModelBrowserMock(),
    toggleExpandedEditor: vi.fn(),
    bottomPanelTab: () => 'memory',
    switchBottomPanelTab: vi.fn(),
    bottomPanelVisible: () => false,
  }),
}))

vi.mock('../stores/project', () => ({
  useProject: () => ({
    currentProject: () => ({ id: 'project-1' }),
  }),
}))

vi.mock('../stores/session', () => ({
  useSession: () => ({
    messages: () => [],
    undoFileChange: vi.fn(),
    redoFileChange: vi.fn(),
    createNewSession: vi.fn(),
  }),
}))

vi.mock('../stores/settings', () => ({
  useSettings: () => ({
    settings: () => ({
      generation: {
        reasoningEffort: 'medium',
      },
    }),
    updateSettings: vi.fn(),
  }),
}))

vi.mock('../stores/shortcuts', () => ({
  useShortcuts: () => ({
    registerAction: (...args: [string, () => void]) => registerActionMock(...args),
    setupShortcutListener: () => setupShortcutListenerMock(),
  }),
}))

vi.mock('./useAgent', () => ({
  useAgent: () => ({
    togglePlanMode: vi.fn(),
    isPlanMode: () => false,
  }),
}))

import { registerAppShortcuts } from './useAppShortcuts'

describe('registerAppShortcuts', () => {
  beforeEach(() => {
    openModelBrowserMock.mockReset()
    registerActionMock.mockReset()
    setupShortcutListenerMock.mockClear()
  })

  it('routes quick-model-picker shortcut to the model browser entry point', () => {
    const exportDialog = vi.fn()
    const checkpointDialog = vi.fn()
    const projectHubVisible = vi.fn()

    createRoot((dispose) => {
      registerAppShortcuts(exportDialog, checkpointDialog, projectHubVisible)
      dispose()
    })

    const quickModelPickerRegistration = registerActionMock.mock.calls.find(
      ([id]) => id === 'quick-model-picker'
    )

    expect(quickModelPickerRegistration).toBeTruthy()

    const quickModelPickerAction = quickModelPickerRegistration?.[1]
    expect(quickModelPickerAction).toBeTypeOf('function')

    quickModelPickerAction?.()
    expect(openModelBrowserMock).toHaveBeenCalledTimes(1)
  })
})
