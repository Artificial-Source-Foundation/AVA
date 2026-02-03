/**
 * WebFetch Tool
 * Fetch and extract content from web pages
 */

import { ToolError, ToolErrorType } from './errors.js'
import type { Tool, ToolContext, ToolResult } from './types.js'

// ============================================================================
// Types
// ============================================================================

interface WebFetchParams {
  /** URL to fetch */
  url: string
  /** Optional prompt describing what to extract */
  prompt?: string
  /** Maximum characters to return (default: 50000) */
  maxChars?: number
}

// ============================================================================
// Constants
// ============================================================================

/** Default maximum characters */
const DEFAULT_MAX_CHARS = 50000

/** User agent for requests */
const USER_AGENT =
  'Mozilla/5.0 (compatible; Estela/1.0; +https://github.com/estela) Estela WebFetch'

/** Request timeout in milliseconds */
const REQUEST_TIMEOUT = 30000

// ============================================================================
// HTML to Markdown Converter
// ============================================================================

/**
 * Simple HTML to Markdown converter
 * Strips scripts/styles and converts basic HTML to readable text
 */
function htmlToMarkdown(html: string): string {
  let text = html

  // Remove scripts and styles completely
  text = text.replace(/<script\b[^<]*(?:(?!<\/script>)<[^<]*)*<\/script>/gi, '')
  text = text.replace(/<style\b[^<]*(?:(?!<\/style>)<[^<]*)*<\/style>/gi, '')
  text = text.replace(/<noscript\b[^<]*(?:(?!<\/noscript>)<[^<]*)*<\/noscript>/gi, '')

  // Convert headers
  text = text.replace(/<h1[^>]*>(.*?)<\/h1>/gi, '\n# $1\n')
  text = text.replace(/<h2[^>]*>(.*?)<\/h2>/gi, '\n## $1\n')
  text = text.replace(/<h3[^>]*>(.*?)<\/h3>/gi, '\n### $1\n')
  text = text.replace(/<h4[^>]*>(.*?)<\/h4>/gi, '\n#### $1\n')
  text = text.replace(/<h5[^>]*>(.*?)<\/h5>/gi, '\n##### $1\n')
  text = text.replace(/<h6[^>]*>(.*?)<\/h6>/gi, '\n###### $1\n')

  // Convert paragraphs
  text = text.replace(/<p[^>]*>/gi, '\n')
  text = text.replace(/<\/p>/gi, '\n')

  // Convert line breaks
  text = text.replace(/<br\s*\/?>/gi, '\n')
  text = text.replace(/<hr\s*\/?>/gi, '\n---\n')

  // Convert lists
  text = text.replace(/<li[^>]*>/gi, '- ')
  text = text.replace(/<\/li>/gi, '\n')
  text = text.replace(/<\/?[uo]l[^>]*>/gi, '\n')

  // Convert links (preserve URL)
  text = text.replace(/<a[^>]*href=["']([^"']*)["'][^>]*>(.*?)<\/a>/gi, '[$2]($1)')

  // Convert emphasis
  text = text.replace(/<strong[^>]*>(.*?)<\/strong>/gi, '**$1**')
  text = text.replace(/<b[^>]*>(.*?)<\/b>/gi, '**$1**')
  text = text.replace(/<em[^>]*>(.*?)<\/em>/gi, '*$1*')
  text = text.replace(/<i[^>]*>(.*?)<\/i>/gi, '*$1*')

  // Convert code
  text = text.replace(/<code[^>]*>(.*?)<\/code>/gi, '`$1`')
  text = text.replace(/<pre[^>]*>(.*?)<\/pre>/gis, '\n```\n$1\n```\n')

  // Convert blockquotes
  text = text.replace(/<blockquote[^>]*>(.*?)<\/blockquote>/gis, (_, content: string) => {
    return content
      .split('\n')
      .map((line) => `> ${line}`)
      .join('\n')
  })

  // Remove remaining HTML tags
  text = text.replace(/<[^>]+>/g, '')

  // Decode HTML entities
  text = text.replace(/&nbsp;/g, ' ')
  text = text.replace(/&amp;/g, '&')
  text = text.replace(/&lt;/g, '<')
  text = text.replace(/&gt;/g, '>')
  text = text.replace(/&quot;/g, '"')
  text = text.replace(/&#39;/g, "'")
  text = text.replace(/&#x27;/g, "'")
  text = text.replace(/&#x2F;/g, '/')

  // Clean up whitespace
  text = text.replace(/\n{3,}/g, '\n\n')
  text = text.replace(/[ \t]+/g, ' ')
  text = text
    .split('\n')
    .map((line) => line.trim())
    .join('\n')
  text = text.trim()

  return text
}

/**
 * Extract title from HTML
 */
function extractTitle(html: string): string | undefined {
  const match = html.match(/<title[^>]*>(.*?)<\/title>/i)
  return match?.[1]?.trim()
}

/**
 * Extract meta description from HTML
 */
function extractDescription(html: string): string | undefined {
  const match = html.match(/<meta[^>]*name=["']description["'][^>]*content=["']([^"']*)["']/i)
  return match?.[1]?.trim()
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Validate and normalize URL
 */
function normalizeUrl(url: string): string {
  // Add protocol if missing
  if (!url.startsWith('http://') && !url.startsWith('https://')) {
    url = 'https://' + url
  }

  // Validate URL
  try {
    new URL(url)
    return url
  } catch {
    throw new Error(`Invalid URL: ${url}`)
  }
}

/**
 * Truncate content to maximum characters
 */
function truncateContent(
  content: string,
  maxChars: number
): { content: string; truncated: boolean } {
  if (content.length <= maxChars) {
    return { content, truncated: false }
  }

  // Try to truncate at a paragraph boundary
  const truncated = content.slice(0, maxChars)
  const lastParagraph = truncated.lastIndexOf('\n\n')

  if (lastParagraph > maxChars * 0.8) {
    return {
      content: truncated.slice(0, lastParagraph) + '\n\n[Content truncated...]',
      truncated: true,
    }
  }

  // Fall back to word boundary
  const lastSpace = truncated.lastIndexOf(' ')
  if (lastSpace > maxChars * 0.9) {
    return {
      content: truncated.slice(0, lastSpace) + '... [Content truncated]',
      truncated: true,
    }
  }

  return {
    content: truncated + '... [Content truncated]',
    truncated: true,
  }
}

// ============================================================================
// Tool Implementation
// ============================================================================

export const webfetchTool: Tool<WebFetchParams> = {
  definition: {
    name: 'webfetch',
    description: `Fetch and extract content from a web page.

Use this tool when you need to:
- Read documentation from a URL
- Extract content from a specific web page
- Get information from a known URL

The tool fetches the page, converts HTML to readable markdown, and returns the content.

Note: Some sites may block automated requests or require authentication.`,
    input_schema: {
      type: 'object',
      properties: {
        url: {
          type: 'string',
          description: 'The URL to fetch',
        },
        prompt: {
          type: 'string',
          description: 'Optional: What information to extract from the page',
        },
        maxChars: {
          type: 'number',
          description: 'Maximum characters to return (default: 50000)',
        },
      },
      required: ['url'],
    },
  },

  validate(params: unknown): WebFetchParams {
    if (typeof params !== 'object' || params === null) {
      throw new ToolError(
        'Invalid params: expected object',
        ToolErrorType.INVALID_PARAMS,
        'webfetch'
      )
    }

    const { url, prompt, maxChars } = params as Record<string, unknown>

    if (typeof url !== 'string' || !url.trim()) {
      throw new ToolError(
        'Invalid url: must be non-empty string',
        ToolErrorType.INVALID_PARAMS,
        'webfetch'
      )
    }

    if (prompt !== undefined && typeof prompt !== 'string') {
      throw new ToolError(
        'Invalid prompt: must be string',
        ToolErrorType.INVALID_PARAMS,
        'webfetch'
      )
    }

    if (maxChars !== undefined) {
      if (typeof maxChars !== 'number' || maxChars < 1000 || maxChars > 200000) {
        throw new ToolError(
          'Invalid maxChars: must be number between 1000 and 200000',
          ToolErrorType.INVALID_PARAMS,
          'webfetch'
        )
      }
    }

    return {
      url: url.trim(),
      prompt: typeof prompt === 'string' ? prompt.trim() : undefined,
      maxChars: maxChars as number | undefined,
    }
  },

  async execute(params: WebFetchParams, ctx: ToolContext): Promise<ToolResult> {
    // Check abort signal
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    // Normalize URL
    let url: string
    try {
      url = normalizeUrl(params.url)
    } catch (err) {
      return {
        success: false,
        output: err instanceof Error ? err.message : 'Invalid URL',
        error: ToolErrorType.INVALID_PARAMS,
      }
    }

    const maxChars = params.maxChars ?? DEFAULT_MAX_CHARS

    // Stream metadata
    if (ctx.metadata) {
      ctx.metadata({
        title: `Fetching: ${url.slice(0, 50)}...`,
        metadata: { url, maxChars },
      })
    }

    try {
      // Create abort controller for timeout
      const controller = new AbortController()
      const timeout = setTimeout(() => controller.abort(), REQUEST_TIMEOUT)

      // Combine with context signal
      ctx.signal.addEventListener('abort', () => controller.abort())

      // Fetch the URL
      const response = await fetch(url, {
        headers: {
          'User-Agent': USER_AGENT,
          Accept: 'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
          'Accept-Language': 'en-US,en;q=0.5',
        },
        signal: controller.signal,
      })

      clearTimeout(timeout)

      if (!response.ok) {
        return {
          success: false,
          output: `Failed to fetch URL (${response.status}): ${response.statusText}`,
          error: ToolErrorType.UNKNOWN,
        }
      }

      // Check content type
      const contentType = response.headers.get('content-type') ?? ''
      const isHtml = contentType.includes('text/html') || contentType.includes('application/xhtml')

      // Get content
      const rawContent = await response.text()

      // Extract metadata
      const title = isHtml ? extractTitle(rawContent) : undefined
      const description = isHtml ? extractDescription(rawContent) : undefined

      // Convert to markdown if HTML
      const content = isHtml ? htmlToMarkdown(rawContent) : rawContent

      // Truncate if needed
      const { content: truncatedContent, truncated } = truncateContent(content, maxChars)

      // Format output
      const outputLines: string[] = []

      if (title) {
        outputLines.push(`# ${title}`)
        outputLines.push('')
      }

      outputLines.push(`**URL:** ${url}`)

      if (description) {
        outputLines.push(`**Description:** ${description}`)
      }

      if (truncated) {
        outputLines.push(`**Note:** Content truncated to ${maxChars} characters`)
      }

      outputLines.push('')
      outputLines.push('---')
      outputLines.push('')
      outputLines.push(truncatedContent)

      const output = outputLines.join('\n')

      // Stream completion
      if (ctx.metadata) {
        ctx.metadata({
          title: `Fetched: ${title ?? url.slice(0, 50)}`,
          metadata: {
            url,
            title,
            contentLength: content.length,
            truncated,
          },
        })
      }

      return {
        success: true,
        output,
        metadata: {
          url,
          title,
          description,
          contentLength: content.length,
          truncated,
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

      if (message.includes('aborted') || message.includes('abort')) {
        return {
          success: false,
          output: 'Request timed out',
          error: ToolErrorType.EXECUTION_TIMEOUT,
        }
      }

      return {
        success: false,
        output: `Failed to fetch URL: ${message}`,
        error: ToolErrorType.UNKNOWN,
      }
    }
  },
}
