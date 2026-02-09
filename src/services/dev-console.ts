/**
 * Developer Console Capture
 *
 * Intercepts console.log/warn/error/info and stores entries in a reactive
 * signal so they can be displayed in the Developer settings tab.
 * Capped at 1000 entries to avoid memory bloat.
 */

import { createSignal } from 'solid-js'

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
let nextId = 0
let installed = false

const [entries, setEntries] = createSignal<DevLogEntry[]>([])

// Store original console methods
const originals = {
  log: console.log.bind(console),
  info: console.info.bind(console),
  warn: console.warn.bind(console),
  error: console.error.bind(console),
}

// ============================================================================
// Stringify helper
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
  setEntries((prev) => {
    const next = [...prev, entry]
    return next.length > MAX_ENTRIES ? next.slice(-MAX_ENTRIES) : next
  })
}

// ============================================================================
// Public API
// ============================================================================

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
}

/** Stop capturing console output and restore originals. */
export function uninstallConsoleCapture(): void {
  if (!installed) return
  installed = false
  console.log = originals.log
  console.info = originals.info
  console.warn = originals.warn
  console.error = originals.error
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
