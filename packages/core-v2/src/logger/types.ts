/**
 * Logger types.
 */

export type LogLevel = 'debug' | 'info' | 'warn' | 'error'

export const LOG_LEVEL_PRIORITY: Record<LogLevel, number> = {
  debug: 0,
  info: 1,
  warn: 2,
  error: 3,
}

export interface LogEntry {
  timestamp: string
  level: LogLevel
  source: string
  message: string
  data?: Record<string, unknown>
}

export interface LoggerConfig {
  level: LogLevel
  file: boolean
  filePath?: string
  stderr: boolean
  callback?: (entry: LogEntry) => void
}

export const DEFAULT_LOGGER_CONFIG: LoggerConfig = {
  level: 'info',
  file: true,
  stderr: false,
}

export interface SimpleLogger {
  debug(message: string, data?: Record<string, unknown>): void
  info(message: string, data?: Record<string, unknown>): void
  warn(message: string, data?: Record<string, unknown>): void
  error(message: string, data?: Record<string, unknown>): void
  timing(label: string, startMs: number, data?: Record<string, unknown>): void
  child(subsource: string): SimpleLogger
}
