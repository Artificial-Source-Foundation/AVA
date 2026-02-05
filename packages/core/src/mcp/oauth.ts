/**
 * MCP OAuth Manager
 *
 * Handles OAuth authentication for MCP servers that require it
 *
 * Based on Cline's McpOAuthManager.ts pattern
 */

import { getPlatform } from '../platform.js'

// ============================================================================
// Types
// ============================================================================

/**
 * OAuth configuration for an MCP server
 */
export interface MCPOAuthConfig {
  /** Server name */
  serverName: string
  /** OAuth client ID */
  clientId: string
  /** OAuth client secret (optional for PKCE) */
  clientSecret?: string
  /** Authorization endpoint */
  authorizationUrl: string
  /** Token endpoint */
  tokenUrl: string
  /** Required scopes */
  scopes: string[]
  /** Redirect URI for OAuth callback */
  redirectUri: string
  /** Use PKCE (Proof Key for Code Exchange) */
  usePkce?: boolean
}

/**
 * Stored OAuth tokens
 */
export interface MCPOAuthTokens {
  /** Access token */
  accessToken: string
  /** Refresh token (if available) */
  refreshToken?: string
  /** Token expiration timestamp */
  expiresAt?: number
  /** Token type (usually "Bearer") */
  tokenType: string
  /** Scopes granted */
  scopes: string[]
}

/**
 * OAuth state stored for verification
 */
interface OAuthState {
  state: string
  codeVerifier?: string
  serverName: string
  createdAt: number
}

/**
 * Token storage format
 */
interface TokenStorage {
  version: number
  tokens: Record<string, MCPOAuthTokens>
  lastModified: number
}

// ============================================================================
// Constants
// ============================================================================

const TOKEN_FILE = '.estela/mcp-tokens.json'
const STATE_EXPIRY_MS = 10 * 60 * 1000 // 10 minutes
const TOKEN_STORAGE_VERSION = 1

// ============================================================================
// In-Memory State
// ============================================================================

/** Pending OAuth states */
const pendingStates = new Map<string, OAuthState>()

/** Cached tokens */
let tokenCache: TokenStorage | null = null
let cacheWorkspaceRoot: string | null = null

// ============================================================================
// Token Storage
// ============================================================================

/**
 * Load tokens from storage
 */
async function loadTokens(workspaceRoot: string): Promise<TokenStorage> {
  if (tokenCache && cacheWorkspaceRoot === workspaceRoot) {
    return tokenCache
  }

  const platform = getPlatform()
  const filePath = `${workspaceRoot}/${TOKEN_FILE}`

  try {
    const exists = await platform.fs.fileExists(filePath)
    if (!exists) {
      tokenCache = { version: TOKEN_STORAGE_VERSION, tokens: {}, lastModified: Date.now() }
      cacheWorkspaceRoot = workspaceRoot
      return tokenCache
    }

    const content = await platform.fs.readFile(filePath)
    tokenCache = JSON.parse(content) as TokenStorage
    cacheWorkspaceRoot = workspaceRoot
    return tokenCache
  } catch (error) {
    console.error('Failed to load MCP OAuth tokens:', error)
    tokenCache = { version: TOKEN_STORAGE_VERSION, tokens: {}, lastModified: Date.now() }
    cacheWorkspaceRoot = workspaceRoot
    return tokenCache
  }
}

/**
 * Save tokens to storage
 */
async function saveTokens(workspaceRoot: string, storage: TokenStorage): Promise<void> {
  const platform = getPlatform()
  const filePath = `${workspaceRoot}/${TOKEN_FILE}`

  storage.lastModified = Date.now()
  const content = JSON.stringify(storage, null, 2)

  await platform.fs.writeFile(filePath, content)
  tokenCache = storage
  cacheWorkspaceRoot = workspaceRoot
}

// ============================================================================
// PKCE Helpers
// ============================================================================

/**
 * Generate a random string for PKCE code verifier
 */
function generateCodeVerifier(): string {
  const array = new Uint8Array(32)
  crypto.getRandomValues(array)
  return base64UrlEncode(array)
}

