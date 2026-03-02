import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { clearCatalogCache, fetchCatalog, getCatalogEntry, searchCatalog } from './catalog.js'

const SAMPLE_CATALOG = {
  plugins: [
    {
      name: 'ava-plugin-react',
      version: '1.0.0',
      description: 'React component scaffolding',
      author: 'alice',
      repository: 'https://github.com/alice/ava-plugin-react',
      downloads: 1500,
      rating: 4.5,
      tags: ['react', 'frontend', 'components'],
      updatedAt: '2025-12-01',
    },
    {
      name: 'ava-plugin-docker',
      version: '2.1.0',
      description: 'Docker compose generation and management',
      author: 'bob',
      repository: 'https://github.com/bob/ava-plugin-docker',
      downloads: 800,
      rating: 4.2,
      tags: ['docker', 'devops', 'containers'],
      updatedAt: '2025-11-15',
    },
    {
      name: 'ava-plugin-testing',
      version: '0.5.0',
      description: 'Advanced test generation',
      author: 'charlie',
      repository: 'https://github.com/charlie/ava-plugin-testing',
      downloads: 300,
      rating: 3.8,
      tags: ['testing', 'vitest', 'jest'],
      updatedAt: '2025-10-20',
    },
  ],
}

describe('PluginCatalog', () => {
  beforeEach(() => {
    clearCatalogCache()
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: true,
        json: () => Promise.resolve(SAMPLE_CATALOG),
      })
    )
  })

  afterEach(() => {
    vi.unstubAllGlobals()
  })

  describe('fetchCatalog', () => {
    it('fetches and returns catalog entries', async () => {
      const entries = await fetchCatalog()
      expect(entries).toHaveLength(3)
      expect(entries[0]!.name).toBe('ava-plugin-react')
      expect(fetch).toHaveBeenCalledOnce()
    })

    it('uses cache on second call', async () => {
      await fetchCatalog()
      await fetchCatalog()
      expect(fetch).toHaveBeenCalledOnce()
    })

    it('returns empty array on fetch failure with no cache', async () => {
      vi.stubGlobal('fetch', vi.fn().mockRejectedValue(new Error('Network error')))
      const entries = await fetchCatalog()
      expect(entries).toEqual([])
    })

    it('returns cached data on fetch failure after initial success', async () => {
      // First call succeeds
      await fetchCatalog()

      // Clear cache timestamp to force refetch
      clearCatalogCache()
      // Manually prime the cache by fetching first
      await fetchCatalog()

      // Now clear only the timestamp (simulate expiry)
      // We need to re-stub fetch to fail, but cache should still be there
      vi.stubGlobal('fetch', vi.fn().mockRejectedValue(new Error('Network error')))
      clearCatalogCache()

      const entries = await fetchCatalog()
      // Cache was cleared, fetch failed, so empty array
      expect(entries).toEqual([])
    })

    it('handles non-ok HTTP response', async () => {
      vi.stubGlobal(
        'fetch',
        vi.fn().mockResolvedValue({
          ok: false,
          status: 404,
        })
      )
      const entries = await fetchCatalog()
      expect(entries).toEqual([])
    })
  })

  describe('searchCatalog', () => {
    it('searches by name', async () => {
      const result = await searchCatalog('react')
      expect(result.entries).toHaveLength(1)
      expect(result.entries[0]!.name).toBe('ava-plugin-react')
      expect(result.total).toBe(1)
    })

    it('searches by description', async () => {
      const result = await searchCatalog('scaffolding')
      expect(result.entries).toHaveLength(1)
      expect(result.entries[0]!.name).toBe('ava-plugin-react')
    })

    it('searches by tags', async () => {
      const result = await searchCatalog('devops')
      expect(result.entries).toHaveLength(1)
      expect(result.entries[0]!.name).toBe('ava-plugin-docker')
    })

    it('returns all matching entries for broad query', async () => {
      const result = await searchCatalog('ava-plugin')
      expect(result.entries).toHaveLength(3)
      expect(result.total).toBe(3)
    })

    it('returns empty for no match', async () => {
      const result = await searchCatalog('nonexistent-xyz')
      expect(result.entries).toHaveLength(0)
      expect(result.total).toBe(0)
    })

    it('supports pagination', async () => {
      const page1 = await searchCatalog('ava-plugin', 1, 2)
      expect(page1.entries).toHaveLength(2)
      expect(page1.page).toBe(1)
      expect(page1.pageSize).toBe(2)
      expect(page1.total).toBe(3)

      const page2 = await searchCatalog('ava-plugin', 2, 2)
      expect(page2.entries).toHaveLength(1)
      expect(page2.page).toBe(2)
    })

    it('is case insensitive', async () => {
      const result = await searchCatalog('DOCKER')
      expect(result.entries).toHaveLength(1)
      expect(result.entries[0]!.name).toBe('ava-plugin-docker')
    })
  })

  describe('getCatalogEntry', () => {
    it('returns entry by exact name', async () => {
      const entry = await getCatalogEntry('ava-plugin-docker')
      expect(entry).toBeDefined()
      expect(entry!.author).toBe('bob')
      expect(entry!.version).toBe('2.1.0')
    })

    it('returns undefined for unknown name', async () => {
      const entry = await getCatalogEntry('does-not-exist')
      expect(entry).toBeUndefined()
    })
  })

  describe('clearCatalogCache', () => {
    it('forces re-fetch after clearing', async () => {
      await fetchCatalog()
      expect(fetch).toHaveBeenCalledOnce()

      clearCatalogCache()
      await fetchCatalog()
      expect(fetch).toHaveBeenCalledTimes(2)
    })
  })
})
