/**
 * Delta9 Structured Logger
 *
 * Provides consistent logging across all Delta9 components with:
 * - Level-based logging (debug, info, warn, error)
 * - Named component loggers (e.g., 'background', 'delegation', 'mission')
 * - Context injection (task IDs, session IDs, agent names)
 * - Integration with OpenCode's app.log when available
 * - Graceful fallback to console with formatting
 */

// =============================================================================
// Log Levels
// =============================================================================

export type LogLevel = 'debug' | 'info' | 'warn' | 'error'

const LOG_LEVEL_PRIORITY: Record<LogLevel, number> = {
  debug: 0,
  info: 1,
  warn: 2,
  error: 3,
}

// =============================================================================
// Logger Interface
// =============================================================================

export interface Logger {
  debug(message: string, data?: Record<string, unknown>): void
  info(message: string, data?: Record<string, unknown>): void
  warn(message: string, data?: Record<string, unknown>): void
  error(message: string, data?: Record<string, unknown>): void
  child(context: Record<string, unknown>): Logger
}

// =============================================================================
// Console Logger (Development Fallback)
// =============================================================================

function formatMessage(
  level: LogLevel,
  message: string,
  context: Record<string, unknown>,
  data?: Record<string, unknown>
): string {
  // Compact timestamp: HH:MM:SS.mmm
  const timestamp = new Date().toISOString().slice(11, 23)

  // Extract component name from context for cleaner display
  const { component, ...restContext } = context
  const componentStr = component ? `:${component}` : ''
  const prefix = `${timestamp} [delta9${componentStr}] [${level.toUpperCase()}]`

  // Format remaining context as key=value pairs (more readable than JSON)
  const contextPairs = Object.entries(restContext)
    .filter(([, v]) => v !== undefined)
    .map(([k, v]) => `${k}=${typeof v === 'object' ? JSON.stringify(v) : v}`)

  const dataPairs = data
    ? Object.entries(data)
        .filter(([, v]) => v !== undefined)
        .map(([k, v]) => `${k}=${typeof v === 'object' ? JSON.stringify(v) : v}`)
    : []

  const allPairs = [...contextPairs, ...dataPairs]
  const pairsStr = allPairs.length > 0 ? ` | ${allPairs.join(' ')}` : ''

  return `${prefix} ${message}${pairsStr}`
}

function createConsoleLogger(
  context: Record<string, unknown> = {},
  minLevel: LogLevel = 'debug'
): Logger {
  const shouldLog = (level: LogLevel): boolean => {
    return LOG_LEVEL_PRIORITY[level] >= LOG_LEVEL_PRIORITY[minLevel]
  }

  return {
    debug(message: string, data?: Record<string, unknown>): void {
      if (shouldLog('debug')) {
        console.debug(formatMessage('debug', message, context, data))
      }
    },

    info(message: string, data?: Record<string, unknown>): void {
      if (shouldLog('info')) {
        console.info(formatMessage('info', message, context, data))
      }
    },

    warn(message: string, data?: Record<string, unknown>): void {
      if (shouldLog('warn')) {
        console.warn(formatMessage('warn', message, context, data))
      }
    },

    error(message: string, data?: Record<string, unknown>): void {
      if (shouldLog('error')) {
        console.error(formatMessage('error', message, context, data))
      }
    },

    child(childContext: Record<string, unknown>): Logger {
      return createConsoleLogger({ ...context, ...childContext }, minLevel)
    },
  }
}

// =============================================================================
// OpenCode Logger Wrapper
// =============================================================================

// Re-export OpenCodeClient from background-manager for type compatibility
export type { OpenCodeClient } from './background-manager.js'
import type { OpenCodeClient } from './background-manager.js'

