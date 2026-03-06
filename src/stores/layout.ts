/**
 * Layout Store
 * Global state for IDE-like layout: activity bar, sidebar, panels, settings modal
 */

import { createSignal } from 'solid-js'
import { STORAGE_KEYS } from '../config/constants'
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
  setSidebarVisible(!sidebarVisible())
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
  dialogs.switchRightPanelTab(tab, () => {
    if (!rightPanelVisible()) setRightPanelVisible(true)
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
  setBottomPanelTab(tab)
  save('ava-bottom-panel-tab', tab)
  if (!bottomPanelVisible()) setBottomPanelVisible(true)
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
  setProjectHubVisible(true)
}

function closeProjectHub(): void {
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

    // Project hub
    projectHubVisible,
    setProjectHubVisible,
    openProjectHub,
    closeProjectHub,
    toggleProjectHub,

    // Model browser
    modelBrowserOpen: dialogs.modelBrowserOpen,
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
  }
}
