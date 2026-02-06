/**
 * A2A Bearer Token Authentication
 *
 * Simple bearer token authentication for A2A HTTP endpoints.
 * Extracts token from Authorization header and validates against config.
 */

import type { IncomingMessage } from 'node:http'

// ============================================================================
// Types
// ============================================================================

export interface AuthResult {
  authenticated: boolean
  error?: string
}

// ============================================================================
// Authentication
// ============================================================================

/**
 * Extract bearer token from an HTTP request's Authorization header.
 *
 * @param req - Incoming HTTP request
 * @returns The bearer token string, or null if not present/invalid
 */
export function extractBearerToken(req: IncomingMessage): string | null {
  const authHeader = req.headers.authorization
  if (!authHeader) return null

  const parts = authHeader.split(' ')
  if (parts.length !== 2 || parts[0]!.toLowerCase() !== 'bearer') {
    return null
  }

  const token = parts[1]
  return token ? token : null
}

/**
 * Validate a bearer token against the expected token.
 *
 * @param req - Incoming HTTP request
 * @param expectedToken - The token to validate against
 * @returns Authentication result
 */
export function validateBearerToken(req: IncomingMessage, expectedToken: string): AuthResult {
  const token = extractBearerToken(req)

  if (!token) {
    return {
      authenticated: false,
      error: 'Missing or invalid Authorization header. Expected: Bearer <token>',
    }
  }

  if (!timingSafeEqual(token, expectedToken)) {
    return {
      authenticated: false,
      error: 'Invalid bearer token',
    }
  }

  return { authenticated: true }
}

/**
 * Check if a request requires authentication and validate if so.
 *
 * @param req - Incoming HTTP request
 * @param authToken - Expected token (null = no auth required)
 * @returns Authentication result (always passes if no token configured)
 */
export function checkAuth(req: IncomingMessage, authToken: string | null | undefined): AuthResult {
  // No auth configured → always pass
  if (!authToken) {
    return { authenticated: true }
  }

  return validateBearerToken(req, authToken)
}

// ============================================================================
// Utilities
// ============================================================================

/**
 * Timing-safe string comparison to prevent timing attacks.
 * Falls back to byte-by-byte comparison if crypto is unavailable.
 */
function timingSafeEqual(a: string, b: string): boolean {
  if (a.length !== b.length) return false

  try {
    const bufA = Buffer.from(a)
    const bufB = Buffer.from(b)
    // Use crypto.timingSafeEqual for constant-time comparison
    const { timingSafeEqual: cryptoCompare } = require('node:crypto') as typeof import('crypto')
    return cryptoCompare(bufA, bufB)
  } catch {
    // Fallback: still compare all bytes (not truly constant-time)
    let result = 0
    for (let i = 0; i < a.length; i++) {
      result |= a.charCodeAt(i) ^ b.charCodeAt(i)
    }
    return result === 0
  }
}
