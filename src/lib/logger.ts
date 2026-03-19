/**
 * Frontend Logger — Lightweight Structured Logging
 *
 * Provides a simple `log.debug/info/warn/error(category, message, data?)` API.
 *
 * - In Tauri mode: writes to `~/.ava/logs/frontend.log` via Tauri FS plugin,
 *   with automatic rotation/truncation at 1 MB.
 * - In web mode: sends logs to `POST /api/log` endpoint.
 * - Always mirrors to the browser console with the appropriate method.
 *
 * Usage:
 *   import { log, initFrontendLogger, disposeFrontendLogger } from '../lib/logger'
 *   await initFrontendLogger()
 *   log.info('app', 'Initialized', { version: '2.0.0' })
 */

import { type FlushWriter, LogBuffer, type LogBufferEntry } from './log-buffer'

// ============================================================================
// Constants
// ============================================================================

const LOG_DIR_NAME = '.ava/log'
const LOG_FILE_NAME = 'app.log'
const MAX_LOG_FILE_BYTES = 1_024 * 1_024 // 1 MB

// ============================================================================
// State
// ============================================================================

const buffer = new LogBuffer()
let logFilePath = ''
let isTauriEnv = false
let initialized = false
let currentMinLevel: LogBufferEntry['level'] = 'debug'

const LEVEL_PRIORITY: Record<LogBufferEntry['level'], number> = {
  debug: 0,
  info: 1,
  warn: 2,
  error: 3,
}

// ============================================================================
// Console mirror
// ============================================================================

function mirrorToConsole(entry: LogBufferEntry): void {
  const prefix = `[${entry.category}]`
  const args: unknown[] = [prefix, entry.message]
  if (entry.data !== undefined) args.push(entry.data)

  switch (entry.level) {
    case 'debug':
      console.debug(...args)
      break
    case 'info':
      console.info(...args)
      break
    case 'warn':
      console.warn(...args)
      break
    case 'error':
      console.error(...args)
      break
  }
}

// ============================================================================
// Format
// ============================================================================

function formatEntry(entry: LogBufferEntry): string {
  const dataStr =
    entry.data !== undefined
      ? ` | ${typeof entry.data === 'string' ? entry.data : JSON.stringify(entry.data)}`
      : ''
  return `[${entry.timestamp}] [${entry.level.toUpperCase()}] [${entry.category}] ${entry.message}${dataStr}`
}

// ============================================================================
// Writers
// ============================================================================

/** Tauri writer: appends lines via invoke, with size-based rotation. */
function createTauriWriter(): FlushWriter {
  return async (entries: LogBufferEntry[]) => {
    if (!logFilePath) return
    const { invoke } = await import('@tauri-apps/api/core')
    const lines = `${entries.map(formatEntry).join('\n')}\n`

    // Check file size and truncate if needed (keep last ~half)
    try {
      const { stat } = await import('@tauri-apps/plugin-fs')
      try {
        const info = await stat(logFilePath)
        if (info.size >= MAX_LOG_FILE_BYTES) {
          const { readTextFile, writeTextFile } = await import('@tauri-apps/plugin-fs')
          const content = await readTextFile(logFilePath)
          // Keep the last half of the file
          const halfPoint = Math.floor(content.length / 2)
          const nextNewline = content.indexOf('\n', halfPoint)
          const truncated =
            nextNewline >= 0
              ? `--- log truncated at ${new Date().toISOString()} ---\n${content.slice(nextNewline + 1)}`
              : content.slice(halfPoint)
          await writeTextFile(logFilePath, truncated)
        }
      } catch {
        // File may not exist yet; that is fine.
      }
    } catch {
      // stat/fs import failed; skip rotation check
    }

    await invoke('append_log', { path: logFilePath, content: lines })
  }
}

/** Web writer: POST entries to /api/log endpoint. */
function createWebWriter(baseUrl: string): FlushWriter {
  return async (entries: LogBufferEntry[]) => {
    for (const entry of entries) {
      try {
        await fetch(`${baseUrl}/api/log`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            level: entry.level,
            category: entry.category,
            message: entry.message,
            data: entry.data,
            timestamp: entry.timestamp,
          }),
        })
      } catch {
        // Network failure; entry was already mirrored to console.
      }
    }
  }
}

