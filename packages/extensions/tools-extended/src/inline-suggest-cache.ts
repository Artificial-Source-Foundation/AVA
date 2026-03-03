interface CacheEntry {
  value: string
  timestamp: number
}

const CACHE_TTL_MS = 30_000
const CACHE_LIMIT = 100

const cache = new Map<string, CacheEntry>()

function trimCache(): void {
  if (cache.size <= CACHE_LIMIT) {
    return
  }

  const sorted = [...cache.entries()].sort((a, b) => a[1].timestamp - b[1].timestamp)
  const overflow = cache.size - CACHE_LIMIT
  for (const [key] of sorted.slice(0, overflow)) {
    cache.delete(key)
  }
}

export function buildInlineSuggestCacheKey(parts: {
  path: string
  line: number
  column: number
  provider: string
  model: string
  prefix: string
  suffix: string
}): string {
  const prefixSlice = parts.prefix.slice(-120)
  const suffixSlice = parts.suffix.slice(0, 120)
  return [
    parts.path,
    parts.line,
    parts.column,
    parts.provider,
    parts.model,
    prefixSlice,
    suffixSlice,
  ].join('::')
}

export function getCachedInlineSuggestion(key: string): string | null {
  const entry = cache.get(key)
  if (!entry) {
    return null
  }

  if (Date.now() - entry.timestamp > CACHE_TTL_MS) {
    cache.delete(key)
    return null
  }

  return entry.value
}

export function setCachedInlineSuggestion(key: string, value: string): void {
  cache.set(key, { value, timestamp: Date.now() })
  trimCache()
}

export function clearInlineSuggestCache(): void {
  cache.clear()
}
