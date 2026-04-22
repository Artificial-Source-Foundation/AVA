import { createRoot } from 'solid-js'
import { beforeEach, describe, expect, it, vi } from 'vitest'

const openModelBrowserMock = vi.fn()
const registerActionMock = vi.fn<(id: string, action: () => void) => void>()
const setupShortcutListenerMock = vi.fn(() => () => {})
const errorMock = vi.fn()
const infoMock = vi.fn()
const togglePlanModeMock = vi.fn()
const cyclePrimaryAgentProfileMock = vi.fn<(direction: 1 | -1) => Promise<string | null>>()
let hasPrimaryAgentProfilesMock = false

vi.mock('../components/chat/message-input/toolbar-buttons', () => ({
  cycleReasoningEffort: vi.fn(() => 'low'),
}))

vi.mock('../contexts/notification', () => ({
  useNotification: () => ({
    error: (...args: unknown[]) => errorMock(...args),
    info: (...args: unknown[]) => infoMock(...args),
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
    togglePlanMode: (...args: unknown[]) => togglePlanModeMock(...args),
    isPlanMode: () => false,
    hasPrimaryAgentProfiles: () => hasPrimaryAgentProfilesMock,
    cyclePrimaryAgentProfile: (direction: 1 | -1) => cyclePrimaryAgentProfileMock(direction),
  }),
}))

import { registerAppShortcuts } from './useAppShortcuts'

describe('registerAppShortcuts', () => {
  beforeEach(() => {
    openModelBrowserMock.mockReset()
    registerActionMock.mockReset()
    setupShortcutListenerMock.mockClear()
    errorMock.mockReset()
    infoMock.mockReset()
    togglePlanModeMock.mockReset()
    cyclePrimaryAgentProfileMock.mockReset()
    hasPrimaryAgentProfilesMock = false
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

  it('falls back to plan mode cycling when no primary-agent profiles exist', () => {
    const exportDialog = vi.fn()
    const checkpointDialog = vi.fn()
    const projectHubVisible = vi.fn()

    createRoot((dispose) => {
      registerAppShortcuts(exportDialog, checkpointDialog, projectHubVisible)
      dispose()
    })

    const modeCycleAction = registerActionMock.mock.calls.find(([id]) => id === 'mode-cycle')?.[1]
    expect(modeCycleAction).toBeTypeOf('function')

    modeCycleAction?.()

    expect(togglePlanModeMock).toHaveBeenCalledTimes(1)
    expect(cyclePrimaryAgentProfileMock).not.toHaveBeenCalled()
  })

  it('cycles primary-agent profiles when they are available', async () => {
    hasPrimaryAgentProfilesMock = true
    cyclePrimaryAgentProfileMock.mockResolvedValue('architect')

    const exportDialog = vi.fn()
    const checkpointDialog = vi.fn()
    const projectHubVisible = vi.fn()

    createRoot((dispose) => {
      registerAppShortcuts(exportDialog, checkpointDialog, projectHubVisible)
      dispose()
    })

    const modeCycleAction = registerActionMock.mock.calls.find(([id]) => id === 'mode-cycle')?.[1]
    expect(modeCycleAction).toBeTypeOf('function')

    modeCycleAction?.()
    await Promise.resolve()

    expect(cyclePrimaryAgentProfileMock).toHaveBeenCalledWith(1)
    expect(togglePlanModeMock).not.toHaveBeenCalled()
    expect(infoMock).toHaveBeenCalledWith('Primary Agent', 'architect')
  })

  it('cycles primary-agent profiles in reverse when requested', async () => {
    hasPrimaryAgentProfilesMock = true
    cyclePrimaryAgentProfileMock.mockResolvedValue('reviewer')

    const exportDialog = vi.fn()
    const checkpointDialog = vi.fn()
    const projectHubVisible = vi.fn()

    createRoot((dispose) => {
      registerAppShortcuts(exportDialog, checkpointDialog, projectHubVisible)
      dispose()
    })

    const reverseAction = registerActionMock.mock.calls.find(
      ([id]) => id === 'mode-cycle-reverse'
    )?.[1]
    expect(reverseAction).toBeTypeOf('function')

    reverseAction?.()
    await Promise.resolve()

    expect(cyclePrimaryAgentProfileMock).toHaveBeenCalledWith(-1)
    expect(infoMock).toHaveBeenCalledWith('Primary Agent', 'reviewer')
  })

  it('shows an error notification when primary-agent cycling fails', async () => {
    hasPrimaryAgentProfilesMock = true
    cyclePrimaryAgentProfileMock.mockRejectedValue(new Error('Cannot switch while running'))

    const exportDialog = vi.fn()
    const checkpointDialog = vi.fn()
    const projectHubVisible = vi.fn()

    createRoot((dispose) => {
      registerAppShortcuts(exportDialog, checkpointDialog, projectHubVisible)
      dispose()
    })

    const modeCycleAction = registerActionMock.mock.calls.find(([id]) => id === 'mode-cycle')?.[1]
    expect(modeCycleAction).toBeTypeOf('function')

    modeCycleAction?.()
    await Promise.resolve()
    await Promise.resolve()

    expect(errorMock).toHaveBeenCalledWith('Primary Agent', 'Cannot switch while running')
  })
})
