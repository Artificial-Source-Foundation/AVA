/**
 * App Shell - Main Layout Container
 *
 * Layout: Activity Bar | Sidebar (animated) | Main Area
 * Sidebar uses CSS width transition for smooth open/close.
 * Resize handle allows drag-to-resize when sidebar is visible.
 */

import { type Component, Show } from 'solid-js'
import { useLayout } from '../../stores/layout'
import { ActivityBar } from './ActivityBar'
import { MainArea } from './MainArea'
import { SidebarPanel } from './SidebarPanel'
import { StatusBar } from './StatusBar'

export const AppShell: Component = () => {
  const { sidebarVisible, sidebarWidth, setSidebarWidth } = useLayout()

  // Resize handler
  const startResize = (e: MouseEvent) => {
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

        {/* Resize handle — only interactive when sidebar is visible */}
        <Show when={sidebarVisible()}>
          {/* biome-ignore lint/a11y/noStaticElementInteractions: resize handle uses mouse-only interaction by design */}
          <div
            class="
              w-[3px] flex-shrink-0 cursor-col-resize
              bg-transparent hover:bg-[var(--accent-muted)]
              active:bg-[var(--accent)]
              transition-colors duration-150
            "
            onMouseDown={startResize}
          />
        </Show>

        {/* Main content fills remaining space */}
        <div class="flex-1 overflow-hidden min-w-0">
          <MainArea />
        </div>
      </div>
    </div>
  )
}
