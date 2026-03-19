/**
 * Layout Dialogs & Overlays
 * Simple boolean-toggled dialog/overlay signals that don't depend on
 * other layout state. Each follows the pattern: signal + open/close/toggle.
 */

import { createSignal } from 'solid-js'
import { log } from '../lib/logger'
import { loadString, save } from './layout-persistence'

// ============================================================================
// Settings Modal
// ============================================================================

const [settingsOpen, setSettingsOpen] = createSignal(false)

export function openSettings(): void {
  log.debug('nav', 'Settings opened')
  setSettingsOpen(true)
}

export function closeSettings(): void {
  log.debug('nav', 'Settings closed')
  setSettingsOpen(false)
}

export function toggleSettings(): void {
  setSettingsOpen(!settingsOpen())
}

export { settingsOpen }

// ============================================================================
// Model Browser
// ============================================================================

const [modelBrowserOpen, setModelBrowserOpen] = createSignal(false)

export function openModelBrowser(): void {
  setModelBrowserOpen(true)
}

export function closeModelBrowser(): void {
  setModelBrowserOpen(false)
}

export function toggleModelBrowser(): void {
  setModelBrowserOpen(!modelBrowserOpen())
}

export { modelBrowserOpen }

// ============================================================================
// Quick Model Picker
// ============================================================================

const [quickModelPickerOpen, setQuickModelPickerOpen] = createSignal(false)

export function toggleQuickModelPicker(): void {
  setQuickModelPickerOpen(!quickModelPickerOpen())
}

export { quickModelPickerOpen, setQuickModelPickerOpen }

// ============================================================================
// Session Switcher
// ============================================================================

const [sessionSwitcherOpen, setSessionSwitcherOpen] = createSignal(false)

export function toggleSessionSwitcher(): void {
  setSessionSwitcherOpen(!sessionSwitcherOpen())
}

export { sessionSwitcherOpen, setSessionSwitcherOpen }

// ============================================================================
// Chat Search
// ============================================================================

const [chatSearchOpen, setChatSearchOpen] = createSignal(false)

export function openChatSearch(): void {
  setChatSearchOpen(true)
}

export function closeChatSearch(): void {
  setChatSearchOpen(false)
}

export function toggleChatSearch(): void {
  setChatSearchOpen(!chatSearchOpen())
}

export { chatSearchOpen }

// ============================================================================
// Expanded Editor
// ============================================================================

const [expandedEditorOpen, setExpandedEditorOpen] = createSignal(false)

export function toggleExpandedEditor(): void {
  setExpandedEditorOpen(!expandedEditorOpen())
}

export { expandedEditorOpen, setExpandedEditorOpen }

// ============================================================================
// Right Panel Tab
// ============================================================================

export type RightPanelTab = 'activity' | 'files' | 'review' | 'trajectory' | 'team'

const [rightPanelTab, setRightPanelTabRaw] = createSignal<RightPanelTab>(
  loadString('ava-right-panel-tab', 'activity') as RightPanelTab
)

/** Switch right panel tab. Caller must ensure panel is visible. */
export function switchRightPanelTab(tab: RightPanelTab, ensureVisible: () => void): void {
  setRightPanelTabRaw(tab)
  save('ava-right-panel-tab', tab)
  ensureVisible()
}

export { rightPanelTab }
