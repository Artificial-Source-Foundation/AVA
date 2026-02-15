/**
 * MCP OAuth Tests
 * Tests: PKCE, token expiry, state validation, credential store integration
 */

import { afterEach, describe, expect, it, vi } from 'vitest'

// Mock the platform
const mockCredentials = {
  get: vi.fn().mockResolvedValue(null),
  set: vi.fn().mockResolvedValue(undefined),
  delete: vi.fn().mockResolvedValue(undefined),
  has: vi.fn().mockResolvedValue(false),
}

const mockFs = {
  exists: vi.fn().mockResolvedValue(false),
  readFile: vi.fn().mockResolvedValue(''),
  writeFile: vi.fn().mockResolvedValue(undefined),
  remove: vi.fn().mockResolvedValue(undefined),
  readBinary: vi.fn(),
  writeBinary: vi.fn(),
  readDir: vi.fn(),
  readDirWithTypes: vi.fn(),
  stat: vi.fn(),
  isFile: vi.fn(),
  isDirectory: vi.fn(),
  mkdir: vi.fn(),
  glob: vi.fn(),
  realpath: vi.fn().mockImplementation((p: string) => Promise.resolve(p)),
}

vi.mock('../platform.js', () => ({
  getPlatform: () => ({
    credentials: mockCredentials,
    fs: mockFs,
  }),
}))

import {
  areTokensExpired,
  clearPendingStates,
  getStoredTokens,
  type MCPOAuthConfig,
  type MCPOAuthTokens,
  removeTokens,
  startOAuthFlow,
  storeTokens,
} from './oauth.js'

const testConfig: MCPOAuthConfig = {
  serverName: 'test-server',
  clientId: 'test-client-id',
  authorizationUrl: 'https://auth.example.com/authorize',
  tokenUrl: 'https://auth.example.com/token',
  scopes: ['read', 'write'],
  redirectUri: 'http://localhost:3000/callback',
  usePkce: true,
}

afterEach(() => {
  vi.clearAllMocks()
  clearPendingStates()
})

// ============================================================================
// Token Expiry
// ============================================================================

describe('areTokensExpired', () => {
  it('returns false when no expiry set', () => {
    const tokens: MCPOAuthTokens = {
      accessToken: 'test',
      tokenType: 'Bearer',
      scopes: ['read'],
    }
    expect(areTokensExpired(tokens)).toBe(false)
  })

  it('returns false for non-expired tokens', () => {
    const tokens: MCPOAuthTokens = {
      accessToken: 'test',
      tokenType: 'Bearer',
      scopes: ['read'],
      expiresAt: Date.now() + 60 * 60 * 1000, // 1 hour from now
    }
    expect(areTokensExpired(tokens)).toBe(false)
  })

  it('returns true for expired tokens', () => {
    const tokens: MCPOAuthTokens = {
      accessToken: 'test',
      tokenType: 'Bearer',
      scopes: ['read'],
      expiresAt: Date.now() - 1000, // 1 second ago
    }
    expect(areTokensExpired(tokens)).toBe(true)
  })

  it('returns true when within 5 minute buffer', () => {
    const tokens: MCPOAuthTokens = {
      accessToken: 'test',
      tokenType: 'Bearer',
      scopes: ['read'],
      expiresAt: Date.now() + 2 * 60 * 1000, // 2 minutes from now (within 5 min buffer)
    }
    expect(areTokensExpired(tokens)).toBe(true)
  })
})

// ============================================================================
// OAuth Flow State
// ============================================================================

describe('startOAuthFlow', () => {
  it('returns authorization URL with state', async () => {
    const result = await startOAuthFlow(testConfig)
    expect(result.authorizationUrl).toContain('https://auth.example.com/authorize')
    expect(result.authorizationUrl).toContain('client_id=test-client-id')
    expect(result.authorizationUrl).toContain('response_type=code')
    expect(result.authorizationUrl).toContain('scope=read+write')
    expect(result.state).toBeTruthy()
  })

  it('includes PKCE parameters when enabled', async () => {
    const result = await startOAuthFlow(testConfig)
    expect(result.authorizationUrl).toContain('code_challenge=')
    expect(result.authorizationUrl).toContain('code_challenge_method=S256')
  })

  it('excludes PKCE when disabled', async () => {
    const noPkceConfig = { ...testConfig, usePkce: false }
    const result = await startOAuthFlow(noPkceConfig)
    expect(result.authorizationUrl).not.toContain('code_challenge=')
  })

  it('generates unique state per flow', async () => {
    const result1 = await startOAuthFlow(testConfig)
    const result2 = await startOAuthFlow(testConfig)
    expect(result1.state).not.toBe(result2.state)
  })
})

// ============================================================================
// Token Storage (Credential Store)
// ============================================================================

describe('token storage with credential store', () => {
  it('stores tokens in credential store', async () => {
    const tokens: MCPOAuthTokens = {
      accessToken: 'access-123',
      refreshToken: 'refresh-456',
      tokenType: 'Bearer',
      scopes: ['read'],
      expiresAt: Date.now() + 3600000,
    }

    await storeTokens('/workspace', 'test-server', tokens)
    expect(mockCredentials.set).toHaveBeenCalledWith(
      'mcp-oauth-tokens',
      expect.stringContaining('access-123')
    )
  })

  it('retrieves stored tokens', async () => {
    // Set up mock to return stored tokens
    const storedData = {
      version: 1,
      tokens: {
        'test-server': {
          accessToken: 'stored-token',
          tokenType: 'Bearer',
          scopes: ['read'],
        },
      },
      lastModified: Date.now(),
    }
    mockCredentials.get.mockResolvedValueOnce(JSON.stringify(storedData))

    const tokens = await getStoredTokens('/workspace', 'test-server')
    expect(tokens).not.toBeNull()
    expect(tokens?.accessToken).toBe('stored-token')
  })

  it('returns null for non-existent server tokens', async () => {
    mockCredentials.get.mockResolvedValueOnce(
      JSON.stringify({ version: 1, tokens: {}, lastModified: Date.now() })
    )

    const tokens = await getStoredTokens('/workspace', 'non-existent')
    expect(tokens).toBeNull()
  })

  it('migrates from legacy file on first load', async () => {
    // Credential store is empty
    mockCredentials.get.mockResolvedValueOnce(null)

    // Legacy file exists with tokens
    mockFs.exists.mockResolvedValueOnce(true)
    mockFs.readFile.mockResolvedValueOnce(
      JSON.stringify({
        version: 1,
        tokens: {
          'legacy-server': {
            accessToken: 'legacy-token',
            tokenType: 'Bearer',
            scopes: [],
          },
        },
        lastModified: Date.now(),
      })
    )

    const tokens = await getStoredTokens('/workspace', 'legacy-server')
    expect(tokens?.accessToken).toBe('legacy-token')

    // Should have saved to credential store
    expect(mockCredentials.set).toHaveBeenCalled()

    // Should have tried to remove legacy file
    expect(mockFs.remove).toHaveBeenCalled()
  })

  it('removes tokens from credential store', async () => {
    // Load existing tokens first
    mockCredentials.get.mockResolvedValueOnce(
      JSON.stringify({
        version: 1,
        tokens: {
          'test-server': { accessToken: 'x', tokenType: 'Bearer', scopes: [] },
        },
        lastModified: Date.now(),
      })
    )

    await removeTokens('/workspace', 'test-server')

    // Should save updated storage without the removed server
    expect(mockCredentials.set).toHaveBeenCalledWith(
      'mcp-oauth-tokens',
      expect.not.stringContaining('"test-server"')
    )
  })
})
