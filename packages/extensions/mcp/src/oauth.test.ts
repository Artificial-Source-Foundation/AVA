import { describe, expect, it, vi } from 'vitest'
import { MCPOAuth } from './oauth.js'

describe('MCPOAuth', () => {
  const config = {
    authorizationUrl: 'https://auth.example.com/authorize',
    tokenUrl: 'https://auth.example.com/token',
    clientId: 'test-client',
    clientSecret: 'test-secret',
    scopes: ['read', 'write'],
    redirectUri: 'http://localhost:3000/callback',
  }

  it('builds authorization URL with all params', () => {
    const oauth = new MCPOAuth(config)
    const url = oauth.buildAuthorizationUrl('test-state')
    const parsed = new URL(url)

    expect(parsed.origin + parsed.pathname).toBe('https://auth.example.com/authorize')
    expect(parsed.searchParams.get('client_id')).toBe('test-client')
    expect(parsed.searchParams.get('response_type')).toBe('code')
    expect(parsed.searchParams.get('redirect_uri')).toBe('http://localhost:3000/callback')
    expect(parsed.searchParams.get('scope')).toBe('read write')
    expect(parsed.searchParams.get('state')).toBe('test-state')
  })

  it('builds URL without optional params', () => {
    const oauth = new MCPOAuth({
      authorizationUrl: 'https://auth.example.com/authorize',
      tokenUrl: 'https://auth.example.com/token',
      clientId: 'test',
    })
    const url = oauth.buildAuthorizationUrl()
    const parsed = new URL(url)
    expect(parsed.searchParams.has('state')).toBe(false)
    expect(parsed.searchParams.has('scope')).toBe(false)
  })

  it('exchanges code for tokens', async () => {
    const oauth = new MCPOAuth(config)

    const mockResponse = {
      access_token: 'at_123',
      refresh_token: 'rt_456',
      expires_in: 3600,
      token_type: 'Bearer',
    }

    vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
      ok: true,
      json: async () => mockResponse,
    } as Response)

    const tokens = await oauth.exchangeCode('auth-code-123')
    expect(tokens.accessToken).toBe('at_123')
    expect(tokens.refreshToken).toBe('rt_456')
    expect(tokens.tokenType).toBe('Bearer')
    expect(tokens.expiresAt).toBeGreaterThan(Date.now())
    expect(oauth.isAuthenticated).toBe(true)
  })

  it('throws on exchange failure', async () => {
    const oauth = new MCPOAuth(config)

    vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
      ok: false,
      status: 400,
      statusText: 'Bad Request',
    } as Response)

    await expect(oauth.exchangeCode('bad')).rejects.toThrow('OAuth token exchange failed')
  })

  it('refreshes access token', async () => {
    const oauth = new MCPOAuth(config)
    oauth.setTokens({
      accessToken: 'old',
      refreshToken: 'rt_456',
      expiresAt: Date.now() - 1000, // expired
    })

    vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
      ok: true,
      json: async () => ({
        access_token: 'new_at',
        refresh_token: 'new_rt',
        expires_in: 3600,
      }),
    } as Response)

    const tokens = await oauth.refreshAccessToken()
    expect(tokens.accessToken).toBe('new_at')
    expect(tokens.refreshToken).toBe('new_rt')
  })

  it('throws refresh without refresh token', async () => {
    const oauth = new MCPOAuth(config)
    oauth.setTokens({ accessToken: 'at' })
    await expect(oauth.refreshAccessToken()).rejects.toThrow('No refresh token')
  })

  it('getAccessToken auto-refreshes when expired', async () => {
    const oauth = new MCPOAuth(config)
    oauth.setTokens({
      accessToken: 'old',
      refreshToken: 'rt',
      expiresAt: Date.now() - 1000,
    })

    vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
      ok: true,
      json: async () => ({
        access_token: 'refreshed',
        expires_in: 3600,
      }),
    } as Response)

    const token = await oauth.getAccessToken()
    expect(token).toBe('refreshed')
  })

  it('getAccessToken returns current token when not expired', async () => {
    const oauth = new MCPOAuth(config)
    oauth.setTokens({
      accessToken: 'valid',
      expiresAt: Date.now() + 300_000, // 5 min ahead
    })

    const token = await oauth.getAccessToken()
    expect(token).toBe('valid')
  })

  it('throws getAccessToken when not authenticated', async () => {
    const oauth = new MCPOAuth(config)
    await expect(oauth.getAccessToken()).rejects.toThrow('Not authenticated')
  })

  it('calls onTokens callback', async () => {
    const onTokens = vi.fn()
    const oauth = new MCPOAuth(config, onTokens)

    vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
      ok: true,
      json: async () => ({ access_token: 'at', expires_in: 3600 }),
    } as Response)

    await oauth.exchangeCode('code')
    expect(onTokens).toHaveBeenCalledOnce()
  })
})
