/**
 * Simple source-scoped logger.
 *
 * Structured logging via callback — extensions can route to files, services, etc.
 */

import { formatLogEntry } from './format.js'
import {
  DEFAULT_LOGGER_CONFIG,
  LOG_LEVEL_PRIORITY,
  type LogEntry,
  type LogFieldValue,
  type LoggerConfig,
  type LogLevel,
  type SimpleLogger,
} from './types.js'

let _config: LoggerConfig = { ...DEFAULT_LOGGER_CONFIG }

export function configureLogger(config: Partial<LoggerConfig>): void {
  _config = { ..._config, ...config }
}

export function getLoggerConfig(): LoggerConfig {
  return { ..._config }
}

export function resetLogger(): void {
  _config = { ...DEFAULT_LOGGER_CONFIG }
}

function shouldLog(level: LogLevel): boolean {
  return LOG_LEVEL_PRIORITY[level] >= LOG_LEVEL_PRIORITY[_config.level]
}

function emit(entry: LogEntry): void {
  if (_config.callback) {
    _config.callback(entry)
  }
  if (_config.stderr && typeof process !== 'undefined' && process.stderr) {
    process.stderr.write(`${formatLogEntry(entry)}\n`)
  }
}

function makeLogger(source: string): SimpleLogger {
  const log = (level: LogLevel, message: string, data?: Record<string, LogFieldValue>): void => {
    if (!shouldLog(level)) return
    emit({
      timestamp: new Date().toISOString(),
      level,
      source,
      message,
      data,
    })
  }

  return {
    debug: (msg, data) => log('debug', msg, data),
    info: (msg, data) => log('info', msg, data),
    warn: (msg, data) => log('warn', msg, data),
    error: (msg, data) => log('error', msg, data),
    timing: (label, startMs, data) => {
      log('debug', label, { duration_ms: Date.now() - startMs, ...data })
    },
    time: (label) => {
      const startMs = Date.now()
      return {
        end: (data) => {
          log('info', label, { duration_ms: Date.now() - startMs, ...data })
        },
      }
    },
    child: (subsource) => makeLogger(`${source}:${subsource}`),
  }
}

/** Create a source-scoped logger. */
export function createLogger(source: string): SimpleLogger {
  return makeLogger(source)
}
