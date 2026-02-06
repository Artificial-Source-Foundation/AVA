/**
 * OAuth Service for LLM Providers
 *
 * Implements OAuth 2.0 with PKCE for:
 * - Anthropic Claude Max/Pro subscriptions
 * - OpenAI Codex (ChatGPT Plus/Pro)
 *
 * Based on research from:
 * - https://github.com/querymt/anthropic-auth
 * - https://github.com/numman-ali/opencode-openai-codex-auth
 */

import { open } from '@tauri-apps/plugin-shell'
import { STORAGE_KEYS } from '../../config/constants'
import type { Credentials, LLMProvider } from '../../types/llm'

// ============================================================================
// Types
// ============================================================================

interface OAuthConfig {
  clientId: string
  authorizationUrl: string
  tokenUrl: string
  scopes: string[]
  redirectPort: number
}

interface PKCEParams {
  verifier: string
  challenge: string
  state: string
}

interface OAuthTokens {
  accessToken: string
  refreshToken?: string
  expiresAt?: number
}

// ============================================================================
// OAuth Configurations
// ============================================================================

const OAUTH_CONFIGS: Record<string, OAuthConfig> = {
  anthropic: {
    // Claude Max/Pro OAuth
    clientId: 'claude-code', // Standard client ID used by Claude Code
    authorizationUrl: 'https://claude.ai/oauth/authorize',
    tokenUrl: 'https://console.anthropic.com/v1/oauth/token',
    scopes: ['user:inference'],
    redirectPort: 8716,
  },
  openai: {
    // OpenAI Codex OAuth (ChatGPT Plus/Pro)
    clientId: 'app_EMoamEEZ73f0CkXaXp7hrann',
    authorizationUrl: 'https://auth.openai.com/oauth/authorize',
    tokenUrl: 'https://auth.openai.com/oauth/token',
    scopes: ['openid', 'profile', 'email', 'offline_access'],
    redirectPort: 8717,
  },
}

// ============================================================================
// PKCE Utilities
// ============================================================================

/**
 * Generate cryptographically secure random string
 */
function generateRandomString(length: number): string {
  const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~'
  const array = new Uint8Array(length)
  crypto.getRandomValues(array)
  return Array.from(array, (byte) => chars[byte % chars.length]).join('')
}

/**
 * Generate SHA-256 hash and base64url encode it
 */
