import type { LogEntry, LogFieldValue } from './types.js'

function formatValue(value: LogFieldValue): string {
  if (typeof value === 'string') {
    if (value.length === 0) return '""'
    if (/\s/.test(value) || value.includes('"')) return JSON.stringify(value)
    return value
  }
  if (value === null) return 'null'
  return String(value)
}

function formatFields(fields: Record<string, LogFieldValue> | undefined): string {
  if (!fields) return ''
  const entries = Object.entries(fields).filter(([, value]) => value !== undefined)
  if (entries.length === 0) return ''
  return entries.map(([key, value]) => `${key}=${formatValue(value)}`).join(' ')
}

export function formatLogEntry(entry: LogEntry): string {
  const prefix = `[${entry.timestamp}] [${entry.level.toUpperCase()}] [${entry.source}] ${entry.message}`
  const fields = formatFields(entry.data)
  return fields ? `${prefix} | ${fields}` : prefix
}
