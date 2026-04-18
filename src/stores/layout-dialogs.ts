/**
 * Layout Dialogs & Overlays
 * Simple boolean-toggled dialog/overlay signals that don't depend on
 * other layout state. Each follows the pattern: signal + open/close/toggle.
 */

import { type Accessor, createSignal } from 'solid-js'
import type { LLMProviderConfig } from '../config/defaults/provider-defaults'
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

export interface ModelBrowserRequest {
  selectedModel: Accessor<string>
  selectedProvider?: Accessor<string | null>
  enabledProviders: Accessor<LLMProviderConfig[]>
  onSelect: (modelId: string, providerId: string) => void
}

const [modelBrowserOpen, setModelBrowserOpen] = createSignal(false)
const [modelBrowserRequest, setModelBrowserRequest] = createSignal<ModelBrowserRequest | null>(null)

export function openModelBrowser(request?: ModelBrowserRequest): void {
  setModelBrowserRequest(request ?? null)
  setModelBrowserOpen(true)
}

export function closeModelBrowser(): void {
  setModelBrowserOpen(false)
  setModelBrowserRequest(null)
}

export function toggleModelBrowser(): void {
  setModelBrowserOpen(!modelBrowserOpen())
}

export { modelBrowserOpen, modelBrowserRequest }

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

export type RightPanelTab = 'activity' | 'files' | 'review' | 'trajectory' | 'todos' | 'changes'

const RIGHT_PANEL_TABS: readonly RightPanelTab[] = [
  'activity',
  'files',
  'review',
  'trajectory',
  'todos',
  'changes',
]

function normalizeRightPanelTab(raw: string | null): RightPanelTab {
  // Legacy/de-cored values (for example "team") should not re-surface in core UI.
  if (!raw) return 'changes'
  return RIGHT_PANEL_TABS.includes(raw as RightPanelTab) ? (raw as RightPanelTab) : 'changes'
}

const [rightPanelTab, setRightPanelTabRaw] = createSignal<RightPanelTab>(
  normalizeRightPanelTab(loadString('ava-right-panel-tab', 'changes'))
)

/** Switch right panel tab. Caller must ensure panel is visible. */
export function switchRightPanelTab(tab: RightPanelTab, ensureVisible: () => void): void {
  setRightPanelTabRaw(tab)
  save('ava-right-panel-tab', tab)
  ensureVisible()
}

export { rightPanelTab }
