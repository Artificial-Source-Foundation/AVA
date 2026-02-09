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

import { invoke } from '@tauri-apps/api/core'
import { open } from '@tauri-apps/plugin-shell'
import { STORAGE_KEYS } from '../../config/constants'
import { syncProviderCredentials } from '../../stores/settings'
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
  redirectPath?: string // defaults to '/callback'
  extraAuthParams?: Record<string, string>
  /** Post-OAuth step: mint an API key from the access token */
  apiKeyUrl?: string
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
}

// ============================================================================
// OAuth Configurations
// ============================================================================

const OAUTH_CONFIGS: Record<string, OAuthConfig> = {
  anthropic: {
    // Claude Max/Pro OAuth — uses same client as querymt/anthropic-auth
    clientId: '9d1c250a-e61b-44d9-88ed-5944d1962f5e',
    authorizationUrl: 'https://claude.ai/oauth/authorize',
    tokenUrl: 'https://console.anthropic.com/v1/oauth/token',
    scopes: ['org:create_api_key', 'user:profile', 'user:inference'],
    redirectPort: 1455,
    apiKeyUrl: 'https://api.anthropic.com/api/oauth/claude_cli/create_api_key',
    flow: 'pkce',
  },
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
// API Key Minting (Anthropic)
// ============================================================================

/**
 * Use an OAuth access token to mint an API key.
 * Anthropic's OAuth flow returns a token that must be exchanged for a real API key
 * via the create_api_key endpoint.
 */
async function mintApiKey(apiKeyUrl: string, accessToken: string): Promise<string> {
  const response = await fetch(apiKeyUrl, {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${accessToken}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ name: 'estela-desktop' }),
  })

  if (!response.ok) {
    const error = await response.text()
    throw new Error(`API key creation failed: ${error}`)
  }

  const data = (await response.json()) as Record<string, unknown>
  const apiKey = data.api_key as string | undefined
  if (!apiKey) {
    throw new Error('API key creation returned no key')
  }

  return apiKey
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

  // Standard PKCE flow with Rust callback server
  const pkce = await generatePKCE()
  storePKCE(provider, pkce)
  const authUrl = buildAuthUrl(config, pkce)

  // Start the Rust callback server BEFORE opening the browser
  // (it waits for the redirect with a 120s timeout)
  const callbackPromise = invoke<RustOAuthCallback>('oauth_listen', {
    port: config.redirectPort,
  })

  // Open the browser for user authorization
  await open(authUrl)

  // Wait for the callback from the Rust server
  const callback = await callbackPromise

  // Exchange the authorization code for tokens
  const tokens = await exchangeCodeForTokens(provider, callback.code, callback.state)

  // For providers with apiKeyUrl (Anthropic), mint an API key from the OAuth token
  if (config.apiKeyUrl) {
    const apiKey = await mintApiKey(config.apiKeyUrl, tokens.accessToken)
    tokens.accessToken = apiKey
  }

  // Store tokens and bridge to core credential store
  storeOAuthCredentials(provider, tokens)

  return tokens
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

  const response = await fetch(config.authorizationUrl, {
    method: 'POST',
    headers: {
      Accept: 'application/json',
      'Content-Type': 'application/x-www-form-urlencoded',
    },
    body: new URLSearchParams({
      client_id: config.clientId,
      scope: config.scopes.join(' '),
    }).toString(),
  })

  if (!response.ok) {
    throw new Error(`Device code request failed: ${response.statusText}`)
  }

  const data = (await response.json()) as Record<string, unknown>

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

  const pollOnce = async (): Promise<OAuthTokens | 'pending' | 'expired'> => {
    const response = await fetch(config.tokenUrl, {
      method: 'POST',
      headers: {
        Accept: 'application/json',
        'Content-Type': 'application/x-www-form-urlencoded',
      },
      body: new URLSearchParams({
        client_id: config.clientId,
        device_code: deviceCode,
        grant_type: 'urn:ietf:params:oauth:grant-type:device_code',
      }).toString(),
    })

    const data = (await response.json()) as Record<string, unknown>

    if (data.access_token) {
      return {
        accessToken: data.access_token as string,
        refreshToken: data.refresh_token as string | undefined,
        expiresAt: data.expires_in ? Date.now() + (data.expires_in as number) * 1000 : undefined,
      }
    }

    const error = data.error as string
    if (error === 'authorization_pending' || error === 'slow_down') {
      return 'pending'
    }

    return 'expired'
  }

  // Poll loop
  while (!signal?.aborted) {
    await new Promise((resolve) => setTimeout(resolve, interval * 1000))
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
      redirect_uri: `http://localhost:${config.redirectPort}${config.redirectPath || '/callback'}`,
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
 * Store OAuth credentials in both frontend store and core credential store
 */
export function storeOAuthCredentials(provider: LLMProvider, tokens: OAuthTokens): void {
  const credentials: Credentials = {
    provider,
    type: 'oauth-token',
    value: tokens.accessToken,
    expiresAt: tokens.expiresAt,
    refreshToken: tokens.refreshToken,
  }

  // Store in frontend credentials store
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

  // Bridge to core credential store so LLM clients can find the token
  syncProviderCredentials(provider, tokens.accessToken)
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
