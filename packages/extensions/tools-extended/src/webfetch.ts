/**
 * WebFetch Tool — fetch and extract content from web pages.
 *
 * Ported from packages/core/src/tools/webfetch.ts (427→~170 lines).
 * Pure HTTP + HTML→markdown, no platform deps.
 */

import { defineTool } from '@ava/core-v2/tools'
import { z } from 'zod'

const DEFAULT_MAX_CHARS = 50000
const USER_AGENT = 'Mozilla/5.0 (compatible; AVA/1.0; +https://github.com/ava) AVA WebFetch'
const REQUEST_TIMEOUT = 30000

export function htmlToMarkdown(html: string): string {
  let text = html

  // Remove scripts, styles, noscript
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

  // Convert paragraphs and line breaks
  text = text.replace(/<p[^>]*>/gi, '\n')
  text = text.replace(/<\/p>/gi, '\n')
  text = text.replace(/<br\s*\/?>/gi, '\n')
  text = text.replace(/<hr\s*\/?>/gi, '\n---\n')

  // Convert lists
  text = text.replace(/<li[^>]*>/gi, '- ')
  text = text.replace(/<\/li>/gi, '\n')
  text = text.replace(/<\/?[uo]l[^>]*>/gi, '\n')

  // Convert links
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

export function extractTitle(html: string): string | undefined {
  const match = html.match(/<title[^>]*>(.*?)<\/title>/i)
  return match?.[1]?.trim()
}

export function extractDescription(html: string): string | undefined {
  const match = html.match(/<meta[^>]*name=["']description["'][^>]*content=["']([^"']*)["']/i)
  return match?.[1]?.trim()
}

export function normalizeUrl(url: string): string {
  if (!url.startsWith('http://') && !url.startsWith('https://')) {
    url = `https://${url}`
  }
  try {
    new URL(url)
    return url
  } catch {
    throw new Error(`Invalid URL: ${url}`)
  }
}

export function truncateContent(
  content: string,
  maxChars: number
): { content: string; truncated: boolean } {
  if (content.length <= maxChars) {
    return { content, truncated: false }
  }

  const truncated = content.slice(0, maxChars)
  const lastParagraph = truncated.lastIndexOf('\n\n')

  if (lastParagraph > maxChars * 0.8) {
    return {
      content: `${truncated.slice(0, lastParagraph)}\n\n[Content truncated...]`,
      truncated: true,
    }
  }

  const lastSpace = truncated.lastIndexOf(' ')
  if (lastSpace > maxChars * 0.9) {
    return { content: `${truncated.slice(0, lastSpace)}... [Content truncated]`, truncated: true }
  }

  return { content: `${truncated}... [Content truncated]`, truncated: true }
}

export const webfetchTool = defineTool({
  name: 'webfetch',
  description: `Fetch and extract content from a web page.

Use this tool when you need to:
- Read documentation from a URL
- Extract content from a specific web page
- Get information from a known URL

The tool fetches the page, converts HTML to readable markdown, and returns the content.
Note: Some sites may block automated requests or require authentication.`,

  schema: z.object({
    url: z.string().describe('The URL to fetch'),
    prompt: z.string().optional().describe('What information to extract from the page'),
    maxChars: z
      .number()
      .min(1000)
      .max(200000)
      .optional()
      .describe('Maximum characters to return (default: 50000)'),
  }),

  permissions: ['read'],

  async execute(input, ctx) {
    if (ctx.signal.aborted) {
      return { success: false, output: 'Operation was cancelled', error: 'EXECUTION_ABORTED' }
    }

    let url: string
    try {
      url = normalizeUrl(input.url)
    } catch (err) {
      return {
        success: false,
        output: err instanceof Error ? err.message : 'Invalid URL',
        error: 'INVALID_URL',
      }
    }

    const maxChars = input.maxChars ?? DEFAULT_MAX_CHARS

    if (ctx.metadata) {
      ctx.metadata({ title: `Fetching: ${url.slice(0, 50)}...`, metadata: { url, maxChars } })
    }

    try {
      const controller = new AbortController()
      const timeout = setTimeout(() => controller.abort(), REQUEST_TIMEOUT)
      ctx.signal.addEventListener('abort', () => controller.abort())

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
          error: 'FETCH_FAILED',
        }
      }

      const contentType = response.headers.get('content-type') ?? ''
      const isHtml = contentType.includes('text/html') || contentType.includes('application/xhtml')
      const rawContent = await response.text()

      const title = isHtml ? extractTitle(rawContent) : undefined
      const description = isHtml ? extractDescription(rawContent) : undefined
      const content = isHtml ? htmlToMarkdown(rawContent) : rawContent
      const { content: truncatedContent, truncated } = truncateContent(content, maxChars)

      const outputLines: string[] = []
      if (title) {
        outputLines.push(`# ${title}`, '')
      }
      outputLines.push(`**URL:** ${url}`)
      if (description) {
        outputLines.push(`**Description:** ${description}`)
      }
      if (truncated) {
        outputLines.push(`**Note:** Content truncated to ${maxChars} characters`)
      }
      outputLines.push('', '---', '', truncatedContent)

      const output = outputLines.join('\n')

      if (ctx.metadata) {
        ctx.metadata({
          title: `Fetched: ${title ?? url.slice(0, 50)}`,
          metadata: { url, title, contentLength: content.length, truncated },
        })
      }

      return {
        success: true,
        output,
        metadata: { url, title, description, contentLength: content.length, truncated },
      }
    } catch (err) {
      if (ctx.signal.aborted) {
        return { success: false, output: 'Operation was cancelled', error: 'EXECUTION_ABORTED' }
      }
      const message = err instanceof Error ? err.message : String(err)
      if (message.includes('aborted') || message.includes('abort')) {
        return { success: false, output: 'Request timed out', error: 'TIMEOUT' }
      }
      return { success: false, output: `Failed to fetch URL: ${message}`, error: 'FETCH_FAILED' }
    }
  },
})
