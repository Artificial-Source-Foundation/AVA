/**
 * Auth Manager Tests
 */

import { beforeEach, describe, expect, it, vi } from 'vitest'

// In-memory credential store
const credentialStore = new Map<string, string>()
const mockCredentials = {
  get: vi
    .fn()
    .mockImplementation((key: string) => Promise.resolve(credentialStore.get(key) ?? null)),
  set: vi.fn().mockImplementation((key: string, value: string) => {
    credentialStore.set(key, value)
    return Promise.resolve()
  }),
  delete: vi.fn().mockImplementation((key: string) => {
    credentialStore.delete(key)
    return Promise.resolve()
  }),
  has: vi.fn().mockImplementation((key: string) => Promise.resolve(credentialStore.has(key))),
}

vi.mock('../platform.js', () => ({
  getPlatform: () => ({
    credentials: mockCredentials,
  }),
}))

// Mock OAuth modules to prevent import errors
vi.mock('./openai-oauth.js', () => ({
  authorizeOpenAI: vi.fn(),
  refreshOpenAIToken: vi.fn(),
}))
vi.mock('./google-oauth.js', () => ({
  authorizeGoogle: vi.fn(),
  refreshGoogleToken: vi.fn(),
}))
vi.mock('./copilot-oauth.js', () => ({
  authorizeCopilot: vi.fn(),
  refreshCopilotToken: vi.fn(),
}))

import {
  getAuthStatus,
  getStoredAuth,
  getValidAccessToken,
  removeStoredAuth,
  setStoredAuth,
} from './manager.js'
import type { StoredAuth } from './types.js'

beforeEach(() => {
  credentialStore.clear()
  vi.clearAllMocks()
})

// ============================================================================
// getStoredAuth / setStoredAuth / removeStoredAuth
// ============================================================================

describe('auth storage round-trips', () => {
  it('should return null when no auth is stored', async () => {
    const result = await getStoredAuth('anthropic')
    expect(result).toBeNull()
  })

  it('should store and retrieve API key auth', async () => {
    const auth: StoredAuth = { type: 'api-key', key: 'sk-test-123' }
    await setStoredAuth('anthropic', auth)

    const result = await getStoredAuth('anthropic')
    expect(result).toEqual(auth)
  })

  it('should store and retrieve OAuth auth', async () => {
    const auth: StoredAuth = {
      type: 'oauth',
      accessToken: 'access-abc',
      refreshToken: 'refresh-def',
      expiresAt: Date.now() + 3600000,
    }
    await setStoredAuth('openai', auth)

    const result = await getStoredAuth('openai')
    expect(result).toEqual(auth)
  })

  it('should store and retrieve OAuth auth with accountId', async () => {
    const auth: StoredAuth = {
      type: 'oauth',
      accessToken: 'access-abc',
      refreshToken: 'refresh-def',
      expiresAt: Date.now() + 3600000,
      accountId: 'acct-123',
    }
    await setStoredAuth('openai', auth)

    const result = await getStoredAuth('openai')
    expect(result).toEqual(auth)
    expect(result?.type === 'oauth' && result.accountId).toBe('acct-123')
  })

  it('should remove stored auth', async () => {
    const auth: StoredAuth = { type: 'api-key', key: 'sk-test-123' }
    await setStoredAuth('anthropic', auth)

    await removeStoredAuth('anthropic')

    const result = await getStoredAuth('anthropic')
    expect(result).toBeNull()
  })

  it('should handle invalid JSON gracefully', async () => {
    credentialStore.set('auth-anthropic', 'not-json')

    const result = await getStoredAuth('anthropic')
    expect(result).toBeNull()
  })

  it('should store different providers independently', async () => {
    const anthropicAuth: StoredAuth = { type: 'api-key', key: 'sk-anth' }
    const openaiAuth: StoredAuth = { type: 'api-key', key: 'sk-oai' }

    await setStoredAuth('anthropic', anthropicAuth)
    await setStoredAuth('openai', openaiAuth)

    expect(await getStoredAuth('anthropic')).toEqual(anthropicAuth)
    expect(await getStoredAuth('openai')).toEqual(openaiAuth)
  })
})

// ============================================================================
// getValidAccessToken
// ============================================================================

describe('getValidAccessToken', () => {
  it('should return null when no auth is stored', async () => {
    const token = await getValidAccessToken('anthropic')
    expect(token).toBeNull()
  })

  it('should return null for API key auth (not OAuth)', async () => {
    await setStoredAuth('anthropic', { type: 'api-key', key: 'sk-test' })

    const token = await getValidAccessToken('anthropic')
    expect(token).toBeNull()
  })

  it('should return access token when not expired', async () => {
    await setStoredAuth('anthropic', {
      type: 'oauth',
      accessToken: 'valid-token',
      refreshToken: 'refresh-token',
      expiresAt: Date.now() + 7200000, // 2 hours from now (well past 1-hour refresh buffer)
    })

    const token = await getValidAccessToken('anthropic')
    expect(token).toBe('valid-token')
  })
})

// ============================================================================
// getAuthStatus
// ============================================================================

describe('getAuthStatus', () => {
  it('should return none when no auth exists', async () => {
    const status = await getAuthStatus('anthropic')
    expect(status.isAuthenticated).toBe(false)
    expect(status.authType).toBe('none')
    expect(status.provider).toBe('anthropic')
  })

  it('should return oauth status for OAuth auth', async () => {
    const expiresAt = Date.now() + 3600000
    await setStoredAuth('anthropic', {
      type: 'oauth',
      accessToken: 'token',
      refreshToken: 'refresh',
      expiresAt,
    })

    const status = await getAuthStatus('anthropic')
    expect(status.isAuthenticated).toBe(true)
    expect(status.authType).toBe('oauth')
    expect(status.expiresAt).toBe(expiresAt)
  })

  it('should return api-key status when env key exists', async () => {
    // Store an API key in credentials under the env key format
    credentialStore.set('anthropic-api-key', 'sk-env-key')

    const status = await getAuthStatus('anthropic')
    expect(status.isAuthenticated).toBe(true)
    expect(status.authType).toBe('api-key')
  })
})
