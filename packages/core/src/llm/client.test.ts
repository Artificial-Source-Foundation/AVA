/**
 * LLM Client Tests
 * Tests for client registry, factory, credential resolution, and auth
 */

import { beforeEach, describe, expect, it, vi } from 'vitest'
import type { LLMProvider } from '../types/llm.js'

// ============================================================================
// Mocks
// ============================================================================

const mockCredentialsGet = vi.fn().mockResolvedValue(null)
const mockCredentialsSet = vi.fn().mockResolvedValue(undefined)
const mockCredentialsDelete = vi.fn().mockResolvedValue(undefined)
const mockCredentialsHas = vi.fn().mockResolvedValue(false)

vi.mock('../platform.js', () => ({
  getPlatform: () => ({
    credentials: {
      get: mockCredentialsGet,
      set: mockCredentialsSet,
      delete: mockCredentialsDelete,
      has: mockCredentialsHas,
    },
  }),
}))

const mockGetStoredAuth = vi.fn().mockResolvedValue(null)
const mockGetValidAccessToken = vi.fn().mockResolvedValue(null)
const mockGetAccountId = vi.fn().mockResolvedValue(null)

vi.mock('../auth/index.js', () => ({
  getStoredAuth: (...args: unknown[]) => mockGetStoredAuth(...args),
  getValidAccessToken: (...args: unknown[]) => mockGetValidAccessToken(...args),
  getAccountId: (...args: unknown[]) => mockGetAccountId(...args),
}))

// ============================================================================
// Import after mocks
// ============================================================================

import type { AuthInfo, LLMClient } from './client.js'
import { createClient, getApiKey, getAuth, registerClient } from './client.js'

// ============================================================================
// Mock LLM Client
// ============================================================================

class MockLLMClient implements LLMClient {
  async *stream() {
    yield { type: 'content' as const, content: 'test response' }
  }
}

class AnotherMockClient implements LLMClient {
  async *stream() {
    yield { type: 'content' as const, content: 'another response' }
  }
}

// ============================================================================
// Setup / Teardown
// ============================================================================

beforeEach(() => {
  vi.clearAllMocks()
})

// ============================================================================
// registerClient
// ============================================================================

describe('registerClient', () => {
  it('should register a client class for a provider', () => {
    // Register should not throw
    registerClient('anthropic', MockLLMClient as unknown as new () => LLMClient)
  })

  it('should allow registering different providers', () => {
    registerClient('openai' as LLMProvider, MockLLMClient as unknown as new () => LLMClient)
    registerClient('google' as LLMProvider, AnotherMockClient as unknown as new () => LLMClient)
  })
})

// ============================================================================
// createClient
// ============================================================================

describe('createClient', () => {
  beforeEach(() => {
    // Pre-register to avoid dynamic import issues in test environment
    registerClient('anthropic', MockLLMClient as unknown as new () => LLMClient)
  })

  it('should return instance of registered client', async () => {
    const client = await createClient('anthropic')
    expect(client).toBeInstanceOf(MockLLMClient)
  })

  it('should create a new instance each time', async () => {
    const client1 = await createClient('anthropic')
    const client2 = await createClient('anthropic')
    expect(client1).not.toBe(client2)
  })

  it('should throw for copilot provider', async () => {
    await expect(createClient('copilot')).rejects.toThrow('not yet implemented')
  })

  it('should throw for unknown provider', async () => {
    await expect(createClient('nonexistent' as LLMProvider)).rejects.toThrow('Unknown provider')
  })

  it('should return client with stream method', async () => {
    const client = await createClient('anthropic')
    expect(typeof client.stream).toBe('function')
  })

  it('should use cached constructor for already-registered provider', async () => {
    registerClient('deepseek' as LLMProvider, AnotherMockClient as unknown as new () => LLMClient)
    const client = await createClient('deepseek' as LLMProvider)
    expect(client).toBeInstanceOf(AnotherMockClient)
  })
})

