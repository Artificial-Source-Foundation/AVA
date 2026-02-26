import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import {
  FALLBACK_CATALOG,
  FEATURED_PLUGIN_IDS,
  getFeaturedPluginIds,
  getPluginCatalog,
  syncPluginCatalog,
} from './plugins-catalog'

describe('plugins-catalog', () => {
  beforeEach(() => {
    localStorage.clear()
    vi.restoreAllMocks()
  })

  afterEach(() => {
    localStorage.clear()
  })

  it('getPluginCatalog returns fallback catalog initially', () => {
    const catalog = getPluginCatalog()
    expect(catalog).toEqual(FALLBACK_CATALOG)
    expect(catalog.length).toBe(4)
  })

  it('getFeaturedPluginIds returns expected ids', () => {
    expect(getFeaturedPluginIds()).toEqual(FEATURED_PLUGIN_IDS)
  })

  it('fallback catalog has expected items', () => {
    const ids = FALLBACK_CATALOG.map((p) => p.id)
    expect(ids).toContain('task-planner')
    expect(ids).toContain('test-guard')
    expect(ids).toContain('git-helper')
    expect(ids).toContain('mcp-inspector')
  })

  it('syncPluginCatalog fetches and caches remote catalog', async () => {
    const remote = [{ ...FALLBACK_CATALOG[0], version: '2.0.0' }]
    vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce(
      new Response(JSON.stringify(remote), { status: 200 })
    )

    const result = await syncPluginCatalog()
    expect(result).toEqual(remote)
    expect(getPluginCatalog()).toEqual(remote)
    expect(localStorage.getItem('ava:plugin-catalog')).toBeTruthy()
  })

  it('syncPluginCatalog uses cache when within TTL', async () => {
    const cached = [{ ...FALLBACK_CATALOG[0], version: '1.5.0' }]
    localStorage.setItem('ava:plugin-catalog', JSON.stringify(cached))
    localStorage.setItem('ava:plugin-catalog-ts', String(Date.now()))

    const fetchSpy = vi.spyOn(globalThis, 'fetch')
    const result = await syncPluginCatalog()
    expect(result).toEqual(cached)
    expect(fetchSpy).not.toHaveBeenCalled()
  })

  it('syncPluginCatalog falls back to hardcoded on network error', async () => {
    vi.spyOn(globalThis, 'fetch').mockRejectedValueOnce(new Error('Network failure'))

    const result = await syncPluginCatalog()
    expect(result).toEqual(FALLBACK_CATALOG)
  })

  it('syncPluginCatalog falls back to cache on HTTP error', async () => {
    const cached = [{ ...FALLBACK_CATALOG[0], version: '1.3.0' }]
    localStorage.setItem('ava:plugin-catalog', JSON.stringify(cached))
    // Expired cache — force refetch
    localStorage.setItem('ava:plugin-catalog-ts', String(Date.now() - 60 * 60 * 1000))

    vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce(
      new Response('Server Error', { status: 500 })
    )

    const result = await syncPluginCatalog()
    expect(result).toEqual(cached)
  })

  it('catalog items have required fields', () => {
    for (const item of FALLBACK_CATALOG) {
      expect(item.id).toBeTruthy()
      expect(item.name).toBeTruthy()
      expect(item.description).toBeTruthy()
      expect(item.category).toBeTruthy()
      expect(item.version).toBeTruthy()
      expect(item.source).toBeTruthy()
      expect(item.trust).toBeTruthy()
    }
  })
})
