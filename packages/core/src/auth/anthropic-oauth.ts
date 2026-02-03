/**
 * Anthropic OAuth Provider
 * Enables authentication with Claude Pro/Max subscriptions
 * Based on Plandex's claude_max implementation
 */

import { generatePKCE, generateState } from './pkce.js'
import {
  ANTHROPIC_OAUTH_CONFIG,
  type OAuthAuthorizationResult,
  type OAuthTokenResult,
} from './types.js'

interface TokenResponse {
  access_token: string
  refresh_token: string
  expires_in: number
  token_type: string
}

/**
 * Start Anthropic OAuth authorization flow
 * Opens browser for user to authorize, returns code via callback URL
 */
export async function authorizeAnthropic(): Promise<OAuthAuthorizationResult> {
  const pkce = await generatePKCE()
  const state = generateState()

  const params = new URLSearchParams({
    code: 'true', // Tells Anthropic to show the code to user
    client_id: ANTHROPIC_OAUTH_CONFIG.clientId,
    response_type: 'code',
    scope: ANTHROPIC_OAUTH_CONFIG.scopes,
    redirect_uri: ANTHROPIC_OAUTH_CONFIG.redirectUrl,
    code_challenge: pkce.challenge,
    code_challenge_method: 'S256',
    state,
  })

  const authUrl = `${ANTHROPIC_OAUTH_CONFIG.authorizeUrl}?${params.toString()}`

  return {
    url: authUrl,
    instructions:
      "Click 'Authorize' in your browser, then copy the Authentication Code and paste it back here.",
    method: 'code',
    callback: async (pastedCode?: string): Promise<OAuthTokenResult> => {
      if (!pastedCode) {
        return { type: 'failed', error: 'No authorization code provided' }
      }

      // Parse the code#state format from Anthropic
      const parts = pastedCode.split('#')
      if (parts.length !== 2) {
        return {
          type: 'failed',
          error: 'Invalid code format. Expected format: code#state',
        }
      }

      const [code, pastedState] = parts

      if (!code || pastedState !== state) {
        return {
          type: 'failed',
          error: 'Invalid or mismatched authorization code/state',
        }
      }

      // Exchange code for tokens
      const result = await exchangeAnthropicCode(code, pkce.verifier, state)
      return result
    },
  }
}

/**
 * Exchange authorization code for tokens
 */
async function exchangeAnthropicCode(
  code: string,
  verifier: string,
  state: string
): Promise<OAuthTokenResult> {
  try {
    const response = await fetch(ANTHROPIC_OAUTH_CONFIG.tokenUrl, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'anthropic-beta': ANTHROPIC_OAUTH_CONFIG.betaHeader,
      },
      body: JSON.stringify({
        grant_type: 'authorization_code',
        code,
        state,
        code_verifier: verifier,
        redirect_uri: ANTHROPIC_OAUTH_CONFIG.redirectUrl,
        client_id: ANTHROPIC_OAUTH_CONFIG.clientId,
      }),
    })

    if (!response.ok) {
      const errorBody = await response.text()
      return {
        type: 'failed',
        error: `Token exchange failed: ${response.status} - ${errorBody}`,
      }
    }

    const tokens = (await response.json()) as TokenResponse

    return {
      type: 'success',
      accessToken: tokens.access_token,
      refreshToken: tokens.refresh_token,
      expiresAt: Date.now() + tokens.expires_in * 1000,
    }
  } catch (error) {
    return {
      type: 'failed',
      error: `Token exchange error: ${error instanceof Error ? error.message : 'Unknown'}`,
    }
  }
}

/**
 * Refresh Anthropic access token using refresh token
 */
export async function refreshAnthropicToken(refreshToken: string): Promise<OAuthTokenResult> {
  try {
    const response = await fetch(ANTHROPIC_OAUTH_CONFIG.tokenUrl, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'anthropic-beta': ANTHROPIC_OAUTH_CONFIG.betaHeader,
      },
      body: JSON.stringify({
        grant_type: 'refresh_token',
        refresh_token: refreshToken,
        client_id: ANTHROPIC_OAUTH_CONFIG.clientId,
      }),
    })

    if (!response.ok) {
      const errorBody = await response.text()
      return {
        type: 'failed',
        error: `Token refresh failed: ${response.status} - ${errorBody}`,
      }
    }

    const tokens = (await response.json()) as TokenResponse

    return {
      type: 'success',
      accessToken: tokens.access_token,
      refreshToken: tokens.refresh_token,
      expiresAt: Date.now() + tokens.expires_in * 1000,
    }
  } catch (error) {
    return {
      type: 'failed',
      error: `Token refresh error: ${error instanceof Error ? error.message : 'Unknown'}`,
    }
  }
}

/**
 * Check if tokens need refresh (1 hour buffer)
 */
export function needsRefresh(expiresAt: number): boolean {
  const oneHourMs = 60 * 60 * 1000
  return Date.now() > expiresAt - oneHourMs
}
