/**
 * File Logger Service
 *
 * Writes errors and debug info to a log file in the app data directory.
 * Uses Tauri's FS plugin for file access.
 *
 * Log file location: $APPDATA/ava/logs/ava.log
 * Rotates when file exceeds ~500KB.
 */

import {
  BaseDirectory,
  exists,
  mkdir,
  readTextFile,
  remove,
  rename,
  writeTextFile,
} from '@tauri-apps/plugin-fs'

// ============================================================================
// Types
// ============================================================================

type LogLevel = 'debug' | 'info' | 'warn' | 'error' | 'fatal'

interface LogEntry {
  timestamp: string
  level: LogLevel
  source: string
  message: string
  data?: unknown
}

// ============================================================================
// Constants
// ============================================================================

const LOG_DIR = 'logs'
const LOG_FILE = 'logs/ava.log'
const LOG_FILE_PREV = 'logs/ava.prev.log'
const MAX_LOG_SIZE = 512 * 1024 // 512KB before rotation
const FLUSH_INTERVAL_MS = 2000

// ============================================================================
// State
// ============================================================================

let buffer: string[] = []
let flushTimer: ReturnType<typeof setInterval> | null = null
let initialized = false
let currentSize = 0

// ============================================================================
// Internal
// ============================================================================

function formatEntry(entry: LogEntry): string {
  const dataStr =
    entry.data !== undefined
      ? ` | ${typeof entry.data === 'string' ? entry.data : JSON.stringify(entry.data)}`
      : ''
  return `[${entry.timestamp}] ${entry.level.toUpperCase().padEnd(5)} [${entry.source}] ${entry.message}${dataStr}`
}

async function ensureLogDir(): Promise<void> {
  const dirExists = await exists(LOG_DIR, { baseDir: BaseDirectory.AppData })
  if (!dirExists) {
    await mkdir(LOG_DIR, { baseDir: BaseDirectory.AppData, recursive: true })
  }
}

async function rotateIfNeeded(): Promise<void> {
  if (currentSize < MAX_LOG_SIZE) return

  try {
    // Rename current → prev (overwrite prev if it exists)
    const prevExists = await exists(LOG_FILE_PREV, { baseDir: BaseDirectory.AppData })
    if (prevExists) {
      await remove(LOG_FILE_PREV, { baseDir: BaseDirectory.AppData })
    }
    await rename(LOG_FILE, LOG_FILE_PREV, {
      oldPathBaseDir: BaseDirectory.AppData,
      newPathBaseDir: BaseDirectory.AppData,
    })
    currentSize = 0
  } catch {
    // If rotation fails, just truncate
    currentSize = 0
  }
}

async function flushBuffer(): Promise<void> {
  if (buffer.length === 0) return

  const lines = `${buffer.join('\n')}\n`
  buffer = []

  try {
    await rotateIfNeeded()

    // Append to log file
    let existing = ''
    const fileExists = await exists(LOG_FILE, { baseDir: BaseDirectory.AppData })
    if (fileExists) {
      existing = await readTextFile(LOG_FILE, { baseDir: BaseDirectory.AppData })
    }

    const content = existing + lines
    await writeTextFile(LOG_FILE, content, { baseDir: BaseDirectory.AppData })
    currentSize = content.length
  } catch (err) {
    // Last resort: dump to console so we don't lose info
    console.warn('[Logger] Failed to write log file:', err)
    console.warn('[Logger] Buffered entries:', lines)
  }
}

function pushEntry(level: LogLevel, source: string, message: string, data?: unknown): void {
  const entry: LogEntry = {
    timestamp: new Date().toISOString(),
    level,
    source,
    message,
  }
  if (data !== undefined) entry.data = data

  const line = formatEntry(entry)
  buffer.push(line)

  // Also mirror errors to console for dev tools
  if (level === 'error' || level === 'fatal') {
    console.error(`[${source}]`, message, data ?? '')
  }

  // Immediate flush for errors
  if (level === 'error' || level === 'fatal') {
    flushBuffer()
  }
}

// ============================================================================
// Public API
// ============================================================================

/**
 * Initialize the logger. Call once at app startup (after Tauri is ready).
 * Sets up the log directory and periodic flush.
 */
export async function initLogger(): Promise<void> {
  if (initialized) return

  try {
    await ensureLogDir()

    // Get current file size
    const fileExists = await exists(LOG_FILE, { baseDir: BaseDirectory.AppData })
    if (fileExists) {
      const content = await readTextFile(LOG_FILE, { baseDir: BaseDirectory.AppData })
      currentSize = content.length
    }

    // Periodic flush for non-error entries
    flushTimer = setInterval(flushBuffer, FLUSH_INTERVAL_MS)

    initialized = true

    // Write startup marker
    pushEntry('info', 'Logger', '--- AVA session started ---')
    pushEntry('info', 'Logger', `Log file: $APPDATA/${LOG_FILE}`)
  } catch (err) {
    console.warn('[Logger] Failed to initialize file logger:', err)
  }
}

/** Log a debug message */
export function logDebug(source: string, message: string, data?: unknown): void {
  pushEntry('debug', source, message, data)
}

/** Log an info message */
export function logInfo(source: string, message: string, data?: unknown): void {
  pushEntry('info', source, message, data)
}

/** Log a warning */
export function logWarn(source: string, message: string, data?: unknown): void {
  pushEntry('warn', source, message, data)
}

/** Log an error (also prints to console, flushes immediately) */
export function logError(source: string, message: string, data?: unknown): void {
  pushEntry('error', source, message, data)
}

/** Log a fatal error (also prints to console, flushes immediately) */
export function logFatal(source: string, message: string, data?: unknown): void {
  pushEntry('fatal', source, message, data)
}

/** Force flush all buffered entries to disk */
export async function flushLogs(): Promise<void> {
  await flushBuffer()
}

/** Read the current log file contents (for in-app debug view) */
export async function readLogFile(): Promise<string> {
  try {
    const fileExists = await exists(LOG_FILE, { baseDir: BaseDirectory.AppData })
    if (!fileExists) return '(no log file yet)'
    return await readTextFile(LOG_FILE, { baseDir: BaseDirectory.AppData })
  } catch {
    return '(failed to read log file)'
  }
}

/** Cleanup — call on app exit */
export async function destroyLogger(): Promise<void> {
  if (flushTimer) {
    clearInterval(flushTimer)
    flushTimer = null
  }
  await flushBuffer()
  initialized = false
}
