/**
 * WebSearch Tool
 * Search the web for information
 *
 * Supports multiple providers:
 * - Tavily API (AI-optimized search)
 * - Exa API (Neural search)
 */

import { ToolError, ToolErrorType } from './errors.js'
import type { Tool, ToolContext, ToolResult } from './types.js'

// ============================================================================
// Types
// ============================================================================

interface WebSearchParams {
  /** The search query */
  query: string
  /** Number of results to return (default: 8) */
  numResults?: number
  /** Search provider to use (default: auto-detect) */
  provider?: 'tavily' | 'exa'
}

interface SearchResult {
  /** Result title */
  title: string
  /** Result URL */
  url: string
  /** Content snippet */
  snippet: string
  /** Relevance score (0-1) */
  score?: number
}

interface SearchResponse {
  /** The search results */
  results: SearchResult[]
  /** The query used */
  query: string
  /** Provider used */
  provider: string
}

// ============================================================================
// Provider Implementations
// ============================================================================

/**
 * Search using Tavily API
 * https://tavily.com/
 */
async function searchTavily(
  query: string,
  numResults: number,
  apiKey: string,
  signal: AbortSignal
): Promise<SearchResponse> {
  const response = await fetch('https://api.tavily.com/search', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({
      api_key: apiKey,
      query,
      max_results: numResults,
      include_answer: false,
      include_raw_content: false,
      include_images: false,
    }),
    signal,
  })

  if (!response.ok) {
    const text = await response.text()
    throw new Error(`Tavily API error (${response.status}): ${text}`)
  }

  const data = (await response.json()) as {
    results: Array<{
      title: string
      url: string
      content: string
      score: number
    }>
  }

  return {
    query,
    provider: 'tavily',
    results: data.results.map((r) => ({
      title: r.title,
      url: r.url,
      snippet: r.content,
      score: r.score,
    })),
  }
}

/**
 * Search using Exa API
 * https://exa.ai/
 */
async function searchExa(
  query: string,
  numResults: number,
  apiKey: string,
  signal: AbortSignal
): Promise<SearchResponse> {
  const response = await fetch('https://api.exa.ai/search', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'x-api-key': apiKey,
    },
    body: JSON.stringify({
      query,
      numResults,
      useAutoprompt: true,
      type: 'neural',
    }),
    signal,
  })

  if (!response.ok) {
    const text = await response.text()
    throw new Error(`Exa API error (${response.status}): ${text}`)
  }

  const data = (await response.json()) as {
    results: Array<{
      title: string
      url: string
      text?: string
      score: number
    }>
  }

  return {
    query,
    provider: 'exa',
    results: data.results.map((r) => ({
      title: r.title,
      url: r.url,
      snippet: r.text ?? '',
      score: r.score,
    })),
  }
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Get API key for a provider from environment
 */
function getApiKey(provider: 'tavily' | 'exa'): string | undefined {
  const envKey = provider === 'tavily' ? 'TAVILY_API_KEY' : 'EXA_API_KEY'
  return process.env[envKey]
}

/**
 * Auto-detect which provider to use based on available API keys
 */
function detectProvider(): 'tavily' | 'exa' | undefined {
  if (getApiKey('tavily')) return 'tavily'
  if (getApiKey('exa')) return 'exa'
  return undefined
}

/**
 * Format search results for LLM output
 */
function formatResults(response: SearchResponse): string {
  if (response.results.length === 0) {
    return `No results found for: "${response.query}"`
  }

  const lines: string[] = [`Search results for: "${response.query}" (via ${response.provider})`, '']

  for (let i = 0; i < response.results.length; i++) {
    const r = response.results[i]
    lines.push(`${i + 1}. ${r.title}`)
    lines.push(`   URL: ${r.url}`)
    if (r.snippet) {
      // Truncate long snippets
      const snippet = r.snippet.length > 300 ? r.snippet.slice(0, 300) + '...' : r.snippet
      lines.push(`   ${snippet}`)
    }
    lines.push('')
  }

  return lines.join('\n')
}

// ============================================================================
// Tool Implementation
// ============================================================================

