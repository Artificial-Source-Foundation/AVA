/**
 * Logger
 * Singleton logger with file (NDJSON), stderr, and callback outputs
 */

import { homedir } from 'node:os'
import { join } from 'node:path'
import type { AgentEvent } from '../agent/types.js'
import {
  DEFAULT_LOGGER_CONFIG,
  LOG_LEVEL_PRIORITY,
  type LogEntry,
  type Logger,
  type LoggerConfig,
  type LogLevel,
} from './types.js'

// ============================================================================
// Agent Event → Log Entry Mapping
// ============================================================================

function mapAgentEventToEntry(event: AgentEvent): LogEntry {
  const base = {
    timestamp: new Date(event.timestamp).toISOString(),
    agentEventType: event.type,
    agentId: event.agentId,
  }

  switch (event.type) {
    case 'agent:start':
      return {
        ...base,
        level: 'info',
        message: `Agent started: ${event.goal}`,
        data: { goal: event.goal, config: event.config as unknown as Record<string, unknown> },
      }
    case 'agent:finish':
      return {
        ...base,
        level: event.result.success ? 'info' : 'warn',
        message: `Agent finished: ${event.result.terminateMode}`,
        data: {
          success: event.result.success,
          terminateMode: event.result.terminateMode,
          turns: event.result.turns,
          tokensUsed: event.result.tokensUsed,
          durationMs: event.result.durationMs,
        },
      }
    case 'turn:start':
      return { ...base, level: 'debug', message: `Turn ${event.turn} started` }
    case 'turn:finish':
      return {
        ...base,
        level: 'debug',
        message: `Turn ${event.turn} finished (${event.toolCalls.length} tool calls)`,
        data: { turn: event.turn, toolCallCount: event.toolCalls.length },
      }
    case 'tool:start':
      return {
        ...base,
        level: 'debug',
        message: `Tool ${event.toolName} started`,
        data: { toolName: event.toolName, args: event.args },
      }
    case 'tool:finish':
      return {
        ...base,
        level: event.success ? 'debug' : 'warn',
        message: `Tool ${event.toolName} ${event.success ? 'succeeded' : 'failed'} (${event.durationMs}ms)`,
        data: {
          toolName: event.toolName,
          success: event.success,
          durationMs: event.durationMs,
        },
      }
    case 'tool:error':
      return {
        ...base,
        level: 'error',
        message: `Tool ${event.toolName} error: ${event.error}`,
        data: { toolName: event.toolName, error: event.error },
      }
    case 'tool:metadata':
      return {
        ...base,
        level: 'debug',
        message: `Tool ${event.toolName} metadata${event.title ? `: ${event.title}` : ''}`,
        data: { toolName: event.toolName, metadata: event.metadata },
      }
    case 'thought':
      return {
        ...base,
        level: 'debug',
        message: `Thought: ${event.text.slice(0, 200)}`,
        data: { text: event.text },
      }
    case 'recovery:start':
      return {
        ...base,
        level: 'warn',
        message: `Recovery started: ${event.reason}`,
        data: { reason: event.reason, turn: event.turn },
      }
    case 'recovery:finish':
      return {
        ...base,
        level: event.success ? 'info' : 'warn',
        message: `Recovery ${event.success ? 'succeeded' : 'failed'} (${event.durationMs}ms)`,
        data: { success: event.success, durationMs: event.durationMs },
      }
    case 'validation:start':
      return {
        ...base,
        level: 'info',
        message: `Validation started for ${event.files.length} files`,
        data: { files: event.files },
      }
    case 'validation:result':
      return {
        ...base,
        level: event.passed ? 'info' : 'warn',
        message: `Validation ${event.passed ? 'passed' : 'failed'}: ${event.summary}`,
        data: { passed: event.passed, summary: event.summary },
      }
    case 'validation:finish':
      return {
        ...base,
        level: event.passed ? 'info' : 'warn',
        message: `Validation finished: ${event.passed ? 'passed' : 'failed'} (${event.durationMs}ms)`,
        data: { passed: event.passed, durationMs: event.durationMs },
      }
    case 'provider:switch':
      return {
        ...base,
        level: 'info',
        message: `Provider switched to ${event.provider}/${event.model}`,
        data: { provider: event.provider, model: event.model },
      }
    case 'error':
      return {
        ...base,
        level: 'error',
        message: `Error: ${event.error}`,
        data: { error: event.error, context: event.context },
      }
  }
}

// ============================================================================
// Default Log File Path
// ============================================================================

function getDefaultLogPath(): string {
  const date = new Date().toISOString().slice(0, 10) // YYYY-MM-DD
  return join(homedir(), '.ava', 'logs', `ava-${date}.ndjson`)
}

// ============================================================================
// AVA Logger
// ============================================================================

export class AvaLogger implements Logger {
  private config: LoggerConfig
  private appendFileSync: ((path: string, data: string) => void) | null = null
  private mkdirSync: ((path: string, opts: { recursive: boolean }) => void) | null = null
  private fsInitialized = false

