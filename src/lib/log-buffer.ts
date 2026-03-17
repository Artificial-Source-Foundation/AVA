/**
 * Log Buffer — In-Memory Ring Buffer with Periodic Flush
 *
 * Keeps the last N log entries in memory and flushes to a writer
 * function periodically (every 5 seconds) or immediately on error-level entries.
 * On dispose, flushes any remaining entries.
 */

export interface LogBufferEntry {
  timestamp: string
  level: 'debug' | 'info' | 'warn' | 'error'
  category: string
  message: string
  data?: unknown
}

export type FlushWriter = (entries: LogBufferEntry[]) => Promise<void>

const MAX_ENTRIES = 500
const FLUSH_INTERVAL_MS = 5_000

export class LogBuffer {
  private entries: LogBufferEntry[] = []
  private flushTimer: ReturnType<typeof setInterval> | null = null
  private writer: FlushWriter | null = null
  private flushing = false

  /** Start periodic flushing with the given writer function. */
  start(writer: FlushWriter): void {
    this.writer = writer
    this.flushTimer = setInterval(() => {
      void this.flush()
    }, FLUSH_INTERVAL_MS)
  }

  /** Push a log entry into the ring buffer. Triggers immediate flush for errors. */
  push(entry: LogBufferEntry): void {
    this.entries.push(entry)
    if (this.entries.length > MAX_ENTRIES) {
      this.entries = this.entries.slice(-MAX_ENTRIES)
    }
    if (entry.level === 'error') {
      void this.flush()
    }
  }

  /** Get all in-memory entries (read-only snapshot). */
  getEntries(): ReadonlyArray<LogBufferEntry> {
    return this.entries
  }

  /** Get the number of buffered entries. */
  get size(): number {
    return this.entries.length
  }

  /** Flush all buffered entries to the writer. */
  async flush(): Promise<void> {
    if (this.flushing || !this.writer || this.entries.length === 0) return
    this.flushing = true
    const toFlush = [...this.entries]
    this.entries = []
    try {
      await this.writer(toFlush)
    } catch {
      // Re-prepend failed entries so they aren't lost (but cap at max)
      this.entries = [...toFlush, ...this.entries].slice(-MAX_ENTRIES)
    } finally {
      this.flushing = false
    }
  }

  /** Stop periodic flushing and flush remaining entries. */
  async dispose(): Promise<void> {
    if (this.flushTimer) {
      clearInterval(this.flushTimer)
      this.flushTimer = null
    }
    await this.flush()
    this.writer = null
  }
}
