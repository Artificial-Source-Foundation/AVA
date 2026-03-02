/**
 * Authentication Manager
 * Coordinates API key and OAuth authentication across providers
 */

import type { LLMProvider } from '@ava/core-v2'
import { getPlatform } from '@ava/core-v2'
import { authorizeCopilot, refreshCopilotToken } from './copilot-oauth.js'
import { authorizeGoogle, refreshGoogleToken } from './google-oauth.js'
import { authorizeOpenAI, refreshOpenAIToken } from './openai-oauth.js'
import type { OAuthProvider, OAuthTokenResult, StoredAuth } from './types.js'

// ============================================================================
// Auth Storage Keys
// ============================================================================

/** Check if tokens need refresh (1 hour buffer). expiresAt=0 means no expiry. */
function needsRefresh(expiresAt: number): boolean {
  if (expiresAt === 0) return false
  const oneHourMs = 60 * 60 * 1000
  return Date.now() > expiresAt - oneHourMs
}

const AUTH_KEY_PREFIX = 'auth-'

function getAuthKey(provider: LLMProvider): string {
  return `${AUTH_KEY_PREFIX}${provider}`
}

/** Core-v2 credential keys — these are what getAuth() reads */
function getCoreTokenKey(provider: LLMProvider): string {
  return `ava:${provider}:oauth_token`
}

function getCoreAccountKey(provider: LLMProvider): string {
  return `ava:${provider}:account_id`
}

/** Sync OAuth token to core-v2 format so getAuth() can find it */
async function syncAuthToCore(provider: LLMProvider, auth: StoredAuth): Promise<void> {
  if (auth.type !== 'oauth') return
  const platform = getPlatform()
  await platform.credentials.set(getCoreTokenKey(provider), auth.accessToken)
  if (auth.accountId) {
    await platform.credentials.set(getCoreAccountKey(provider), auth.accountId)
  }
}

/** Remove core-v2 format keys */
async function removeCoreAuth(provider: LLMProvider): Promise<void> {
  const platform = getPlatform()
  await platform.credentials.delete(getCoreTokenKey(provider))
  await platform.credentials.delete(getCoreAccountKey(provider))
}

// ============================================================================
// Auth Storage
// ============================================================================

/**
 * Get stored auth for a provider
 */
export async function getStoredAuth(provider: LLMProvider): Promise<StoredAuth | null> {
  const platform = getPlatform()
  const data = await platform.credentials.get(getAuthKey(provider))
  if (!data) return null

  try {
    return JSON.parse(data) as StoredAuth
  } catch {
    return null
  }
}

/**
 * Save auth for a provider
 */
export async function setStoredAuth(provider: LLMProvider, auth: StoredAuth): Promise<void> {
  const platform = getPlatform()
  await platform.credentials.set(getAuthKey(provider), JSON.stringify(auth))
}

/**
 * Remove stored auth for a provider (both CLI and core-v2 keys)
 */
export async function removeStoredAuth(provider: LLMProvider): Promise<void> {
  const platform = getPlatform()
  await platform.credentials.delete(getAuthKey(provider))
  await removeCoreAuth(provider)
}

// ============================================================================
// OAuth Flow
// ============================================================================

/**
 * Start OAuth authorization flow for a provider
 */
export async function startOAuthFlow(provider: OAuthProvider) {
  switch (provider) {
    case 'openai':
      return authorizeOpenAI()
    case 'google':
      return authorizeGoogle()
    case 'copilot':
      return authorizeCopilot()
    default:
      throw new Error(`OAuth not supported for provider: ${provider}`)
  }
}

/**
 * Complete OAuth flow and save tokens
 */
export async function completeOAuthFlow(
  provider: OAuthProvider,
  result: OAuthTokenResult
): Promise<boolean> {
  if (result.type !== 'success') {
    return false
  }

  const auth: StoredAuth = {
    type: 'oauth',
    accessToken: result.accessToken,
    refreshToken: result.refreshToken,
    expiresAt: result.expiresAt,
    accountId: result.accountId,
  }

  await setStoredAuth(provider, auth)
  await syncAuthToCore(provider, auth)
  return true
}

