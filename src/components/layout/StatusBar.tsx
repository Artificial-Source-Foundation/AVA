/**
 * Title Bar Component
 *
 * Custom title bar replacing the native window chrome.
 * Shows app name, menu bar, window title, and window controls.
 * Draggable for window movement via startDragging() API.
 */

import { AppWindow, Minus, Square, X } from 'lucide-solid'
import type { Component } from 'solid-js'
import { useProject } from '../../stores/project'
import { MenuBar } from './MenuBar'

let windowCounter = 0

export const StatusBar: Component = () => {
  const { currentProject } = useProject()

  const windowTitle = () => {
    const project = currentProject()
    if (project && project.name !== 'Default') return `AVA — ${project.name}`
    return 'AVA'
  }

  const startDrag = async (e: MouseEvent) => {
    if (e.button !== 0) return
    if ((e.target as HTMLElement).closest('button, [data-menubar]')) return
    try {
      const { getCurrentWindow } = await import('@tauri-apps/api/window')
      await getCurrentWindow().startDragging()
    } catch {
      /* ignore in non-Tauri */
    }
  }

  const handleMinimize = async () => {
    try {
      const { getCurrentWindow } = await import('@tauri-apps/api/window')
      await getCurrentWindow().minimize()
    } catch {
      /* ignore in non-Tauri */
    }
  }

  const handleMaximize = async () => {
    try {
      const { getCurrentWindow } = await import('@tauri-apps/api/window')
      await getCurrentWindow().toggleMaximize()
    } catch {
      /* ignore in non-Tauri */
    }
  }

  const handleNewWindow = async () => {
    try {
      const { WebviewWindow } = await import('@tauri-apps/api/webviewWindow')
      windowCounter++
      new WebviewWindow(`ava-${Date.now()}-${windowCounter}`, {
        url: '/',
        title: 'AVA',
        width: 1200,
        height: 800,
        minWidth: 640,
        minHeight: 480,
        decorations: false,
      })
    } catch {
      /* ignore in non-Tauri */
    }
  }

  const handleClose = async () => {
    try {
      const { getCurrentWindow } = await import('@tauri-apps/api/window')
      await getCurrentWindow().close()
    } catch {
      /* ignore in non-Tauri */
    }
  }

  return (
    <div
      role="toolbar"
      onMouseDown={startDrag}
      onDblClick={handleMaximize}
      class="
        flex items-center justify-between
        h-9 px-3
        bg-[var(--gray-1)]
        border-b border-[var(--border-subtle)]
        select-none cursor-default
      "
    >
      {/* Left - App name + menus */}
      <div class="flex items-center gap-3 min-w-0">
        <span class="text-[11px] font-bold tracking-widest text-[var(--accent)] uppercase font-[var(--font-ui-mono)] pointer-events-none">
          AVA
        </span>

        <span class="h-3 w-px bg-[var(--border-strong)]" />

        <MenuBar />
      </div>

      {/* Center - window title (project name) */}
      <div class="absolute left-1/2 -translate-x-1/2 pointer-events-none">
        <span class="text-[10px] font-medium tracking-wide text-[var(--text-muted)] font-[var(--font-ui-mono)]">
          {windowTitle()}
        </span>
      </div>

      {/* Right - New window + window controls */}
      <div class="flex items-center gap-0.5 -mr-1">
        <button
          type="button"
          onClick={handleNewWindow}
          class="flex items-center justify-center w-8 h-7 text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)] transition-colors rounded-[var(--radius-sm)]"
          title="New Window"
        >
          <AppWindow class="w-3.5 h-3.5" />
        </button>

        <span class="h-3 w-px bg-[var(--border-subtle)] mx-0.5" />

        <button
          type="button"
          onClick={handleMinimize}
          class="flex items-center justify-center w-8 h-7 text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)] transition-colors rounded-[var(--radius-sm)]"
        >
          <Minus class="w-3.5 h-3.5" />
        </button>
        <button
          type="button"
          onClick={handleMaximize}
          class="flex items-center justify-center w-8 h-7 text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)] transition-colors rounded-[var(--radius-sm)]"
        >
          <Square class="w-3 h-3" />
        </button>
        <button
          type="button"
          onClick={handleClose}
          class="flex items-center justify-center w-8 h-7 text-[var(--text-muted)] hover:text-white hover:bg-[var(--error)] transition-colors rounded-[var(--radius-sm)]"
        >
          <X class="w-3.5 h-3.5" />
        </button>
      </div>
    </div>
  )
}
