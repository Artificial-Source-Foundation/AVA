export type StructuredLogLevel = 'debug' | 'info' | 'warn' | 'error'

export type StructuredLogValue = string | number | boolean | null

export interface StructuredLogEntry {
  timestamp: string
  level: StructuredLogLevel
  source: string
  message: string
  fields?: Record<string, StructuredLogValue>
}

const LOG_LEVEL_PRIORITY: Record<StructuredLogLevel, number> = {
  debug: 0,
  info: 1,
  warn: 2,
  error: 3,
}

function formatValue(value: StructuredLogValue): string {
  if (typeof value === 'string') {
    if (value.length === 0) return '""'
    if (/\s/.test(value) || value.includes('"')) return JSON.stringify(value)
    return value
  }
  if (value === null) return 'null'
  return String(value)
}

export function isLogLevelEnabled(level: StructuredLogLevel, minimum: StructuredLogLevel): boolean {
  return LOG_LEVEL_PRIORITY[level] >= LOG_LEVEL_PRIORITY[minimum]
}

export function toStructuredFields(input: unknown): Record<string, StructuredLogValue> | undefined {
  if (input === undefined) return undefined
  if (input === null) return { data: null }
  if (typeof input === 'string') return { data: input }
  if (typeof input === 'number' || typeof input === 'boolean') return { data: input }

  if (typeof input === 'object') {
    const out: Record<string, StructuredLogValue> = {}
    for (const [key, value] of Object.entries(input as Record<string, unknown>)) {
      if (value === undefined) continue
      if (value === null) {
        out[key] = null
      } else if (
        typeof value === 'string' ||
        typeof value === 'number' ||
        typeof value === 'boolean'
      ) {
        out[key] = value
      } else {
        out[key] = JSON.stringify(value)
      }
    }
    return Object.keys(out).length > 0 ? out : undefined
  }

  return { data: String(input) }
}

export function formatLogEntry(entry: StructuredLogEntry): string {
  const prefix = `[${entry.timestamp}] [${entry.level.toUpperCase()}] [${entry.source}] ${entry.message}`
  if (!entry.fields || Object.keys(entry.fields).length === 0) return prefix

  const fields = Object.entries(entry.fields)
    .map(([key, value]) => `${key}=${formatValue(value)}`)
    .join(' ')
  return `${prefix} | ${fields}`
}
