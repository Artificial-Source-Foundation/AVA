import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { installMockPlatform } from '../__test-utils__/mock-platform.js'
import { resetLogger } from '../logger/logger.js'
import {
  createClient,
  getApiKey,
  getAuth,
  getRegisteredProviders,
  hasProvider,
  registerProvider,
  resetProviders,
  unregisterProvider,
} from './client.js'
import type { LLMClient } from './types.js'

describe('LLM Client Registry', () => {
  beforeEach(() => {
    resetProviders()
    installMockPlatform()
  })

  afterEach(() => {
    resetProviders()
    resetLogger()
  })

  // ─── registerProvider ─────────────────────────────────────────────────

  describe('registerProvider', () => {
    it('registers a provider factory', () => {
      registerProvider('test', () => ({}) as LLMClient)
      expect(hasProvider('test')).toBe(true)
    })

    it('shows in registered providers list', () => {
      registerProvider('test', () => ({}) as LLMClient)
      expect(getRegisteredProviders()).toContain('test')
    })
  })

  // ─── unregisterProvider ───────────────────────────────────────────────

  describe('unregisterProvider', () => {
    it('removes a registered provider', () => {
      registerProvider('test', () => ({}) as LLMClient)
      unregisterProvider('test')
      expect(hasProvider('test')).toBe(false)
    })

    it('is safe for non-existent provider', () => {
      expect(() => unregisterProvider('nonexistent')).not.toThrow()
    })
  })

  // ─── createClient ─────────────────────────────────────────────────────

  describe('createClient', () => {
    it('creates client from factory', () => {
      const mockClient = { stream: async function* () {} } as unknown as LLMClient
      registerProvider('test', () => mockClient)
      expect(createClient('test')).toBe(mockClient)
    })

    it('throws for unregistered provider', () => {
      expect(() => createClient('nonexistent')).toThrow('No LLM provider registered')
    })

    it('includes available providers in error', () => {
      registerProvider('foo', () => ({}) as LLMClient)
      registerProvider('bar', () => ({}) as LLMClient)
      try {
        createClient('baz')
      } catch (err) {
        expect((err as Error).message).toContain('foo')
        expect((err as Error).message).toContain('bar')
      }
    })

    it('shows "none" when no providers registered', () => {
      try {
        createClient('test')
      } catch (err) {
        expect((err as Error).message).toContain('none')
      }
    })
  })

  // ─── hasProvider ──────────────────────────────────────────────────────

  describe('hasProvider', () => {
    it('returns false for non-existent', () => {
      expect(hasProvider('nope')).toBe(false)
    })

    it('returns true for registered', () => {
      registerProvider('test', () => ({}) as LLMClient)
      expect(hasProvider('test')).toBe(true)
    })
  })

  // ─── getRegisteredProviders ───────────────────────────────────────────

  describe('getRegisteredProviders', () => {
    it('returns empty array initially', () => {
      expect(getRegisteredProviders()).toEqual([])
    })

    it('returns all registered', () => {
      registerProvider('a', () => ({}) as LLMClient)
      registerProvider('b', () => ({}) as LLMClient)
      expect(getRegisteredProviders()).toEqual(['a', 'b'])
    })
  })

  // ─── resetProviders ───────────────────────────────────────────────────

  describe('resetProviders', () => {
    it('clears all providers', () => {
      registerProvider('test', () => ({}) as LLMClient)
      resetProviders()
      expect(getRegisteredProviders()).toEqual([])
    })
  })
})

// ─── Credential Resolution ──────────────────────────────────────────────

describe('Credential Resolution', () => {
  let mockPlatform: ReturnType<typeof installMockPlatform>

  beforeEach(() => {
    resetProviders()
    mockPlatform = installMockPlatform()
  })

  afterEach(() => {
    resetProviders()
    resetLogger()
  })

  describe('getApiKey', () => {
    it('returns null when no key stored', async () => {
      expect(await getApiKey('anthropic')).toBeNull()
    })

    it('returns key when stored', async () => {
      await mockPlatform.credentials.set('ava:anthropic:api_key', 'sk-test-123')
      expect(await getApiKey('anthropic')).toBe('sk-test-123')
    })
  })

  describe('getAuth', () => {
    it('returns null when nothing stored', async () => {
      expect(await getAuth('anthropic')).toBeNull()
    })

    it('prefers OAuth token', async () => {
      await mockPlatform.credentials.set('ava:openai:oauth_token', 'oauth-token-123')
      await mockPlatform.credentials.set('ava:openai:api_key', 'sk-key-123')
      const auth = await getAuth('openai')
      expect(auth).toEqual({ type: 'oauth', token: 'oauth-token-123' })
    })

    it('falls back to API key', async () => {
      await mockPlatform.credentials.set('ava:anthropic:api_key', 'sk-key-456')
      const auth = await getAuth('anthropic')
      expect(auth).toEqual({ type: 'api-key', token: 'sk-key-456' })
    })
  })
})