/**
 * Generate code challenge from verifier
 */
async function generateCodeChallenge(verifier: string): Promise<string> {
  const encoder = new TextEncoder()
  const data = encoder.encode(verifier)
  const hash = await crypto.subtle.digest('SHA-256', data)
  return base64UrlEncode(new Uint8Array(hash))
}

/**
 * Generate a random state parameter
 */
function generateState(): string {
  const array = new Uint8Array(16)
  crypto.getRandomValues(array)
  return base64UrlEncode(array)
}

/**
 * Base64 URL encode
 */
function base64UrlEncode(buffer: Uint8Array): string {
  const base64 = btoa(String.fromCharCode(...buffer))
  return base64.replace(/\+/g, '-').replace(/\//g, '_').replace(/=/g, '')
}

// ============================================================================
// OAuth Flow
// ============================================================================

/**
 * Start the OAuth authorization flow
 * Returns the authorization URL to open in the browser
 */
export async function startOAuthFlow(config: MCPOAuthConfig): Promise<{
  authorizationUrl: string
  state: string
}> {
  const state = generateState()
  let codeVerifier: string | undefined

  // Build authorization URL
  const params = new URLSearchParams({
    client_id: config.clientId,
    redirect_uri: config.redirectUri,
    response_type: 'code',
    scope: config.scopes.join(' '),
    state,
  })

  // Add PKCE if enabled
  if (config.usePkce !== false) {
    codeVerifier = generateCodeVerifier()
    const codeChallenge = await generateCodeChallenge(codeVerifier)
    params.set('code_challenge', codeChallenge)
    params.set('code_challenge_method', 'S256')
  }

  // Store state for verification
  pendingStates.set(state, {
    state,
    codeVerifier,
    serverName: config.serverName,
    createdAt: Date.now(),
  })

  // Clean up old states
  cleanupExpiredStates()

  const authUrl = `${config.authorizationUrl}?${params.toString()}`
  return { authorizationUrl: authUrl, state }
}

/**
 * Complete the OAuth flow by exchanging the authorization code for tokens
 */
export async function completeOAuthFlow(
  workspaceRoot: string,
  config: MCPOAuthConfig,
  code: string,
  state: string
): Promise<MCPOAuthTokens> {
  // Verify state
  const pendingState = pendingStates.get(state)
  if (!pendingState) {
    throw new Error('Invalid or expired OAuth state')
  }

  if (pendingState.serverName !== config.serverName) {
    throw new Error('OAuth state does not match server')
  }

  // Remove used state
  pendingStates.delete(state)

  // Exchange code for tokens
  const tokenParams = new URLSearchParams({
    grant_type: 'authorization_code',
    client_id: config.clientId,
    code,
    redirect_uri: config.redirectUri,
  })

  if (config.clientSecret) {
    tokenParams.set('client_secret', config.clientSecret)
  }

  if (pendingState.codeVerifier) {
    tokenParams.set('code_verifier', pendingState.codeVerifier)
  }

  const response = await fetch(config.tokenUrl, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded',
    },
    body: tokenParams.toString(),
  })

  if (!response.ok) {
    const error = await response.text()
    throw new Error(`Token exchange failed: ${error}`)
  }

  const data = await response.json()

  const tokens: MCPOAuthTokens = {
    accessToken: data.access_token,
    refreshToken: data.refresh_token,
    expiresAt: data.expires_in ? Date.now() + data.expires_in * 1000 : undefined,
    tokenType: data.token_type || 'Bearer',
    scopes: data.scope ? data.scope.split(' ') : config.scopes,
  }

  // Store tokens
  await storeTokens(workspaceRoot, config.serverName, tokens)

  return tokens
}

/**
 * Refresh expired tokens
 */
