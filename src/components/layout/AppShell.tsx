/**
 * App Shell - Main Layout Container
 *
 * Layout: Activity Bar | Sidebar | Main Area (+ Bottom Panel) | Right Panel
 * Settings is rendered as a modal overlay.
 * Sidebar uses CSS width transition for smooth open/close.
 * Resize handles allow drag-to-resize for sidebar and bottom panel.
 */

import { Bot, Brain, FolderOpen, GitCompareArrows, ScrollText, Terminal, X } from 'lucide-solid'
import { type Component, lazy, Show } from 'solid-js'
import { useLayout } from '../../stores/layout'
import { useSettings } from '../../stores/settings'
import { AgentActivityPanel } from '../panels/AgentActivityPanel'
import { DiffReviewPanel } from '../panels/DiffReviewPanel'
import { FileOperationsPanel } from '../panels/FileOperationsPanel'
import { TerminalPanel } from '../panels/TerminalPanel'
import { SettingsModal } from '../settings'
import { SidebarMemory } from '../sidebar/SidebarMemory'
import { ActivityBar } from './ActivityBar'
import { MainArea } from './MainArea'
import { SidebarPanel } from './SidebarPanel'
import { StatusBar } from './StatusBar'

const XTerminal = lazy(() => import('../panels/XTerminal').then((m) => ({ default: m.XTerminal })))

