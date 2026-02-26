import { describe, expect, it } from 'vitest'
import {
  ensureTrailingNewline,
  hasMarkdownFences,
  normalizeLineEndings,
  sanitizeContent,
  stripMarkdownFences,
} from './sanitize.js'

describe('sanitizeContent', () => {
  it('strips markdown fences by default', () => {
    const input = '```typescript\nconst x = 1\n```'
    expect(sanitizeContent(input)).toBe('const x = 1\n')
  })

  it('normalizes line endings by default', () => {
    expect(sanitizeContent('line1\r\nline2')).toBe('line1\nline2\n')
  })

  it('ensures trailing newline by default', () => {
    expect(sanitizeContent('content')).toBe('content\n')
  })

  it('respects stripFences: false', () => {
    const input = '```ts\ncode\n```'
    expect(sanitizeContent(input, { stripFences: false })).toBe('```ts\ncode\n```\n')
  })

  it('respects normalizeLineEndings: false', () => {
    expect(sanitizeContent('a\r\nb', { normalizeLineEndings: false })).toBe('a\r\nb\n')
  })

  it('respects ensureTrailingNewline: false', () => {
    expect(sanitizeContent('content', { ensureTrailingNewline: false })).toBe('content')
  })

  it('handles empty string', () => {
    expect(sanitizeContent('')).toBe('')
  })
})

describe('stripMarkdownFences', () => {
  it('strips basic fences', () => {
    expect(stripMarkdownFences('```\ncode\n```')).toBe('code')
  })

  it('strips fences with language tag', () => {
    expect(stripMarkdownFences('```typescript\nconst x = 1\n```')).toBe('const x = 1')
  })

  it('preserves content without fences', () => {
    expect(stripMarkdownFences('just text')).toBe('just text')
  })

  it('preserves content with only opening fence', () => {
    expect(stripMarkdownFences('```ts\ncode')).toBe('```ts\ncode')
  })

  it('handles single line', () => {
    expect(stripMarkdownFences('hello')).toBe('hello')
  })

  it('handles multiline content within fences', () => {
    const input = '```js\nline1\nline2\nline3\n```'
    expect(stripMarkdownFences(input)).toBe('line1\nline2\nline3')
  })
})

describe('normalizeLineEndings', () => {
  it('converts CRLF to LF', () => {
    expect(normalizeLineEndings('a\r\nb\r\nc')).toBe('a\nb\nc')
  })

  it('leaves LF unchanged', () => {
    expect(normalizeLineEndings('a\nb\nc')).toBe('a\nb\nc')
  })

  it('handles mixed line endings', () => {
    expect(normalizeLineEndings('a\r\nb\nc\r\nd')).toBe('a\nb\nc\nd')
  })

  it('handles empty string', () => {
    expect(normalizeLineEndings('')).toBe('')
  })
})

describe('ensureTrailingNewline', () => {
  it('adds newline when missing', () => {
    expect(ensureTrailingNewline('content')).toBe('content\n')
  })

  it('does not double newline', () => {
    expect(ensureTrailingNewline('content\n')).toBe('content\n')
  })

  it('handles empty string', () => {
    expect(ensureTrailingNewline('')).toBe('')
  })
})

describe('hasMarkdownFences', () => {
  it('detects fences', () => {
    expect(hasMarkdownFences('```ts\ncode\n```')).toBe(true)
  })

  it('returns false without fences', () => {
    expect(hasMarkdownFences('just text')).toBe(false)
  })

  it('returns false for single line', () => {
    expect(hasMarkdownFences('hello')).toBe(false)
  })

  it('requires both opening and closing', () => {
    expect(hasMarkdownFences('```ts\ncode')).toBe(false)
  })
})