// ============================================================================
// getApiKey
// ============================================================================

describe('getApiKey', () => {
  it('should return null when no credentials stored', async () => {
    mockCredentialsGet.mockResolvedValueOnce(null)
    const result = await getApiKey('anthropic')
    expect(result).toBeNull()
  })

  it('should return key when credentials exist', async () => {
    mockCredentialsGet.mockResolvedValueOnce('sk-test-key-123')
    const result = await getApiKey('anthropic')
    expect(result).toBe('sk-test-key-123')
  })

  it('should request the correct credential key for anthropic', async () => {
    await getApiKey('anthropic')
    expect(mockCredentialsGet).toHaveBeenCalledWith('anthropic-api-key')
  })

  it('should request the correct credential key for openrouter', async () => {
    await getApiKey('openrouter')
    expect(mockCredentialsGet).toHaveBeenCalledWith('openrouter-api-key')
  })

  it('should request the correct credential key for openai', async () => {
    await getApiKey('openai')
    expect(mockCredentialsGet).toHaveBeenCalledWith('openai-api-key')
  })

  it('should request the correct credential key for google', async () => {
    await getApiKey('google')
    expect(mockCredentialsGet).toHaveBeenCalledWith('google-api-key')
  })

  it('should request the correct credential key for mistral', async () => {
    await getApiKey('mistral')
    expect(mockCredentialsGet).toHaveBeenCalledWith('mistral-api-key')
  })

  it('should request the correct credential key for groq', async () => {
    await getApiKey('groq')
    expect(mockCredentialsGet).toHaveBeenCalledWith('groq-api-key')
  })

  it('should request the correct credential key for deepseek', async () => {
    await getApiKey('deepseek')
    expect(mockCredentialsGet).toHaveBeenCalledWith('deepseek-api-key')
  })

  it('should request the correct credential key for xai', async () => {
    await getApiKey('xai')
    expect(mockCredentialsGet).toHaveBeenCalledWith('xai-api-key')
  })

  it('should request the correct credential key for ollama', async () => {
    await getApiKey('ollama')
    expect(mockCredentialsGet).toHaveBeenCalledWith('ollama-api-key')
  })

  it('should request the correct credential key for together', async () => {
    await getApiKey('together')
    expect(mockCredentialsGet).toHaveBeenCalledWith('together-api-key')
  })

  it('should request the correct credential key for cohere', async () => {
    await getApiKey('cohere')
    expect(mockCredentialsGet).toHaveBeenCalledWith('cohere-api-key')
  })
})

// ============================================================================
// getAuth
// ============================================================================

