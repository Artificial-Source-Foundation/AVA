/**
 * OAuth Configuration and PKCE Utilities
 *
 * Types, provider configs, and cryptographic helpers for OAuth flows.
 */

import type { LLMProvider } from '../../types/llm'

// ============================================================================
// Types
// ============================================================================

export interface OAuthConfig {
  clientId: string
  authorizationUrl: string
  tokenUrl: string
  scopes: string[]
  redirectPort: number
  redirectPath?: string // defaults to '/callback'
  extraAuthParams?: Record<string, string>
  flow?: 'pkce' | 'device-code'
}

/** Device code flow response from GitHub */
export interface DeviceCodeResponse {
  deviceCode: string
  userCode: string
  verificationUri: string
  expiresIn: number
  interval: number
}

export interface PKCEParams {
  verifier: string
  challenge: string
  state: string
}

export interface OAuthTokens {
  accessToken: string
  refreshToken?: string
  expiresAt?: number
  /** JWT id_token from OpenID Connect (contains account/org claims) */
  idToken?: string
}

/** Response shape from the Rust oauth_listen command */
export interface RustOAuthCallback {
  code: string
  state: string
}

// ============================================================================
// OAuth Configurations
// ============================================================================

export const OAUTH_CONFIGS: Record<string, OAuthConfig> = {
  openai: {
    // OpenAI Codex OAuth (ChatGPT Plus/Pro) — same client as openai/codex CLI
    clientId: 'app_EMoamEEZ73f0CkXaXp7hrann',
    authorizationUrl: 'https://auth.openai.com/oauth/authorize',
    tokenUrl: 'https://auth.openai.com/oauth/token',
    scopes: ['openid', 'profile', 'email', 'offline_access'],
    redirectPort: 1455,
    redirectPath: '/auth/callback',
    extraAuthParams: {
      id_token_add_organizations: 'true',
      codex_cli_simplified_flow: 'true',
      originator: 'codex_cli_rs',
    },
    flow: 'pkce',
  },
  copilot: {
    // GitHub Copilot Device Code flow
    clientId: 'Iv1.b507a08c87ecfe98', // GitHub Copilot CLI client ID
    authorizationUrl: 'https://github.com/login/device/code',
    tokenUrl: 'https://github.com/login/oauth/access_token',
    scopes: ['read:user'],
    redirectPort: 0, // Not used for device code flow
    flow: 'device-code',
  },
}

// ============================================================================
// PKCE Utilities
// ============================================================================

/**
 * Generate cryptographically secure random string
 */
export function generateRandomString(length: number): string {
  const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~'
  const array = new Uint8Array(length)
  crypto.getRandomValues(array)
  return Array.from(array, (byte) => chars[byte % chars.length]).join('')
}

/**
 * Generate SHA-256 hash and base64url encode it
 */
export async function sha256Base64Url(input: string): Promise<string> {
  const encoder = new TextEncoder()
  const data = encoder.encode(input)
  const hashBuffer = await crypto.subtle.digest('SHA-256', data)
  const hashArray = new Uint8Array(hashBuffer)
  const base64 = btoa(String.fromCharCode(...hashArray))
  // Convert to base64url
  return base64.replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '')
}

/**
 * Generate PKCE parameters (verifier, challenge, state)
 */
export async function generatePKCE(): Promise<PKCEParams> {
  const verifier = generateRandomString(64)
  const challenge = await sha256Base64Url(verifier)
  const state = generateRandomString(32)

  return { verifier, challenge, state }
}

// ============================================================================
// JWT Utilities (for OpenID Connect id_token parsing)
// ============================================================================

/**
 * Decode JWT payload without signature verification.
 * Safe for extracting claims from tokens already validated by the auth server.
 */
export function decodeJwtPayload(jwt: string): Record<string, unknown> {
  const parts = jwt.split('.')
  if (parts.length !== 3) return {}
  try {
    const payload = parts[1].replace(/-/g, '+').replace(/_/g, '/')
    const padded = payload + '='.repeat((4 - (payload.length % 4)) % 4)
    return JSON.parse(atob(padded)) as Record<string, unknown>
  } catch {
    return {}
  }
}

/**
 * Extract ChatGPT account ID from an OpenAI id_token.
 * The id_token contains organization claims when `id_token_add_organizations=true`.
 */
export function extractAccountId(idToken: string): string | undefined {
  const payload = decodeJwtPayload(idToken)
  // Direct field (some responses include it at top level)
  if (typeof payload.chatgpt_account_id === 'string') return payload.chatgpt_account_id
  // In organizations array (from id_token_add_organizations param)
  const orgs = payload.organizations as Array<Record<string, unknown>> | undefined
  if (Array.isArray(orgs) && orgs.length > 0) {
    return orgs[0].id as string | undefined
  }
  return undefined
}

/**
 * Check if OAuth is supported for a provider
 */
export function isOAuthSupported(provider: LLMProvider): boolean {
  return provider in OAUTH_CONFIGS
}

/**
 * Get OAuth config for a provider (for UI display)
 */
export function getOAuthConfig(provider: LLMProvider): OAuthConfig | null {
  return OAUTH_CONFIGS[provider] || null
}
