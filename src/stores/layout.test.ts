import { afterEach, beforeEach, describe, expect, it } from 'vitest'

// Layout store uses module-level signals that read localStorage on load.
// We clear localStorage before each test and re-import the module fresh.

// Helper: dynamically import a fresh copy of the layout store
async function loadLayout() {
  // Bust the module cache so signals re-initialize from localStorage
  const mod = await import('./layout')
  return mod.useLayout()
}

describe('layout store', () => {
  beforeEach(() => localStorage.clear())
  afterEach(() => localStorage.clear())

  // ============================================================================
  // handleActivityClick
  // ============================================================================

  describe('handleActivityClick', () => {
    it('toggles sidebar when clicking the active activity', async () => {
      const layout = await loadLayout()
      // Default activity is 'sessions', sidebar starts visible
      layout.setSidebarVisible(true)
      layout.setActiveActivity('sessions')

      layout.handleActivityClick('sessions')
      expect(layout.sidebarVisible()).toBe(false)

      layout.handleActivityClick('sessions')
      expect(layout.sidebarVisible()).toBe(true)
    })

    it('switches activity and ensures sidebar is visible', async () => {
      const layout = await loadLayout()
      layout.setActiveActivity('sessions')
      layout.setSidebarVisible(true)

      layout.handleActivityClick('explorer')
      expect(layout.activeActivity()).toBe('explorer')
      expect(layout.sidebarVisible()).toBe(true)
    })

    it('opens sidebar when switching activity while hidden', async () => {
      const layout = await loadLayout()
      layout.setActiveActivity('sessions')
      layout.setSidebarVisible(false)

      layout.handleActivityClick('explorer')
      expect(layout.activeActivity()).toBe('explorer')
      expect(layout.sidebarVisible()).toBe(true)
    })
  })

  // ============================================================================
  // toggleSidebar
  // ============================================================================

  describe('toggleSidebar', () => {
    it('flips sidebar visibility', async () => {
      const layout = await loadLayout()
      layout.setSidebarVisible(true)

      layout.toggleSidebar()
      expect(layout.sidebarVisible()).toBe(false)

      layout.toggleSidebar()
      expect(layout.sidebarVisible()).toBe(true)
    })
  })

  // ============================================================================
  // setSidebarWidth
  // ============================================================================

  describe('setSidebarWidth', () => {
    it('clamps below minimum to 180', async () => {
      const layout = await loadLayout()
      layout.setSidebarWidth(50)
      expect(layout.sidebarWidth()).toBe(180)
    })

    it('clamps above maximum to 480', async () => {
      const layout = await loadLayout()
      layout.setSidebarWidth(999)
      expect(layout.sidebarWidth()).toBe(480)
    })

    it('accepts values within range', async () => {
      const layout = await loadLayout()
      layout.setSidebarWidth(300)
      expect(layout.sidebarWidth()).toBe(300)
    })

    it('persists width to localStorage', async () => {
      const layout = await loadLayout()
      layout.setSidebarWidth(350)
      expect(localStorage.getItem('ava-sidebar-width')).toBe('350')
    })
  })

  // ============================================================================
  // Persistence
  // ============================================================================

  describe('persistence', () => {
    it('persists active activity to localStorage', async () => {
      const layout = await loadLayout()
      layout.setActiveActivity('explorer')
      expect(localStorage.getItem('ava-layout-activity')).toBe('explorer')
    })

    it('persists sidebar visibility to localStorage', async () => {
      const layout = await loadLayout()
      layout.setSidebarVisible(false)
      expect(localStorage.getItem('ava-layout-sidebar-visible')).toBe('false')
    })

    it('persists project hub visibility to localStorage', async () => {
      const layout = await loadLayout()
      layout.setProjectHubVisible(true)
      expect(localStorage.getItem('ava-layout-project-hub-visible')).toBe('true')
    })
  })

  describe('project hub controls', () => {
    it('opens, closes, and toggles project hub visibility', async () => {
      const layout = await loadLayout()

      layout.closeProjectHub()
      expect(layout.projectHubVisible()).toBe(false)

      layout.openProjectHub()
      expect(layout.projectHubVisible()).toBe(true)

      layout.toggleProjectHub()
      expect(layout.projectHubVisible()).toBe(false)
    })
  })

  // setupLayoutShortcuts moved to src/stores/shortcuts.ts
})