describe('getAuth', () => {
  it('should return null when no auth is available', async () => {
    mockGetStoredAuth.mockResolvedValueOnce(null)
    mockCredentialsGet.mockResolvedValueOnce(null)

    const result = await getAuth('anthropic')
    expect(result).toBeNull()
  })

  it('should return api-key auth when API key exists but no OAuth', async () => {
    mockGetStoredAuth.mockResolvedValueOnce(null)
    mockCredentialsGet.mockResolvedValueOnce('sk-test-key')

    const result = await getAuth('anthropic')
    expect(result).toEqual({
      type: 'api-key',
      token: 'sk-test-key',
    } satisfies AuthInfo)
  })

  it('should return oauth auth when OAuth is stored and valid', async () => {
    mockGetStoredAuth.mockResolvedValueOnce({ type: 'oauth', accessToken: 'old-token' })
    mockGetValidAccessToken.mockResolvedValueOnce('fresh-access-token')
    mockGetAccountId.mockResolvedValueOnce(null)

    const result = await getAuth('anthropic')
    expect(result).toEqual({
      type: 'oauth',
      token: 'fresh-access-token',
      accountId: undefined,
    } satisfies AuthInfo)
  })

  it('should include accountId in oauth auth when available', async () => {
    mockGetStoredAuth.mockResolvedValueOnce({ type: 'oauth', accessToken: 'old-token' })
    mockGetValidAccessToken.mockResolvedValueOnce('fresh-access-token')
    mockGetAccountId.mockResolvedValueOnce('acct-123')

    const result = await getAuth('openai')
    expect(result).toEqual({
      type: 'oauth',
      token: 'fresh-access-token',
      accountId: 'acct-123',
    } satisfies AuthInfo)
  })

  it('should preserve accountId when returned for non-openai providers', async () => {
    mockGetStoredAuth.mockResolvedValueOnce({ type: 'oauth', accessToken: 'old-token' })
    mockGetValidAccessToken.mockResolvedValueOnce('fresh-access-token')
    mockGetAccountId.mockResolvedValueOnce('acct-anthropic')

    const result = await getAuth('anthropic')
    expect(result).toEqual({
      type: 'oauth',
      token: 'fresh-access-token',
      accountId: 'acct-anthropic',
    } satisfies AuthInfo)
  })

  it('should fall back to API key when OAuth stored but token refresh fails', async () => {
    mockGetStoredAuth.mockResolvedValueOnce({ type: 'oauth', accessToken: 'expired-token' })
    mockGetValidAccessToken.mockResolvedValueOnce(null) // refresh failed
    mockCredentialsGet.mockResolvedValueOnce('sk-fallback-key')

    const result = await getAuth('anthropic')
    expect(result).toEqual({
      type: 'api-key',
      token: 'sk-fallback-key',
    } satisfies AuthInfo)
  })

  it('should return null when OAuth fails and no API key exists', async () => {
    mockGetStoredAuth.mockResolvedValueOnce({ type: 'oauth', accessToken: 'expired-token' })
    mockGetValidAccessToken.mockResolvedValueOnce(null)
    mockCredentialsGet.mockResolvedValueOnce(null)

    const result = await getAuth('google')
    expect(result).toBeNull()
  })

  it('should skip OAuth check when stored auth is not oauth type', async () => {
    mockGetStoredAuth.mockResolvedValueOnce({ type: 'api-key' })
    mockCredentialsGet.mockResolvedValueOnce('sk-direct-key')

    const result = await getAuth('anthropic')
    expect(result).toEqual({
      type: 'api-key',
      token: 'sk-direct-key',
    } satisfies AuthInfo)
    expect(mockGetValidAccessToken).not.toHaveBeenCalled()
  })

  it('should not call getAccountId when valid oauth access token is missing', async () => {
    mockGetStoredAuth.mockResolvedValueOnce({ type: 'oauth', accessToken: 'expired-token' })
    mockGetValidAccessToken.mockResolvedValueOnce(null)
    mockCredentialsGet.mockResolvedValueOnce('sk-fallback-key')

    await getAuth('openai')

    expect(mockGetAccountId).not.toHaveBeenCalled()
  })

  it('should pass provider to getStoredAuth', async () => {
    await getAuth('google')
    expect(mockGetStoredAuth).toHaveBeenCalledWith('google')
  })

  it('should pass provider to getValidAccessToken when OAuth is present', async () => {
    mockGetStoredAuth.mockResolvedValueOnce({ type: 'oauth', accessToken: 'token' })
    mockGetValidAccessToken.mockResolvedValueOnce('fresh-token')
    mockGetAccountId.mockResolvedValueOnce(null)

    await getAuth('copilot')
    expect(mockGetValidAccessToken).toHaveBeenCalledWith('copilot')
  })

  it('should pass provider to getAccountId when OAuth is valid', async () => {
    mockGetStoredAuth.mockResolvedValueOnce({ type: 'oauth', accessToken: 'token' })
    mockGetValidAccessToken.mockResolvedValueOnce('fresh-token')
    mockGetAccountId.mockResolvedValueOnce(null)

    await getAuth('openai')
    expect(mockGetAccountId).toHaveBeenCalledWith('openai')
  })
})
