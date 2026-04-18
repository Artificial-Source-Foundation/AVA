/**
 * Layout Store
 * Global state for IDE-like layout: activity bar, sidebar, panels, settings modal
 */

import { createSignal } from 'solid-js'
import { STORAGE_KEYS } from '../config/constants'
import { log } from '../lib/logger'
import * as dialogs from './layout-dialogs'
import { loadBool, loadNumber, loadString, save } from './layout-persistence'

// Re-export types so consumers keep importing from 'stores/layout'
export type { RightPanelTab } from './layout-dialogs'
export type ActivityId = 'sessions' | 'explorer'
export type BottomPanelTab = 'memory' | 'terminal' | 'output'

// ============================================================================
// Activity Bar
// ============================================================================

const [activeActivity, setActiveActivityRaw] = createSignal<ActivityId>(
  loadString(STORAGE_KEYS.LAYOUT_ACTIVITY, 'sessions') as ActivityId
)

function setActiveActivity(id: ActivityId): void {
  setActiveActivityRaw(id)
  save(STORAGE_KEYS.LAYOUT_ACTIVITY, id)
}

// ============================================================================
// Sidebar
// ============================================================================

const [sidebarVisible, setSidebarVisibleRaw] = createSignal(
  loadBool(STORAGE_KEYS.LAYOUT_SIDEBAR_VISIBLE, true)
)

function setSidebarVisible(visible: boolean): void {
  setSidebarVisibleRaw(visible)
  save(STORAGE_KEYS.LAYOUT_SIDEBAR_VISIBLE, String(visible))
}

function handleActivityClick(id: ActivityId): void {
  if (activeActivity() === id) {
    setSidebarVisible(!sidebarVisible())
  } else {
    setActiveActivity(id)
    if (!sidebarVisible()) {
      setSidebarVisible(true)
    }
  }
}

function toggleSidebar(): void {
  const next = !sidebarVisible()
  log.debug('nav', 'Sidebar toggled', { visible: next })
  setSidebarVisible(next)
}

const SIDEBAR_WIDTH_KEY = 'ava-sidebar-width'

const [sidebarWidth, setSidebarWidthRaw] = createSignal(
  loadNumber(SIDEBAR_WIDTH_KEY, 260, 180, 480)
)

function setSidebarWidth(w: number): void {
  const clamped = Math.min(480, Math.max(180, w))
  setSidebarWidthRaw(clamped)
  save(SIDEBAR_WIDTH_KEY, String(clamped))
}

// ============================================================================
// Right Panel
// ============================================================================

const [rightPanelVisible, setRightPanelVisibleRaw] = createSignal(
  loadBool(STORAGE_KEYS.LAYOUT_RIGHT_VISIBLE, false)
)

function setRightPanelVisible(visible: boolean): void {
  setRightPanelVisibleRaw(visible)
  save(STORAGE_KEYS.LAYOUT_RIGHT_VISIBLE, String(visible))
}

function toggleRightPanel(): void {
  setRightPanelVisible(!rightPanelVisible())
}

const RIGHT_PANEL_WIDTH_KEY = 'ava-right-panel-width'

const [rightPanelWidth, setRightPanelWidthRaw] = createSignal(
  loadNumber(RIGHT_PANEL_WIDTH_KEY, 320, 250, 600)
)

function setRightPanelWidth(w: number): void {
  const clamped = Math.min(600, Math.max(250, w))
  setRightPanelWidthRaw(clamped)
  save(RIGHT_PANEL_WIDTH_KEY, String(clamped))
}

function switchRightPanelTab(tab: dialogs.RightPanelTab): void {
  log.debug('nav', 'Right panel tab changed', { tab })
  const wasVisible = rightPanelVisible()
  dialogs.switchRightPanelTab(tab, () => {
    if (!wasVisible) setRightPanelVisible(true)
  })
}

// ============================================================================
// Bottom Panel
// ============================================================================

const [bottomPanelVisible, setBottomPanelVisibleRaw] = createSignal(
  loadBool(STORAGE_KEYS.LAYOUT_BOTTOM_VISIBLE, false)
)

function setBottomPanelVisible(visible: boolean): void {
  setBottomPanelVisibleRaw(visible)
  save(STORAGE_KEYS.LAYOUT_BOTTOM_VISIBLE, String(visible))
}

function toggleBottomPanel(): void {
  setBottomPanelVisible(!bottomPanelVisible())
}

const [bottomPanelHeight, setBottomPanelHeightRaw] = createSignal(
  loadNumber(STORAGE_KEYS.LAYOUT_BOTTOM_HEIGHT, 200, 100, 600)
)

function setBottomPanelHeight(h: number): void {
  const clamped = Math.min(600, Math.max(100, h))
  setBottomPanelHeightRaw(clamped)
  save(STORAGE_KEYS.LAYOUT_BOTTOM_HEIGHT, String(clamped))
}

const [bottomPanelTab, setBottomPanelTab] = createSignal<BottomPanelTab>(
  loadString('ava-bottom-panel-tab', 'memory') as BottomPanelTab
)

function switchBottomPanelTab(tab: BottomPanelTab): void {
  log.debug('nav', 'Bottom panel tab changed', { tab })
  setBottomPanelTab(tab)
  save('ava-bottom-panel-tab', tab)
  if (!bottomPanelVisible()) setBottomPanelVisible(true)
}

// ============================================================================
// Subagent Detail View
// ============================================================================

/** Tool call ID of the subagent currently being viewed (null = normal chat) */
const [viewingSubagentId, setViewingSubagentIdRaw] = createSignal<string | null>(null)