export async function refreshTokens(
  workspaceRoot: string,
  config: MCPOAuthConfig,
  refreshToken: string
): Promise<MCPOAuthTokens> {
  const tokenParams = new URLSearchParams({
    grant_type: 'refresh_token',
    client_id: config.clientId,
    refresh_token: refreshToken,
  })

  if (config.clientSecret) {
    tokenParams.set('client_secret', config.clientSecret)
  }

  const response = await fetch(config.tokenUrl, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded',
    },
    body: tokenParams.toString(),
  })

  if (!response.ok) {
    const error = await response.text()
    throw new Error(`Token refresh failed: ${error}`)
  }

  const data = await response.json()

  const tokens: MCPOAuthTokens = {
    accessToken: data.access_token,
    refreshToken: data.refresh_token || refreshToken, // Keep old refresh token if not provided
    expiresAt: data.expires_in ? Date.now() + data.expires_in * 1000 : undefined,
    tokenType: data.token_type || 'Bearer',
    scopes: data.scope ? data.scope.split(' ') : config.scopes,
  }

  // Store updated tokens
  await storeTokens(workspaceRoot, config.serverName, tokens)

  return tokens
}

// ============================================================================
// Token Management
// ============================================================================

/**
 * Store tokens for a server
 */
export async function storeTokens(
  workspaceRoot: string,
  serverName: string,
  tokens: MCPOAuthTokens
): Promise<void> {
  const storage = await loadTokens(workspaceRoot)
  storage.tokens[serverName] = tokens
  await saveTokens(workspaceRoot, storage)
}

/**
 * Get stored tokens for a server
 */
export async function getStoredTokens(
  workspaceRoot: string,
  serverName: string
): Promise<MCPOAuthTokens | null> {
  const storage = await loadTokens(workspaceRoot)
  return storage.tokens[serverName] || null
}

/**
 * Check if tokens are expired (with 5 minute buffer)
 */
export function areTokensExpired(tokens: MCPOAuthTokens): boolean {
  if (!tokens.expiresAt) {
    return false // No expiry, assume valid
  }
  const buffer = 5 * 60 * 1000 // 5 minutes
  return Date.now() >= tokens.expiresAt - buffer
}

/**
 * Get valid tokens, refreshing if necessary
 */
export async function getValidTokens(
  workspaceRoot: string,
  config: MCPOAuthConfig
): Promise<MCPOAuthTokens | null> {
  const tokens = await getStoredTokens(workspaceRoot, config.serverName)
  if (!tokens) {
    return null
  }

  if (!areTokensExpired(tokens)) {
    return tokens
  }

  // Try to refresh
  if (tokens.refreshToken) {
    try {
      return await refreshTokens(workspaceRoot, config, tokens.refreshToken)
    } catch (error) {
      console.error('Token refresh failed:', error)
      // Remove invalid tokens
      await removeTokens(workspaceRoot, config.serverName)
      return null
    }
  }

  // Tokens expired and no refresh token
  return null
}

/**
 * Remove stored tokens for a server
 */
export async function removeTokens(workspaceRoot: string, serverName: string): Promise<void> {
  const storage = await loadTokens(workspaceRoot)
  delete storage.tokens[serverName]
  await saveTokens(workspaceRoot, storage)
}

/**
 * Check if a server has stored tokens
 */
export async function hasStoredTokens(workspaceRoot: string, serverName: string): Promise<boolean> {
  const tokens = await getStoredTokens(workspaceRoot, serverName)
  return tokens !== null
}

// ============================================================================
// Cleanup
// ============================================================================

/**
 * Clean up expired OAuth states
 */
function cleanupExpiredStates(): void {
  const now = Date.now()
  for (const [state, data] of pendingStates) {
    if (now - data.createdAt > STATE_EXPIRY_MS) {
      pendingStates.delete(state)
    }
  }
}

/**
 * Clear all pending OAuth states
 */
export function clearPendingStates(): void {
  pendingStates.clear()
}

// ============================================================================
// Integration with MCP Client
// ============================================================================

/**
 * Get authorization header for MCP server
 */
export async function getAuthorizationHeader(
  workspaceRoot: string,
  config: MCPOAuthConfig
): Promise<string | null> {
  const tokens = await getValidTokens(workspaceRoot, config)
  if (!tokens) {
    return null
  }
  return `${tokens.tokenType} ${tokens.accessToken}`
}
