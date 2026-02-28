/**
 * Layout Store
 * Global state for IDE-like layout: activity bar, sidebar, panels, settings modal
 */

import { createSignal } from 'solid-js'
import { STORAGE_KEYS } from '../config/constants'

// ============================================================================
// Types
// ============================================================================

export type ActivityId = 'sessions' | 'explorer'

// ============================================================================
// Persistence Helpers
// ============================================================================

function loadString<T extends string>(key: string, fallback: T): T {
  try {
    const raw = localStorage.getItem(key)
    if (raw) return raw as T
  } catch {
    /* ignore */
  }
  return fallback
}

function loadBool(key: string, fallback: boolean): boolean {
  try {
    const raw = localStorage.getItem(key)
    if (raw !== null) return raw === 'true'
  } catch {
    /* ignore */
  }
  return fallback
}

function loadNumber(key: string, fallback: number, min: number, max: number): number {
  try {
    const raw = localStorage.getItem(key)
    if (raw) {
      const n = Number(raw)
      if (n >= min && n <= max) return n
    }
  } catch {
    /* ignore */
  }
  return fallback
}

function save(key: string, value: string) {
  try {
    localStorage.setItem(key, value)
  } catch {
    /* ignore */
  }
}

// ============================================================================
// Activity Bar State
// ============================================================================

const [activeActivity, setActiveActivityRaw] = createSignal<ActivityId>(
  loadString(STORAGE_KEYS.LAYOUT_ACTIVITY, 'sessions') as ActivityId
)

function setActiveActivity(id: ActivityId) {
  setActiveActivityRaw(id)
  save(STORAGE_KEYS.LAYOUT_ACTIVITY, id)
}

// ============================================================================
// Sidebar Visibility
// ============================================================================

const [sidebarVisible, setSidebarVisibleRaw] = createSignal(
  loadBool(STORAGE_KEYS.LAYOUT_SIDEBAR_VISIBLE, true)
)

function setSidebarVisible(visible: boolean) {
  setSidebarVisibleRaw(visible)
  save(STORAGE_KEYS.LAYOUT_SIDEBAR_VISIBLE, String(visible))
}

/**
 * Toggle sidebar visibility.
 * If clicking the already-active activity icon, collapse/expand the sidebar (VS Code behavior).
 * If clicking a different icon, switch to it and ensure sidebar is visible.
 */
function handleActivityClick(id: ActivityId) {
  if (activeActivity() === id) {
    setSidebarVisible(!sidebarVisible())
  } else {
    setActiveActivity(id)
    if (!sidebarVisible()) {
      setSidebarVisible(true)
    }
  }
}

function toggleSidebar() {
  setSidebarVisible(!sidebarVisible())
}

// ============================================================================
// Sidebar Width (persisted)
// ============================================================================

const SIDEBAR_WIDTH_KEY = 'ava-sidebar-width'

const [sidebarWidth, setSidebarWidthRaw] = createSignal(
  loadNumber(SIDEBAR_WIDTH_KEY, 260, 180, 480)
)

function setSidebarWidth(w: number) {
  const clamped = Math.min(480, Math.max(180, w))
  setSidebarWidthRaw(clamped)
  save(SIDEBAR_WIDTH_KEY, String(clamped))
}

// ============================================================================
// Right Panel (Agent Activity)
// ============================================================================

const [rightPanelVisible, setRightPanelVisibleRaw] = createSignal(
  loadBool(STORAGE_KEYS.LAYOUT_RIGHT_VISIBLE, false)
)

function setRightPanelVisible(visible: boolean) {
  setRightPanelVisibleRaw(visible)
  save(STORAGE_KEYS.LAYOUT_RIGHT_VISIBLE, String(visible))
}

function toggleRightPanel() {
  setRightPanelVisible(!rightPanelVisible())
}

// ============================================================================
// Bottom Panel (Memory/Context)
// ============================================================================

const [bottomPanelVisible, setBottomPanelVisibleRaw] = createSignal(
  loadBool(STORAGE_KEYS.LAYOUT_BOTTOM_VISIBLE, false)
)

function setBottomPanelVisible(visible: boolean) {
  setBottomPanelVisibleRaw(visible)
  save(STORAGE_KEYS.LAYOUT_BOTTOM_VISIBLE, String(visible))
}

function toggleBottomPanel() {
  setBottomPanelVisible(!bottomPanelVisible())
}

const [bottomPanelHeight, setBottomPanelHeightRaw] = createSignal(
  loadNumber(STORAGE_KEYS.LAYOUT_BOTTOM_HEIGHT, 200, 100, 600)
)

function setBottomPanelHeight(h: number) {
  const clamped = Math.min(600, Math.max(100, h))
  setBottomPanelHeightRaw(clamped)
  save(STORAGE_KEYS.LAYOUT_BOTTOM_HEIGHT, String(clamped))
}

// ============================================================================
// Bottom Panel Tab
// ============================================================================

export type BottomPanelTab = 'memory' | 'terminal' | 'output'

const [bottomPanelTab, setBottomPanelTab] = createSignal<BottomPanelTab>(
  loadString('ava-bottom-panel-tab', 'memory') as BottomPanelTab
)

