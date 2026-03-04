/**
 * Developer Console Capture
 *
 * Intercepts console.log/warn/error/info and stores entries in a reactive
 * signal so they can be displayed in the Developer settings tab.
 * Also flushes to daily log files in `logs/` for offline debugging.
 * Capped at 1000 entries in memory to avoid bloat.
 */

import { invoke } from '@tauri-apps/api/core'
import { createRoot, createSignal } from 'solid-js'

// ============================================================================
// Types
// ============================================================================

export interface DevLogEntry {
  id: number
  timestamp: number
  level: 'log' | 'info' | 'warn' | 'error'
  message: string
}

// ============================================================================
// State
// ============================================================================

const MAX_ENTRIES = 1000
const FLUSH_INTERVAL_MS = 3000
const LOG_RETENTION_DAYS = 7

let nextId = 0
let installed = false
let flushTimer: ReturnType<typeof setInterval> | null = null
let fileBuffer: string[] = []
let logDir = ''

const { entries, setEntries } = createRoot(() => {
  const [entries, setEntries] = createSignal<DevLogEntry[]>([])
  return { entries, setEntries }
})

// Store original console methods
const originals = {
  log: console.log.bind(console),
  info: console.info.bind(console),
  warn: console.warn.bind(console),
  error: console.error.bind(console),
}

// ============================================================================
// Helpers
// ============================================================================

function stringify(args: unknown[]): string {
  return args
    .map((a) => {
      if (typeof a === 'string') return a
      if (a instanceof Error) return `${a.name}: ${a.message}\n${a.stack ?? ''}`
      try {
        return JSON.stringify(a, null, 2)
      } catch {
        return String(a)
      }
    })
    .join(' ')
}

const LEVEL_LABELS: Record<string, string> = {
  log: 'LOG',
  info: 'INF',
  warn: 'WRN',
  error: 'ERR',
}

function formatTimestamp(ts: number): string {
  const d = new Date(ts)
  return d.toISOString().replace('T', ' ').replace('Z', '')
}

function todayDateString(): string {
  const d = new Date()
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`
}

function formatForFile(entry: DevLogEntry): string {
  return `[${formatTimestamp(entry.timestamp)}] ${LEVEL_LABELS[entry.level]} ${entry.message}`
}

// ============================================================================
// File flushing
// ============================================================================

async function flushToFile(): Promise<void> {
  if (fileBuffer.length === 0 || !logDir) return

  const lines = `${fileBuffer.join('\n')}\n`
  fileBuffer = []

  const logPath = `${logDir}/ava-${todayDateString()}.log`
  try {
    await invoke('append_log', { path: logPath, content: lines })
  } catch {
    // Silent — don't recurse into console.error
    originals.warn('[dev-console] Failed to flush to file')
  }
}

async function cleanupOldLogs(): Promise<void> {
  if (!logDir) return
  try {
    const deleted = await invoke<number>('cleanup_old_logs', {
      dir: logDir,
      maxAgeDays: LOG_RETENTION_DAYS,
    })
    if (deleted && (deleted as number) > 0) {
      originals.info(`[dev-console] Cleaned up ${deleted} old log file(s)`)
    }
  } catch {
    // Silent
  }
}

// ============================================================================
// Capture
// ============================================================================

function capture(level: DevLogEntry['level'], args: unknown[]): void {
  const entry: DevLogEntry = {
    id: nextId++,
    timestamp: Date.now(),
    level,
    message: stringify(args),
  }

  // In-memory for UI
  setEntries((prev) => {
    const next = [...prev, entry]
    return next.length > MAX_ENTRIES ? next.slice(-MAX_ENTRIES) : next
  })

  // Buffer for file
  fileBuffer.push(formatForFile(entry))

  // Immediate flush for errors
  if (level === 'error') {
    flushToFile()
  }
}

// ============================================================================
// Public API
// ============================================================================

/**
 * Set the logs directory path. Call once during init.
 * Logs will be written directly to this directory.
 */
export function setLogDirectory(dir: string): void {
  logDir = dir
}

/** Start capturing console output. Idempotent. */
export function installConsoleCapture(): void {
  if (installed) return
  installed = true

  console.log = (...args: unknown[]) => {
    originals.log(...args)
    capture('log', args)
  }
  console.info = (...args: unknown[]) => {
    originals.info(...args)
    capture('info', args)
  }
  console.warn = (...args: unknown[]) => {
    originals.warn(...args)
    capture('warn', args)
  }
  console.error = (...args: unknown[]) => {
    originals.error(...args)
    capture('error', args)
  }

  // Periodic flush + cleanup
  flushTimer = setInterval(flushToFile, FLUSH_INTERVAL_MS)
  cleanupOldLogs()

  // Write session start marker
  const marker: DevLogEntry = {
    id: nextId++,
    timestamp: Date.now(),
    level: 'info',
    message: '--- Dev console session started ---',
  }
  setEntries((prev) => [...prev, marker])
  fileBuffer.push(formatForFile(marker))
}

/** Stop capturing console output and restore originals. */
export function uninstallConsoleCapture(): void {
  if (!installed) return
  installed = false
  console.log = originals.log
  console.info = originals.info
  console.warn = originals.warn
  console.error = originals.error

  // Final flush
  flushToFile()

  if (flushTimer) {
    clearInterval(flushTimer)
    flushTimer = null
  }
}

/** Reactive accessor for captured log entries. */
export function getDevLogs() {
  return entries
}

/** Clear all captured entries. */
export function clearDevLogs(): void {
  setEntries([])
}

/** Whether capture is currently installed. */
export function isCapturing(): boolean {
  return installed
}
