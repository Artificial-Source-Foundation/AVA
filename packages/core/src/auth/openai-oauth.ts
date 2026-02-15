/**
 * OpenAI OAuth Provider (Codex)
 * Enables authentication with ChatGPT Plus/Pro subscriptions
 * Based on OpenCode's codex.ts implementation
 */

import {
  type OAuthAuthorizationResult,
  type OAuthTokenResult,
  OPENAI_OAUTH_CONFIG,
} from './types.js'

interface TokenResponse {
  id_token: string
  access_token: string
  refresh_token: string
  expires_in?: number
}

interface IdTokenClaims {
  chatgpt_account_id?: string
  organizations?: Array<{ id: string }>
  email?: string
  'https://api.openai.com/auth'?: {
    chatgpt_account_id?: string
  }
}

/**
 * Parse JWT claims from token
 */
function parseJwtClaims(token: string): IdTokenClaims | undefined {
  const parts = token.split('.')
  if (parts.length !== 3) return undefined
  try {
    // Node.js Buffer or browser atob
    const decoded =
      typeof Buffer !== 'undefined'
        ? Buffer.from(parts[1], 'base64url').toString()
        : atob(parts[1].replace(/-/g, '+').replace(/_/g, '/'))
    return JSON.parse(decoded)
  } catch {
    return undefined
  }
}

/**
 * Extract account ID from JWT claims
 */
function extractAccountId(tokens: TokenResponse): string | undefined {
  if (tokens.id_token) {
    const claims = parseJwtClaims(tokens.id_token)
    if (claims) {
      return (
        claims.chatgpt_account_id ||
        claims['https://api.openai.com/auth']?.chatgpt_account_id ||
        claims.organizations?.[0]?.id
      )
    }
  }
  if (tokens.access_token) {
    const claims = parseJwtClaims(tokens.access_token)
    if (claims) {
      return (
        claims.chatgpt_account_id ||
        claims['https://api.openai.com/auth']?.chatgpt_account_id ||
        claims.organizations?.[0]?.id
      )
    }
  }
  return undefined
}

/**
 * Start OpenAI OAuth authorization flow using device code flow
 * This works without needing a local callback server
 */
export async function authorizeOpenAI(): Promise<OAuthAuthorizationResult> {
  // Use device code flow for simpler CLI integration
  const deviceResponse = await fetch(
    `${OPENAI_OAUTH_CONFIG.issuer}/api/accounts/deviceauth/usercode`,
    {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'User-Agent': 'ava-cli/0.1.0',
      },
      body: JSON.stringify({ client_id: OPENAI_OAUTH_CONFIG.clientId }),
    }
  )

  if (!deviceResponse.ok) {
    throw new Error(`Failed to initiate device authorization: ${deviceResponse.status}`)
  }

  const deviceData = (await deviceResponse.json()) as {
    device_auth_id: string
    user_code: string
    interval: string
  }

  const pollInterval = Math.max(parseInt(deviceData.interval, 10) || 5, 1) * 1000

  return {
    url: `${OPENAI_OAUTH_CONFIG.issuer}/codex/device`,
    instructions: `Enter code: ${deviceData.user_code}`,
    method: 'auto',
    callback: async (): Promise<OAuthTokenResult> => {
      // Poll for completion
      const maxAttempts = 60 // 5 minutes max
      for (let attempt = 0; attempt < maxAttempts; attempt++) {
        const response = await fetch(
          `${OPENAI_OAUTH_CONFIG.issuer}/api/accounts/deviceauth/token`,
          {
            method: 'POST',
            headers: {
              'Content-Type': 'application/json',
              'User-Agent': 'ava-cli/0.1.0',
            },
            body: JSON.stringify({
              device_auth_id: deviceData.device_auth_id,
              user_code: deviceData.user_code,
            }),
          }
        )

        if (response.ok) {
          const data = (await response.json()) as {
            authorization_code: string
            code_verifier: string
          }

          // Exchange the authorization code for tokens
          const tokenResponse = await fetch(`${OPENAI_OAUTH_CONFIG.issuer}/oauth/token`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: new URLSearchParams({
              grant_type: 'authorization_code',
              code: data.authorization_code,
              redirect_uri: `${OPENAI_OAUTH_CONFIG.issuer}/deviceauth/callback`,
              client_id: OPENAI_OAUTH_CONFIG.clientId,
              code_verifier: data.code_verifier,
            }).toString(),
          })

          if (!tokenResponse.ok) {
            return {
              type: 'failed',
              error: `Token exchange failed: ${tokenResponse.status}`,
            }
          }

          const tokens = (await tokenResponse.json()) as TokenResponse

          return {
            type: 'success',
            accessToken: tokens.access_token,
            refreshToken: tokens.refresh_token,
            expiresAt: Date.now() + (tokens.expires_in ?? 3600) * 1000,
            accountId: extractAccountId(tokens),
          }
        }

        // Still pending - wait and retry
        if (response.status === 403 || response.status === 404) {
          await new Promise((resolve) => setTimeout(resolve, pollInterval + 3000))
          continue
        }

        // Unexpected error
        return { type: 'failed', error: `Device auth polling failed: ${response.status}` }
      }

      return { type: 'failed', error: 'Authorization timeout - took too long' }
    },
  }
}

/**
 * Refresh OpenAI access token using refresh token
 */
export async function refreshOpenAIToken(refreshToken: string): Promise<OAuthTokenResult> {
  try {
    const response = await fetch(`${OPENAI_OAUTH_CONFIG.issuer}/oauth/token`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({
        grant_type: 'refresh_token',
        refresh_token: refreshToken,
        client_id: OPENAI_OAUTH_CONFIG.clientId,
      }).toString(),
    })

    if (!response.ok) {
      return {
        type: 'failed',
        error: `Token refresh failed: ${response.status}`,
      }
    }

    const tokens = (await response.json()) as TokenResponse

    return {
      type: 'success',
      accessToken: tokens.access_token,
      refreshToken: tokens.refresh_token,
      expiresAt: Date.now() + (tokens.expires_in ?? 3600) * 1000,
      accountId: extractAccountId(tokens),
    }
  } catch (error) {
    return {
      type: 'failed',
      error: `Token refresh error: ${error instanceof Error ? error.message : 'Unknown'}`,
    }
  }
}
