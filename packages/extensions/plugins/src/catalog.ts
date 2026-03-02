/**
 * Plugin catalog -- search, fetch metadata from registry.
 */

import { createLogger } from '@ava/core-v2/logger'

const log = createLogger('PluginCatalog')

export interface CatalogEntry {
  name: string
  version: string
  description: string
  author: string
  repository: string
  downloads: number
  rating: number
  tags: string[]
  updatedAt: string
}

export interface CatalogSearchResult {
  entries: CatalogEntry[]
  total: number
  page: number
  pageSize: number
}

const CATALOG_URL = 'https://raw.githubusercontent.com/anthropics/ava-plugins/main/catalog.json'

let cachedCatalog: CatalogEntry[] | null = null
let cacheTimestamp = 0
const CACHE_TTL = 30 * 60 * 1000 // 30 minutes

export async function fetchCatalog(): Promise<CatalogEntry[]> {
  if (cachedCatalog && Date.now() - cacheTimestamp < CACHE_TTL) {
    return cachedCatalog
  }

  try {
    const response = await fetch(CATALOG_URL)
    if (!response.ok) throw new Error(`HTTP ${response.status}`)
    const data = (await response.json()) as { plugins: CatalogEntry[] }
    cachedCatalog = data.plugins ?? []
    cacheTimestamp = Date.now()
    return cachedCatalog
  } catch (err) {
    log.warn(`Failed to fetch catalog: ${err instanceof Error ? err.message : 'unknown'}`)
    return cachedCatalog ?? []
  }
}

export async function searchCatalog(
  query: string,
  page = 1,
  pageSize = 20
): Promise<CatalogSearchResult> {
  const all = await fetchCatalog()
  const lowerQuery = query.toLowerCase()

  const filtered = all.filter(
    (entry) =>
      entry.name.toLowerCase().includes(lowerQuery) ||
      entry.description.toLowerCase().includes(lowerQuery) ||
      entry.tags.some((t) => t.toLowerCase().includes(lowerQuery))
  )

  const start = (page - 1) * pageSize
  const entries = filtered.slice(start, start + pageSize)

  return { entries, total: filtered.length, page, pageSize }
}

export async function getCatalogEntry(name: string): Promise<CatalogEntry | undefined> {
  const all = await fetchCatalog()
  return all.find((e) => e.name === name)
}

export function clearCatalogCache(): void {
  cachedCatalog = null
  cacheTimestamp = 0
}
