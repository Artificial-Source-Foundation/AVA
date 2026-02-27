/**
 * OAuth Service for LLM Providers
 *
 * Implements OAuth 2.0 with PKCE for:
 * - OpenAI Codex (ChatGPT Plus/Pro)
 *
 * And Device Code flow for:
 * - GitHub Copilot
 *
 * Based on research from:
 * - https://github.com/numman-ali/opencode-openai-codex-auth
 */

import { setStoredAuth } from '@ava/core'
import { invoke } from '@tauri-apps/api/core'
import { openUrl } from '@tauri-apps/plugin-opener'
import { STORAGE_KEYS } from '../../config/constants'
import type { Credentials, LLMProvider } from '../../types/llm'
import { logDebug, logError, logInfo, logWarn } from '../logger'

const LOG_SRC = 'oauth'
const AVA_CREDENTIALS_KEY = 'ava_credentials'

// ============================================================================
// Types
// ============================================================================

interface OAuthConfig {
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

interface PKCEParams {
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

// ============================================================================
// OAuth Configurations
// ============================================================================

const OAUTH_CONFIGS: Record<string, OAuthConfig> = {
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
    redirect_uri: `http://localhost:${config.redirectPort}${config.redirectPath || '/callback'}`,
    scope: config.scopes.join(' '),
    state: pkce.state,
    code_challenge: pkce.challenge,
    code_challenge_method: 'S256',
    ...config.extraAuthParams,
  })

  return `${config.authorizationUrl}?${params.toString()}`
}

/** Response shape from the Rust oauth_listen command */
interface RustOAuthCallback {
  code: string
  state: string
}

/** Guard: only one PKCE OAuth flow can run at a time (port 1455 is shared) */
let pkceInProgress = false

/**
 * Start OAuth flow - opens browser for user authorization.
 * For PKCE providers: starts Rust callback server, opens browser, waits for
 * redirect, exchanges code for tokens, stores credentials, and returns tokens.
 * For device-code providers (copilot): returns DeviceCodeResponse for UI polling.
 */
export async function startOAuthFlow(
  provider: LLMProvider
): Promise<DeviceCodeResponse | OAuthTokens> {
  const config = OAUTH_CONFIGS[provider]
  if (!config) {
    throw new Error(`OAuth not supported for provider: ${provider}`)
  }

  // Device code flow (GitHub Copilot)
  if (config.flow === 'device-code') {
    return startDeviceCodeFlow(provider)
  }

  // Prevent multiple PKCE flows — they share port 1455
  if (pkceInProgress) {
    logWarn(LOG_SRC, `PKCE flow already in progress, rejecting ${provider}`)
    throw new Error('An OAuth flow is already in progress. Complete or close it first.')
  }
  pkceInProgress = true
  logInfo(LOG_SRC, `Starting PKCE OAuth flow for ${provider}`)

  try {
    // Standard PKCE flow with Rust callback server
    const pkce = await generatePKCE()
    storePKCE(provider, pkce)
    const authUrl = buildAuthUrl(config, pkce)
    logDebug(LOG_SRC, `Auth URL built for ${provider}`, { port: config.redirectPort })

    // Start the Rust callback server BEFORE opening the browser
    // (it waits for the redirect with a 120s timeout)
    const callbackPromise = invoke<RustOAuthCallback>('oauth_listen', {
      port: config.redirectPort,
    })

    // Open the browser for user authorization
    await openUrl(authUrl)
    logDebug(LOG_SRC, `Browser opened for ${provider} authorization`)

    // Wait for the callback from the Rust server
    const callback = await callbackPromise
    logDebug(LOG_SRC, `Callback received for ${provider}`, { hasCode: !!callback.code })

    // Exchange the authorization code for tokens
    const tokens = await exchangeCodeForTokens(provider, callback.code, callback.state)
    logInfo(LOG_SRC, `Token exchange successful for ${provider}`, {
      hasRefreshToken: !!tokens.refreshToken,
      hasIdToken: !!tokens.idToken,
      expiresAt: tokens.expiresAt,
    })

    // Store tokens and bridge to core credential store
    storeOAuthCredentials(provider, tokens)

    return tokens
  } catch (err) {
    logError(LOG_SRC, `OAuth flow failed for ${provider}`, {
      error: err instanceof Error ? err.message : String(err),
      stack: err instanceof Error ? err.stack : undefined,
    })
    throw err
  } finally {
    pkceInProgress = false
  }
}

