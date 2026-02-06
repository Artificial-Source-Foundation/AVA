/**
 * Title Bar Component
 *
 * Custom title bar replacing the native window chrome.
 * Shows connection status, model, token usage, and window controls.
 * Draggable for window movement via startDragging() API.
 */

import { Minus, Square, X, Zap } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { useSession } from '../../stores/session'

export const StatusBar: Component = () => {
  const { sessionTokenStats, currentSession, selectedModel } = useSession()

  const formatTokens = (count: number): string => {
    if (count >= 1000000) return `${(count / 1000000).toFixed(1)}M`
    if (count >= 1000) return `${(count / 1000).toFixed(1)}K`
    return count.toString()
  }

  const startDrag = async (e: MouseEvent) => {
    // Only drag on left click, not on buttons
    if (e.button !== 0) return
    if ((e.target as HTMLElement).closest('button')) return
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
        h-8 px-3
        bg-[var(--gray-1)]
        border-b border-[var(--border-subtle)]
        select-none cursor-default
      "
    >
      {/* Left - Status */}
      <div class="flex items-center gap-3 font-[var(--font-ui-mono)] text-[10px] tracking-wide text-[var(--text-muted)] pointer-events-none">
        <div class="flex items-center gap-1.5">
          <span class="relative flex h-1.5 w-1.5">
            <span class="animate-ping absolute inline-flex h-full w-full rounded-full bg-[var(--success)] opacity-75" />
            <span class="relative inline-flex rounded-full h-1.5 w-1.5 bg-[var(--success)]" />
          </span>
          <span>Ready</span>
        </div>

        <span class="text-[var(--border-strong)]">|</span>

        <span class="text-[var(--accent)]">{selectedModel()}</span>

        <Show when={currentSession() && sessionTokenStats().total > 0}>
          <span class="text-[var(--border-strong)]">|</span>
          <span class="flex items-center gap-1">
            <Zap class="w-2.5 h-2.5 text-[var(--warning)]" />
            {formatTokens(sessionTokenStats().total)}
          </span>
        </Show>
      </div>

      {/* Center - drag region (implicit via parent mousedown) */}
      <div class="flex-1" />

      {/* Right - Window controls */}
      <div class="flex items-center gap-0.5 -mr-1">
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
