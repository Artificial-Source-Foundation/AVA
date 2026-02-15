/**
 * Credentials Manager Tests
 */

import { afterEach, describe, expect, it, vi } from 'vitest'

const mockCredentials = {
  get: vi.fn().mockResolvedValue(null),
  set: vi.fn().mockResolvedValue(undefined),
  delete: vi.fn().mockResolvedValue(undefined),
  has: vi.fn().mockResolvedValue(false),
}

vi.mock('../platform.js', () => ({
  getPlatform: () => ({
    credentials: mockCredentials,
  }),
}))

import {
  CredentialsManager,
  CredentialValidationError,
  createCredentialsManager,
  getCredentialsManager,
  KNOWN_PROVIDERS,
  PROVIDER_NAMES,
  setCredentialsManager,
} from './credentials.js'

afterEach(() => {
  vi.clearAllMocks()
  setCredentialsManager(null)
})

// ============================================================================
// Constants
// ============================================================================

describe('KNOWN_PROVIDERS', () => {
  it('includes expected providers', () => {
    expect(KNOWN_PROVIDERS).toContain('anthropic')
    expect(KNOWN_PROVIDERS).toContain('openai')
    expect(KNOWN_PROVIDERS).toContain('google')
  })
})

describe('PROVIDER_NAMES', () => {
  it('maps all known providers', () => {
    for (const p of KNOWN_PROVIDERS) {
      expect(PROVIDER_NAMES[p]).toBeTruthy()
    }
  })
})

// ============================================================================
// Key Operations
// ============================================================================

describe('CredentialsManager key operations', () => {
  it('getApiKey returns stored key', async () => {
    mockCredentials.get.mockResolvedValueOnce('sk-ant-abc123')
    const mgr = new CredentialsManager()
    const key = await mgr.getApiKey('anthropic')
    expect(key).toBe('sk-ant-abc123')
    expect(mockCredentials.get).toHaveBeenCalledWith('ava:anthropic:api_key')
  })

  it('getApiKey returns null when not set', async () => {
    const mgr = new CredentialsManager()
    expect(await mgr.getApiKey('anthropic')).toBeNull()
  })

  it('setApiKey stores valid key', async () => {
    const mgr = new CredentialsManager()
    await mgr.setApiKey('anthropic', 'sk-ant-validkey123')
    expect(mockCredentials.set).toHaveBeenCalledWith('ava:anthropic:api_key', 'sk-ant-validkey123')
  })

  it('setApiKey rejects invalid format', async () => {
    const mgr = new CredentialsManager()
    await expect(mgr.setApiKey('anthropic', 'invalid-key')).rejects.toThrow(
      CredentialValidationError
    )
  })

  it('setApiKey accepts keys for providers without pattern', async () => {
    const mgr = new CredentialsManager()
    await mgr.setApiKey('cohere', 'any-key-format')
    expect(mockCredentials.set).toHaveBeenCalled()
  })

  it('deleteApiKey removes key', async () => {
    const mgr = new CredentialsManager()
    await mgr.deleteApiKey('openai')
    expect(mockCredentials.delete).toHaveBeenCalledWith('ava:openai:api_key')
  })

  it('hasApiKey checks existence', async () => {
    mockCredentials.has.mockResolvedValueOnce(true)
    const mgr = new CredentialsManager()
    expect(await mgr.hasApiKey('anthropic')).toBe(true)
  })
})

// ============================================================================
// Provider Operations
// ============================================================================

describe('CredentialsManager provider operations', () => {
  it('listProviders returns all with status', async () => {
    mockCredentials.has.mockResolvedValue(false)
    mockCredentials.has.mockResolvedValueOnce(true) // anthropic has key
    const mgr = new CredentialsManager()
    const providers = await mgr.listProviders()
    expect(providers).toHaveLength(KNOWN_PROVIDERS.length)
    expect(providers[0].hasKey).toBe(true)
    expect(providers[0].provider).toBe('anthropic')
  })

  it('getConfiguredProviders returns only configured', async () => {
    mockCredentials.has.mockResolvedValue(false)
    mockCredentials.has.mockResolvedValueOnce(true) // anthropic
    const mgr = new CredentialsManager()
    const configured = await mgr.getConfiguredProviders()
    expect(configured).toEqual(['anthropic'])
  })

  it('hasAnyApiKey returns true when at least one exists', async () => {
    mockCredentials.has.mockResolvedValue(false)
    mockCredentials.has.mockResolvedValueOnce(true)
    const mgr = new CredentialsManager()
    expect(await mgr.hasAnyApiKey()).toBe(true)
  })

  it('hasAnyApiKey returns false when none exist', async () => {
    mockCredentials.has.mockResolvedValue(false)
    const mgr = new CredentialsManager()
    expect(await mgr.hasAnyApiKey()).toBe(false)
  })
})

// ============================================================================
// Validation
// ============================================================================

describe('CredentialsManager validation', () => {
  it('validates correct anthropic key', () => {
    const mgr = new CredentialsManager()
    expect(mgr.validateApiKey('anthropic', 'sk-ant-abc123')).toBe(true)
  })

  it('rejects incorrect anthropic key', () => {
    const mgr = new CredentialsManager()
    expect(mgr.validateApiKey('anthropic', 'wrong-key')).toBe(false)
  })

  it('validates correct openai key', () => {
    const mgr = new CredentialsManager()
    expect(mgr.validateApiKey('openai', 'sk-abc123')).toBe(true)
  })

  it('accepts any non-empty string for unknown pattern', () => {
    const mgr = new CredentialsManager()
    expect(mgr.validateApiKey('cohere', 'anything')).toBe(true)
    expect(mgr.validateApiKey('cohere', '')).toBe(false)
  })

  it('getKeyFormatHint returns hints', () => {
    const mgr = new CredentialsManager()
    expect(mgr.getKeyFormatHint('anthropic')).toContain('sk-ant')
    expect(mgr.getKeyFormatHint('openai')).toContain('sk-')
    expect(mgr.getKeyFormatHint('cohere')).toBe('API key')
  })
})

// ============================================================================
// Singleton
// ============================================================================

describe('singleton', () => {
  it('getCredentialsManager returns same instance', () => {
    expect(getCredentialsManager()).toBe(getCredentialsManager())
  })

  it('createCredentialsManager creates new instance', () => {
    expect(createCredentialsManager()).not.toBe(createCredentialsManager())
  })
})
