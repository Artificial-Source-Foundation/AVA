/**
 * WebSearch Tool — search the web via DuckDuckGo (free), Tavily, or Exa.
 *
 * DuckDuckGo HTML scraping is the default — no API key required.
 * Tavily and Exa are optional power-user fallbacks when API keys are set.
 */

import { defineTool } from '@ava/core-v2/tools'
import { z } from 'zod'
import { getEnv } from './env.js'

interface SearchResult {
  title: string
  url: string
  snippet: string
  score?: number
}

interface SearchResponse {
  results: SearchResult[]
  query: string
  provider: string
}

async function searchDuckDuckGo(
  query: string,
  numResults: number,
  signal: AbortSignal
): Promise<SearchResponse> {
  const response = await fetch('https://html.duckduckgo.com/html/', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: `q=${encodeURIComponent(query)}`,
    signal,
  })

  if (!response.ok) {
    throw new Error(`DuckDuckGo error (${response.status})`)
  }

  const html = await response.text()
  const results: SearchResult[] = []

  // Parse result blocks: each result lives in a <div class="result ...">
  const resultBlocks = html.split(/class="result\s/)
  for (let i = 1; i < resultBlocks.length && results.length < numResults; i++) {
    const block = resultBlocks[i]!

    // Extract URL and title from <a class="result__a" href="...">title</a>
    const linkMatch = block.match(/class="result__a"[^>]*href="([^"]*)"[^>]*>([\s\S]*?)<\/a>/)
    if (!linkMatch) continue

    let url = linkMatch[1]!
    const title = linkMatch[2]!.replace(/<[^>]*>/g, '').trim()

    // DuckDuckGo wraps URLs in a redirect — extract the actual URL
    const uddgMatch = url.match(/[?&]uddg=([^&]+)/)
    if (uddgMatch?.[1]) {
      url = decodeURIComponent(uddgMatch[1])
    }

    // Extract snippet from <a class="result__snippet" ...>...</a>
    const snippetMatch = block.match(/class="result__snippet"[^>]*>([\s\S]*?)<\/a>/)
    const snippet = snippetMatch?.[1]?.replace(/<[^>]*>/g, '').trim() ?? ''

    if (url && title) {
      results.push({ title, url, snippet })
    }
  }

  return { query, provider: 'duckduckgo', results }
}

async function searchTavily(
  query: string,
  numResults: number,
  apiKey: string,
  signal: AbortSignal
): Promise<SearchResponse> {
  const response = await fetch('https://api.tavily.com/search', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
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
    results: Array<{ title: string; url: string; content: string; score: number }>
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

async function searchExa(
  query: string,
  numResults: number,
  apiKey: string,
  signal: AbortSignal
): Promise<SearchResponse> {
  const response = await fetch('https://api.exa.ai/search', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'x-api-key': apiKey },
    body: JSON.stringify({ query, numResults, useAutoprompt: true, type: 'neural' }),
    signal,
  })

  if (!response.ok) {
    const text = await response.text()
    throw new Error(`Exa API error (${response.status}): ${text}`)
  }

  const data = (await response.json()) as {
    results: Array<{ title: string; url: string; text?: string; score: number }>
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

function getApiKey(provider: 'tavily' | 'exa'): string | undefined {
  const envKey = provider === 'tavily' ? 'TAVILY_API_KEY' : 'EXA_API_KEY'
  return getEnv(envKey)
}

function detectProvider(): 'tavily' | 'exa' | 'duckduckgo' {
  if (getApiKey('tavily')) return 'tavily'
  if (getApiKey('exa')) return 'exa'
  return 'duckduckgo'
}

function formatResults(response: SearchResponse): string {
  if (response.results.length === 0) {
    return `No results found for: "${response.query}"`
  }

  const lines: string[] = [`Search results for: "${response.query}" (via ${response.provider})`, '']

  for (const [i, r] of response.results.entries()) {
    lines.push(`${i + 1}. ${r.title}`)
    lines.push(`   URL: ${r.url}`)
    if (r.snippet) {
      const snippet = r.snippet.length > 300 ? `${r.snippet.slice(0, 300)}...` : r.snippet
      lines.push(`   ${snippet}`)
    }
    lines.push('')
  }

  return lines.join('\n')
}

export const websearchTool = defineTool({
  name: 'websearch',
  description:
    'Search the web. Returns titles, URLs, and snippets. DuckDuckGo by default (no API key needed).',

  schema: z.object({
    query: z.string().describe('The search query'),
    numResults: z
      .number()
      .min(1)
      .max(20)
      .optional()
      .describe('Number of results to return (default: 8)'),
    provider: z
      .enum(['duckduckgo', 'tavily', 'exa'])
      .optional()
      .describe('Search provider to use (default: auto-detect)'),
  }),

  permissions: ['read'],

  async execute(input, ctx) {
    if (ctx.signal.aborted) {
      return { success: false, output: 'Operation was cancelled', error: 'EXECUTION_ABORTED' }
    }

    const provider = input.provider ?? detectProvider()
    const numResults = input.numResults ?? 8

    // Validate API key for paid providers
    if (provider !== 'duckduckgo') {
      const apiKey = getApiKey(provider)
      if (!apiKey) {
        const envKey = provider === 'tavily' ? 'TAVILY_API_KEY' : 'EXA_API_KEY'
        return {
          success: false,
          output: `Missing API key: Set ${envKey} environment variable`,
          error: 'MISSING_API_KEY',
        }
      }
    }

    if (ctx.metadata) {
      ctx.metadata({
        title: `Searching: ${input.query.slice(0, 50)}...`,
        metadata: { query: input.query, provider, numResults },
      })
    }

    try {
      let response: SearchResponse
      if (provider === 'tavily') {
        response = await searchTavily(input.query, numResults, getApiKey('tavily')!, ctx.signal)
      } else if (provider === 'exa') {
        response = await searchExa(input.query, numResults, getApiKey('exa')!, ctx.signal)
      } else {
        response = await searchDuckDuckGo(input.query, numResults, ctx.signal)
      }

      const output = formatResults(response)

      if (ctx.metadata) {
        ctx.metadata({
          title: `Found ${response.results.length} results`,
          metadata: { query: input.query, provider, resultCount: response.results.length },
        })
      }

      return {
        success: true,
        output,
        metadata: {
          query: input.query,
          provider,
          resultCount: response.results.length,
          results: response.results,
        },
      }
    } catch (err) {
      if (ctx.signal.aborted) {
        return { success: false, output: 'Operation was cancelled', error: 'EXECUTION_ABORTED' }
      }
      const message = err instanceof Error ? err.message : String(err)
      return { success: false, output: `Search failed: ${message}`, error: 'SEARCH_FAILED' }
    }
  },
})