/**
 * Start Device Code flow (for GitHub Copilot)
 * Returns the device code response for UI display
 */
export async function startDeviceCodeFlow(provider: LLMProvider): Promise<DeviceCodeResponse> {
  const config = OAUTH_CONFIGS[provider]
  if (!config) {
    throw new Error(`Device code flow not supported for provider: ${provider}`)
  }
  logInfo(LOG_SRC, `Starting device code flow for ${provider}`)

  let data: Record<string, unknown>
  try {
    data = (await invoke('oauth_copilot_device_start', {
      clientId: config.clientId,
      scope: config.scopes.join(' '),
    })) as Record<string, unknown>
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
    throw new Error(`Device code request failed: ${message}`)
  }

  return {
    deviceCode: data.device_code as string,
    userCode: data.user_code as string,
    verificationUri: data.verification_uri as string,
    expiresIn: data.expires_in as number,
    interval: (data.interval as number) || 5,
  }
}

/**
 * Poll for device code authorization (used by Copilot)
 * Returns tokens when user has authorized, or null if expired
 */
export async function pollDeviceCodeAuth(
  provider: LLMProvider,
  deviceCode: string,
  interval: number,
  signal?: AbortSignal
): Promise<OAuthTokens | null> {
  const config = OAUTH_CONFIGS[provider]
  if (!config) {
    throw new Error(`Device code flow not supported for provider: ${provider}`)
  }

  let currentInterval = interval

  const pollOnce = async (): Promise<OAuthTokens | 'pending' | 'expired'> => {
    let data: Record<string, unknown>
    try {
      data = (await invoke('oauth_copilot_device_poll', {
        clientId: config.clientId,
        deviceCode,
      })) as Record<string, unknown>
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      throw new Error(`Device code poll failed: ${message}`)
    }

    if (data.access_token) {
      return {
        accessToken: data.access_token as string,
        refreshToken: data.refresh_token as string | undefined,
        expiresAt: data.expires_in ? Date.now() + (data.expires_in as number) * 1000 : undefined,
      }
    }

    const error = data.error as string
    if (error === 'slow_down') {
      currentInterval += 5
      return 'pending'
    }

    if (error === 'authorization_pending') {
      return 'pending'
    }

    return 'expired'
  }

  // Poll loop
  while (!signal?.aborted) {
    await new Promise((resolve) => setTimeout(resolve, currentInterval * 1000))
    if (signal?.aborted) return null

    const result = await pollOnce()
    if (result === 'expired') return null
    if (result === 'pending') continue
    return result
  }

  return null
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
    logError(LOG_SRC, `State mismatch for ${provider} — possible CSRF`, {
      expected: `${stored.state.slice(0, 8)}...`,
      got: `${returnedState.slice(0, 8)}...`,
    })
    throw new Error('State mismatch. Possible CSRF attack.')
  }
  logDebug(LOG_SRC, `Exchanging code for tokens (${provider})`)

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

  const data = (await response.json()) as Record<string, unknown>

  return {
    accessToken: data.access_token as string,
    refreshToken: (data.refresh_token as string | undefined) || refreshToken,
    expiresAt: data.expires_in ? Date.now() + (data.expires_in as number) * 1000 : undefined,
    idToken: data.id_token as string | undefined,
  }
}

/**
 * Store OAuth credentials in both frontend store and core credential store.
 *
 * For OpenAI/Copilot: Store as OAuth type in core auth system so provider
 * clients detect `auth.type === 'oauth'` and route to the correct endpoint
 * (e.g. ChatGPT Codex endpoint instead of api.openai.com).
 */
export function storeOAuthCredentials(provider: LLMProvider, tokens: OAuthTokens): void {
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

  // Store as OAuth in core auth system so provider clients
  // see auth.type === 'oauth' and route to the correct endpoint
  const accountId = tokens.idToken ? extractAccountId(tokens.idToken) : undefined
  logInfo(LOG_SRC, `Storing OAuth credentials for ${provider}`, {
    hasAccountId: !!accountId,
    hasRefreshToken: !!tokens.refreshToken,
    expiresAt: tokens.expiresAt,
  })
  setStoredAuth(provider, {
    type: 'oauth',
    accessToken: tokens.accessToken,
    refreshToken: tokens.refreshToken ?? '',
    expiresAt: tokens.expiresAt ?? Date.now() + 3600_000,
    accountId,
  }).catch((e: unknown) => {
    logError(LOG_SRC, `Failed to store OAuth auth for ${provider}`, { error: String(e) })
  })
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
