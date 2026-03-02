/**
 * FileWatcher — polls files for changes using platform fs.
 *
 * Uses mtime comparison to detect changes. No native dependencies
 * required — works on any platform that implements IFileSystem.
 */

import type { IFileSystem } from '@ava/core-v2/platform'

// ─── Types ──────────────────────────────────────────────────────────────────

export interface FileChangeEvent {
  path: string
  previousMtime: number
  currentMtime: number
}

export type FileChangeCallback = (event: FileChangeEvent) => void

export interface FileWatcherConfig {
  /** Polling interval in milliseconds. Default: 2000. */
  intervalMs: number
}

const DEFAULT_CONFIG: FileWatcherConfig = {
  intervalMs: 2000,
}

// ─── FileWatcher ─────────────────────────────────────────────────────────────

export class FileWatcher {
  private readonly config: FileWatcherConfig
  private readonly fs: IFileSystem
  private readonly callback: FileChangeCallback
  private readonly mtimes = new Map<string, number>()
  private timer: ReturnType<typeof setInterval> | null = null
  private disposed = false

  constructor(fs: IFileSystem, callback: FileChangeCallback, config?: Partial<FileWatcherConfig>) {
    this.fs = fs
    this.callback = callback
    this.config = { ...DEFAULT_CONFIG, ...config }
  }

  /** Start watching a file. Records its current mtime immediately. */
  async watch(path: string): Promise<void> {
    if (this.disposed) return

    try {
      const stat = await this.fs.stat(path)
      this.mtimes.set(path, stat.mtime)
    } catch {
      // File may not exist yet — record mtime as 0
      this.mtimes.set(path, 0)
    }

    // Start polling if not already running
    if (!this.timer && this.mtimes.size > 0) {
      this.startPolling()
    }
  }

  /** Stop watching a specific file. */
  unwatch(path: string): void {
    this.mtimes.delete(path)

    // Stop polling if no files are being watched
    if (this.mtimes.size === 0) {
      this.stopPolling()
    }
  }

  /** Stop all watching and clean up. */
  dispose(): void {
    this.disposed = true
    this.stopPolling()
    this.mtimes.clear()
  }

  /** Number of files currently being watched. */
  get watchCount(): number {
    return this.mtimes.size
  }

  /** Whether the watcher is actively polling. */
  get isPolling(): boolean {
    return this.timer !== null
  }

  // ── Internal ──────────────────────────────────────────────────────────────

  private startPolling(): void {
    if (this.timer) return
    this.timer = setInterval(() => void this.poll(), this.config.intervalMs)
  }

  private stopPolling(): void {
    if (this.timer) {
      clearInterval(this.timer)
      this.timer = null
    }
  }

  private async poll(): Promise<void> {
    for (const [path, previousMtime] of this.mtimes) {
      if (this.disposed) return

      try {
        const stat = await this.fs.stat(path)
        if (stat.mtime !== previousMtime) {
          this.mtimes.set(path, stat.mtime)
          this.callback({ path, previousMtime, currentMtime: stat.mtime })
        }
      } catch {
        // File deleted or inaccessible — if it had a non-zero mtime, report change
        if (previousMtime !== 0) {
          this.mtimes.set(path, 0)
          this.callback({ path, previousMtime, currentMtime: 0 })
        }
      }
    }
  }
}
