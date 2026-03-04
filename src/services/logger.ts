/**
 * File Logger Service
 *
 * Writes errors and debug info to daily log files in $APPDATA/ava/logs/.
 * Uses Rust IPC (`invoke('append_log')`) for proper O(1) append.
 * Daily files: ava-YYYY-MM-DD.log with 7-day retention.
 */

import { invoke } from '@tauri-apps/api/core'
import { appDataDir } from '@tauri-apps/api/path'

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

const FLUSH_INTERVAL_MS = 2000
const LOG_RETENTION_DAYS = 7

// ============================================================================
// State
// ============================================================================

let buffer: string[] = []
let flushTimer: ReturnType<typeof setInterval> | null = null
let initialized = false
let logFilePath = ''
let logDirPath = ''

// ============================================================================
// Internal
// ============================================================================

function todayDateString(): string {
  const d = new Date()
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`
}

function formatEntry(entry: LogEntry): string {
  const dataStr =
    entry.data !== undefined
      ? ` | ${typeof entry.data === 'string' ? entry.data : JSON.stringify(entry.data)}`
      : ''
  return `[${entry.timestamp}] ${entry.level.toUpperCase().padEnd(5)} [${entry.source}] ${entry.message}${dataStr}`
}

async function flushBuffer(): Promise<void> {
  if (buffer.length === 0 || !logFilePath) return

  const lines = `${buffer.join('\n')}\n`
  buffer = []

  try {
    await invoke('append_log', { path: logFilePath, content: lines })
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
 * Resolves $APPDATA, sets up daily log file path, and starts periodic flush.
 */
export async function initLogger(): Promise<void> {
  if (initialized) return

  try {
    const appData = await appDataDir()
    logDirPath = `${appData}logs`
    logFilePath = `${logDirPath}/ava-${todayDateString()}.log`

    // Periodic flush for non-error entries
    flushTimer = setInterval(flushBuffer, FLUSH_INTERVAL_MS)

    initialized = true

    // Write startup marker
    pushEntry('info', 'Logger', '--- AVA session started ---')
    pushEntry('info', 'Logger', `Log file: ${logFilePath}`)

    // Cleanup old logs (non-blocking)
    invoke('cleanup_old_logs', { dir: logDirPath, maxAgeDays: LOG_RETENTION_DAYS }).catch(() => {})
  } catch (err) {
    console.warn('[Logger] Failed to initialize file logger:', err)
  }
}

/** Get the resolved log directory path (available after initLogger) */
export function getLogDirectory(): string {
  return logDirPath
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
  if (!logFilePath) return '(logger not initialized)'
  try {
    const { readTextFile } = await import('@tauri-apps/plugin-fs')
    return await readTextFile(logFilePath)
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
