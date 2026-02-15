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

  it('classifies 500 as server', () => {
    expect(classifyHttpError(500)).toBe('server')
  })

  it('classifies 502 as server', () => {
    expect(classifyHttpError(502)).toBe('server')
  })

  it('classifies 503 as server', () => {
    expect(classifyHttpError(503)).toBe('server')
  })

  it('classifies 529 as server (Anthropic overloaded)', () => {
    expect(classifyHttpError(529)).toBe('server')
  })

  it('classifies 400 as unknown', () => {
    expect(classifyHttpError(400)).toBe('unknown')
  })

  it('classifies 404 as unknown', () => {
    expect(classifyHttpError(404)).toBe('unknown')
  })
})

describe('extractErrorMessage', () => {
  it('extracts OpenAI-format error message', () => {
    const body = JSON.stringify({ error: { message: 'Model not found' } })
    const msg = extractErrorMessage(404, body, 'DeepSeek')
    expect(msg).toBe('DeepSeek API error (404): Model not found')
  })

  it('extracts simple message format', () => {
    const body = JSON.stringify({ message: 'Bad request' })
    const msg = extractErrorMessage(400, body, 'xAI')
    expect(msg).toBe('xAI API error (400): Bad request')
  })

  it('falls back to status-based message for non-JSON', () => {
    const msg = extractErrorMessage(429, 'not json', 'Groq')
    expect(msg).toBe('Groq API error: Rate limit exceeded')
  })

  it('falls back to HTTP status for unknown codes', () => {
    const msg = extractErrorMessage(418, 'teapot', 'Together')
    expect(msg).toBe('Together API error: HTTP 418')
  })

  it('handles known status codes', () => {
    expect(extractErrorMessage(401, '', 'Test')).toContain('Invalid API key')
    expect(extractErrorMessage(403, '', 'Test')).toContain('Access forbidden')
    expect(extractErrorMessage(500, '', 'Test')).toContain('Server error')
    expect(extractErrorMessage(529, '', 'Test')).toContain('overloaded')
  })
})

describe('parseRetryAfter', () => {
  it('parses numeric seconds', () => {
    expect(parseRetryAfter('30')).toBe(30)
  })

  it('returns undefined for null', () => {
    expect(parseRetryAfter(null)).toBeUndefined()
  })

  it('returns undefined for undefined', () => {
    expect(parseRetryAfter(undefined)).toBeUndefined()
  })

  it('returns undefined for non-numeric string', () => {
    expect(parseRetryAfter('not-a-number')).toBeUndefined()
  })

  it('parses zero', () => {
    expect(parseRetryAfter('0')).toBe(0)
  })
})

describe('buildHttpError', () => {
  it('builds a complete StreamError', () => {
    const error = buildHttpError(429, '{"error":{"message":"too many"}}', 'Mistral')
    expect(error.type).toBe('rate_limit')
    expect(error.message).toContain('too many')
    expect(error.status).toBe(429)
  })

  it('builds error for server failure', () => {
    const error = buildHttpError(500, 'Internal Server Error', 'DeepSeek')
    expect(error.type).toBe('server')
    expect(error.message).toContain('Server error')
  })
})
