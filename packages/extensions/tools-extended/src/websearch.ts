/**
 * WebSearch Tool — search the web via Tavily or Exa APIs.
 *
 * Ported from packages/core/src/tools/websearch.ts (380→~120 lines).
 * Uses defineTool() + Zod instead of manual validation.
 */

import { defineTool } from '@ava/core-v2/tools'
import { z } from 'zod'

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
  return process.env[envKey]
}

function detectProvider(): 'tavily' | 'exa' | undefined {
  if (getApiKey('tavily')) return 'tavily'
  if (getApiKey('exa')) return 'exa'
  return undefined
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
  description: `Search the web for information.

Use this tool when you need to:
- Find current information beyond your knowledge cutoff
- Research libraries, frameworks, or APIs
- Look up documentation or examples

Returns search results with titles, URLs, and snippets.
Requires API key: Set TAVILY_API_KEY or EXA_API_KEY environment variable.`,

  schema: z.object({
    query: z.string().describe('The search query'),
    numResults: z
      .number()
      .min(1)
      .max(20)
      .optional()
      .describe('Number of results to return (default: 8)'),
    provider: z
      .enum(['tavily', 'exa'])
      .optional()
      .describe('Search provider to use (default: auto-detect)'),
  }),

  permissions: ['read'],

  async execute(input, ctx) {
    if (ctx.signal.aborted) {
      return { success: false, output: 'Operation was cancelled', error: 'EXECUTION_ABORTED' }
    }

    const provider = input.provider ?? detectProvider()
    if (!provider) {
      return {
        success: false,
        output:
          'No search provider configured. Set TAVILY_API_KEY or EXA_API_KEY environment variable.',
        error: 'NO_PROVIDER',
      }
    }

    const apiKey = getApiKey(provider)
    if (!apiKey) {
      const envKey = provider === 'tavily' ? 'TAVILY_API_KEY' : 'EXA_API_KEY'
      return {
        success: false,
        output: `Missing API key: Set ${envKey} environment variable`,
        error: 'MISSING_API_KEY',
      }
    }

    const numResults = input.numResults ?? 8

    if (ctx.metadata) {
      ctx.metadata({
        title: `Searching: ${input.query.slice(0, 50)}...`,
        metadata: { query: input.query, provider, numResults },
      })
    }

    try {
      const response =
        provider === 'tavily'
          ? await searchTavily(input.query, numResults, apiKey, ctx.signal)
          : await searchExa(input.query, numResults, apiKey, ctx.signal)

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
