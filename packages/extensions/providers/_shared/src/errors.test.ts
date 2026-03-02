import { describe, expect, it, vi } from 'vitest'
import {
  buildHttpError,
  classifyHttpError,
  extractErrorMessage,
  parseRetryAfter,
  parseRetryAfterMs,
} from './errors.js'

describe('classifyHttpError', () => {
  it('classifies 401 as auth', () => {
    expect(classifyHttpError(401)).toBe('auth')
  })

  it('classifies 403 as auth', () => {
    expect(classifyHttpError(403)).toBe('auth')
  })

  it('classifies 429 as rate_limit', () => {
    expect(classifyHttpError(429)).toBe('rate_limit')
  })

  it('classifies 529 as server (Anthropic overload)', () => {
    expect(classifyHttpError(529)).toBe('server')
  })

  it('classifies 500+ as server', () => {
    expect(classifyHttpError(500)).toBe('server')
    expect(classifyHttpError(502)).toBe('server')
    expect(classifyHttpError(503)).toBe('server')
  })

  it('classifies 400 as unknown', () => {
    expect(classifyHttpError(400)).toBe('unknown')
  })
})

describe('extractErrorMessage', () => {
  it('extracts error.message from JSON body', () => {
    const body = JSON.stringify({ error: { message: 'Invalid model' } })
    expect(extractErrorMessage(400, body, 'TestProvider')).toContain('Invalid model')
  })

  it('extracts message from JSON body', () => {
    const body = JSON.stringify({ message: 'Rate limited' })
    expect(extractErrorMessage(429, body, 'TestProvider')).toContain('Rate limited')
  })

  it('falls back to status-based message', () => {
    const msg = extractErrorMessage(429, 'not json', 'TestProvider')
    expect(msg).toContain('Rate limit exceeded')
    expect(msg).toContain('TestProvider')
  })

  it('handles unknown status', () => {
    const msg = extractErrorMessage(418, 'body', 'TestProvider')
    expect(msg).toContain('HTTP 418')
  })
})

describe('parseRetryAfter', () => {
  it('parses numeric seconds', () => {
    expect(parseRetryAfter('30')).toBe(30)
  })

  it('parses decimal seconds (floors)', () => {
    expect(parseRetryAfter('1.5')).toBe(1)
  })

  it('returns 0 for zero', () => {
    expect(parseRetryAfter('0')).toBe(0)
  })

  it('returns undefined for null', () => {
    expect(parseRetryAfter(null)).toBeUndefined()
  })

  it('returns undefined for undefined', () => {
    expect(parseRetryAfter(undefined)).toBeUndefined()
  })

  it('returns undefined for empty string', () => {
    expect(parseRetryAfter('')).toBeUndefined()
  })

  it('parses HTTP date format', () => {
    // Set a known "now" so we can predict the result
    vi.useFakeTimers()
    vi.setSystemTime(new Date('2026-03-02T16:00:00Z'))

    const futureDate = 'Mon, 02 Mar 2026 17:00:00 GMT'
    const result = parseRetryAfter(futureDate)

    // Should be ~3600 seconds (1 hour in the future)
    expect(result).toBe(3600)

    vi.useRealTimers()
  })

  it('returns 0 for HTTP date in the past', () => {
    vi.useFakeTimers()
    vi.setSystemTime(new Date('2026-03-02T18:00:00Z'))

    const pastDate = 'Mon, 02 Mar 2026 17:00:00 GMT'
    const result = parseRetryAfter(pastDate)

    expect(result).toBe(0)

    vi.useRealTimers()
  })

  it('returns undefined for completely invalid string', () => {
    expect(parseRetryAfter('not-a-number-or-date')).toBeUndefined()
  })

  it('clamps negative values to 0', () => {
    expect(parseRetryAfter('-5')).toBe(0)
  })
})

describe('parseRetryAfterMs', () => {
  it('parses retry-after-ms header (milliseconds)', () => {
    const headers = new Headers({ 'retry-after-ms': '2500' })
    expect(parseRetryAfterMs(headers)).toBe(2500)
  })

  it('prefers retry-after-ms over retry-after', () => {
    const headers = new Headers({
      'retry-after-ms': '500',
      'retry-after': '30',
    })
    expect(parseRetryAfterMs(headers)).toBe(500)
  })

  it('falls back to retry-after in seconds', () => {
    const headers = new Headers({ 'retry-after': '5' })
    expect(parseRetryAfterMs(headers)).toBe(5000)
  })

  it('falls back to retry-after as HTTP date', () => {
    vi.useFakeTimers()
    vi.setSystemTime(new Date('2026-03-02T16:59:00Z'))

    const headers = new Headers({
      'retry-after': 'Mon, 02 Mar 2026 17:00:00 GMT',
    })
    const result = parseRetryAfterMs(headers)

    // 60 seconds * 1000 = 60000ms
    expect(result).toBe(60000)

    vi.useRealTimers()
  })

  it('returns undefined when no retry headers present', () => {
    const headers = new Headers({ 'content-type': 'application/json' })
    expect(parseRetryAfterMs(headers)).toBeUndefined()
  })

  it('skips invalid retry-after-ms and falls back', () => {
    const headers = new Headers({
      'retry-after-ms': 'invalid',
      'retry-after': '10',
    })
    expect(parseRetryAfterMs(headers)).toBe(10000)
  })

  it('returns 0 for retry-after-ms of 0', () => {
    const headers = new Headers({ 'retry-after-ms': '0' })
    expect(parseRetryAfterMs(headers)).toBe(0)
  })

  it('floors fractional milliseconds', () => {
    const headers = new Headers({ 'retry-after-ms': '1234.56' })
    expect(parseRetryAfterMs(headers)).toBe(1234)
  })
})

describe('buildHttpError', () => {
  it('builds a complete StreamError', () => {
    const error = buildHttpError(429, '{"message":"slow down"}', 'TestProvider')
    expect(error.type).toBe('rate_limit')
    expect(error.message).toContain('slow down')
    expect(error.status).toBe(429)
  })
})
