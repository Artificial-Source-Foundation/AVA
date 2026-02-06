/**
 * Code Search Tool
 * Search API documentation and code examples using Exa API
 *
 * Based on OpenCode's codesearch tool pattern
 */

import { z } from 'zod'
import {
  type ExaSearchResponse,
  type ExaSearchResult,
  getExaClient,
  isExaConfigured,
} from '../integrations/exa.js'
import { defineTool } from './define.js'
import { ToolErrorType } from './errors.js'
import type { ToolResult } from './types.js'

// ============================================================================
// Constants
// ============================================================================

/** Default number of results */
const DEFAULT_NUM_RESULTS = 5

/** Minimum tokens (characters) per result */
const MIN_TOKENS = 1000

/** Maximum tokens (characters) per result */
const MAX_TOKENS = 50000

/** Default tokens per result */
const DEFAULT_TOKENS = 5000

// ============================================================================
// Schema
// ============================================================================

const CodeSearchSchema = z.object({
  query: z.string().min(1).describe('Search query for documentation or code examples'),
  tokensNum: z
    .number()
    .min(MIN_TOKENS)
    .max(MAX_TOKENS)
    .optional()
    .describe(
      `Number of tokens (characters) per result (${MIN_TOKENS}-${MAX_TOKENS}, default: ${DEFAULT_TOKENS})`
    ),
  numResults: z
    .number()
    .min(1)
    .max(10)
    .optional()
    .describe('Number of results to return (1-10, default: 5)'),
  searchType: z
    .enum(['general', 'docs', 'code'])
    .optional()
    .describe('Type of search: general (default), docs (API documentation), code (code examples)'),
  language: z
    .string()
    .optional()
    .describe('Programming language for code searches (e.g., "typescript", "python")'),
  library: z
    .string()
    .optional()
    .describe('Library/framework name for documentation searches (e.g., "react", "express")'),
})

type CodeSearchParams = z.infer<typeof CodeSearchSchema>

// ============================================================================
// Tool Implementation
// ============================================================================

export const codesearchTool = defineTool({
  name: 'codesearch',
  description: `Search API documentation and code examples using Exa.

Key features:
- Search up-to-date library documentation
- Find code examples from GitHub, StackOverflow, and more
- Configure token count per result for more/less detail
- Three search modes: general, docs (documentation), code (examples)

Search modes:
- general: Search across all relevant sources
- docs: Focus on official documentation and API references
- code: Focus on code examples and implementations

Usage examples:
- Documentation: { "query": "React useEffect cleanup", "searchType": "docs" }
- Code examples: { "query": "express middleware", "searchType": "code", "language": "typescript" }
- Library docs: { "query": "error handling", "searchType": "docs", "library": "express" }

Requirements:
- Requires EXA_API_KEY environment variable
- Results are cached for 5 minutes to reduce API calls

Token guidelines:
- 1000: Quick snippets and summaries
- 5000: Balanced detail (default)
- 20000+: Comprehensive documentation`,

  schema: CodeSearchSchema,

  permissions: ['read'],

  async execute(params: CodeSearchParams, ctx): Promise<ToolResult> {
    // Check abort signal
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    // Check if Exa is configured
    if (!isExaConfigured()) {
      return {
        success: false,
        output: `Exa API is not configured. Set the EXA_API_KEY environment variable to use code search.

To get an API key:
1. Visit https://exa.ai
2. Create an account
3. Generate an API key
4. Set EXA_API_KEY in your environment`,
        error: 'EXA_NOT_CONFIGURED',
      }
    }

    const {
      query,
      tokensNum = DEFAULT_TOKENS,
      numResults = DEFAULT_NUM_RESULTS,
      searchType = 'general',
      language,
      library,
    } = params

    try {
      const client = getExaClient()
      let response: ExaSearchResponse

      // Execute appropriate search type
      switch (searchType) {
        case 'docs':
          if (library) {
            response = await client.searchDocs(library, query, {
              numResults,
              maxCharacters: tokensNum,
            })
          } else {
            response = await client.search(`${query} documentation API reference`, {
              numResults,
              maxCharacters: tokensNum,
              useDocDomains: true,
            })
          }
          break

        case 'code':
          response = await client.searchCode(query, language, {
            numResults,
            maxCharacters: tokensNum,
          })
          break

        default:
          response = await client.search(query, {
            numResults,
            maxCharacters: tokensNum,
          })
      }

      // Check if we got results
      if (!response.results || response.results.length === 0) {
        return {
          success: true,
          output: `No results found for: "${query}"

Try:
- Using different keywords
- Broadening your search
- Checking the library name spelling`,
          metadata: {
            query,
            searchType,
            resultCount: 0,
          },
        }
      }

      // Format results
      const output = formatSearchResults(response.results, query, searchType)

      // Stream metadata if available
      if (ctx.metadata) {
        ctx.metadata({
          title: `Code search: ${response.results.length} results`,
          metadata: {
            query,
            searchType,
            resultCount: response.results.length,
            tokensNum,
          },
        })
      }

      return {
        success: true,
        output,
        metadata: {
          query,
          searchType,
          resultCount: response.results.length,
          tokensNum,
          results: response.results.map((r) => ({
            url: r.url,
            title: r.title,
            score: r.score,
          })),
        },
      }
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)

      // Handle specific error cases
      if (message.includes('401') || message.includes('Unauthorized')) {
        return {
          success: false,
          output: `Invalid Exa API key. Please check your EXA_API_KEY environment variable.`,
          error: 'EXA_AUTH_ERROR',
        }
      }

      if (message.includes('429') || message.includes('rate limit')) {
        return {
          success: false,
          output: `Exa API rate limit exceeded. Please wait a moment and try again.`,
          error: 'EXA_RATE_LIMIT',
        }
      }

      return {
        success: false,
        output: `Code search failed: ${message}`,
        error: ToolErrorType.UNKNOWN,
      }
    }
  },
})

// ============================================================================
// Output Formatting
// ============================================================================

/**
 * Format search results into readable output
 */
function formatSearchResults(
  results: ExaSearchResult[],
  query: string,
  searchType: string
): string {
  const lines: string[] = []

  // Header
  lines.push(`## Code Search Results`)
  lines.push(``)
  lines.push(`**Query:** ${query}`)
  lines.push(`**Type:** ${searchType}`)
  lines.push(`**Results:** ${results.length}`)
  lines.push(``)
  lines.push(`---`)
  lines.push(``)

  // Individual results
  for (let i = 0; i < results.length; i++) {
    const result = results[i]

    lines.push(`### [${i + 1}] ${result.title || 'Untitled'}`)
    lines.push(``)
    lines.push(`**URL:** ${result.url}`)

    if (result.publishedDate) {
      lines.push(`**Published:** ${result.publishedDate}`)
    }

    if (result.author) {
      lines.push(`**Author:** ${result.author}`)
    }

    lines.push(`**Relevance:** ${(result.score * 100).toFixed(1)}%`)
    lines.push(``)

    if (result.text) {
      // Clean up and format the text content
      const cleanText = result.text
        .replace(/\n{3,}/g, '\n\n') // Collapse multiple newlines
        .trim()

      lines.push(`<content>`)
      lines.push(cleanText)
      lines.push(`</content>`)
    }

    lines.push(``)
  }

  return lines.join('\n')
}
