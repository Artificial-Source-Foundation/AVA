/**
 * Codex OAuth PKCE Implementation
 * OAuth 2.0 Authorization Code Flow with PKCE for OpenAI Codex
 */

// ============================================================================
// Configuration
// ============================================================================

const CODEX_CONFIG = {
  // Replace with actual client ID from OpenAI developer portal
  clientId: import.meta.env.VITE_CODEX_CLIENT_ID || 'estela-desktop',
  authorizationEndpoint: 'https://auth.openai.com/authorize',
  tokenEndpoint: 'https://auth.openai.com/oauth/token',
  redirectUri: 'estela://oauth/callback', // Custom protocol for Tauri
  scopes: ['openid', 'profile', 'model.request'],
}

// ============================================================================
// PKCE Helpers
// ============================================================================

/**
 * Generate a cryptographically random code verifier (43-128 characters)
 */
function generateCodeVerifier(): string {
  const array = new Uint8Array(32)
  crypto.getRandomValues(array)
  return base64UrlEncode(array)
}

/**
 * Generate code challenge from verifier using SHA-256
 */
async function generateCodeChallenge(verifier: string): Promise<string> {
  const encoder = new TextEncoder()
  const data = encoder.encode(verifier)
  const digest = await crypto.subtle.digest('SHA-256', data)
  return base64UrlEncode(new Uint8Array(digest))
}

/**
 * Base64 URL encode (RFC 4648)
 */
function base64UrlEncode(buffer: Uint8Array): string {
  const base64 = btoa(String.fromCharCode(...buffer))
  return base64.replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '')
}

// ============================================================================
// Session Storage Keys
// ============================================================================

const VERIFIER_KEY = 'estela_oauth_verifier'
const STATE_KEY = 'estela_oauth_state'

// ============================================================================
// OAuth Flow
// ============================================================================

/**
 * Token response from OAuth server
 */
export interface TokenResponse {
  accessToken: string
  refreshToken?: string
  expiresIn: number
  tokenType: string
}

/**
 * Start the OAuth authorization flow
 * Returns the authorization URL to open in browser/webview
 */
export async function startCodexAuth(): Promise<string> {
  // Generate PKCE values
  const verifier = generateCodeVerifier()
  const challenge = await generateCodeChallenge(verifier)

  // Generate state for CSRF protection
  const state = crypto.randomUUID()

  // Store verifier and state for callback validation
  sessionStorage.setItem(VERIFIER_KEY, verifier)
  sessionStorage.setItem(STATE_KEY, state)

  // Build authorization URL
  const params = new URLSearchParams({
    response_type: 'code',
    client_id: CODEX_CONFIG.clientId,
    redirect_uri: CODEX_CONFIG.redirectUri,
    scope: CODEX_CONFIG.scopes.join(' '),
    code_challenge: challenge,
    code_challenge_method: 'S256',
    state,
  })

  return `${CODEX_CONFIG.authorizationEndpoint}?${params}`
}

/**
 * Exchange authorization code for tokens
 * Call this after receiving the OAuth callback
 */
export async function exchangeCodeForTokens(code: string, state: string): Promise<TokenResponse> {
  // Validate state to prevent CSRF
  const storedState = sessionStorage.getItem(STATE_KEY)
  if (state !== storedState) {
    throw new Error('Invalid state parameter. Possible CSRF attack.')
  }

  // Get code verifier
  const verifier = sessionStorage.getItem(VERIFIER_KEY)
  if (!verifier) {
    throw new Error('No code verifier found. Please start the auth flow again.')
  }

  // Exchange code for tokens
  const response = await fetch(CODEX_CONFIG.tokenEndpoint, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded',
    },
    body: new URLSearchParams({
      grant_type: 'authorization_code',
      client_id: CODEX_CONFIG.clientId,
      code,
      redirect_uri: CODEX_CONFIG.redirectUri,
      code_verifier: verifier,
    }),
  })

  if (!response.ok) {
    const error = await response.json().catch(() => ({}))
    throw new Error(
      error.error_description || error.error || `Token exchange failed: ${response.status}`
    )
  }

  const data = await response.json()

  // Clean up session storage
  sessionStorage.removeItem(VERIFIER_KEY)
  sessionStorage.removeItem(STATE_KEY)

  return {
    accessToken: data.access_token,
    refreshToken: data.refresh_token,
    expiresIn: data.expires_in,
    tokenType: data.token_type,
  }
}

/**
 * Refresh an expired access token
 */
export async function refreshCodexToken(refreshToken: string): Promise<TokenResponse> {
  const response = await fetch(CODEX_CONFIG.tokenEndpoint, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded',
    },
    body: new URLSearchParams({
      grant_type: 'refresh_token',
      client_id: CODEX_CONFIG.clientId,
      refresh_token: refreshToken,
    }),
  })

  if (!response.ok) {
    const error = await response.json().catch(() => ({}))
    throw new Error(
      error.error_description || error.error || `Token refresh failed: ${response.status}`
    )
  }

  const data = await response.json()

  return {
    accessToken: data.access_token,
    refreshToken: data.refresh_token || refreshToken, // Keep old refresh token if not rotated
    expiresIn: data.expires_in,
    tokenType: data.token_type,
  }
}

/**
 * Check if there's a pending OAuth flow
 */
export function hasPendingOAuth(): boolean {
  return !!sessionStorage.getItem(VERIFIER_KEY)
}

/**
 * Cancel pending OAuth flow
 */
export function cancelOAuthFlow(): void {
  sessionStorage.removeItem(VERIFIER_KEY)
  sessionStorage.removeItem(STATE_KEY)
}
