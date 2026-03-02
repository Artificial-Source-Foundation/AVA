/**
 * Tauri File Watcher
 *
 * Uses @tauri-apps/plugin-fs watch() for native file system notifications.
 * Falls back gracefully when the Tauri runtime or plugin is unavailable.
 */

/** Minimal event shape emitted by Tauri's fs watch callback. */
export interface FileWatchEvent {
  path: string
  type: 'create' | 'modify' | 'remove' | 'unknown'
}

/** Callback for file change events. */
export type FileWatchHandler = (event: FileWatchEvent) => void

/**
 * TauriFileWatcher — wraps @tauri-apps/plugin-fs watch().
 *
 * Each call to watch() sets up a native file watcher and returns
 * an unwatch function. dispose() cleans up all active watchers.
 */
export class TauriFileWatcher {
  private unwatchers = new Map<string, () => void>()
  private disposed = false

  /**
   * Start watching a file or directory for changes.
   * Returns an unwatch function that stops watching this specific path.
   */
  async watch(path: string, handler: FileWatchHandler): Promise<() => void> {
    if (this.disposed) {
      throw new Error('TauriFileWatcher has been disposed')
    }

    // Stop any existing watcher for this path
    const existing = this.unwatchers.get(path)
    if (existing) existing()

    try {
      const fs = await import('@tauri-apps/plugin-fs')

      // Tauri watch() accepts a path and a callback, returns an unwatch function
      const unwatch = await fs.watch(path, (event) => {
        // Normalize Tauri's DebouncedEvent into our simpler shape
        const normalized = normalizeTauriEvent(path, event)
        if (normalized) handler(normalized)
      })

      const unwatchFn = () => {
        void unwatch()
        this.unwatchers.delete(path)
      }

      this.unwatchers.set(path, unwatchFn)
      return unwatchFn
    } catch {
      // Plugin not available — return a no-op unwatch
      return () => {}
    }
  }

  /** Stop all active watchers and mark as disposed. */
  dispose(): void {
    this.disposed = true
    for (const [, unwatch] of this.unwatchers) {
      unwatch()
    }
    this.unwatchers.clear()
  }

  /** Number of active watchers. */
  get watchCount(): number {
    return this.unwatchers.size
  }
}

/**
 * Normalize a Tauri fs watch event into our FileWatchEvent shape.
 * Tauri watch events vary by platform; this provides a stable interface.
 */
function normalizeTauriEvent(
  watchedPath: string,
  event: { type?: unknown; paths?: string[] }
): FileWatchEvent | null {
  // Tauri DebouncedEvent has { type: { kind: string }, paths: string[] }
  const eventType = event.type as { kind?: string } | string | undefined
  const kind = typeof eventType === 'object' ? eventType?.kind : String(eventType ?? 'unknown')

  let type: FileWatchEvent['type'] = 'unknown'
  if (kind === 'create' || kind === 'Create') type = 'create'
  else if (kind === 'modify' || kind === 'Modify') type = 'modify'
  else if (kind === 'remove' || kind === 'Remove') type = 'remove'

  const eventPath = event.paths?.[0] ?? watchedPath

  return { path: eventPath, type }
}
