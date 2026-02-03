/**
 * Authentication Manager
 * Coordinates API key and OAuth authentication across providers
 */

import { getPlatform } from '../platform.js'
import type { LLMProvider } from '../types/llm.js'
import { authorizeAnthropic, needsRefresh, refreshAnthropicToken } from './anthropic-oauth.js'
import { authorizeCopilot, refreshCopilotToken } from './copilot-oauth.js'
import { authorizeGoogle, refreshGoogleToken } from './google-oauth.js'
import { authorizeOpenAI, refreshOpenAIToken } from './openai-oauth.js'
import type { OAuthProvider, OAuthTokenResult, StoredAuth } from './types.js'

// ============================================================================
// Auth Storage Keys
// ============================================================================

const AUTH_KEY_PREFIX = 'auth-'

function getAuthKey(provider: LLMProvider): string {
  return `${AUTH_KEY_PREFIX}${provider}`
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
 * Remove stored auth for a provider
 */
export async function removeStoredAuth(provider: LLMProvider): Promise<void> {
  const platform = getPlatform()
  await platform.credentials.delete(getAuthKey(provider))
}

// ============================================================================
// OAuth Flow
// ============================================================================

/**
 * Start OAuth authorization flow for a provider
 */
export async function startOAuthFlow(provider: OAuthProvider) {
  switch (provider) {
    case 'anthropic':
      return authorizeAnthropic()
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
      // Update stored auth with new tokens
      const newAuth: StoredAuth = {
        type: 'oauth',
        accessToken: refreshResult.accessToken,
        refreshToken: refreshResult.refreshToken,
        expiresAt: refreshResult.expiresAt,
        accountId: refreshResult.accountId || auth.accountId,
      }
      await setStoredAuth(provider, newAuth)
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
    case 'anthropic':
      return refreshAnthropicToken(refreshTokenValue)
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
