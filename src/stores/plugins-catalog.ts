import type { PluginCatalogItem } from '../types/plugin'

export type PluginCatalogStatus = 'idle' | 'syncing' | 'ready' | 'error'

const CATALOG_URL =
  'https://raw.githubusercontent.com/anthropic-ava/plugin-catalog/main/catalog.json'
const CATALOG_CACHE_KEY = 'ava:plugin-catalog'
const CATALOG_TIMESTAMP_KEY = 'ava:plugin-catalog-ts'
const CATALOG_TTL_MS = 30 * 60 * 1000 // 30 minutes

export const FALLBACK_CATALOG: PluginCatalogItem[] = [
  {
    id: 'task-planner',
    name: 'Task Planner',
    description: 'Breaks goals into actionable implementation steps.',
    category: 'workflow',
    version: '1.4.0',
    source: 'official',
    trust: 'verified',
    changelogSummary: 'Added milestone templates and dependency hints.',
  },
  {
    id: 'test-guard',
    name: 'Test Guard',
    description: 'Runs verification checks before completion.',
    category: 'quality',
    version: '0.9.3',
    source: 'official',
    trust: 'verified',
    changelogSummary: 'Expanded flaky-test diagnostics and retry classification.',
  },
  {
    id: 'git-helper',
    name: 'Git Helper',
    description: 'Guides commit hygiene and pull request workflows.',
    category: 'workflow',
    version: '1.1.2',
    source: 'community',
    trust: 'reviewed',
    changelogSummary: 'Improved commit message hints and branch naming checks.',
  },
  {
    id: 'mcp-inspector',
    name: 'MCP Inspector',
    description: 'Inspects and validates MCP server wiring.',
    category: 'integration',
    version: '0.7.0',
    source: 'official',
    trust: 'verified',
    changelogSummary: 'Added auth flow checks and endpoint handshake diagnostics.',
  },
]

/** @deprecated Use getPluginCatalog() for live catalog */
export const PLUGIN_CATALOG = FALLBACK_CATALOG

export const FEATURED_PLUGIN_IDS = ['task-planner', 'test-guard']

let cachedCatalog: PluginCatalogItem[] = FALLBACK_CATALOG

export function getPluginCatalog(): PluginCatalogItem[] {
  return cachedCatalog
}

export function getFeaturedPluginIds(): string[] {
  return FEATURED_PLUGIN_IDS
}

function isCacheValid(): boolean {
  if (typeof localStorage === 'undefined') return false
  const ts = localStorage.getItem(CATALOG_TIMESTAMP_KEY)
  if (!ts) return false
  return Date.now() - Number(ts) < CATALOG_TTL_MS
}

function loadFromCache(): PluginCatalogItem[] | null {
  if (typeof localStorage === 'undefined') return null
  const raw = localStorage.getItem(CATALOG_CACHE_KEY)
  if (!raw) return null
  try {
    return JSON.parse(raw) as PluginCatalogItem[]
  } catch {
    return null
  }
}

function saveToCache(catalog: PluginCatalogItem[]): void {
  if (typeof localStorage === 'undefined') return
  localStorage.setItem(CATALOG_CACHE_KEY, JSON.stringify(catalog))
  localStorage.setItem(CATALOG_TIMESTAMP_KEY, String(Date.now()))
}

export async function syncPluginCatalog(): Promise<PluginCatalogItem[]> {
  // Check cache first
  if (isCacheValid()) {
    const cached = loadFromCache()
    if (cached) {
      cachedCatalog = cached
      return cached
    }
  }

  // Try fetching remote catalog
  try {
    const response = await fetch(CATALOG_URL)
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`)
    }
    const data = (await response.json()) as PluginCatalogItem[]
    saveToCache(data)
    cachedCatalog = data
    return data
  } catch {
    // Network error or parse error — fall back
    const cached = loadFromCache()
    if (cached) {
      cachedCatalog = cached
      return cached
    }
    cachedCatalog = FALLBACK_CATALOG
    return FALLBACK_CATALOG
  }
}
