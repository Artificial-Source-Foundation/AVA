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
  loadNumber(STORAGE_KEYS.LAYOUT_BOTTOM_HEIGHT, 200, 100, 400)
)

function setBottomPanelHeight(h: number) {
  const clamped = Math.min(400, Math.max(100, h))
  setBottomPanelHeightRaw(clamped)
  save(STORAGE_KEYS.LAYOUT_BOTTOM_HEIGHT, String(clamped))
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

    // Right panel (agent activity)
    rightPanelVisible,
    setRightPanelVisible,
    toggleRightPanel,

    // Bottom panel (memory)
    bottomPanelVisible,
    setBottomPanelVisible,
    toggleBottomPanel,
    bottomPanelHeight,
    setBottomPanelHeight,

    // Code editor
    codeEditorFile,
    openCodeEditor,
    closeCodeEditor,

    // Settings modal
    settingsOpen,
    openSettings,
    closeSettings,
    toggleSettings,
  }
}
