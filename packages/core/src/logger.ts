/**
 * Core Structured Logger
 *
 * Platform-agnostic structured logging for @ava/core.
 * Uses console.* under the hood (captured by dev-console in Tauri,
 * visible in terminal for CLI).
 *
 * Usage:
 *   import { createLogger } from './logger.js'
 *   const log = createLogger('Agent:loop')
 *   log.info('Turn started', { turn: 3, tools: ['read_file'] })
 *   log.error('Tool failed', { tool: 'bash', error: err.message })
 *   log.timing('LLM call', startMs, { provider: 'anthropic', model: 'opus' })
 */

// ============================================================================
// Types
// ============================================================================

export type LogLevel = 'debug' | 'info' | 'warn' | 'error'

export interface LogData {
  [key: string]: unknown
}

export interface Logger {
  debug(message: string, data?: LogData): void
  info(message: string, data?: LogData): void
  warn(message: string, data?: LogData): void
  error(message: string, data?: LogData): void
  /** Log a timing measurement (auto-calculates duration from startMs) */
  timing(label: string, startMs: number, data?: LogData): void
  /** Create a child logger with a sub-source (e.g., 'Agent:loop' → 'Agent:loop:recovery') */
  child(subsource: string): Logger
}

// ============================================================================
// Configuration
// ============================================================================

let globalLevel: LogLevel = 'info'
let enabled = true

const LEVEL_ORDER: Record<LogLevel, number> = {
  debug: 0,
  info: 1,
  warn: 2,
  error: 3,
}

/**
 * Set the minimum log level. Messages below this level are suppressed.
 * Default: 'info'. Set to 'debug' for verbose output.
 */
export function setLogLevel(level: LogLevel): void {
  globalLevel = level
}

/** Get the current log level */
export function getLogLevel(): LogLevel {
  return globalLevel
}

/** Enable or disable all core logging */
export function setLoggingEnabled(value: boolean): void {
  enabled = value
}

// ============================================================================
// Formatting
// ============================================================================

function formatPrefix(source: string): string {
  return `[${source}]`
}

function formatData(data: LogData): string {
  const parts: string[] = []
  for (const [key, value] of Object.entries(data)) {
    if (value === undefined) continue
    if (typeof value === 'string') {
      parts.push(`${key}=${value}`)
    } else if (typeof value === 'number' || typeof value === 'boolean') {
      parts.push(`${key}=${value}`)
    } else {
      try {
        parts.push(`${key}=${JSON.stringify(value)}`)
      } catch {
        parts.push(`${key}=[unserializable]`)
      }
    }
  }
  return parts.join(' ')
}

// ============================================================================
// Logger Factory
// ============================================================================

/**
 * Create a structured logger for a source module.
 *
 * @param source - Module identifier (e.g., 'Agent:loop', 'Commander', 'Tool:bash')
 * @returns Logger instance with debug/info/warn/error/timing methods
 */
export function createLogger(source: string): Logger {
  function shouldLog(level: LogLevel): boolean {
    return enabled && LEVEL_ORDER[level] >= LEVEL_ORDER[globalLevel]
  }

  function log(level: LogLevel, message: string, data?: LogData): void {
    if (!shouldLog(level)) return

    const prefix = formatPrefix(source)
    const dataStr = data ? ` | ${formatData(data)}` : ''
    const formatted = `${prefix} ${message}${dataStr}`

    switch (level) {
      case 'debug':
        console.debug(formatted)
        break
      case 'info':
        console.info(formatted)
        break
      case 'warn':
        console.warn(formatted)
        break
      case 'error':
        console.error(formatted)
        break
    }
  }

  return {
    debug(message: string, data?: LogData) {
      log('debug', message, data)
    },
    info(message: string, data?: LogData) {
      log('info', message, data)
    },
    warn(message: string, data?: LogData) {
      log('warn', message, data)
    },
    error(message: string, data?: LogData) {
      log('error', message, data)
    },
    timing(label: string, startMs: number, data?: LogData) {
      const durationMs = Math.round(performance.now() - startMs)
      log('debug', `${label} completed`, { ...data, durationMs })
    },
    child(subsource: string): Logger {
      return createLogger(`${source}:${subsource}`)
    },
  }
}
