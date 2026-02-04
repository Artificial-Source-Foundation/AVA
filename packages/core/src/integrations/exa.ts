/**
 * Exa API Integration
 * Search API documentation and code examples
 *
 * Based on OpenCode's codesearch pattern
 */

// ============================================================================
// Types
// ============================================================================

/**
 * Exa search request parameters
 */
export interface ExaSearchRequest {
  /** Search query */
  query: string
  /** Number of results to return (default: 5) */
  numResults?: number
  /** Content options */
  contents?: {
    /** Number of characters to return per result */
    text?: {
      maxCharacters?: number
    }
  }
  /** Whether to use autoprompt for better results */
  useAutoprompt?: boolean
  /** Domain filters */
  includeDomains?: string[]
  /** Category filter */
  category?: string
}

/**
 * Exa search result
 */
export interface ExaSearchResult {
  /** Result URL */
  url: string
  /** Result title */
  title: string
  /** Published date if available */
  publishedDate?: string
  /** Author if available */
  author?: string
  /** Relevance score */
  score: number
  /** Text content */
  text?: string
}

/**
 * Exa search response
 */
export interface ExaSearchResponse {
  /** Search results */
  results: ExaSearchResult[]
  /** Request ID */
  requestId?: string
  /** Whether autoprompt was used */
  autopromptString?: string
}

/**
 * Exa API error
 */
export interface ExaError {
  /** Error message */
  message: string
  /** Error code */
  code?: string
}

// ============================================================================
// Constants
// ============================================================================

/** Exa API base URL */
const EXA_API_BASE = 'https://api.exa.ai'

/** Default number of results */
const DEFAULT_NUM_RESULTS = 5

/** Default max characters per result */
const DEFAULT_MAX_CHARS = 5000

/** Cache TTL in milliseconds (5 minutes) */
const CACHE_TTL_MS = 5 * 60 * 1000

/** Documentation-focused domains */
const DOC_DOMAINS = [
  'docs.python.org',
  'developer.mozilla.org',
  'reactjs.org',
  'react.dev',
  'nodejs.org',
  'typescriptlang.org',
  'npmjs.com',
  'github.com',
  'stackoverflow.com',
  'docs.rs',
  'pkg.go.dev',
]

// ============================================================================
// Cache
// ============================================================================

interface CacheEntry {
  response: ExaSearchResponse
  timestamp: number
}

const cache = new Map<string, CacheEntry>()

/**
 * Generate cache key from request
 */
function getCacheKey(request: ExaSearchRequest): string {
  return JSON.stringify({
    query: request.query,
    numResults: request.numResults,
    maxChars: request.contents?.text?.maxCharacters,
    domains: request.includeDomains,
    category: request.category,
  })
}

/**
 * Get cached response if valid
 */
function getFromCache(key: string): ExaSearchResponse | null {
  const entry = cache.get(key)
  if (!entry) return null

  const now = Date.now()
  if (now - entry.timestamp > CACHE_TTL_MS) {
    cache.delete(key)
    return null
  }

  return entry.response
}

/**
 * Store response in cache
 */
function setInCache(key: string, response: ExaSearchResponse): void {
  cache.set(key, {
    response,
    timestamp: Date.now(),
  })

  // Limit cache size
  if (cache.size > 100) {
    const firstKey = cache.keys().next().value
    if (firstKey) {
      cache.delete(firstKey)
    }
  }
}

/**
 * Clear the cache
 */
export function clearExaCache(): void {
  cache.clear()
}

// ============================================================================
// API Client
// ============================================================================

/**
 * Exa API client
 */
export class ExaClient {
  private apiKey: string

  constructor(apiKey: string) {
    this.apiKey = apiKey
  }

  /**
   * Search for documentation and code examples
   *
   * @param query - Search query
   * @param options - Search options
   * @returns Search results
   */
  async search(
    query: string,
    options: {
      numResults?: number
      maxCharacters?: number
      includeDomains?: string[]
      useDocDomains?: boolean
      category?: string
    } = {}
  ): Promise<ExaSearchResponse> {
    const request: ExaSearchRequest = {
      query,
      numResults: options.numResults ?? DEFAULT_NUM_RESULTS,
      useAutoprompt: true,
      contents: {
        text: {
          maxCharacters: options.maxCharacters ?? DEFAULT_MAX_CHARS,
        },
      },
    }

    // Add domain filters
    if (options.includeDomains && options.includeDomains.length > 0) {
      request.includeDomains = options.includeDomains
    } else if (options.useDocDomains) {
      request.includeDomains = DOC_DOMAINS
    }

    // Add category if specified
    if (options.category) {
      request.category = options.category
    }

    // Check cache
    const cacheKey = getCacheKey(request)
    const cached = getFromCache(cacheKey)
    if (cached) {
      return cached
    }

    // Make API request
    const response = await fetch(`${EXA_API_BASE}/search`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'x-api-key': this.apiKey,
      },
      body: JSON.stringify(request),
    })

    if (!response.ok) {
      const error = (await response.json().catch(() => ({}))) as ExaError
      throw new Error(error.message || `Exa API error: ${response.status} ${response.statusText}`)
    }

    const result = (await response.json()) as ExaSearchResponse

    // Cache the result
    setInCache(cacheKey, result)

    return result
  }

  /**
   * Search specifically for API documentation
   *
   * @param library - Library/framework name
   * @param topic - Specific topic to search for
   * @param options - Additional search options
   */
  async searchDocs(
    library: string,
    topic: string,
    options: {
      numResults?: number
      maxCharacters?: number
    } = {}
  ): Promise<ExaSearchResponse> {
    const query = `${library} ${topic} documentation API reference`
    return this.search(query, {
      ...options,
      useDocDomains: true,
      category: 'research paper',
    })
  }

  /**
   * Search for code examples
   *
   * @param query - What kind of code to search for
   * @param language - Programming language
   * @param options - Additional search options
   */
  async searchCode(
    query: string,
    language?: string,
    options: {
      numResults?: number
      maxCharacters?: number
    } = {}
  ): Promise<ExaSearchResponse> {
    const fullQuery = language ? `${language} ${query} code example` : `${query} code example`
    return this.search(fullQuery, {
      ...options,
      includeDomains: ['github.com', 'stackoverflow.com', 'gist.github.com'],
    })
  }
}

// ============================================================================
// Factory
// ============================================================================

/** Singleton client instance */
let clientInstance: ExaClient | null = null

/**
 * Get or create Exa client
 *
 * @param apiKey - Exa API key (uses EXA_API_KEY env var if not provided)
 * @returns Exa client instance
 */
export function getExaClient(apiKey?: string): ExaClient {
  const key = apiKey || process.env.EXA_API_KEY

  if (!key) {
    throw new Error(
      'Exa API key not provided. Set EXA_API_KEY environment variable or pass apiKey.'
    )
  }

  if (!clientInstance || (apiKey && apiKey !== clientInstance['apiKey'])) {
    clientInstance = new ExaClient(key)
  }

  return clientInstance
}

/**
 * Check if Exa is configured
 */
export function isExaConfigured(): boolean {
  return Boolean(process.env.EXA_API_KEY)
}