// ============================================================================
// Public API
// ============================================================================

/**
 * Initialize the frontend logger.
 *
 * Call once at app startup. Detects whether running in Tauri or web mode
 * and configures the appropriate flush writer.
 *
 * @param options.webBaseUrl — base URL for the web API (default: window.location.origin)
 */
export async function initFrontendLogger(options?: { webBaseUrl?: string }): Promise<void> {
  if (initialized) return
  initialized = true

  try {
    const { isTauri } = await import('@tauri-apps/api/core')
    isTauriEnv = isTauri()
  } catch {
    isTauriEnv = false
  }

  if (isTauriEnv) {
    try {
      const { homeDir } = await import('@tauri-apps/api/path')
      const home = await homeDir()
      const logsDir = `${home}${LOG_DIR_NAME}`

      // Ensure ~/.ava/log/ directory exists
      try {
        const { mkdir } = await import('@tauri-apps/plugin-fs')
        await mkdir(logsDir, { recursive: true })
      } catch {
        // Directory may already exist
      }

      logFilePath = `${logsDir}/${LOG_FILE_NAME}`
      buffer.start(createTauriWriter())
    } catch {
      // Fall back to console-only
      buffer.start(async () => {})
    }
  } else {
    const baseUrl =
      options?.webBaseUrl ?? (typeof window !== 'undefined' ? window.location.origin : '')
    buffer.start(createWebWriter(baseUrl))
  }

  pushEntry('info', 'logger', 'Frontend logger initialized', {
    mode: isTauriEnv ? 'tauri' : 'web',
    logFile: logFilePath || '(none)',
  })
}

/** Set the minimum log level. Entries below this level are dropped. */
export function setFrontendLogLevel(level: LogBufferEntry['level']): void {
  currentMinLevel = level
}

/** Flush buffered entries to disk immediately. */
export async function flushFrontendLogs(): Promise<void> {
  await buffer.flush()
}

/** Dispose the logger — flush remaining entries and stop periodic writes. */
export async function disposeFrontendLogger(): Promise<void> {
  pushEntry('info', 'logger', 'Frontend logger shutting down')
  await buffer.dispose()
  initialized = false
}

/** Get a read-only snapshot of the in-memory log entries. */
export function getFrontendLogEntries(): ReadonlyArray<LogBufferEntry> {
  return buffer.getEntries()
}

/** Get the resolved log file path (available after init). */
export function getFrontendLogFilePath(): string {
  return logFilePath
}

/**
 * Read the last N lines from the log file on disk.
 * Returns empty string if not in Tauri mode or file doesn't exist.
 */
export async function readFrontendLogFile(lines = 100): Promise<string> {
  if (!logFilePath || !isTauriEnv) {
    // In web mode, format the in-memory buffer
    const entries = buffer.getEntries()
    const tail = entries.slice(-lines)
    return tail.map(formatEntry).join('\n')
  }
  try {
    const { invoke } = await import('@tauri-apps/api/core')
    return await invoke<string>('read_latest_logs', { path: logFilePath, lines })
  } catch {
    return '(failed to read log file)'
  }
}

// ============================================================================
// Core push
// ============================================================================

function pushEntry(
  level: LogBufferEntry['level'],
  category: string,
  message: string,
  data?: unknown
): void {
  if (LEVEL_PRIORITY[level] < LEVEL_PRIORITY[currentMinLevel]) return

  const entry: LogBufferEntry = {
    timestamp: new Date().toISOString(),
    level,
    category,
    message,
    data,
  }

  mirrorToConsole(entry)
  buffer.push(entry)
}

// ============================================================================
// log object — primary public API
// ============================================================================

export const log = {
  debug(category: string, message: string, data?: unknown): void {
    pushEntry('debug', category, message, data)
  },
  info(category: string, message: string, data?: unknown): void {
    pushEntry('info', category, message, data)
  },
  warn(category: string, message: string, data?: unknown): void {
    pushEntry('warn', category, message, data)
  },
  error(category: string, message: string, data?: unknown): void {
    pushEntry('error', category, message, data)
  },
}