function openSubagentDetail(toolCallId: string): void {
  log.debug('nav', 'Subagent detail opened', { toolCallId })
  setViewingSubagentIdRaw(toolCallId)
}

function closeSubagentDetail(): void {
  log.debug('nav', 'Subagent detail closed')
  setViewingSubagentIdRaw(null)
}

// ============================================================================
// Plan Full-Screen Viewer
// ============================================================================

/** Plan ID currently being viewed in full-screen Plannotator mode (null = not viewing) */
const [viewingPlanId, setViewingPlanIdRaw] = createSignal<string | null>(null)

function openPlanViewer(planId: string): void {
  log.debug('nav', 'Plan viewer opened', { planId })
  setViewingPlanIdRaw(planId)
}

function closePlanViewer(): void {
  log.debug('nav', 'Plan viewer closed')
  setViewingPlanIdRaw(null)
}

// ============================================================================
// Code Editor
// ============================================================================

const [codeEditorFile, setCodeEditorFileRaw] = createSignal<string | null>(null)

function openCodeEditor(filePath: string): void {
  setCodeEditorFileRaw(filePath)
}

function closeCodeEditor(): void {
  setCodeEditorFileRaw(null)
}

// ============================================================================
// Dashboard
// ============================================================================

const [dashboardVisible, setDashboardVisibleRaw] = createSignal(false)

function setDashboardVisible(visible: boolean): void {
  setDashboardVisibleRaw(visible)
}

function openDashboard(): void {
  log.debug('nav', 'Dashboard opened')
  setDashboardVisible(true)
}

function closeDashboard(): void {
  log.debug('nav', 'Dashboard closed')
  setDashboardVisible(false)
}

function toggleDashboard(): void {
  setDashboardVisible(!dashboardVisible())
}

// ============================================================================
// Project Hub
// ============================================================================

const [projectHubVisible, setProjectHubVisibleRaw] = createSignal(
  loadBool(STORAGE_KEYS.LAYOUT_PROJECT_HUB_VISIBLE, false)
)

function setProjectHubVisible(visible: boolean): void {
  setProjectHubVisibleRaw(visible)
  save(STORAGE_KEYS.LAYOUT_PROJECT_HUB_VISIBLE, String(visible))
}

function openProjectHub(): void {
  log.debug('nav', 'Project hub opened')
  setProjectHubVisible(true)
}

function closeProjectHub(): void {
  log.debug('nav', 'Project hub closed')
  setProjectHubVisible(false)
}

function toggleProjectHub(): void {
  setProjectHubVisible(!projectHubVisible())
}

// ============================================================================
// Export Hook
// ============================================================================

export function useLayout() {
  return {
    // Activity bar
    activeActivity,
    setActiveActivity,
    handleActivityClick,

    // Sidebar
    sidebarVisible,
    setSidebarVisible,
    toggleSidebar,
    sidebarWidth,
    setSidebarWidth,

    // Expanded editor
    expandedEditorOpen: dialogs.expandedEditorOpen,
    setExpandedEditorOpen: dialogs.setExpandedEditorOpen,
    toggleExpandedEditor: dialogs.toggleExpandedEditor,

    // Right panel
    rightPanelVisible,
    setRightPanelVisible,
    toggleRightPanel,
    rightPanelWidth,
    setRightPanelWidth,

    // Bottom panel
    bottomPanelVisible,
    setBottomPanelVisible,
    toggleBottomPanel,
    bottomPanelHeight,
    setBottomPanelHeight,
    bottomPanelTab,
    switchBottomPanelTab,

    // Code editor
    codeEditorFile,
    openCodeEditor,
    closeCodeEditor,

    // Settings modal
    settingsOpen: dialogs.settingsOpen,
    openSettings: dialogs.openSettings,
    closeSettings: dialogs.closeSettings,
    toggleSettings: dialogs.toggleSettings,

    // Dashboard
    dashboardVisible,
    setDashboardVisible,
    openDashboard,
    closeDashboard,
    toggleDashboard,

    // Project hub
    projectHubVisible,
    setProjectHubVisible,
    openProjectHub,
    closeProjectHub,
    toggleProjectHub,

    // Model browser
    modelBrowserOpen: dialogs.modelBrowserOpen,
    modelBrowserRequest: dialogs.modelBrowserRequest,
    openModelBrowser: dialogs.openModelBrowser,
    closeModelBrowser: dialogs.closeModelBrowser,
    toggleModelBrowser: dialogs.toggleModelBrowser,

    // Right panel tab
    rightPanelTab: dialogs.rightPanelTab,
    switchRightPanelTab,

    // Quick model picker
    quickModelPickerOpen: dialogs.quickModelPickerOpen,
    setQuickModelPickerOpen: dialogs.setQuickModelPickerOpen,
    toggleQuickModelPicker: dialogs.toggleQuickModelPicker,

    // Session switcher
    sessionSwitcherOpen: dialogs.sessionSwitcherOpen,
    setSessionSwitcherOpen: dialogs.setSessionSwitcherOpen,
    toggleSessionSwitcher: dialogs.toggleSessionSwitcher,

    // Chat search
    chatSearchOpen: dialogs.chatSearchOpen,
    openChatSearch: dialogs.openChatSearch,
    closeChatSearch: dialogs.closeChatSearch,
    toggleChatSearch: dialogs.toggleChatSearch,

    // Subagent detail view
    viewingSubagentId,
    openSubagentDetail,
    closeSubagentDetail,

    // Plan full-screen viewer
    viewingPlanId,
    openPlanViewer,
    closePlanViewer,
  }
}