function switchBottomPanelTab(tab: BottomPanelTab) {
  setBottomPanelTab(tab)
  save('ava-bottom-panel-tab', tab)
  // Also ensure the panel is visible
  if (!bottomPanelVisible()) setBottomPanelVisible(true)
}

// ============================================================================
// Code Editor Panel
// ============================================================================

const [codeEditorFile, setCodeEditorFileRaw] = createSignal<string | null>(null)

function openCodeEditor(filePath: string) {
  setCodeEditorFileRaw(filePath)
}

function closeCodeEditor() {
  setCodeEditorFileRaw(null)
}

// ============================================================================
// Settings Modal
// ============================================================================

const [settingsOpen, setSettingsOpen] = createSignal(false)

// ============================================================================
// Model Browser Dialog
// ============================================================================

const [modelBrowserOpen, setModelBrowserOpen] = createSignal(false)

function openModelBrowser() {
  setModelBrowserOpen(true)
}

function closeModelBrowser() {
  setModelBrowserOpen(false)
}

function toggleModelBrowser() {
  setModelBrowserOpen(!modelBrowserOpen())
}

// ============================================================================
// Project Hub Visibility
// ============================================================================

const [projectHubVisible, setProjectHubVisibleRaw] = createSignal(
  loadBool(STORAGE_KEYS.LAYOUT_PROJECT_HUB_VISIBLE, false)
)

function setProjectHubVisible(visible: boolean) {
  setProjectHubVisibleRaw(visible)
  save(STORAGE_KEYS.LAYOUT_PROJECT_HUB_VISIBLE, String(visible))
}

function openProjectHub() {
  setProjectHubVisible(true)
}

function closeProjectHub() {
  setProjectHubVisible(false)
}

function toggleProjectHub() {
  setProjectHubVisible(!projectHubVisible())
}

function openSettings() {
  setSettingsOpen(true)
}

function closeSettings() {
  setSettingsOpen(false)
}

function toggleSettings() {
  setSettingsOpen(!settingsOpen())
}

// ============================================================================
// Right Panel Tab
// ============================================================================

export type RightPanelTab = 'activity' | 'files' | 'review'

const [rightPanelTab, setRightPanelTab] = createSignal<RightPanelTab>(
  loadString('ava-right-panel-tab', 'activity') as RightPanelTab
)

function switchRightPanelTab(tab: RightPanelTab) {
  setRightPanelTab(tab)
  save('ava-right-panel-tab', tab)
  // Also ensure the panel is visible
  if (!rightPanelVisible()) setRightPanelVisible(true)
}

// ============================================================================
// Expanded Editor
// ============================================================================

const [expandedEditorOpen, setExpandedEditorOpen] = createSignal(false)

function toggleExpandedEditor() {
  setExpandedEditorOpen(!expandedEditorOpen())
}

// ============================================================================
// Right Panel Width (persisted)
// ============================================================================

const RIGHT_PANEL_WIDTH_KEY = 'ava-right-panel-width'

const [rightPanelWidth, setRightPanelWidthRaw] = createSignal(
  loadNumber(RIGHT_PANEL_WIDTH_KEY, 320, 250, 600)
)

function setRightPanelWidth(w: number) {
  const clamped = Math.min(600, Math.max(250, w))
  setRightPanelWidthRaw(clamped)
  save(RIGHT_PANEL_WIDTH_KEY, String(clamped))
}

// ============================================================================
// Quick Model Picker
// ============================================================================

const [quickModelPickerOpen, setQuickModelPickerOpen] = createSignal(false)

function toggleQuickModelPicker() {
  setQuickModelPickerOpen(!quickModelPickerOpen())
}

// ============================================================================
// Session Switcher
// ============================================================================

const [sessionSwitcherOpen, setSessionSwitcherOpen] = createSignal(false)

function toggleSessionSwitcher() {
  setSessionSwitcherOpen(!sessionSwitcherOpen())
}

// ============================================================================
// Chat Search
// ============================================================================

const [chatSearchOpen, setChatSearchOpen] = createSignal(false)

function openChatSearch() {
  setChatSearchOpen(true)
}

function closeChatSearch() {
  setChatSearchOpen(false)
}

function toggleChatSearch() {
  setChatSearchOpen(!chatSearchOpen())
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

    // Sidebar width
    sidebarWidth,
    setSidebarWidth,

    // Expanded editor
    expandedEditorOpen,
    setExpandedEditorOpen,
    toggleExpandedEditor,

    // Right panel (agent activity)
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
    settingsOpen,
    openSettings,
    closeSettings,
    toggleSettings,

    // Project hub
    projectHubVisible,
    setProjectHubVisible,
    openProjectHub,
    closeProjectHub,
    toggleProjectHub,

    // Model browser
    modelBrowserOpen,
    openModelBrowser,
    closeModelBrowser,
    toggleModelBrowser,

    // Right panel tab
    rightPanelTab,
    switchRightPanelTab,

    // Quick model picker
    quickModelPickerOpen,
    setQuickModelPickerOpen,
    toggleQuickModelPicker,

    // Session switcher
    sessionSwitcherOpen,
    setSessionSwitcherOpen,
    toggleSessionSwitcher,

    // Chat search
    chatSearchOpen,
    openChatSearch,
    closeChatSearch,
    toggleChatSearch,
  }
}
