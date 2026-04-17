import * as tauriCore from '@tauri-apps/api/core'
import { render } from 'solid-js/web'
import { beforeEach, describe, expect, it, vi } from 'vitest'

// Mock stores before importing AppShell
vi.mock('../../stores/layout', () => ({
  useLayout: () => ({
    sidebarVisible: () => true,
    sidebarWidth: () => 256,
    setSidebarWidth: vi.fn(),
    rightPanelWidth: () => 300,
    setRightPanelWidth: vi.fn(),
    bottomPanelVisible: () => false,
    setBottomPanelVisible: vi.fn(),
    bottomPanelHeight: () => 200,
    setBottomPanelHeight: vi.fn(),
    bottomPanelTab: () => 'memory',
    switchBottomPanelTab: vi.fn(),
    settingsOpen: () => false,
    openSettings: vi.fn(),
    closeSettings: vi.fn(),
  }),
}))

vi.mock('../../stores/planOverlayStore', () => ({
  usePlanOverlay: () => ({
    isOpen: () => false,
  }),
}))

vi.mock('../../stores/settings', () => ({
  useSettings: () => ({
    settings: () => ({
      ui: { showBottomPanel: true },
    }),
  }),
}))

vi.mock('../../stores/project', () => ({
  useProject: () => ({
    currentProject: () => null,
  }),
}))

vi.mock('../../stores/session', () => ({
  useSession: () => ({
    currentSession: () => null,
  }),
}))

// Mock child components
vi.mock('./MainArea', () => ({
  MainArea: () => <div data-testid="main-area">Main Area</div>,
}))

vi.mock('./TitleBar', () => ({
  TitleBar: () => (
    <div data-testid="title-bar" role="toolbar">
      Title Bar
    </div>
  ),
}))

vi.mock('./SidebarPanel', () => ({
  SidebarPanel: () => <div data-testid="sidebar">Sidebar</div>,
}))

vi.mock('./RightPanel', () => ({
  RightPanel: () => <div data-testid="right-panel">Right Panel</div>,
}))

vi.mock('../chat/PlanOverlay', () => ({
  PlanOverlay: () => <div data-testid="plan-overlay">Plan Overlay</div>,
}))

vi.mock('../settings', () => ({
  SettingsModal: () => null,
}))

vi.mock('../sidebar/SidebarMemory', () => ({
  SidebarMemory: () => <div>Sidebar Memory</div>,
}))

vi.mock('../ui/PanelErrorBoundary', () => ({
  PanelErrorBoundary: (props: { children: unknown }) => props.children,
}))

vi.mock('./useResizeHandlers', () => ({
  createResizeHandlers: () => ({
    startSidebarResize: vi.fn(),
    startRightResize: vi.fn(),
    startBottomResize: vi.fn(),
  }),
}))

import { AppShell } from './AppShell'

describe('AppShell', () => {
  let container: HTMLDivElement

  beforeEach(() => {
    container = document.createElement('div')
    document.body.appendChild(container)
  })

  it('should NOT render TitleBar in web mode (isTauri returns false)', () => {
    // Default stub behavior: isTauri() returns false (web mode)
    render(() => <AppShell />, container)

    const titleBar = container.querySelector('[data-testid="title-bar"]')
    expect(titleBar).toBeNull()
  })

  it('should render TitleBar in desktop/Tauri mode (isTauri returns true)', () => {
    // Override stub to simulate Tauri mode
    vi.spyOn(tauriCore, 'isTauri').mockReturnValue(true)

    render(() => <AppShell />, container)

    const titleBar = container.querySelector('[data-testid="title-bar"]')
    expect(titleBar).not.toBeNull()

    // Reset mock after test
    vi.restoreAllMocks()
  })
})