function createOpenCodeLogger(
  client: OpenCodeClient,
  context: Record<string, unknown> = {},
  minLevel: LogLevel = 'info'
): Logger {
  const shouldLog = (level: LogLevel): boolean => {
    return LOG_LEVEL_PRIORITY[level] >= LOG_LEVEL_PRIORITY[minLevel]
  }

  // Check if app.log is available
  const hasAppLog = client.app && typeof client.app.log === 'function'

  const log = (level: LogLevel, message: string, data?: Record<string, unknown>): void => {
    if (!shouldLog(level)) return

    const formatted = formatMessage(level, message, context, data)

    // Use OpenCode's log function if available
    if (hasAppLog && client.app) {
      client.app.log(formatted)
    }

    // Also log to console (always, since app.log may not be available)
    const consoleFn =
      level === 'error'
        ? console.error
        : level === 'warn'
          ? console.warn
          : level === 'debug'
            ? console.debug
            : console.info
    consoleFn(formatted)
  }

  return {
    debug(message: string, data?: Record<string, unknown>): void {
      log('debug', message, data)
    },

    info(message: string, data?: Record<string, unknown>): void {
      log('info', message, data)
    },

    warn(message: string, data?: Record<string, unknown>): void {
      log('warn', message, data)
    },

    error(message: string, data?: Record<string, unknown>): void {
      log('error', message, data)
    },

    child(childContext: Record<string, unknown>): Logger {
      return createOpenCodeLogger(client, { ...context, ...childContext }, minLevel)
    },
  }
}

// =============================================================================
// Factory Functions
// =============================================================================

/**
 * Create a logger instance.
 *
 * @param client - OpenCode client (optional, uses console if not provided)
 * @param context - Initial context for the logger
 * @param minLevel - Minimum log level to output
 */
export function createLogger(
  client?: OpenCodeClient,
  context: Record<string, unknown> = {},
  minLevel: LogLevel = 'info'
): Logger {
  if (client) {
    return createOpenCodeLogger(client, context, minLevel)
  }
  return createConsoleLogger(context, minLevel)
}

// =============================================================================
// Default Logger
// =============================================================================

let defaultLogger: Logger = createConsoleLogger()

/**
 * Set the default logger instance
 */
export function setDefaultLogger(logger: Logger): void {
  defaultLogger = logger
}

/**
 * Get the default logger instance
 */
export function getLogger(): Logger {
  return defaultLogger
}

/**
 * Get a named logger for a specific component.
 * The component name will appear in log output: [delta9:component]
 *
 * @example
 * const log = getNamedLogger('background')
 * log.info('Task started', { taskId: 'bg_123' })
 * // Output: 12:34:56.789 [delta9:background] [INFO] Task started | taskId=bg_123
 */
export function getNamedLogger(component: string): Logger {
  return defaultLogger.child({ component })
}

// =============================================================================
// Initialization
// =============================================================================

/**
 * Initialize the default logger with an OpenCode client.
 * Call this once during plugin initialization.
 *
 * @example
 * const Delta9: Plugin = async (ctx) => {
 *   const client = (ctx as unknown as { client?: OpenCodeClient }).client
 *   initLogger(client)
 *   // ... rest of plugin setup
 * }
 */
export function initLogger(client?: OpenCodeClient, minLevel: LogLevel = 'info'): void {
  defaultLogger = createLogger(client, { component: 'core' }, minLevel)
}

// =============================================================================
// Convenience Functions
// =============================================================================

export function debug(message: string, data?: Record<string, unknown>): void {
  defaultLogger.debug(message, data)
}

export function info(message: string, data?: Record<string, unknown>): void {
  defaultLogger.info(message, data)
}

export function warn(message: string, data?: Record<string, unknown>): void {
  defaultLogger.warn(message, data)
}

export function error(message: string, data?: Record<string, unknown>): void {
  defaultLogger.error(message, data)
}

// =============================================================================
// Common Context Types
// =============================================================================

/**
 * Common log context fields for Delta9
 */
export interface Delta9LogContext {
  taskId?: string
  sessionId?: string
  agent?: string
  missionId?: string
  component?: string
  [key: string]: unknown
}
