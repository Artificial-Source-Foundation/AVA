import { appendFileSync, mkdirSync } from 'node:fs'
import * as os from 'node:os'
import * as path from 'node:path'
import {
  configureLogger,
  createLogger,
  type LogEntry,
  type LogLevel,
  type SimpleLogger,
} from '@ava/core-v2/logger'

let initialized = false

function formatValue(value: unknown): string {
  if (value === undefined) return 'undefined'
  if (value === null) return 'null'
  if (typeof value === 'string') {
    if (value.length === 0) return '""'
    if (/\s/.test(value) || value.includes('"')) return JSON.stringify(value)
    return value
  }
  if (typeof value === 'object') return JSON.stringify(value)
  return String(value)
}

function formatCliLogEntry(entry: LogEntry): string {
  const prefix = `[${entry.timestamp}] [${entry.level.toUpperCase()}] [${entry.source}] ${entry.message}`
  if (!entry.data) return prefix
  const fields = Object.entries(entry.data)
    .filter(([, value]) => value !== undefined)
    .map(([key, value]) => `${key}=${formatValue(value)}`)
    .join(' ')
  return fields ? `${prefix} | ${fields}` : prefix
}

function getDateStamp(): string {
  const now = new Date()
  return `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}-${String(now.getDate()).padStart(2, '0')}`
}

export function initCliLogger(level: LogLevel = 'info'): void {
  if (initialized) return

  const logDir = path.join(os.homedir(), '.ava', 'logs')
  mkdirSync(logDir, { recursive: true })
  const logFile = path.join(logDir, `cli-${getDateStamp()}.log`)

  configureLogger({
    level,
    stderr: true,
    callback: (entry) => {
      appendFileSync(logFile, `${formatCliLogEntry(entry)}\n`)
    },
  })

  createLogger('cli').info('CLI logger initialized', {
    log_file: logFile,
    pid: process.pid,
  })

  initialized = true
}

export function getCliLogger(source: string): SimpleLogger {
  return createLogger(source)
}
