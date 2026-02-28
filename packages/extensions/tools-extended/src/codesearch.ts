/**
 * Code Search Tool — search API docs and code examples via Exa API.
 *
 * Simplified port from packages/core/src/tools/codesearch.ts (314→~120 lines).
 * Direct Exa API calls instead of the legacy Exa client wrapper.
 */

import { defineTool } from '@ava/core-v2/tools'
import { z } from 'zod'

const DEFAULT_NUM_RESULTS = 5
const DEFAULT_TOKENS = 5000

interface ExaResult {
  title: string
  url: string
  text?: string
  publishedDate?: string
  author?: string
  score: number
}

function getExaApiKey(): string | undefined {
  return process.env.EXA_API_KEY
}

async function searchExa(
  query: string,
  numResults: number,
  maxChars: number,
  signal: AbortSignal,
  category?: string
): Promise<ExaResult[]> {
  const apiKey = getExaApiKey()
  if (!apiKey) throw new Error('EXA_API_KEY not set')

  const body: Record<string, unknown> = {
    query,
    numResults,
    useAutoprompt: true,
    type: 'neural',
    contents: { text: { maxCharacters: maxChars } },
  }

  if (category) {
    body.category = category
  }

  const response = await fetch('https://api.exa.ai/search', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'x-api-key': apiKey },
    body: JSON.stringify(body),
    signal,
  })

  if (!response.ok) {
    const text = await response.text()
    throw new Error(`Exa API error (${response.status}): ${text}`)
  }

  const data = (await response.json()) as { results: ExaResult[] }
  return data.results ?? []
}

function formatSearchResults(results: ExaResult[], query: string, searchType: string): string {
  const lines: string[] = [
    `## Code Search Results`,
    ``,
    `**Query:** ${query}`,
    `**Type:** ${searchType}`,
    `**Results:** ${results.length}`,
    ``,
    `---`,
    ``,
  ]

  for (const [i, result] of results.entries()) {
    lines.push(`### [${i + 1}] ${result.title || 'Untitled'}`)
    lines.push(``)
    lines.push(`**URL:** ${result.url}`)
    if (result.publishedDate) lines.push(`**Published:** ${result.publishedDate}`)
    if (result.author) lines.push(`**Author:** ${result.author}`)
    lines.push(`**Relevance:** ${(result.score * 100).toFixed(1)}%`)
    lines.push(``)

    if (result.text) {
      const cleanText = result.text.replace(/\n{3,}/g, '\n\n').trim()
      lines.push(`<content>`, cleanText, `</content>`)
    }
    lines.push(``)
  }

  return lines.join('\n')
}

export const codesearchTool = defineTool({
  name: 'codesearch',
  description: `Search API documentation and code examples using Exa.

Search modes:
- general: Search across all relevant sources
- docs: Focus on official documentation and API references
- code: Focus on code examples and implementations

Usage examples:
- Documentation: { "query": "React useEffect cleanup", "searchType": "docs" }
- Code examples: { "query": "express middleware", "searchType": "code" }

Requires EXA_API_KEY environment variable.`,

  schema: z.object({
    query: z.string().min(1).describe('Code or documentation search query'),
    numResults: z.number().min(1).max(10).optional().describe('Number of results (default: 5)'),
    searchType: z
      .enum(['general', 'docs', 'code'])
      .optional()
      .describe('Search type (default: general)'),
  }),

  permissions: ['read'],

  async execute(input, ctx) {
    if (ctx.signal.aborted) {
      return { success: false, output: 'Operation was cancelled', error: 'EXECUTION_ABORTED' }
    }

    if (!getExaApiKey()) {
      return {
        success: false,
        output: `Exa API is not configured. Set the EXA_API_KEY environment variable.\n\nVisit https://exa.ai to get an API key.\nAlternatively, install the Exa MCP server for use via the MCP extension.`,
        error: 'EXA_NOT_CONFIGURED',
      }
    }

    const { query, numResults = DEFAULT_NUM_RESULTS, searchType = 'general' } = input

    try {
      // Map searchType to Exa category
      let category: string | undefined
      let searchQuery = query
      if (searchType === 'docs') {
        searchQuery = `${query} documentation API reference`
      } else if (searchType === 'code') {
        category = 'github'
      }

      const results = await searchExa(searchQuery, numResults, DEFAULT_TOKENS, ctx.signal, category)

      if (results.length === 0) {
        return {
          success: true,
          output: `No results found for: "${query}"\n\nTry using different keywords or broadening your search.`,
          metadata: { query, searchType, resultCount: 0 },
        }
      }

      const output = formatSearchResults(results, query, searchType)

      if (ctx.metadata) {
        ctx.metadata({
          title: `Code search: ${results.length} results`,
          metadata: { query, searchType, resultCount: results.length },
        })
      }

      return {
        success: true,
        output,
        metadata: {
          query,
          searchType,
          resultCount: results.length,
          results: results.map((r) => ({ url: r.url, title: r.title, score: r.score })),
        },
      }
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      if (message.includes('401') || message.includes('Unauthorized')) {
        return {
          success: false,
          output: 'Invalid Exa API key. Check your EXA_API_KEY environment variable.',
          error: 'EXA_AUTH_ERROR',
        }
      }
      if (message.includes('429') || message.includes('rate limit')) {
        return {
          success: false,
          output: 'Exa API rate limit exceeded. Please wait and try again.',
          error: 'EXA_RATE_LIMIT',
        }
      }
      return { success: false, output: `Code search failed: ${message}`, error: 'SEARCH_FAILED' }
    }
  },
})
