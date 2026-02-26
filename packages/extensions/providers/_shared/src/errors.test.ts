import { describe, expect, it } from 'vitest'
import {
  buildHttpError,
  classifyHttpError,
  extractErrorMessage,
  parseRetryAfter,
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
  it('parses numeric value', () => {
    expect(parseRetryAfter('30')).toBe(30)
  })

  it('returns undefined for null', () => {
    expect(parseRetryAfter(null)).toBeUndefined()
  })

  it('returns undefined for non-numeric', () => {
    expect(parseRetryAfter('not-a-number')).toBeUndefined()
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
