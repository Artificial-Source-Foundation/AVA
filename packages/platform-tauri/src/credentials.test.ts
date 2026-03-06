import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { TauriCredentialStore } from '../src/credentials.js'

/**
 * Tests for TauriCredentialStore
 *
 * Verifies multi-layer credential storage with env vars, localStorage, and disk.
 */
describe('TauriCredentialStore', () => {
  let store: TauriCredentialStore

  beforeEach(() => {
    store = new TauriCredentialStore()
    // Clear localStorage
    localStorage.clear()
  })

  afterEach(() => {
    vi.restoreAllMocks()
    localStorage.clear()
  })

  describe('get', () => {
    it('should return null for non-existent key', async () => {
      const value = await store.get('non_existent_key')
      expect(value).toBeNull()
    })

    it('should get value from localStorage', async () => {
      localStorage.setItem('ava_cred_test_key', 'test_value')
      const value = await store.get('test_key')
      expect(value).toBe('test_value')
    })

    it('should handle key normalization', async () => {
      localStorage.setItem('ava_cred_api_key', 'value')
      const value = await store.get('api_key')
      expect(value).toBe('value')
    })

    it('should check environment variables', async () => {
      // Mock process.env
      const originalEnv = process.env
      process.env = { ...originalEnv, TEST_CRED: 'env_value' }

      const value = await store.get('test_cred')
      expect(value).toBe('env_value')

      process.env = originalEnv
    })

    it('should prioritize env vars over localStorage', async () => {
      // Set both env var and localStorage
      const originalEnv = process.env
      process.env = { ...originalEnv, DUAL_KEY: 'env_value' }
      localStorage.setItem('ava_cred_dual_key', 'local_value')

      const value = await store.get('dual_key')
      expect(value).toBe('env_value')

      process.env = originalEnv
    })
  })

  describe('set', () => {
    it('should set value in localStorage', async () => {
      await store.set('new_key', 'new_value')
      expect(localStorage.getItem('ava_cred_new_key')).toBe('new_value')
    })

    it('should update existing value', async () => {
      localStorage.setItem('ava_cred_existing', 'old_value')
      await store.set('existing', 'new_value')
      expect(localStorage.getItem('ava_cred_existing')).toBe('new_value')
    })
  })

  describe('delete', () => {
    it('should remove key from localStorage', async () => {
      localStorage.setItem('ava_cred_delete_me', 'value')
      await store.delete('delete_me')
      expect(localStorage.getItem('ava_cred_delete_me')).toBeNull()
    })

    it('should handle deleting non-existent key', async () => {
      await expect(store.delete('non_existent')).resolves.not.toThrow()
    })
  })

  describe('has', () => {
    it('should return true for existing key', async () => {
      localStorage.setItem('ava_cred_exists', 'value')
      expect(await store.has('exists')).toBe(true)
    })

    it('should return false for non-existent key', async () => {
      expect(await store.has('not_exists')).toBe(false)
    })

    it('should check env vars', async () => {
      const originalEnv = process.env
      process.env = { ...originalEnv, HAS_KEY: 'value' }

      expect(await store.has('has_key')).toBe(true)

      process.env = originalEnv
    })
  })

  describe('cache management', () => {
    it('should clear disk cache', () => {
      // Just verify it doesn't throw
      expect(() => store.clearCache()).not.toThrow()
    })
  })

  describe('key normalization', () => {
    it('should convert hyphens to underscores', async () => {
      const originalEnv = process.env
      process.env = { ...originalEnv, MY_API_KEY: 'value' }

      const value = await store.get('my-api-key')
      expect(value).toBe('value')

      process.env = originalEnv
    })

    it('should convert to uppercase', async () => {
      const originalEnv = process.env
      process.env = { ...originalEnv, UPPER_KEY: 'value' }

      const value = await store.get('upper_key')
      expect(value).toBe('value')

      process.env = originalEnv
    })
  })
})
