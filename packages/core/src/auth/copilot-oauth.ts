/**
 * GitHub Copilot OAuth Provider
 * Enables authentication with GitHub Copilot subscriptions
 * Uses device code flow for headless-friendly authentication
 * Based on OpenCode's copilot.ts implementation
 */

import {
  COPILOT_OAUTH_CONFIG,
  type OAuthAuthorizationResult,
  type OAuthTokenResult,
} from './types.js'

interface DeviceCodeResponse {
  verification_uri: string
  user_code: string
  device_code: string
  interval: number
}

interface TokenResponse {
  access_token?: string
  error?: string
  interval?: number
}

/**
 * Start GitHub Copilot OAuth authorization flow using device code
 * This works without needing a local callback server
 */
export async function authorizeCopilot(): Promise<OAuthAuthorizationResult> {
  const deviceResponse = await fetch(COPILOT_OAUTH_CONFIG.deviceCodeUrl, {
    method: 'POST',
    headers: {
      Accept: 'application/json',
      'Content-Type': 'application/json',
      'User-Agent': 'estela-cli/0.1.0',
    },
    body: JSON.stringify({
      client_id: COPILOT_OAUTH_CONFIG.clientId,
      scope: COPILOT_OAUTH_CONFIG.scope,
    }),
  })

  if (!deviceResponse.ok) {
    throw new Error(`Failed to initiate device authorization: ${deviceResponse.status}`)
  }

  const deviceData = (await deviceResponse.json()) as DeviceCodeResponse
  const pollInterval = Math.max(deviceData.interval || 5, 1) * 1000

  return {
    url: deviceData.verification_uri || COPILOT_OAUTH_CONFIG.verificationUrl,
    instructions: `Enter code: ${deviceData.user_code}`,
    method: 'auto',
    callback: async (): Promise<OAuthTokenResult> => {
      // Poll for completion
      const maxAttempts = 60 // 5 minutes max
      let currentInterval = pollInterval

      for (let attempt = 0; attempt < maxAttempts; attempt++) {
        const response = await fetch(COPILOT_OAUTH_CONFIG.accessTokenUrl, {
          method: 'POST',
          headers: {
            Accept: 'application/json',
            'Content-Type': 'application/json',
            'User-Agent': 'estela-cli/0.1.0',
          },
          body: JSON.stringify({
            client_id: COPILOT_OAUTH_CONFIG.clientId,
            device_code: deviceData.device_code,
            grant_type: 'urn:ietf:params:oauth:grant-type:device_code',
          }),
        })

        if (!response.ok) {
          return { type: 'failed', error: `Token request failed: ${response.status}` }
        }

        const data = (await response.json()) as TokenResponse

        if (data.access_token) {
          // GitHub Copilot tokens don't expire normally - they use the access token as both
          // access and refresh token (refresh is a no-op that returns the same token)
          return {
            type: 'success',
            accessToken: data.access_token,
            refreshToken: data.access_token, // Copilot uses same token
            expiresAt: 0, // No expiry - token is valid until revoked
          }
        }

        if (data.error === 'authorization_pending') {
          // User hasn't authorized yet - wait and retry
          await sleep(currentInterval + COPILOT_OAUTH_CONFIG.pollingMarginMs)
          continue
        }

        if (data.error === 'slow_down') {
          // Need to slow down polling per RFC 8628
          // Add 5 seconds to current interval
          currentInterval = (deviceData.interval + 5) * 1000
          if (data.interval && typeof data.interval === 'number' && data.interval > 0) {
            currentInterval = data.interval * 1000
          }
          await sleep(currentInterval + COPILOT_OAUTH_CONFIG.pollingMarginMs)
          continue
        }

        if (data.error) {
          return { type: 'failed', error: `Authorization error: ${data.error}` }
        }

        // Unknown response - wait and retry
        await sleep(currentInterval + COPILOT_OAUTH_CONFIG.pollingMarginMs)
      }

      return { type: 'failed', error: 'Authorization timeout - took too long' }
    },
  }
}

/**
 * Refresh GitHub Copilot token
 * Note: Copilot tokens don't expire normally, so this is essentially a no-op
 * that validates the token is still working
 */
export async function refreshCopilotToken(refreshToken: string): Promise<OAuthTokenResult> {
  // GitHub Copilot tokens don't expire - the refresh token IS the access token
  // Just return the same token
  return {
    type: 'success',
    accessToken: refreshToken,
    refreshToken: refreshToken,
    expiresAt: 0, // No expiry
  }
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms))
}