  constructor(config: Partial<LoggerConfig> = {}) {
    this.config = { ...DEFAULT_LOGGER_CONFIG, ...config }
  }

  private initFs(): void {
    if (this.fsInitialized) return
    this.fsInitialized = true

    try {
      // Dynamic import of node:fs to avoid issues in non-Node environments
      // eslint-disable-next-line @typescript-eslint/no-require-imports
      const fs = require('node:fs')
      this.appendFileSync = fs.appendFileSync
      this.mkdirSync = fs.mkdirSync
    } catch {
      // fs not available — file logging disabled
    }
  }

  debug(message: string, data?: Record<string, unknown>): void {
    this.log('debug', message, data)
  }

  info(message: string, data?: Record<string, unknown>): void {
    this.log('info', message, data)
  }

  warn(message: string, data?: Record<string, unknown>): void {
    this.log('warn', message, data)
  }

  error(message: string, data?: Record<string, unknown>): void {
    this.log('error', message, data)
  }

  fromAgentEvent(event: AgentEvent): void {
    const entry = mapAgentEventToEntry(event)
    this.emit(entry)
  }

  private log(level: LogLevel, message: string, data?: Record<string, unknown>): void {
    const entry: LogEntry = {
      timestamp: new Date().toISOString(),
      level,
      message,
      data,
    }
    this.emit(entry)
  }

  private emit(entry: LogEntry): void {
    // Check level filter
    if (LOG_LEVEL_PRIORITY[entry.level] < LOG_LEVEL_PRIORITY[this.config.level]) {
      return
    }

    // Write to file
    if (this.config.file) {
      this.writeToFile(entry)
    }

    // Write to stderr
    if (this.config.stderr) {
      const prefix = `[${entry.level.toUpperCase()}]`
      process.stderr.write(`${prefix} ${entry.message}\n`)
    }

    // Custom callback
    if (this.config.callback) {
      try {
        this.config.callback(entry)
      } catch {
        // Ignore callback errors
      }
    }
  }

  private writeToFile(entry: LogEntry): void {
    this.initFs()
    if (!this.appendFileSync || !this.mkdirSync) return

    const filePath = this.config.filePath ?? getDefaultLogPath()
    const line = `${JSON.stringify(entry)}\n`

    try {
      // Ensure directory exists
      const dir = filePath.substring(0, filePath.lastIndexOf('/'))
      this.mkdirSync(dir, { recursive: true })
      this.appendFileSync(filePath, line)
    } catch {
      // Silently fail — logging should never crash the app
    }
  }

  /**
   * Update logger configuration
   */
  configure(config: Partial<LoggerConfig>): void {
    this.config = { ...this.config, ...config }
  }

  /**
   * Get current configuration (for testing)
   */
  getConfig(): LoggerConfig {
    return { ...this.config }
  }
}

// ============================================================================
// Simple Console Logger (source-scoped)
// ============================================================================

interface SimpleLogger {
  debug(message: string, data?: Record<string, unknown>): void
  info(message: string, data?: Record<string, unknown>): void
  warn(message: string, data?: Record<string, unknown>): void
  error(message: string, data?: Record<string, unknown>): void
  timing(label: string, startMs: number, data?: Record<string, unknown>): void
  child(subsource: string): SimpleLogger
}

function formatLogData(data: Record<string, unknown>): string {
  const parts: string[] = []
  for (const [key, value] of Object.entries(data)) {
    if (value === undefined) continue
    if (typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean') {
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

/**
 * Create a simple console logger scoped to a source module.
 * Used for inline logging (e.g., `this.log.info(...)` in agent loop).
 */
export function createLogger(source: string): SimpleLogger {
  function shouldLog(level: LogLevel): boolean {
    return LOG_LEVEL_PRIORITY[level] >= LOG_LEVEL_PRIORITY['info']
  }

  function log(level: LogLevel, message: string, data?: Record<string, unknown>): void {
    if (!shouldLog(level)) return
    const prefix = `[${source}]`
    const dataStr = data ? ` | ${formatLogData(data)}` : ''
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
    debug(message: string, data?: Record<string, unknown>) {
      log('debug', message, data)
    },
    info(message: string, data?: Record<string, unknown>) {
      log('info', message, data)
    },
    warn(message: string, data?: Record<string, unknown>) {
      log('warn', message, data)
    },
    error(message: string, data?: Record<string, unknown>) {
      log('error', message, data)
    },
    timing(label: string, startMs: number, data?: Record<string, unknown>) {
      const durationMs = Math.round(performance.now() - startMs)
      log('debug', `${label} completed`, { ...data, durationMs })
    },
    child(subsource: string): SimpleLogger {
      return createLogger(`${source}:${subsource}`)
    },
  }
}

// ============================================================================
// Singleton Instance
// ============================================================================

let _instance: AvaLogger | null = null

export function getLogger(): AvaLogger {
  if (!_instance) {
    _instance = new AvaLogger()
  }
  return _instance
}

export function setLogger(logger: AvaLogger | null): void {
  _instance = logger
}

export function resetLogger(): void {
  _instance = null
}
