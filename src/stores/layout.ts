/**
 * Layout Store
 * Global state for IDE-like layout: activity bar + sidebar
 */

import { createSignal } from 'solid-js'
import { STORAGE_KEYS } from '../config/constants'

// ============================================================================
// Types
// ============================================================================

export type ActivityId =
  | 'sessions'
  | 'explorer'
  | 'agents'
  | 'team'
  | 'memory'
  | 'activity'
  | 'plugins'

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
  loadString(STORAGE_KEYS.LAYOUT_ACTIVITY, 'sessions')
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

const SIDEBAR_WIDTH_KEY = 'estela-sidebar-width'

function loadSidebarWidth(): number {
  try {
    const raw = localStorage.getItem(SIDEBAR_WIDTH_KEY)
    if (raw) {
      const n = Number(raw)
      if (n >= 180 && n <= 480) return n
    }
  } catch {
    /* ignore */
  }
  return 260
}

const [sidebarWidth, setSidebarWidthRaw] = createSignal(loadSidebarWidth())

function setSidebarWidth(w: number) {
  const clamped = Math.min(480, Math.max(180, w))
  setSidebarWidthRaw(clamped)
  try {
    localStorage.setItem(SIDEBAR_WIDTH_KEY, String(clamped))
  } catch {
    /* ignore */
  }
}

// ============================================================================
// Keyboard Shortcuts
// ============================================================================

function setupLayoutShortcuts() {
  const handler = (e: KeyboardEvent) => {
    const mod = e.ctrlKey || e.metaKey

    // Ctrl+B → toggle sidebar
    if (mod && e.key === 'b') {
      e.preventDefault()
      toggleSidebar()
    }
  }

  document.addEventListener('keydown', handler)
  // eslint-disable-next-line solid/reactivity -- cleanup function returned from non-tracked scope
  return () => document.removeEventListener('keydown', handler)
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

    // Keyboard shortcuts (call in onMount, returns cleanup)
    setupLayoutShortcuts,
  }
}
