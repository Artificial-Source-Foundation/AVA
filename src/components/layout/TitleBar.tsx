/**
 * Title Bar Component
 *
 * Custom title bar replacing the native window chrome.
 * Shows app name, menu bar, window title, and window controls.
 * Draggable for window movement via startDragging() API.
 */

import type { Window } from '@tauri-apps/api/window'
import { AppWindow, FolderOpen, Layers, Minus, Square, X } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { useProject } from '../../stores/project'
import { useSession } from '../../stores/session'
import { MenuBar } from './MenuBar'

let windowCounter = 0

/** Execute a window action, silently ignoring failures in non-Tauri environments. */
async function tauriWindowAction(fn: (win: Window) => Promise<void>): Promise<void> {
  try {
    const { getCurrentWindow } = await import('@tauri-apps/api/window')
    await fn(getCurrentWindow())
  } catch {
    /* non-Tauri environment */
  }
}

export const TitleBar: Component = () => {
  const { currentProject, setCurrentDirectory } = useProject()
  const sessionStore = useSession()

  const windowTitle = () => {
    const project = currentProject()
    if (project && project.name !== 'Default') return `AVA — ${project.name}`
    return 'AVA'
  }

  /** Truncate a directory path to show only the last 2-3 segments */
  const truncatedDir = () => {
    const dir = currentProject()?.directory
    if (!dir) return ''
    const parts = dir.replace(/\\/g, '/').split('/')
    return parts.length > 3 ? `.../${parts.slice(-2).join('/')}` : dir
  }

  const handlePickDirectory = async () => {
    try {
      const { open } = await import('@tauri-apps/plugin-dialog')
      const selected = await open({ directory: true, title: 'Choose working directory' })
      if (selected && typeof selected === 'string') {
        await setCurrentDirectory(selected)
      }
    } catch {
      /* ignore in non-Tauri */
    }
  }

  const startDrag = async (e: MouseEvent) => {
    if (e.button !== 0) return
    if ((e.target as HTMLElement).closest('button, [data-menubar]')) return
    await tauriWindowAction((win) => win.startDragging())
  }

  const handleMinimize = () => tauriWindowAction((win) => win.minimize())

  const handleMaximize = () => tauriWindowAction((win) => win.toggleMaximize())

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

  const handleClose = () => tauriWindowAction((win) => win.close())

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

      {/* Center - window title + directory switcher */}
      <div class="absolute left-1/2 -translate-x-1/2 flex items-center gap-2">
        <span class="text-[10px] font-medium tracking-wide text-[var(--text-muted)] font-[var(--font-ui-mono)] pointer-events-none">
          {windowTitle()}
        </span>
        <Show when={currentProject()}>
          <button
            type="button"
            onClick={handlePickDirectory}
            class="flex items-center gap-1 px-1.5 py-0.5 rounded-[var(--radius-sm)] text-[9px] text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--alpha-white-5)] transition-colors"
            title={currentProject()?.directory || 'Change working directory'}
          >
            <FolderOpen class="w-3 h-3" />
            <span class="max-w-[140px] truncate font-[var(--font-ui-mono)]">{truncatedDir()}</span>
          </button>
        </Show>

        {/* Background plan indicator */}
        <Show when={sessionStore.backgroundPlanActive()}>
          <span class="flex items-center gap-1.5 px-2 py-0.5 rounded-full bg-[var(--accent-subtle)] text-[9px] text-[var(--accent)] font-medium font-[var(--font-ui-mono)]">
            <span class="w-1.5 h-1.5 bg-[var(--accent)] rounded-full animate-pulse" />
            <Layers class="w-3 h-3" />
            {sessionStore.backgroundPlanProgress() || 'Plan running'}
          </span>
        </Show>
      </div>

      {/* Right - New window + window controls */}
      <div class="flex items-center gap-0.5 -mr-1">
        <button
          type="button"
          onClick={handleNewWindow}
          class="flex items-center justify-center w-8 h-7 text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)] transition-colors rounded-[var(--radius-sm)]"
          title="New Window"
          aria-label="New Window"
        >
          <AppWindow class="w-3.5 h-3.5" />
        </button>

        <span class="h-3 w-px bg-[var(--border-subtle)] mx-0.5" />

        <button
          type="button"
          onClick={handleMinimize}
          class="flex items-center justify-center w-8 h-7 text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)] transition-colors rounded-[var(--radius-sm)]"
          title="Minimize"
          aria-label="Minimize"
        >
          <Minus class="w-3.5 h-3.5" />
        </button>
        <button
          type="button"
          onClick={handleMaximize}
          class="flex items-center justify-center w-8 h-7 text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)] transition-colors rounded-[var(--radius-sm)]"
          title="Maximize"
          aria-label="Maximize"
        >
          <Square class="w-3 h-3" />
        </button>
        <button
          type="button"
          onClick={handleClose}
          class="flex items-center justify-center w-8 h-7 text-[var(--text-muted)] hover:text-white hover:bg-[var(--error)] transition-colors rounded-[var(--radius-sm)]"
          title="Close"
          aria-label="Close"
        >
          <X class="w-3.5 h-3.5" />
        </button>
      </div>
    </div>
  )
}
