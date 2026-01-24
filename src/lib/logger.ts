/**
 * Delta9 Logger
 *
 * Structured logging utility for Delta9.
 * In production, this wraps OpenCode's client.app.log().
 * In development, it falls back to console.
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
  const timestamp = new Date().toISOString()
  const prefix = `[${timestamp}] [Delta9] [${level.toUpperCase()}]`

  const contextStr = Object.keys(context).length > 0
    ? ` ${JSON.stringify(context)}`
    : ''

  const dataStr = data && Object.keys(data).length > 0
    ? ` ${JSON.stringify(data)}`
    : ''

  return `${prefix}${contextStr} ${message}${dataStr}`
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

export interface OpenCodeClient {
  app: {
    log: (message: string) => void
  }
}

function createOpenCodeLogger(
  client: OpenCodeClient,
  context: Record<string, unknown> = {},
  minLevel: LogLevel = 'info'
): Logger {
  const shouldLog = (level: LogLevel): boolean => {
    return LOG_LEVEL_PRIORITY[level] >= LOG_LEVEL_PRIORITY[minLevel]
  }

  const log = (level: LogLevel, message: string, data?: Record<string, unknown>): void => {
    if (!shouldLog(level)) return

    const formatted = formatMessage(level, message, context, data)

    // Use OpenCode's log function
    client.app.log(formatted)

    // Also log to console in development
    if (process.env.NODE_ENV === 'development') {
      const consoleFn = level === 'error' ? console.error
        : level === 'warn' ? console.warn
        : level === 'debug' ? console.debug
        : console.info
      consoleFn(formatted)
    }
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