async function sha256Base64Url(input: string): Promise<string> {
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
async function generatePKCE(): Promise<PKCEParams> {
  const verifier = generateRandomString(64)
  const challenge = await sha256Base64Url(verifier)
  const state = generateRandomString(32)

  return { verifier, challenge, state }
}

// ============================================================================
// OAuth Flow
// ============================================================================

/**
 * Store PKCE params for later verification
 */
function storePKCE(provider: LLMProvider, params: PKCEParams): void {
  localStorage.setItem(`${STORAGE_KEYS.OAUTH_VERIFIER}_${provider}`, params.verifier)
  localStorage.setItem(`${STORAGE_KEYS.OAUTH_STATE}_${provider}`, params.state)
}

/**
 * Retrieve and clear stored PKCE params
 */
function retrievePKCE(provider: LLMProvider): { verifier: string; state: string } | null {
  const verifier = localStorage.getItem(`${STORAGE_KEYS.OAUTH_VERIFIER}_${provider}`)
  const state = localStorage.getItem(`${STORAGE_KEYS.OAUTH_STATE}_${provider}`)

  if (!verifier || !state) return null

  // Clear after retrieval for security
  localStorage.removeItem(`${STORAGE_KEYS.OAUTH_VERIFIER}_${provider}`)
  localStorage.removeItem(`${STORAGE_KEYS.OAUTH_STATE}_${provider}`)

  return { verifier, state }
}

/**
 * Build OAuth authorization URL
 */
function buildAuthUrl(config: OAuthConfig, pkce: PKCEParams): string {
  const params = new URLSearchParams({
    client_id: config.clientId,
    response_type: 'code',
    redirect_uri: `http://localhost:${config.redirectPort}/callback`,
    scope: config.scopes.join(' '),
    state: pkce.state,
    code_challenge: pkce.challenge,
    code_challenge_method: 'S256',
  })

  return `${config.authorizationUrl}?${params.toString()}`
}

/**
 * Start OAuth flow - opens browser for user authorization
 */
export async function startOAuthFlow(provider: LLMProvider): Promise<void> {
  const config = OAUTH_CONFIGS[provider]
  if (!config) {
    throw new Error(`OAuth not supported for provider: ${provider}`)
  }

  // Generate PKCE params
  const pkce = await generatePKCE()

  // Store for later verification
  storePKCE(provider, pkce)

  // Build and open authorization URL
  const authUrl = buildAuthUrl(config, pkce)

  // Open in default browser using Tauri
  await open(authUrl)

  // Note: The callback will be handled by a local server
  // that needs to be started before this
  console.log(`OAuth flow started for ${provider}. Waiting for callback...`)
}

/**
 * Exchange authorization code for tokens
 */
export async function exchangeCodeForTokens(
  provider: LLMProvider,
  code: string,
  returnedState: string
): Promise<OAuthTokens> {
  const config = OAUTH_CONFIGS[provider]
  if (!config) {
    throw new Error(`OAuth not supported for provider: ${provider}`)
  }

  // Retrieve stored PKCE params
  const stored = retrievePKCE(provider)
  if (!stored) {
    throw new Error('No stored PKCE params found. OAuth flow may have expired.')
  }

  // Validate state for CSRF protection
  if (stored.state !== returnedState) {
    throw new Error('State mismatch. Possible CSRF attack.')
  }

  // Exchange code for tokens
  const response = await fetch(config.tokenUrl, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded',
    },
    body: new URLSearchParams({
      grant_type: 'authorization_code',
      client_id: config.clientId,
      code,
      redirect_uri: `http://localhost:${config.redirectPort}/callback`,
      code_verifier: stored.verifier,
    }).toString(),
  })

  if (!response.ok) {
    const error = await response.text()
    throw new Error(`Token exchange failed: ${error}`)
  }

  const data = await response.json()

  return {
    accessToken: data.access_token,
    refreshToken: data.refresh_token,
    expiresAt: data.expires_in ? Date.now() + data.expires_in * 1000 : undefined,
  }
}

/**
 * Refresh an OAuth token
 */
export async function refreshOAuthToken(
  provider: LLMProvider,
  refreshToken: string
): Promise<OAuthTokens> {
  const config = OAUTH_CONFIGS[provider]
  if (!config) {
    throw new Error(`OAuth not supported for provider: ${provider}`)
  }

  const response = await fetch(config.tokenUrl, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded',
    },
    body: new URLSearchParams({
      grant_type: 'refresh_token',
      client_id: config.clientId,
      refresh_token: refreshToken,
    }).toString(),
  })

  if (!response.ok) {
    const error = await response.text()
    throw new Error(`Token refresh failed: ${error}`)
  }

  const data = await response.json()

  return {
    accessToken: data.access_token,
    refreshToken: data.refresh_token || refreshToken,
    expiresAt: data.expires_in ? Date.now() + data.expires_in * 1000 : undefined,
  }
}

/**
 * Store OAuth credentials
 */
export function storeOAuthCredentials(provider: LLMProvider, tokens: OAuthTokens): void {
  const credentials: Credentials = {
    provider,
    type: 'oauth-token',
    value: tokens.accessToken,
    expiresAt: tokens.expiresAt,
    refreshToken: tokens.refreshToken,
  }

  const stored = localStorage.getItem('estela_credentials')
  let all: Record<string, Credentials> = {}

  try {
    if (stored) {
      all = JSON.parse(stored)
    }
  } catch {
    all = {}
  }

  all[provider] = credentials
  localStorage.setItem('estela_credentials', JSON.stringify(all))
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
