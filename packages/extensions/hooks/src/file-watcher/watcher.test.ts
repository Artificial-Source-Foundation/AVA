/**
 * Tests for FileWatcher class.
 */

import type { IFileSystem } from '@ava/core-v2/platform'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { FileChangeEvent } from './watcher.js'
import { FileWatcher } from './watcher.js'

// ─── Mock FS ────────────────────────────────────────────────────────────────

function createMockFs(files: Record<string, number> = {}): IFileSystem {
  const mtimes = new Map(Object.entries(files))
  return {
    async readFile(path: string): Promise<string> {
      if (!mtimes.has(path)) throw new Error(`ENOENT: ${path}`)
      return ''
    },
    async writeFile(): Promise<void> {},
    async stat(path: string) {
      const mtime = mtimes.get(path)
      if (mtime === undefined) throw new Error(`ENOENT: ${path}`)
      return { isFile: true, isDirectory: false, size: 100, mtime }
    },
    async exists(path: string) {
      return mtimes.has(path)
    },
    async mkdir(): Promise<void> {},
    async remove(): Promise<void> {},
    /** Expose for test manipulation. */
    _mtimes: mtimes,
  } as IFileSystem & { _mtimes: Map<string, number> }
}

describe('FileWatcher', () => {
  beforeEach(() => {
    vi.useFakeTimers()
  })

  afterEach(() => {
    vi.useRealTimers()
  })

  it('records initial mtime when watch() is called', async () => {
    const fs = createMockFs({ '/test/file.txt': 1000 })
    const callback = vi.fn()
    const watcher = new FileWatcher(fs, callback, { intervalMs: 100 })

    await watcher.watch('/test/file.txt')
    expect(watcher.watchCount).toBe(1)
    expect(watcher.isPolling).toBe(true)

    watcher.dispose()
  })

  it('detects mtime changes on poll', async () => {
    const fs = createMockFs({ '/test/file.txt': 1000 })
    const events: FileChangeEvent[] = []
    const watcher = new FileWatcher(fs, (e) => events.push(e), { intervalMs: 100 })

    await watcher.watch('/test/file.txt')

    // Simulate file change
    ;(fs as unknown as { _mtimes: Map<string, number> })._mtimes.set('/test/file.txt', 2000)

    // Advance timer to trigger poll
    await vi.advanceTimersByTimeAsync(150)

    expect(events).toHaveLength(1)
    expect(events[0]).toEqual({
      path: '/test/file.txt',
      previousMtime: 1000,
      currentMtime: 2000,
    })

    watcher.dispose()
  })

  it('does not emit when mtime has not changed', async () => {
    const fs = createMockFs({ '/test/file.txt': 1000 })
    const callback = vi.fn()
    const watcher = new FileWatcher(fs, callback, { intervalMs: 100 })

    await watcher.watch('/test/file.txt')

    // Advance timer — no file change
    await vi.advanceTimersByTimeAsync(150)

    expect(callback).not.toHaveBeenCalled()

    watcher.dispose()
  })

  it('handles file that does not exist at watch time', async () => {
    const fs = createMockFs({})
    const callback = vi.fn()
    const watcher = new FileWatcher(fs, callback, { intervalMs: 100 })

    await watcher.watch('/nonexistent')
    expect(watcher.watchCount).toBe(1)

    watcher.dispose()
  })

  it('reports change when previously existing file is deleted', async () => {
    const fs = createMockFs({ '/test/file.txt': 1000 })
    const events: FileChangeEvent[] = []
    const watcher = new FileWatcher(fs, (e) => events.push(e), { intervalMs: 100 })

    await watcher.watch('/test/file.txt')

    // Simulate file deletion
    ;(fs as unknown as { _mtimes: Map<string, number> })._mtimes.delete('/test/file.txt')

    await vi.advanceTimersByTimeAsync(150)

    expect(events).toHaveLength(1)
    expect(events[0]?.currentMtime).toBe(0)
    expect(events[0]?.previousMtime).toBe(1000)

    watcher.dispose()
  })

  it('unwatch removes file from polling', async () => {
    const fs = createMockFs({ '/test/file.txt': 1000 })
    const callback = vi.fn()
    const watcher = new FileWatcher(fs, callback, { intervalMs: 100 })

    await watcher.watch('/test/file.txt')
    expect(watcher.watchCount).toBe(1)

    watcher.unwatch('/test/file.txt')
    expect(watcher.watchCount).toBe(0)
    expect(watcher.isPolling).toBe(false)

    watcher.dispose()
  })

  it('can watch multiple files', async () => {
    const fs = createMockFs({
      '/test/a.txt': 100,
      '/test/b.txt': 200,
    })
    const events: FileChangeEvent[] = []
    const watcher = new FileWatcher(fs, (e) => events.push(e), { intervalMs: 100 })

    await watcher.watch('/test/a.txt')
    await watcher.watch('/test/b.txt')
    expect(watcher.watchCount).toBe(2)

    // Change both files
    ;(fs as unknown as { _mtimes: Map<string, number> })._mtimes.set('/test/a.txt', 300)
    ;(fs as unknown as { _mtimes: Map<string, number> })._mtimes.set('/test/b.txt', 400)

    await vi.advanceTimersByTimeAsync(150)

    expect(events).toHaveLength(2)
    const paths = events.map((e) => e.path)
    expect(paths).toContain('/test/a.txt')
    expect(paths).toContain('/test/b.txt')

    watcher.dispose()
  })

  it('dispose stops polling and clears all watches', async () => {
    const fs = createMockFs({ '/test/file.txt': 1000 })
    const callback = vi.fn()
    const watcher = new FileWatcher(fs, callback, { intervalMs: 100 })

    await watcher.watch('/test/file.txt')
    expect(watcher.isPolling).toBe(true)

    watcher.dispose()
    expect(watcher.isPolling).toBe(false)
    expect(watcher.watchCount).toBe(0)

    // Further watch calls should be no-ops after dispose
    await watcher.watch('/test/another.txt')
    expect(watcher.watchCount).toBe(0)
  })

  it('uses default interval of 2000ms', async () => {
    const fs = createMockFs({ '/test/file.txt': 1000 })
    const callback = vi.fn()
    const watcher = new FileWatcher(fs, callback)

    await watcher.watch('/test/file.txt')

    // Change the file
    ;(fs as unknown as { _mtimes: Map<string, number> })._mtimes.set('/test/file.txt', 2000)

    // Advance less than default interval — should not trigger
    await vi.advanceTimersByTimeAsync(1500)
    expect(callback).not.toHaveBeenCalled()

    // Advance past default interval — should trigger
    await vi.advanceTimersByTimeAsync(600)
    expect(callback).toHaveBeenCalledTimes(1)

    watcher.dispose()
  })
})
