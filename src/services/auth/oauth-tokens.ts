/**
 * OAuth Token Exchange & Storage
 *
 * Handles exchanging authorization codes for tokens, refreshing tokens,
 * and persisting OAuth credentials to both localStorage and the core auth system.
 */

import { STORAGE_KEYS } from '../../config/constants'
import { setStoredAuth } from '../../lib/auth-helpers'
import type { Credentials, LLMProvider } from '../../types/llm'
import { logDebug, logError, logInfo } from '../logger'
import { extractAccountId, OAUTH_CONFIGS, type OAuthTokens } from './oauth-config'

const LOG_SRC = 'oauth'
const AVA_CREDENTIALS_KEY = 'ava_credentials'

// ============================================================================
// PKCE Storage (private to oauth layer)
// ============================================================================

/** Retrieve and clear stored PKCE params */
export function retrievePKCE(provider: LLMProvider): { verifier: string; state: string } | null {
  const verifier = localStorage.getItem(`${STORAGE_KEYS.OAUTH_VERIFIER}_${provider}`)
  const state = localStorage.getItem(`${STORAGE_KEYS.OAUTH_STATE}_${provider}`)

  if (!verifier || !state) return null

  // Clear after retrieval for security
  localStorage.removeItem(`${STORAGE_KEYS.OAUTH_VERIFIER}_${provider}`)
  localStorage.removeItem(`${STORAGE_KEYS.OAUTH_STATE}_${provider}`)

  return { verifier, state }
}

// ============================================================================
// Token Exchange
// ============================================================================

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

  const stored = retrievePKCE(provider)
  if (!stored) {
    throw new Error('No stored PKCE params found. OAuth flow may have expired.')
  }

  // Validate state for CSRF protection
  if (stored.state !== returnedState) {
    logError(LOG_SRC, `State mismatch for ${provider} — possible CSRF`, {
      expected: `${stored.state.slice(0, 8)}...`,
      got: `${returnedState.slice(0, 8)}...`,
    })
    throw new Error('State mismatch. Possible CSRF attack.')
  }
  logDebug(LOG_SRC, `Exchanging code for tokens (${provider})`)

  const response = await fetch(config.tokenUrl, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({
      grant_type: 'authorization_code',
      client_id: config.clientId,
      code,
      redirect_uri: `http://localhost:${config.redirectPort}${config.redirectPath || '/callback'}`,
      code_verifier: stored.verifier,
    }).toString(),
  })

  if (!response.ok) {
    const error = await response.text()
    logError(LOG_SRC, `Token exchange failed for ${provider}`, {
      status: response.status,
      body: error.slice(0, 500),
    })
    throw new Error(`Token exchange failed: ${error}`)
  }

  const data = (await response.json()) as Record<string, unknown>
  logDebug(LOG_SRC, `Token exchange response for ${provider}`, {
    hasAccessToken: !!data.access_token,
    hasRefreshToken: !!data.refresh_token,
    hasIdToken: !!data.id_token,
    expiresIn: data.expires_in,
  })

  return {
    accessToken: data.access_token as string,
    refreshToken: data.refresh_token as string | undefined,
    expiresAt: data.expires_in ? Date.now() + (data.expires_in as number) * 1000 : undefined,
    idToken: data.id_token as string | undefined,
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
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
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

  const data = (await response.json()) as Record<string, unknown>

  return {
    accessToken: data.access_token as string,
    refreshToken: (data.refresh_token as string | undefined) || refreshToken,
    expiresAt: data.expires_in ? Date.now() + (data.expires_in as number) * 1000 : undefined,
    idToken: data.id_token as string | undefined,
  }
}

// ============================================================================
// Credential Storage
// ============================================================================

/**
 * Store OAuth credentials in both frontend store and the Rust credential store.
 */
export async function storeOAuthCredentials(
  provider: LLMProvider,
  tokens: OAuthTokens
): Promise<void> {
  // Store as OAuth in core auth system first so desktop UI state only updates
  // after the authoritative backend credential store is consistent.
  const accountId = tokens.idToken ? extractAccountId(tokens.idToken) : undefined
  logInfo(LOG_SRC, `Storing OAuth credentials for ${provider}`, {
    hasAccountId: !!accountId,
    hasRefreshToken: !!tokens.refreshToken,
    expiresAt: tokens.expiresAt,
  })
  await setStoredAuth(provider, {
    type: 'oauth',
    accessToken: tokens.accessToken,
    refreshToken: tokens.refreshToken,
    expiresAt: tokens.expiresAt,
    accountId,
  })

  // Store in frontend credentials store (for UI state display)
  const credentials: Credentials = {
    provider,
    type: 'oauth-token',
    value: tokens.accessToken,
    expiresAt: tokens.expiresAt,
    refreshToken: tokens.refreshToken,
  }
  const stored =
    localStorage.getItem(AVA_CREDENTIALS_KEY) || localStorage.getItem(STORAGE_KEYS.CREDENTIALS)
  let all: Record<string, Credentials> = {}
  try {
    if (stored) all = JSON.parse(stored)
  } catch {
    all = {}
  }
  all[provider] = credentials
  const serialized = JSON.stringify(all)
  localStorage.setItem(AVA_CREDENTIALS_KEY, serialized)
  localStorage.setItem(STORAGE_KEYS.CREDENTIALS, serialized)
}
