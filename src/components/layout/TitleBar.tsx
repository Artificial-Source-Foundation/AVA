/**
 * Title Bar Component
 *
 * Custom title bar replacing the native window chrome.
 * Shows app name, menu bar, window title, and window controls.
 * Draggable for window movement via startDragging() API.
 */

import type { Window } from '@tauri-apps/api/window'
import { Copy, Maximize2, Minus, X } from 'lucide-solid'
import type { Component } from 'solid-js'
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
  const startDrag = async (e: MouseEvent) => {
    if (e.button !== 0) return
    if ((e.target as HTMLElement).closest('button, [data-menubar], a, input')) return
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
      class="titlebar"
      aria-label="Application title bar"
    >
      {/* Left - App name + separator + menus */}
      <div class="flex items-center min-w-0">
        <div class="flex items-center gap-[6px] px-[10px] h-8">
          <span class="pointer-events-none text-[13px] font-semibold tracking-[0.5px] text-[var(--text-primary)]">
            AVA
          </span>
        </div>

        <span class="w-px h-[14px] bg-[var(--border-default)] shrink-0" />

        <MenuBar />
      </div>

      {/* Right - Window controls */}
      <div class="flex items-center gap-2">
        <button
          type="button"
          onClick={handleNewWindow}
          class="titlebar-btn titlebar-btn--outlined"
          title="New Window"
          aria-label="New Window"
        >
          <Copy style={{ width: '10px', height: '10px' }} />
        </button>

        <button
          type="button"
          onClick={handleMinimize}
          class="titlebar-btn titlebar-btn--outlined"
          title="Minimize"
          aria-label="Minimize"
        >
          <Minus style={{ width: '10px', height: '10px' }} />
        </button>
        <button
          type="button"
          onClick={handleMaximize}
          class="titlebar-btn titlebar-btn--outlined"
          title="Maximize"
          aria-label="Maximize"
        >
          <Maximize2 style={{ width: '10px', height: '10px' }} />
        </button>
        <button
          type="button"
          onClick={handleClose}
          class="titlebar-btn titlebar-btn--close"
          title="Close"
          aria-label="Close"
        >
          <X style={{ width: '10px', height: '10px' }} />
        </button>
      </div>
    </div>
  )
}