export const AppShell: Component = () => {
  const {
    sidebarVisible,
    sidebarWidth,
    setSidebarWidth,
    rightPanelVisible,
    setRightPanelVisible,
    rightPanelTab,
    switchRightPanelTab,
    rightPanelWidth,
    setRightPanelWidth,
    bottomPanelVisible,
    setBottomPanelVisible,
    bottomPanelHeight,
    setBottomPanelHeight,
    bottomPanelTab,
    switchBottomPanelTab,
  } = useLayout()
  const { settings } = useSettings()

  // Sidebar resize handler
  const startSidebarResize = (e: MouseEvent) => {
    e.preventDefault()
    const startX = e.clientX
    const startWidth = sidebarWidth()

    const onMove = (moveEvent: MouseEvent) => {
      const delta = moveEvent.clientX - startX
      setSidebarWidth(startWidth + delta)
    }

    const onUp = () => {
      document.removeEventListener('mousemove', onMove)
      document.removeEventListener('mouseup', onUp)
    }

    document.addEventListener('mousemove', onMove)
    document.addEventListener('mouseup', onUp)
  }

  // Right panel resize handler
  const startRightResize = (e: MouseEvent) => {
    e.preventDefault()
    const startX = e.clientX
    const startWidth = rightPanelWidth()

    const onMove = (moveEvent: MouseEvent) => {
      const delta = startX - moveEvent.clientX
      setRightPanelWidth(startWidth + delta)
    }

    const onUp = () => {
      document.removeEventListener('mousemove', onMove)
      document.removeEventListener('mouseup', onUp)
    }

    document.addEventListener('mousemove', onMove)
    document.addEventListener('mouseup', onUp)
  }

  // Bottom panel resize handler
  const startBottomResize = (e: MouseEvent) => {
    e.preventDefault()
    const startY = e.clientY
    const startHeight = bottomPanelHeight()

    const onMove = (moveEvent: MouseEvent) => {
      const delta = startY - moveEvent.clientY
      setBottomPanelHeight(startHeight + delta)
    }

    const onUp = () => {
      document.removeEventListener('mousemove', onMove)
      document.removeEventListener('mouseup', onUp)
    }

    document.addEventListener('mousemove', onMove)
    document.addEventListener('mouseup', onUp)
  }

  return (
    <div class="h-screen flex flex-col text-[var(--text-primary)] overflow-hidden">
      <StatusBar />

      <div class="flex-1 flex overflow-hidden">
        <ActivityBar />

        {/* Sidebar — width transition with overflow:hidden (no bleed, smooth) */}
        <div
          class="flex-shrink-0 overflow-hidden"
          style={{
            width: sidebarVisible() ? `${sidebarWidth()}px` : '0px',
            transition: 'width 120ms ease',
          }}
        >
          <SidebarPanel />
        </div>

        {/* Sidebar resize handle */}
        <Show when={sidebarVisible()}>
          {/* biome-ignore lint/a11y/noStaticElementInteractions: resize handle uses mouse-only interaction by design */}
          <div
            class="
              w-[3px] flex-shrink-0 cursor-col-resize
              bg-transparent hover:bg-[var(--accent-muted)]
              active:bg-[var(--accent)]
              transition-colors duration-150
            "
            onMouseDown={startSidebarResize}
          />
        </Show>

        {/* Center: Main content + optional bottom panel */}
        <div class="flex-1 flex flex-col overflow-hidden min-w-0">
          {/* Main content */}
          <div class="flex-1 overflow-hidden min-h-0">
            <MainArea />
          </div>

          {/* Bottom panel (Memory / Terminal / Output) — gated on ui.showBottomPanel */}
          <Show when={settings().ui.showBottomPanel && bottomPanelVisible()}>
            {/* biome-ignore lint/a11y/noStaticElementInteractions: resize handle uses mouse-only interaction by design */}
            <div
              class="
                h-[3px] flex-shrink-0 cursor-row-resize
                bg-transparent hover:bg-[var(--accent-muted)]
                active:bg-[var(--accent)]
                transition-colors duration-150
              "
              onMouseDown={startBottomResize}
            />
            <div
              class="flex-shrink-0 overflow-hidden border-t border-[var(--border-subtle)]"
              style={{ height: `${bottomPanelHeight()}px`, 'max-height': '600px' }}
            >
              <div class="flex flex-col h-full bg-[var(--gray-1)]">
                {/* Tab header */}
                <div class="flex items-center h-8 flex-shrink-0 border-b border-[var(--border-subtle)]">
                  <button
                    type="button"
                    onClick={() => switchBottomPanelTab('memory')}
                    class="flex items-center gap-1.5 px-3 h-full text-[10px] font-semibold uppercase tracking-wider transition-colors"
                    classList={{
                      'text-[var(--accent)] border-b border-[var(--accent)]':
                        bottomPanelTab() === 'memory',
                      'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                        bottomPanelTab() !== 'memory',
                    }}
                  >
                    <Brain class="w-3 h-3" />
                    Memory
                  </button>
                  <button
                    type="button"
                    onClick={() => switchBottomPanelTab('terminal')}
                    class="flex items-center gap-1.5 px-3 h-full text-[10px] font-semibold uppercase tracking-wider transition-colors"
                    classList={{
                      'text-[var(--accent)] border-b border-[var(--accent)]':
                        bottomPanelTab() === 'terminal',
                      'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                        bottomPanelTab() !== 'terminal',
                    }}
                  >
                    <Terminal class="w-3 h-3" />
                    Terminal
                  </button>
                  <button
                    type="button"
                    onClick={() => switchBottomPanelTab('output')}
                    class="flex items-center gap-1.5 px-3 h-full text-[10px] font-semibold uppercase tracking-wider transition-colors"
                    classList={{
                      'text-[var(--accent)] border-b border-[var(--accent)]':
                        bottomPanelTab() === 'output',
                      'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                        bottomPanelTab() !== 'output',
                    }}
                  >
                    <ScrollText class="w-3 h-3" />
                    Output
                  </button>
                  <div class="flex-1" />
                  <button
                    type="button"
                    onClick={() => setBottomPanelVisible(false)}
                    class="p-1 mr-1.5 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
                  >
                    <X class="w-3 h-3" />
                  </button>
                </div>
                {/* Tab content — use display:none instead of <Show> so xterm DOM stays alive */}
                <div
                  class="flex-1 overflow-hidden"
                  style={{ display: bottomPanelTab() === 'memory' ? undefined : 'none' }}
                >
                  <SidebarMemory />
                </div>
                <div
                  class="flex-1 overflow-hidden"
                  style={{ display: bottomPanelTab() === 'terminal' ? undefined : 'none' }}
                >
                  <XTerminal />
                </div>
                <div
                  class="flex-1 overflow-hidden"
                  style={{ display: bottomPanelTab() === 'output' ? undefined : 'none' }}
                >
                  <TerminalPanel />
                </div>
              </div>
            </div>
          </Show>
        </div>

        {/* Right panel resize handle + panel */}
        <Show when={settings().ui.showAgentActivity && rightPanelVisible()}>
          {/* biome-ignore lint/a11y/noStaticElementInteractions: resize handle uses mouse-only interaction by design */}
          <div
            class="
              w-[3px] flex-shrink-0 cursor-col-resize
              bg-transparent hover:bg-[var(--accent-muted)]
              active:bg-[var(--accent)]
              transition-colors duration-150
            "
            onMouseDown={startRightResize}
          />
          <div
            class="flex-shrink-0 overflow-hidden border-l border-[var(--border-subtle)]"
            style={{ width: `${rightPanelWidth()}px` }}
          >
            <div class="flex flex-col h-full bg-[var(--gray-1)]">
              {/* Tab header */}
              <div class="flex items-center h-8 flex-shrink-0 border-b border-[var(--border-subtle)]">
                <button
                  type="button"
                  onClick={() => switchRightPanelTab('activity')}
                  class="flex items-center gap-1.5 px-3 h-full text-[10px] font-semibold uppercase tracking-wider transition-colors"
                  classList={{
                    'text-[var(--accent)] border-b border-[var(--accent)]':
                      rightPanelTab() === 'activity',
                    'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                      rightPanelTab() !== 'activity',
                  }}
                >
                  <Bot class="w-3 h-3" />
                  Activity
                </button>
                <button
                  type="button"
                  onClick={() => switchRightPanelTab('files')}
                  class="flex items-center gap-1.5 px-3 h-full text-[10px] font-semibold uppercase tracking-wider transition-colors"
                  classList={{
                    'text-[var(--accent)] border-b border-[var(--accent)]':
                      rightPanelTab() === 'files',
                    'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                      rightPanelTab() !== 'files',
                  }}
                >
                  <FolderOpen class="w-3 h-3" />
                  Files
                </button>
                <button
                  type="button"
                  onClick={() => switchRightPanelTab('review')}
                  class="flex items-center gap-1.5 px-3 h-full text-[10px] font-semibold uppercase tracking-wider transition-colors"
                  classList={{
                    'text-[var(--accent)] border-b border-[var(--accent)]':
                      rightPanelTab() === 'review',
                    'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                      rightPanelTab() !== 'review',
                  }}
                >
                  <GitCompareArrows class="w-3 h-3" />
                  Review
                </button>
                <div class="flex-1" />
                <button
                  type="button"
                  onClick={() => setRightPanelVisible(false)}
                  class="p-1 mr-1.5 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
                >
                  <X class="w-3 h-3" />
                </button>
              </div>
              {/* Tab content */}
              <div class="flex-1 overflow-hidden">
                <Show when={rightPanelTab() === 'activity'}>
                  <AgentActivityPanel compact />
                </Show>
                <Show when={rightPanelTab() === 'files'}>
                  <FileOperationsPanel compact />
                </Show>
                <Show when={rightPanelTab() === 'review'}>
                  <DiffReviewPanel />
                </Show>
              </div>
            </div>
          </div>
        </Show>
      </div>

      {/* Settings Modal (overlay) */}
      <SettingsModal />
    </div>
  )
}
