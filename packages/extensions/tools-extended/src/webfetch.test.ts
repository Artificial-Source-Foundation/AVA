import { describe, expect, it } from 'vitest'
import {
  extractDescription,
  extractTitle,
  htmlToMarkdown,
  normalizeUrl,
  truncateContent,
  webfetchTool,
} from './webfetch.js'

describe('webfetchTool', () => {
  it('has correct definition', () => {
    expect(webfetchTool.definition.name).toBe('webfetch')
  })
})

describe('htmlToMarkdown', () => {
  it('converts headers', () => {
    expect(htmlToMarkdown('<h1>Title</h1>')).toBe('# Title')
    expect(htmlToMarkdown('<h2>Sub</h2>')).toBe('## Sub')
    expect(htmlToMarkdown('<h3>Sub3</h3>')).toBe('### Sub3')
  })

  it('converts links', () => {
    expect(htmlToMarkdown('<a href="https://example.com">Link</a>')).toBe(
      '[Link](https://example.com)'
    )
  })

  it('converts emphasis', () => {
    expect(htmlToMarkdown('<strong>bold</strong>')).toBe('**bold**')
    expect(htmlToMarkdown('<em>italic</em>')).toBe('*italic*')
  })

  it('converts code', () => {
    expect(htmlToMarkdown('<code>inline</code>')).toBe('`inline`')
  })

  it('converts lists', () => {
    const html = '<ul><li>one</li><li>two</li></ul>'
    const md = htmlToMarkdown(html)
    expect(md).toContain('- one')
    expect(md).toContain('- two')
  })

  it('removes scripts and styles', () => {
    const html = '<script>alert("xss")</script><style>.foo{}</style><p>Content</p>'
    const md = htmlToMarkdown(html)
    expect(md).not.toContain('alert')
    expect(md).not.toContain('.foo')
    expect(md).toContain('Content')
  })

  it('decodes HTML entities', () => {
    expect(htmlToMarkdown('&amp; &lt; &gt; &quot;')).toBe('& < > "')
  })
})

describe('extractTitle', () => {
  it('extracts title tag', () => {
    expect(extractTitle('<html><head><title>My Page</title></head></html>')).toBe('My Page')
  })

  it('returns undefined for missing title', () => {
    expect(extractTitle('<html><head></head></html>')).toBeUndefined()
  })
})

describe('extractDescription', () => {
  it('extracts meta description', () => {
    const html = '<meta name="description" content="Page desc">'
    expect(extractDescription(html)).toBe('Page desc')
  })

  it('returns undefined for missing description', () => {
    expect(extractDescription('<html></html>')).toBeUndefined()
  })
})

describe('normalizeUrl', () => {
  it('adds https:// if missing', () => {
    expect(normalizeUrl('example.com')).toBe('https://example.com')
  })

  it('keeps existing protocol', () => {
    expect(normalizeUrl('http://example.com')).toBe('http://example.com')
    expect(normalizeUrl('https://example.com')).toBe('https://example.com')
  })

  it('throws for invalid URL', () => {
    expect(() => normalizeUrl('not a url \\$%^')).toThrow('Invalid URL')
  })
})

describe('truncateContent', () => {
  it('does not truncate short content', () => {
    const result = truncateContent('short', 100)
    expect(result.truncated).toBe(false)
    expect(result.content).toBe('short')
  })

  it('truncates long content', () => {
    const long = 'word '.repeat(20000)
    const result = truncateContent(long, 1000)
    expect(result.truncated).toBe(true)
    expect(result.content.length).toBeLessThanOrEqual(1050) // Allow for truncation marker
  })

  it('tries to break at paragraph boundary', () => {
    const content = `first paragraph\n\n${'a'.repeat(100)}`
    const result = truncateContent(content, 50)
    expect(result.truncated).toBe(true)
  })
})
