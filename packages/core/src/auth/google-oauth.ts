/**
 * Google OAuth Provider (Antigravity/Gemini)
 * Enables authentication with Google AI services
 * Based on Gemini CLI's oauth2.ts implementation
 */

import { generatePKCE, generateState } from './pkce.js'
import {
  GOOGLE_OAUTH_CONFIG,
  type OAuthAuthorizationResult,
  type OAuthTokenResult,
} from './types.js'

interface TokenResponse {
  access_token: string
  refresh_token?: string
  expires_in: number
  token_type: string
  id_token?: string
}

/**
 * Start Google OAuth authorization flow
 * Uses manual code entry (headless-friendly)
 */
export async function authorizeGoogle(): Promise<OAuthAuthorizationResult> {
  const pkce = await generatePKCE()
  const state = generateState()

  const params = new URLSearchParams({
    client_id: GOOGLE_OAUTH_CONFIG.clientId,
    redirect_uri: GOOGLE_OAUTH_CONFIG.redirectUrl,
    response_type: 'code',
    scope: GOOGLE_OAUTH_CONFIG.scopes.join(' '),
    access_type: 'offline',
    code_challenge: pkce.challenge,
    code_challenge_method: 'S256',
    state,
  })

  const authUrl = `${GOOGLE_OAUTH_CONFIG.authUrl}?${params.toString()}`

  return {
    url: authUrl,
    instructions: 'Authorize in your browser, then paste the authorization code here.',
    method: 'code',
    callback: async (code?: string): Promise<OAuthTokenResult> => {
      if (!code) {
        return { type: 'failed', error: 'No authorization code provided' }
      }

      // Exchange code for tokens
      const result = await exchangeGoogleCode(code.trim(), pkce.verifier)
      return result
    },
  }
}

/**
 * Exchange authorization code for tokens
 */
async function exchangeGoogleCode(code: string, verifier: string): Promise<OAuthTokenResult> {
  try {
    const response = await fetch(GOOGLE_OAUTH_CONFIG.tokenUrl, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded',
      },
      body: new URLSearchParams({
        grant_type: 'authorization_code',
        code,
        client_id: GOOGLE_OAUTH_CONFIG.clientId,
        client_secret: GOOGLE_OAUTH_CONFIG.clientSecret,
        redirect_uri: GOOGLE_OAUTH_CONFIG.redirectUrl,
        code_verifier: verifier,
      }).toString(),
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
      refreshToken: tokens.refresh_token || '',
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
 * Refresh Google access token using refresh token
 */
export async function refreshGoogleToken(refreshToken: string): Promise<OAuthTokenResult> {
  try {
    const response = await fetch(GOOGLE_OAUTH_CONFIG.tokenUrl, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded',
      },
      body: new URLSearchParams({
        grant_type: 'refresh_token',
        refresh_token: refreshToken,
        client_id: GOOGLE_OAUTH_CONFIG.clientId,
        client_secret: GOOGLE_OAUTH_CONFIG.clientSecret,
      }).toString(),
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
      // Google might not return a new refresh token, keep the old one
      refreshToken: tokens.refresh_token || refreshToken,
      expiresAt: Date.now() + tokens.expires_in * 1000,
    }
  } catch (error) {
    return {
      type: 'failed',
      error: `Token refresh error: ${error instanceof Error ? error.message : 'Unknown'}`,
    }
  }
}
