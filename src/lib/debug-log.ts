/**
 * Debug Logging — File-Based
 *
 * Routes all debug/info/warn/error output through the file-based frontend
 * logger (`src/lib/logger.ts`) which writes to `~/.ava/log/app.log`.
 *
 * In web mode (no Tauri), entries are buffered in memory and retrievable
 * via `getLogBuffer()`.
 *
 * Debug-level logs are only emitted when devMode is enabled in settings.
 * Info/warn/error always write regardless of devMode.
 *
 * Categories: 'thinking', 'agent', 'tools', 'team', 'settings', 'plan', 'event', etc.
 */

import { log as fileLog } from './logger'

// ============================================================================
// Types
// ============================================================================

export type LogLevel = 'debug' | 'info' | 'warn' | 'error'

export interface LogEntry {
  timestamp: string
  level: LogLevel
  category: string
  message: string
  data?: unknown
}

// ============================================================================
// State
// ============================================================================

/** Whether devMode is active — controls debug-level emission. */
let devModeEnabled = false

// ============================================================================
// Configuration
// ============================================================================

/**
 * Set whether devMode is active. When false, debug-level logs are suppressed.
 * Info/warn/error always pass through.
 */
export function setDebugDevMode(enabled: boolean): void {
  devModeEnabled = enabled
}

// ============================================================================
// Core log function
// ============================================================================

/**
 * Write a structured log entry to `~/.ava/log/app.log` via the frontend logger.
 *
 * - debug level: only written when devMode is enabled
 * - info/warn/error: always written
 */
export function log(level: LogLevel, category: string, message: string, data?: unknown): void {
  // Gate debug-level behind devMode
  if (level === 'debug' && !devModeEnabled) return

  fileLog[level](category, message, data)
}

// ============================================================================
// Convenience API
// ============================================================================

export const appLog = {
  debug(category: string, message: string, data?: unknown): void {
    log('debug', category, message, data)
  },
  info(category: string, message: string, data?: unknown): void {
    log('info', category, message, data)
  },
  warn(category: string, message: string, data?: unknown): void {
    log('warn', category, message, data)
  },
  error(category: string, message: string, data?: unknown): void {
    log('error', category, message, data)
  },
}

// ============================================================================
// Legacy API — backwards compatible with existing call sites
// ============================================================================

/**
 * Log a debug message. Equivalent to `log('debug', category, message)`.
 * Only emits when devMode is enabled.
 */
export function debugLog(category: string, ...args: unknown[]): void {
  if (!devModeEnabled) return
  const message = args.map((a) => (typeof a === 'string' ? a : JSON.stringify(a))).join(' ')
  fileLog.debug(category, message)
}

/**
 * Log a debug warning. Equivalent to `log('warn', category, message)`.
 * Always emits (warnings are not gated by devMode).
 */
export function debugWarn(category: string, ...args: unknown[]): void {
  const message = args.map((a) => (typeof a === 'string' ? a : JSON.stringify(a))).join(' ')
  fileLog.warn(category, message)
}
