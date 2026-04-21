/**
 * File Logger Service
 *
 * Writes errors and debug info to daily log files in AVA's XDG state log dir.
 * Uses Rust IPC (`invoke('append_log')`) for proper O(1) append.
 * Daily files: ava-YYYY-MM-DD.log with 7-day retention.
 */

import { invoke } from '@tauri-apps/api/core'

/** Lightweight frontend logger entry types. */
type CoreLogLevel = 'debug' | 'info' | 'warn' | 'error'
interface CoreLogEntry {
  level: CoreLogLevel
  source: string
  message: string
  timestamp?: string
  fields?: Record<string, unknown>
}

/** No-op logger configuration retained for local frontend compatibility. */
function configureLogger(_opts: {
  level?: CoreLogLevel
  callback?: (entry: CoreLogEntry) => void
}): void {
  // No-op — logging is handled locally
}

function formatCoreLogEntry(entry: CoreLogEntry): string {
  return `[${entry.level}] [${entry.source}] ${entry.message}`
}

import {
  formatLogEntry,
  isLogLevelEnabled,
  type StructuredLogEntry,
  type StructuredLogLevel,
  toStructuredFields,
} from './log-format'

// ============================================================================
// Types
// ============================================================================

type LogLevel = StructuredLogLevel

interface LogEntry {
  timestamp: string
  level: LogLevel
  source: string
  message: string
  fields?: StructuredLogEntry['fields']
}

interface LoggerInitMeta {
  version?: string
  platform?: string
  runtime?: string
  plugins?: number
  tools?: number
}

// ============================================================================
// Constants
// ============================================================================

const FLUSH_INTERVAL_MS = 2000
const LOG_RETENTION_DAYS = 7
const MAX_BUFFER_ENTRIES = 1000
const BACKEND_LOG_FILE_NAME = 'desktop-backend.log'

// ============================================================================
// State
// ============================================================================

let buffer: string[] = []
let flushTimer: ReturnType<typeof setInterval> | null = null
let initialized = false
let logFilePath = ''
let logDirPath = ''
let currentLogLevel: LogLevel = 'info'

// ============================================================================
// Internal
// ============================================================================

function todayDateString(): string {
  const d = new Date()
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`
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
  if (!isLogLevelEnabled(level, currentLogLevel)) return

  const entry: LogEntry = {
    timestamp: new Date().toISOString(),
    level,
    source,
    message,
  }
  const fields = toStructuredFields(data)
  if (fields) entry.fields = fields

  const line = formatLogEntry(entry)
  buffer.push(line)
  if (buffer.length > MAX_BUFFER_ENTRIES) {
    buffer = buffer.slice(-MAX_BUFFER_ENTRIES)
  }

  // Also mirror errors to console for dev tools
  if (level === 'error') {
    console.error(`[${source}]`, message, data ?? '')
  }

  // Immediate flush for errors
  if (level === 'error') {
    flushBuffer()
  }
}

// ============================================================================
// Public API
// ============================================================================

/**
 * Initialize the logger. Call once at app startup (after Tauri is ready).
 * Resolves AVA's XDG state log dir, sets up the daily log file path, and starts periodic flush.
 */
export async function initLogger(meta: LoggerInitMeta = {}): Promise<void> {
  if (initialized) return

  try {
    logDirPath = await invoke<string>('get_state_logs_dir')
    logFilePath = `${logDirPath}/ava-${todayDateString()}.log`

    // Periodic flush for non-error entries
    flushTimer = setInterval(flushBuffer, FLUSH_INTERVAL_MS)

    configureLogger({
      level: currentLogLevel as CoreLogLevel,
      callback: (entry: CoreLogEntry) => {
        buffer.push(formatCoreLogEntry(entry))
        if (entry.level === 'error') {
          void flushBuffer()
        }
      },
    })

    initialized = true

    // Write startup marker
    const separator = '='.repeat(80)
    const startedAt = new Date().toISOString()
    const version = meta.version ?? 'v3.3.0'
    const platform = meta.platform ?? 'unknown'
    const runtime = meta.runtime ?? 'tauri'
    const plugins = meta.plugins ?? 0
    const tools = meta.tools ?? 0
    buffer.push(separator)
    buffer.push(`${version} started at ${startedAt}`)
    buffer.push(
      `Platform: ${platform} | Runtime: ${runtime} | Plugins: ${plugins} | Tools: ${tools}`
    )
    buffer.push(separator)
    pushEntry('info', 'app:logger', 'Logger initialized', { log_file: logFilePath })

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

export function getBackendLogFilePath(): string {
  return logDirPath ? `${logDirPath}/${BACKEND_LOG_FILE_NAME}` : ''
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
  pushEntry('error', source, message, data)
}

export function setLogLevel(level: LogLevel): void {
  currentLogLevel = level
  configureLogger({ level: level as CoreLogLevel })
  pushEntry('info', 'app:logger', 'Log level updated', { level })
}

export function getLogLevel(): LogLevel {
  return currentLogLevel
}

/** Force flush all buffered entries to disk */
export async function flushLogs(): Promise<void> {
  await flushBuffer()
}

/** Read the current log file contents (for in-app debug view) */
export async function readLogFile(): Promise<string> {
  if (!logFilePath) return '(logger not initialized)'
  try {
    return await invoke<string>('read_latest_logs', { path: logFilePath, lines: 5000 })
  } catch {
    return '(failed to read log file)'
  }
}

export async function readLatestLogs(lines: number): Promise<string> {
  if (!logFilePath) return '(logger not initialized)'
  try {
    return await invoke<string>('read_latest_logs', { path: logFilePath, lines })
  } catch {
    return '(failed to read latest logs)'
  }
}

export async function readLatestBackendLogs(lines: number): Promise<string> {
  const backendLogPath = getBackendLogFilePath()
  if (!backendLogPath) return '(backend logger not initialized)'
  try {
    return await invoke<string>('read_latest_logs', { path: backendLogPath, lines })
  } catch {
    return '(failed to read backend logs)'
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
