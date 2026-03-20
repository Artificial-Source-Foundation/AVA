/**
 * MCP OAuth Service
 *
 * Handles OAuth token storage, PKCE flow, and refresh logic
 * for MCP servers that require authentication.
 */

export interface OAuthTokenSet {
  accessToken: string
  refreshToken?: string
  expiresAt?: number
  scopes: string[]
  serverName: string
}

export type OAuthStatus = 'none' | 'authorized' | 'expired' | 'error'

// In-memory token store (persisted via Tauri credentials when available)
const tokenStore = new Map<string, OAuthTokenSet>()

/**
 * Generate PKCE code verifier and S256 challenge.
 * Uses Web Crypto API (SubtleCrypto) to compute SHA-256 of the verifier,
 * then base64url-encodes the digest per RFC 7636.
 */
async function generatePKCE(): Promise<{ verifier: string; challenge: string }> {
  const array = new Uint8Array(32)
  crypto.getRandomValues(array)
  const verifier = btoa(String.fromCharCode(...array))
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/, '')

  // S256: challenge = BASE64URL(SHA-256(ASCII(verifier)))
  const encoded = new TextEncoder().encode(verifier)
  const digest = await crypto.subtle.digest('SHA-256', encoded)
  const challenge = btoa(String.fromCharCode(...new Uint8Array(digest)))
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/, '')

  return { verifier, challenge }
}

/**
 * Start OAuth authorization flow for an MCP server.
 * Opens the browser for the user to authorize.
 */
export async function startOAuthFlow(
  serverName: string,
  authUrl: string,
  clientId: string,
  scopes: string[],
  redirectUri: string
): Promise<{ verifier: string; authorizationUrl: string }> {
  const { verifier, challenge } = await generatePKCE()
  const state = crypto.randomUUID()

  const params = new URLSearchParams({
    response_type: 'code',
    client_id: clientId,
    redirect_uri: redirectUri,
    scope: scopes.join(' '),
    state,
    code_challenge: challenge,
    code_challenge_method: 'S256',
  })

  const authorizationUrl = `${authUrl}?${params.toString()}`

  // Store pending state for callback
  sessionStorage.setItem(
    `mcp-oauth-state:${serverName}`,
    JSON.stringify({ state, verifier, scopes })
  )

  return { verifier, authorizationUrl }
}

/**
 * Exchange authorization code for tokens.
 */
export async function exchangeCode(
  serverName: string,
  tokenUrl: string,
  clientId: string,
  code: string,
  redirectUri: string,
  verifier: string
): Promise<OAuthTokenSet> {
  const response = await fetch(tokenUrl, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({
      grant_type: 'authorization_code',
      client_id: clientId,
      code,
      redirect_uri: redirectUri,
      code_verifier: verifier,
    }),
  })

  if (!response.ok) {
    throw new Error(`Token exchange failed: ${response.status}`)
  }

  const data = (await response.json()) as {
    access_token: string
    refresh_token?: string
    expires_in?: number
    scope?: string
  }

  const tokens: OAuthTokenSet = {
    accessToken: data.access_token,
    refreshToken: data.refresh_token,
    expiresAt: data.expires_in ? Date.now() + data.expires_in * 1000 : undefined,
    scopes: data.scope?.split(' ') ?? [],
    serverName,
  }

  tokenStore.set(serverName, tokens)
  persistToken(serverName, tokens)
  return tokens
}

/**
 * Refresh an expired token.
 */
export async function refreshToken(
  serverName: string,
  tokenUrl: string,
  clientId: string
): Promise<OAuthTokenSet | null> {
  const existing = tokenStore.get(serverName)
  if (!existing?.refreshToken) return null

  try {
    const response = await fetch(tokenUrl, {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({
        grant_type: 'refresh_token',
        client_id: clientId,
        refresh_token: existing.refreshToken,
      }),
    })

    if (!response.ok) {
      tokenStore.delete(serverName)
      return null
    }

    const data = (await response.json()) as {
      access_token: string
      refresh_token?: string
      expires_in?: number
    }

    const tokens: OAuthTokenSet = {
      ...existing,
      accessToken: data.access_token,
      refreshToken: data.refresh_token ?? existing.refreshToken,
      expiresAt: data.expires_in ? Date.now() + data.expires_in * 1000 : undefined,
    }

    tokenStore.set(serverName, tokens)
    persistToken(serverName, tokens)
    return tokens
  } catch {
    return null
  }
}

/**
 * Get the current OAuth status for a server.
 */
export function getOAuthStatus(serverName: string): OAuthStatus {
  const tokens = tokenStore.get(serverName)
  if (!tokens) return 'none'
  if (tokens.expiresAt && Date.now() > tokens.expiresAt) return 'expired'
  return 'authorized'
}

/**
 * Get the current access token for a server.
 */
export function getAccessToken(serverName: string): string | null {
  const tokens = tokenStore.get(serverName)
  if (!tokens) return null
  if (tokens.expiresAt && Date.now() > tokens.expiresAt) return null
  return tokens.accessToken
}

/**
 * Revoke authorization for a server.
 */
export function revokeAuth(serverName: string): void {
  tokenStore.delete(serverName)
  removePersistedToken(serverName)
}

// ─── Persistence helpers (localStorage) ──────────────────────────────────────

function persistToken(_serverName: string, tokens: OAuthTokenSet): void {
  localStorage.setItem(`mcp-oauth:${tokens.serverName}`, JSON.stringify(tokens))
}

function removePersistedToken(serverName: string): void {
  localStorage.removeItem(`mcp-oauth:${serverName}`)
}

/**
 * Load persisted tokens on startup.
 */
export function loadPersistedTokens(): void {
  for (let i = 0; i < localStorage.length; i++) {
    const key = localStorage.key(i)
    if (key?.startsWith('mcp-oauth:')) {
      try {
        const tokens = JSON.parse(localStorage.getItem(key)!) as OAuthTokenSet
        tokenStore.set(tokens.serverName, tokens)
      } catch {
        // ignore parse errors
      }
    }
  }
}