// ============================================================================
// Token Management
// ============================================================================

/**
 * Get a valid access token for a provider, refreshing if needed
 * Returns null if no OAuth auth is configured
 */
export async function getValidAccessToken(provider: LLMProvider): Promise<string | null> {
  const auth = await getStoredAuth(provider)

  if (!auth || auth.type !== 'oauth') {
    return null
  }

  // Check if token needs refresh
  if (needsRefresh(auth.expiresAt)) {
    const refreshResult = await refreshToken(provider as OAuthProvider, auth.refreshToken)
    if (refreshResult.type === 'success') {
      // Update stored auth with new tokens (both CLI and core-v2 formats)
      const newAuth: StoredAuth = {
        type: 'oauth',
        accessToken: refreshResult.accessToken,
        refreshToken: refreshResult.refreshToken,
        expiresAt: refreshResult.expiresAt,
        accountId: refreshResult.accountId || auth.accountId,
      }
      await setStoredAuth(provider, newAuth)
      await syncAuthToCore(provider, newAuth)
      return refreshResult.accessToken
    }
    // Refresh failed - return current token anyway (might still work)
    console.warn(`Token refresh failed for ${provider}: ${refreshResult.error}`)
  }

  return auth.accessToken
}

/**
 * Get account ID for OpenAI Codex (needed for API calls)
 */
export async function getAccountId(provider: LLMProvider): Promise<string | null> {
  const auth = await getStoredAuth(provider)
  if (!auth || auth.type !== 'oauth') {
    return null
  }
  return auth.accountId ?? null
}

/**
 * Refresh token for a provider
 */
async function refreshToken(
  provider: OAuthProvider,
  refreshTokenValue: string
): Promise<OAuthTokenResult> {
  switch (provider) {
    case 'openai':
      return refreshOpenAIToken(refreshTokenValue)
    case 'google':
      return refreshGoogleToken(refreshTokenValue)
    case 'copilot':
      return refreshCopilotToken(refreshTokenValue)
    default:
      return { type: 'failed', error: `Refresh not supported for provider: ${provider}` }
  }
}

// ============================================================================
// Auth Status
// ============================================================================

export interface AuthStatus {
  provider: LLMProvider
  isAuthenticated: boolean
  authType: 'api-key' | 'oauth' | 'none'
  expiresAt?: number
}

/**
 * Get auth status for a provider
 */
export async function getAuthStatus(provider: LLMProvider): Promise<AuthStatus> {
  const auth = await getStoredAuth(provider)

  if (!auth) {
    // Check for API key in environment
    const platform = getPlatform()
    const envKey = await platform.credentials.get(`${provider}-api-key`)
    if (envKey) {
      return {
        provider,
        isAuthenticated: true,
        authType: 'api-key',
      }
    }
    return {
      provider,
      isAuthenticated: false,
      authType: 'none',
    }
  }

  return {
    provider,
    isAuthenticated: true,
    authType: auth.type,
    expiresAt: auth.type === 'oauth' ? auth.expiresAt : undefined,
  }
}

/**
 * Check if provider has any valid authentication
 */
export async function isAuthenticated(provider: LLMProvider): Promise<boolean> {
  const status = await getAuthStatus(provider)
  return status.isAuthenticated
}

// ============================================================================
// Migration
// ============================================================================

const OAUTH_PROVIDERS: OAuthProvider[] = ['openai', 'google', 'copilot']

/**
 * Migrate existing CLI OAuth tokens to core-v2 format.
 * For users who ran `ava auth login <provider>` before the dual-write fix:
 * copies the access token from auth-{provider} to ava:{provider}:oauth_token.
 * Safe to call multiple times — skips if core-v2 key already exists.
 */
export async function migrateOAuthCredentials(): Promise<void> {
  const platform = getPlatform()
  for (const provider of OAUTH_PROVIDERS) {
    const coreKey = getCoreTokenKey(provider)
    const existing = await platform.credentials.get(coreKey)
    if (existing) continue

    const stored = await getStoredAuth(provider)
    if (stored && stored.type === 'oauth') {
      await syncAuthToCore(provider, stored)
    }
  }
}
