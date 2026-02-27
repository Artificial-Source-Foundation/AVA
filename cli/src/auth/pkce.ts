/**
 * PKCE (Proof Key for Code Exchange) Utilities
 * Used for secure OAuth 2.0 authorization code flow
 */

import type { PKCEChallenge } from './types.js'

/**
 * Generate a cryptographically random code verifier
 * RFC 7636 specifies: 43-128 characters from [A-Z] / [a-z] / [0-9] / "-" / "." / "_" / "~"
 */
export function generateCodeVerifier(): string {
  const length = 43
  const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~'
  const randomBytes = crypto.getRandomValues(new Uint8Array(length))
  return Array.from(randomBytes)
    .map((b) => chars[b % chars.length])
    .join('')
}

/**
 * Generate a code challenge from a verifier using SHA-256
 * Returns base64url-encoded hash
 */
export async function generateCodeChallenge(verifier: string): Promise<string> {
  const encoder = new TextEncoder()
  const data = encoder.encode(verifier)
  const hash = await crypto.subtle.digest('SHA-256', data)
  return base64UrlEncode(hash)
}

/**
 * Generate both verifier and challenge for PKCE
 */
export async function generatePKCE(): Promise<PKCEChallenge> {
  const verifier = generateCodeVerifier()
  const challenge = await generateCodeChallenge(verifier)
  return { verifier, challenge }
}

/**
 * Generate a random state string for CSRF protection
 */
export function generateState(): string {
  const randomBytes = crypto.getRandomValues(new Uint8Array(32))
  return base64UrlEncode(randomBytes.buffer)
}

/**
 * Base64url encode an ArrayBuffer
 * RFC 4648 Section 5 - URL-safe base64 without padding
 */
function base64UrlEncode(buffer: ArrayBuffer): string {
  const bytes = new Uint8Array(buffer)
  const binary = String.fromCharCode(...bytes)
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '')
}
