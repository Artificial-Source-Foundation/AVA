import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

// Mock Tauri invoke before importing the module
const mockInvoke = vi.fn()
vi.mock('@tauri-apps/api/core', () => ({
  invoke: (...args: unknown[]) => mockInvoke(...args),
}))

// Import after mocking
const { getEnvVar, env, prefetchEnvVars, clearEnvCache, initEnvBridge, COMMON_ENV_VARS } =
  await import('./env-bridge.js')

describe('env-bridge', () => {
  beforeEach(() => {
    clearEnvCache()
    mockInvoke.mockClear()
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  describe('getEnvVar', () => {
    it('should fetch env var from Rust backend', async () => {
      mockInvoke.mockResolvedValueOnce('test-api-key')

      const result = await getEnvVar('TEST_VAR')

      expect(result).toBe('test-api-key')
      expect(mockInvoke).toHaveBeenCalledWith('get_env_var', { name: 'TEST_VAR' })
    })

    it('should return undefined for unset env vars', async () => {
      mockInvoke.mockResolvedValueOnce(null)

      const result = await getEnvVar('UNSET_VAR')

      expect(result).toBeUndefined()
    })

    it('should cache results', async () => {
      mockInvoke.mockResolvedValueOnce('cached-value')

      // First call
      const result1 = await getEnvVar('CACHED_VAR')
      expect(result1).toBe('cached-value')
      expect(mockInvoke).toHaveBeenCalledTimes(1)

      // Second call should use cache
      const result2 = await getEnvVar('CACHED_VAR')
      expect(result2).toBe('cached-value')
      expect(mockInvoke).toHaveBeenCalledTimes(1) // Still 1, not 2
    })

    it('should handle errors gracefully', async () => {
      mockInvoke.mockRejectedValueOnce(new Error('IPC error'))

      const result = await getEnvVar('ERROR_VAR')

      expect(result).toBeUndefined()
    })

    it('should deduplicate concurrent requests', async () => {
      let resolvePromise: (value: string | null) => void
      const promise = new Promise<string | null>((resolve) => {
        resolvePromise = resolve
      })
      mockInvoke.mockReturnValueOnce(promise)

      // Start two concurrent requests
      const req1 = getEnvVar('DEDUP_VAR')
      const req2 = getEnvVar('DEDUP_VAR')

      // Should only call invoke once
      expect(mockInvoke).toHaveBeenCalledTimes(1)

      // Resolve the promise
      resolvePromise!('dedup-value')

      // Both should get the same result
      const [result1, result2] = await Promise.all([req1, req2])
      expect(result1).toBe('dedup-value')
      expect(result2).toBe('dedup-value')
      expect(mockInvoke).toHaveBeenCalledTimes(1)
    })
  })

  describe('env proxy', () => {
    it('should provide synchronous access to cached env vars', async () => {
      mockInvoke.mockResolvedValueOnce('proxy-value')

      // First fetch async
      await getEnvVar('PROXY_VAR')

      // Then access sync via proxy
      expect(env.PROXY_VAR).toBe('proxy-value')
    })

    it('should return undefined for uncached env vars', () => {
      // Accessing before fetching should return undefined
      expect(env.UNCACHED_VAR).toBeUndefined()
    })

    it('should throw on set', () => {
      expect(() => {
        // Testing write protection
        env.READONLY_VAR = 'value'
      }).toThrow('process.env is read-only in Tauri context')
    })

    it('should support has check for cached vars', async () => {
      mockInvoke.mockResolvedValueOnce('exists')
      await getEnvVar('EXISTS_VAR')

      expect('EXISTS_VAR' in env).toBe(true)
      expect('NOT_EXISTS' in env).toBe(false)
    })
  })

  describe('prefetchEnvVars', () => {
    it('should fetch multiple env vars in parallel', async () => {
      mockInvoke
        .mockResolvedValueOnce('value1')
        .mockResolvedValueOnce('value2')
        .mockResolvedValueOnce(null)

      await prefetchEnvVars(['VAR1', 'VAR2', 'VAR3'])

      expect(mockInvoke).toHaveBeenCalledTimes(3)
      expect(env.VAR1).toBe('value1')
      expect(env.VAR2).toBe('value2')
      expect(env.VAR3).toBeUndefined()
    })
  })

  describe('initEnvBridge', () => {
    it('should prefetch common env vars', async () => {
      mockInvoke.mockResolvedValue(null)

      await initEnvBridge()

      // Should call invoke for each common env var
      expect(mockInvoke).toHaveBeenCalledTimes(COMMON_ENV_VARS.length)

      // Verify some common vars were fetched
      expect(mockInvoke).toHaveBeenCalledWith('get_env_var', { name: 'ANTHROPIC_API_KEY' })
      expect(mockInvoke).toHaveBeenCalledWith('get_env_var', { name: 'TAVILY_API_KEY' })
    })
  })

  describe('clearEnvCache', () => {
    it('should clear the cache', async () => {
      mockInvoke.mockResolvedValueOnce('cached')
      await getEnvVar('CLEAR_TEST')
      expect(env.CLEAR_TEST).toBe('cached')

      clearEnvCache()

      // After clearing, should return undefined
      expect(env.CLEAR_TEST).toBeUndefined()

      // Next fetch should call invoke again
      mockInvoke.mockResolvedValueOnce('new-value')
      const result = await getEnvVar('CLEAR_TEST')
      expect(mockInvoke).toHaveBeenCalledTimes(2)
      expect(result).toBe('new-value')
    })
  })
})