export const websearchTool: Tool<WebSearchParams> = {
  definition: {
    name: 'websearch',
    description: `Search the web for information.

Use this tool when you need to:
- Find current information beyond your knowledge cutoff
- Research libraries, frameworks, or APIs
- Look up documentation or examples
- Get up-to-date information on any topic

Returns search results with titles, URLs, and snippets.

Requires API key: Set TAVILY_API_KEY or EXA_API_KEY environment variable.`,
    input_schema: {
      type: 'object',
      properties: {
        query: {
          type: 'string',
          description: 'The search query',
        },
        numResults: {
          type: 'number',
          description: 'Number of results to return (default: 8, max: 20)',
        },
        provider: {
          type: 'string',
          enum: ['tavily', 'exa'],
          description: 'Search provider to use (default: auto-detect)',
        },
      },
      required: ['query'],
    },
  },

  validate(params: unknown): WebSearchParams {
    if (typeof params !== 'object' || params === null) {
      throw new ToolError(
        'Invalid params: expected object',
        ToolErrorType.INVALID_PARAMS,
        'websearch'
      )
    }

    const { query, numResults, provider } = params as Record<string, unknown>

    if (typeof query !== 'string' || !query.trim()) {
      throw new ToolError(
        'Invalid query: must be non-empty string',
        ToolErrorType.INVALID_PARAMS,
        'websearch'
      )
    }

    if (numResults !== undefined) {
      if (typeof numResults !== 'number' || numResults < 1 || numResults > 20) {
        throw new ToolError(
          'Invalid numResults: must be number between 1 and 20',
          ToolErrorType.INVALID_PARAMS,
          'websearch'
        )
      }
    }

    if (provider !== undefined) {
      if (provider !== 'tavily' && provider !== 'exa') {
        throw new ToolError(
          'Invalid provider: must be "tavily" or "exa"',
          ToolErrorType.INVALID_PARAMS,
          'websearch'
        )
      }
    }

    return {
      query: query.trim(),
      numResults: numResults as number | undefined,
      provider: provider as 'tavily' | 'exa' | undefined,
    }
  },

  async execute(params: WebSearchParams, ctx: ToolContext): Promise<ToolResult> {
    // Check abort signal
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    // Determine provider
    const provider = params.provider ?? detectProvider()
    if (!provider) {
      return {
        success: false,
        output:
          'No search provider configured. Set TAVILY_API_KEY or EXA_API_KEY environment variable.',
        error: ToolErrorType.INVALID_PARAMS,
      }
    }

    // Get API key
    const apiKey = getApiKey(provider)
    if (!apiKey) {
      const envKey = provider === 'tavily' ? 'TAVILY_API_KEY' : 'EXA_API_KEY'
      return {
        success: false,
        output: `Missing API key: Set ${envKey} environment variable`,
        error: ToolErrorType.INVALID_PARAMS,
      }
    }

    const numResults = params.numResults ?? 8

    // Stream metadata
    if (ctx.metadata) {
      ctx.metadata({
        title: `Searching: ${params.query.slice(0, 50)}...`,
        metadata: {
          query: params.query,
          provider,
          numResults,
        },
      })
    }

    try {
      // Perform search
      const response =
        provider === 'tavily'
          ? await searchTavily(params.query, numResults, apiKey, ctx.signal)
          : await searchExa(params.query, numResults, apiKey, ctx.signal)

      // Format output
      const output = formatResults(response)

      // Stream completion
      if (ctx.metadata) {
        ctx.metadata({
          title: `Found ${response.results.length} results`,
          metadata: {
            query: params.query,
            provider,
            resultCount: response.results.length,
          },
        })
      }

      return {
        success: true,
        output,
        metadata: {
          query: params.query,
          provider,
          resultCount: response.results.length,
          results: response.results,
        },
      }
    } catch (err) {
      if (ctx.signal.aborted) {
        return {
          success: false,
          output: 'Operation was cancelled',
          error: ToolErrorType.EXECUTION_ABORTED,
        }
      }

      const message = err instanceof Error ? err.message : String(err)
      return {
        success: false,
        output: `Search failed: ${message}`,
        error: ToolErrorType.UNKNOWN,
      }
    }
  },
}
