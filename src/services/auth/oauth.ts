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

import { invoke } from '@tauri-apps/api/core'
import { openUrl } from '@tauri-apps/plugin-opener'
import { STORAGE_KEYS } from '../../config/constants'
import type { LLMProvider } from '../../types/llm'
import { logDebug, logError, logInfo, logWarn } from '../logger'

// Re-export public types and utilities from config module
export type { DeviceCodeResponse, OAuthTokens } from './oauth-config'
export {
  decodeJwtPayload,
  extractAccountId,
  getOAuthConfig,
  isOAuthSupported,
} from './oauth-config'

// Re-export token exchange/storage functions
export {
  exchangeCodeForTokens,
  refreshOAuthToken,
  storeOAuthCredentials,
} from './oauth-tokens'

import {
  generatePKCE,
  OAUTH_CONFIGS,
  type OAuthTokens,
  type RustOAuthCallback,
} from './oauth-config'
import { exchangeCodeForTokens, storeOAuthCredentials } from './oauth-tokens'

const LOG_SRC = 'oauth'

export interface OAuthConnectedResult {
  kind: 'connected'
  tokens: OAuthTokens
}

export interface OAuthPendingResult {
  kind: 'pending'
  deviceCode: import('./oauth-config').DeviceCodeResponse
}

export type OAuthFlowResult = OAuthConnectedResult | OAuthPendingResult

// ============================================================================
// PKCE Storage
// ============================================================================

/** Store PKCE params for later verification */
function storePKCE(provider: LLMProvider, params: { verifier: string; state: string }): void {
  localStorage.setItem(`${STORAGE_KEYS.OAUTH_VERIFIER}_${provider}`, params.verifier)
  localStorage.setItem(`${STORAGE_KEYS.OAUTH_STATE}_${provider}`, params.state)
}

// ============================================================================
// URL Builder
// ============================================================================

/** Build OAuth authorization URL */
function buildAuthUrl(
  config: {
    clientId: string
    authorizationUrl: string
    scopes: string[]
    redirectPort: number
    redirectPath?: string
    extraAuthParams?: Record<string, string>
  },
  pkce: { challenge: string; state: string }
): string {
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

// ============================================================================
// OAuth Flows
// ============================================================================

/** Guard: only one PKCE OAuth flow can run at a time (port 1455 is shared) */
let pkceInProgress = false

/**
 * Start OAuth flow - opens browser for user authorization.
 * For PKCE providers: starts Rust callback server, opens browser, waits for
 * redirect, exchanges code for tokens, stores credentials, and returns tokens.
 * For device-code providers (copilot): returns DeviceCodeResponse for UI polling.
 */
export async function startOAuthFlow(provider: LLMProvider): Promise<OAuthFlowResult> {
  const config = OAUTH_CONFIGS[provider]
  if (!config) {
    throw new Error(`OAuth not supported for provider: ${provider}`)
  }

  // Device code flow (GitHub Copilot)
  if (config.flow === 'device-code') {
    return {
      kind: 'pending',
      deviceCode: await startDeviceCodeFlow(provider),
    }
  }

  // Prevent multiple PKCE flows — they share port 1455
  if (pkceInProgress) {
    logWarn(LOG_SRC, `PKCE flow already in progress, rejecting ${provider}`)
    throw new Error('An OAuth flow is already in progress. Complete or close it first.')
  }
  pkceInProgress = true
  logInfo(LOG_SRC, `Starting PKCE OAuth flow for ${provider}`)

  try {
    const pkce = await generatePKCE()
    storePKCE(provider, pkce)
    const authUrl = buildAuthUrl(config, pkce)
    logDebug(LOG_SRC, `Auth URL built for ${provider}`, { port: config.redirectPort })

    // Start the Rust callback server BEFORE opening the browser
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
    await storeOAuthCredentials(provider, tokens)

    return {
      kind: 'connected',
      tokens,
    }
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
export async function startDeviceCodeFlow(
  provider: LLMProvider
): Promise<import('./oauth-config').DeviceCodeResponse> {
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
