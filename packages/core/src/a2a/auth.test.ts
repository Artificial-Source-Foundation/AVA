/**
 * A2A Authentication Tests
 */

import type { IncomingMessage } from 'node:http'
import { describe, expect, it } from 'vitest'
import { checkAuth, extractBearerToken, validateBearerToken } from './auth.js'

// ============================================================================
// Helpers
// ============================================================================

function mockReq(authorization?: string): IncomingMessage {
  return {
    headers: authorization !== undefined ? { authorization } : {},
  } as IncomingMessage
}

// ============================================================================
// Tests
// ============================================================================

describe('auth', () => {
  describe('extractBearerToken', () => {
    it('should extract valid bearer token', () => {
      const req = mockReq('Bearer my-secret-token')
      expect(extractBearerToken(req)).toBe('my-secret-token')
    })

    it('should return null for missing header', () => {
      const req = mockReq()
      expect(extractBearerToken(req)).toBeNull()
    })

    it('should return null for non-bearer scheme', () => {
      const req = mockReq('Basic dXNlcjpwYXNz')
      expect(extractBearerToken(req)).toBeNull()
    })

    it('should be case-insensitive for scheme', () => {
      const req = mockReq('bearer my-token')
      expect(extractBearerToken(req)).toBe('my-token')
    })

    it('should return null for malformed header', () => {
      expect(extractBearerToken(mockReq('Bearer'))).toBeNull()
      expect(extractBearerToken(mockReq('Bearer '))).toBeNull()
      expect(extractBearerToken(mockReq('one two three'))).toBeNull()
    })

    it('should return null for empty header', () => {
      const req = mockReq('')
      expect(extractBearerToken(req)).toBeNull()
    })
  })

  describe('validateBearerToken', () => {
    it('should validate correct token', () => {
      const req = mockReq('Bearer correct-token')
      const result = validateBearerToken(req, 'correct-token')

      expect(result.authenticated).toBe(true)
      expect(result.error).toBeUndefined()
    })

    it('should reject incorrect token', () => {
      const req = mockReq('Bearer wrong-token')
      const result = validateBearerToken(req, 'correct-token')

      expect(result.authenticated).toBe(false)
      expect(result.error).toContain('Invalid')
    })

    it('should reject missing token', () => {
      const req = mockReq()
      const result = validateBearerToken(req, 'some-token')

      expect(result.authenticated).toBe(false)
      expect(result.error).toContain('Missing')
    })

    it('should reject tokens of different lengths', () => {
      const req = mockReq('Bearer short')
      const result = validateBearerToken(req, 'much-longer-token')

      expect(result.authenticated).toBe(false)
    })
  })

  describe('checkAuth', () => {
    it('should always pass when no auth token configured', () => {
      const req = mockReq()
      expect(checkAuth(req, null).authenticated).toBe(true)
      expect(checkAuth(req, undefined).authenticated).toBe(true)
    })

    it('should validate when auth token is configured', () => {
      const goodReq = mockReq('Bearer secret')
      expect(checkAuth(goodReq, 'secret').authenticated).toBe(true)

      const badReq = mockReq('Bearer wrong')
      expect(checkAuth(badReq, 'secret').authenticated).toBe(false)
    })

    it('should reject missing auth when token required', () => {
      const req = mockReq()
      const result = checkAuth(req, 'required-token')

      expect(result.authenticated).toBe(false)
    })
  })
})
