import { appendFileSync, mkdirSync } from 'node:fs'
import * as os from 'node:os'
import * as path from 'node:path'
import {
  configureLogger,
  createLogger,
  formatLogEntry,
  type LogLevel,
  type SimpleLogger,
} from '@ava/core-v2/logger'

let initialized = false

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
      appendFileSync(logFile, `${formatLogEntry(entry)}\n`)
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
