/**
 * MCP OAuth — authorization code flow for MCP servers.
 */

import type { MCPOAuthConfig, MCPOAuthTokens } from './types.js'

export class MCPOAuth {
  private tokens: MCPOAuthTokens | null = null

  constructor(
    private config: MCPOAuthConfig,
    private onTokens?: (tokens: MCPOAuthTokens) => void
  ) {}

  /** Build the authorization URL for the user to visit. */
  buildAuthorizationUrl(state?: string): string {
    const url = new URL(this.config.authorizationUrl)
    url.searchParams.set('client_id', this.config.clientId)
    url.searchParams.set('response_type', 'code')
    if (this.config.redirectUri) {
      url.searchParams.set('redirect_uri', this.config.redirectUri)
    }
    if (this.config.scopes?.length) {
      url.searchParams.set('scope', this.config.scopes.join(' '))
    }
    if (state) {
      url.searchParams.set('state', state)
    }
    return url.toString()
  }

  /** Exchange an authorization code for tokens. */
  async exchangeCode(code: string): Promise<MCPOAuthTokens> {
    const body = new URLSearchParams({
      grant_type: 'authorization_code',
      code,
      client_id: this.config.clientId,
    })
    if (this.config.clientSecret) {
      body.set('client_secret', this.config.clientSecret)
    }
    if (this.config.redirectUri) {
      body.set('redirect_uri', this.config.redirectUri)
    }

    const response = await fetch(this.config.tokenUrl, {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: body.toString(),
    })

    if (!response.ok) {
      throw new Error(`OAuth token exchange failed: ${response.status} ${response.statusText}`)
    }

    const data = (await response.json()) as Record<string, unknown>
    this.tokens = {
      accessToken: data.access_token as string,
      refreshToken: data.refresh_token as string | undefined,
      expiresAt: data.expires_in ? Date.now() + (data.expires_in as number) * 1000 : undefined,
      tokenType: (data.token_type as string) ?? 'Bearer',
    }

    this.onTokens?.(this.tokens)
    return this.tokens
  }

  /** Refresh the access token using the refresh token. */
  async refreshAccessToken(): Promise<MCPOAuthTokens> {
    if (!this.tokens?.refreshToken) {
      throw new Error('No refresh token available')
    }

    const body = new URLSearchParams({
      grant_type: 'refresh_token',
      refresh_token: this.tokens.refreshToken,
      client_id: this.config.clientId,
    })
    if (this.config.clientSecret) {
      body.set('client_secret', this.config.clientSecret)
    }

    const response = await fetch(this.config.tokenUrl, {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: body.toString(),
    })

    if (!response.ok) {
      throw new Error(`OAuth token refresh failed: ${response.status} ${response.statusText}`)
    }

    const data = (await response.json()) as Record<string, unknown>
    this.tokens = {
      accessToken: data.access_token as string,
      refreshToken: (data.refresh_token as string) ?? this.tokens.refreshToken,
      expiresAt: data.expires_in ? Date.now() + (data.expires_in as number) * 1000 : undefined,
      tokenType: (data.token_type as string) ?? 'Bearer',
    }

    this.onTokens?.(this.tokens)
    return this.tokens
  }

  /** Get the current access token, refreshing if expired. */
  async getAccessToken(): Promise<string> {
    if (!this.tokens) {
      throw new Error('Not authenticated. Call exchangeCode() first.')
    }

    if (this.tokens.expiresAt && Date.now() >= this.tokens.expiresAt - 60_000) {
      await this.refreshAccessToken()
    }

    return this.tokens.accessToken
  }

  /** Set tokens directly (e.g. from storage). */
  setTokens(tokens: MCPOAuthTokens): void {
    this.tokens = tokens
  }

  get isAuthenticated(): boolean {
    return this.tokens !== null
  }
}
