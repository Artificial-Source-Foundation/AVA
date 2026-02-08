/**
 * App Shell - Main Layout Container
 *
 * Layout: Activity Bar | Sidebar | Main Area (+ Bottom Panel) | Right Panel
 * Settings is rendered as a modal overlay.
 * Sidebar uses CSS width transition for smooth open/close.
 * Resize handles allow drag-to-resize for sidebar and bottom panel.
 */

import { Bot, X } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { useLayout } from '../../stores/layout'
import { useSettings } from '../../stores/settings'
import { AgentActivityPanel } from '../panels/AgentActivityPanel'
import { SettingsModal } from '../settings'
import { SidebarMemory } from '../sidebar/SidebarMemory'
import { ActivityBar } from './ActivityBar'
import { MainArea } from './MainArea'
import { SidebarPanel } from './SidebarPanel'
import { StatusBar } from './StatusBar'

export const AppShell: Component = () => {
  const {
    sidebarVisible,
    sidebarWidth,
    setSidebarWidth,
    rightPanelVisible,
    setRightPanelVisible,
    bottomPanelVisible,
    setBottomPanelVisible,
    bottomPanelHeight,
    setBottomPanelHeight,
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

          {/* Bottom panel (Memory) — gated on ui.showBottomPanel */}
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
              style={{ height: `${bottomPanelHeight()}px` }}
            >
              <div class="flex flex-col h-full bg-[var(--gray-1)]">
                <div class="flex items-center justify-between px-3 h-8 flex-shrink-0 border-b border-[var(--border-subtle)]">
                  <span class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider">
                    Memory
                  </span>
                  <button
                    type="button"
                    onClick={() => setBottomPanelVisible(false)}
                    class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
                  >
                    <X class="w-3 h-3" />
                  </button>
                </div>
                <div class="flex-1 overflow-hidden">
                  <SidebarMemory />
                </div>
              </div>
            </div>
          </Show>
        </div>

        {/* Right panel (Agent Activity) — gated on ui.showAgentActivity */}
        <Show when={settings().ui.showAgentActivity && rightPanelVisible()}>
          <div
            class="flex-shrink-0 overflow-hidden border-l border-[var(--border-subtle)]"
            style={{ width: '320px' }}
          >
            <div class="flex flex-col h-full bg-[var(--gray-1)]">
              <div class="flex items-center justify-between px-3 h-8 flex-shrink-0 border-b border-[var(--border-subtle)]">
                <span class="flex items-center gap-1.5">
                  <Bot class="w-3 h-3 text-[var(--accent)]" />
                  <span class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider">
                    Agent Activity
                  </span>
                </span>
                <button
                  type="button"
                  onClick={() => setRightPanelVisible(false)}
                  class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
                >
                  <X class="w-3 h-3" />
                </button>
              </div>
              <div class="flex-1 overflow-hidden">
                <AgentActivityPanel compact />
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
