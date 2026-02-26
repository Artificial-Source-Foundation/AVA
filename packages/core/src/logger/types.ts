/**
 * Logger Types
 * Core types for the logging infrastructure
 */

import type { AgentEvent } from '../agent/types.js'

// ============================================================================
// Log Levels
// ============================================================================

export type LogLevel = 'debug' | 'info' | 'warn' | 'error'

/** Numeric priority for log levels (higher = more severe) */
export const LOG_LEVEL_PRIORITY: Record<LogLevel, number> = {
  debug: 0,
  info: 1,
  warn: 2,
  error: 3,
}

// ============================================================================
// Log Entry
// ============================================================================

export interface LogEntry {
  /** ISO 8601 timestamp */
  timestamp: string
  /** Log level */
  level: LogLevel
  /** Log message */
  message: string
  /** Optional structured data */
  data?: Record<string, unknown>
  /** Source agent event type (if from agent) */
  agentEventType?: string
  /** Agent ID (if from agent) */
  agentId?: string
}

// ============================================================================
// Logger Configuration
// ============================================================================

export interface LoggerConfig {
  /** Minimum log level to emit (default: 'info') */
  level: LogLevel
  /** Write NDJSON to file (default: ~/.ava/logs/ava-YYYY-MM-DD.ndjson) */
  file: boolean
  /** Custom file path (overrides default) */
  filePath?: string
  /** Write to stderr (default: false) */
  stderr: boolean
  /** Custom callback for log entries */
  callback?: (entry: LogEntry) => void
}

export const DEFAULT_LOGGER_CONFIG: LoggerConfig = {
  level: 'info',
  file: true,
  stderr: false,
}

// ============================================================================
// Logger Interface
// ============================================================================

export interface Logger {
  debug(message: string, data?: Record<string, unknown>): void
  info(message: string, data?: Record<string, unknown>): void
  warn(message: string, data?: Record<string, unknown>): void
  error(message: string, data?: Record<string, unknown>): void
  fromAgentEvent(event: AgentEvent): void
}
