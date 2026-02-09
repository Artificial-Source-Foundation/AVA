/**
 * Content Sanitization Tests
 * Tests for LLM output sanitization before writing to files
 */

import { describe, expect, it } from 'vitest'
import {
  detectSanitizeModelFamily,
  ensureTrailingNewline,
  extractFenceLanguage,
  hasMarkdownFences,
  normalizeLineEndings,
  sanitizeContent,
  stripMarkdownFences,
  trimTrailingWhitespace,
} from './sanitize.js'

// ============================================================================
// detectSanitizeModelFamily
// ============================================================================

describe('detectSanitizeModelFamily', () => {
  describe('Claude / Anthropic', () => {
    it('should detect claude-3-opus', () => {
      expect(detectSanitizeModelFamily('claude-3-opus')).toBe('claude')
    })

    it('should detect claude-3-sonnet', () => {
      expect(detectSanitizeModelFamily('claude-3-sonnet')).toBe('claude')
    })

    it('should detect claude-3.5-sonnet-20241022', () => {
      expect(detectSanitizeModelFamily('claude-3.5-sonnet-20241022')).toBe('claude')
    })

    it('should detect anthropic prefix', () => {
      expect(detectSanitizeModelFamily('anthropic/claude-v2')).toBe('claude')
    })
  })

  describe('GPT / OpenAI', () => {
    it('should detect gpt-4o', () => {
      expect(detectSanitizeModelFamily('gpt-4o')).toBe('gpt')
    })

    it('should detect gpt-4-turbo', () => {
      expect(detectSanitizeModelFamily('gpt-4-turbo')).toBe('gpt')
    })

    it('should detect o1-preview', () => {
      expect(detectSanitizeModelFamily('o1-preview')).toBe('gpt')
    })

    it('should detect o3-mini', () => {
      expect(detectSanitizeModelFamily('o3-mini')).toBe('gpt')
    })

    it('should detect openai prefix', () => {
      expect(detectSanitizeModelFamily('openai/gpt-4')).toBe('gpt')
    })
  })

  describe('Gemini / Google', () => {
    it('should detect gemini-pro', () => {
      expect(detectSanitizeModelFamily('gemini-pro')).toBe('gemini')
    })

    it('should detect gemini-1.5-flash', () => {
      expect(detectSanitizeModelFamily('gemini-1.5-flash')).toBe('gemini')
    })

    it('should detect google prefix', () => {
      expect(detectSanitizeModelFamily('google/gemini-ultra')).toBe('gemini')
    })
  })

  describe('DeepSeek', () => {
    it('should detect deepseek-coder', () => {
      expect(detectSanitizeModelFamily('deepseek-coder')).toBe('deepseek')
    })

    it('should detect deepseek-chat', () => {
      expect(detectSanitizeModelFamily('deepseek-chat')).toBe('deepseek')
    })
  })

  describe('Llama / Meta', () => {
    it('should detect llama-3', () => {
      expect(detectSanitizeModelFamily('llama-3')).toBe('llama')
    })

    it('should detect meta-llama', () => {
      expect(detectSanitizeModelFamily('meta-llama/llama-3-70b')).toBe('llama')
    })

    it('should detect meta prefix alone', () => {
      expect(detectSanitizeModelFamily('meta/some-model')).toBe('llama')
    })
  })

  describe('Mistral', () => {
    it('should detect mistral-7b', () => {
      expect(detectSanitizeModelFamily('mistral-7b')).toBe('mistral')
    })

    it('should detect mistral-large', () => {
      expect(detectSanitizeModelFamily('mistral-large')).toBe('mistral')
    })

    it('should detect mixtral', () => {
      expect(detectSanitizeModelFamily('mixtral-8x7b')).toBe('mistral')
    })
  })

  describe('Unknown', () => {
    it('should return unknown for unrecognized model', () => {
      expect(detectSanitizeModelFamily('some-custom-model')).toBe('unknown')
    })

    it('should return unknown for empty string', () => {
      expect(detectSanitizeModelFamily('')).toBe('unknown')
    })

    it('should be case-insensitive', () => {
      expect(detectSanitizeModelFamily('CLAUDE-3-OPUS')).toBe('claude')
      expect(detectSanitizeModelFamily('GPT-4O')).toBe('gpt')
    })
  })
})

// ============================================================================
// stripMarkdownFences
// ============================================================================

describe('stripMarkdownFences', () => {
  it('should strip typescript fence', () => {
    const input = '```typescript\nconst x = 1\n```'
    expect(stripMarkdownFences(input)).toBe('const x = 1')
  })

  it('should strip plain fence without language', () => {
    const input = '```\nconst x = 1\n```'
    expect(stripMarkdownFences(input)).toBe('const x = 1')
  })

  it('should strip python fence', () => {
    const input = '```python\ndef hello():\n    pass\n```'
    expect(stripMarkdownFences(input)).toBe('def hello():\n    pass')
  })

  it('should return content unchanged when no fences present', () => {
    const input = 'const x = 1'
    expect(stripMarkdownFences(input)).toBe('const x = 1')
  })

  it('should handle multiline content', () => {
    const input = '```js\nline1\nline2\nline3\n```'
    expect(stripMarkdownFences(input)).toBe('line1\nline2\nline3')
  })

  it('should strip fence with json language', () => {
    const input = '```json\n{"key": "value"}\n```'
    expect(stripMarkdownFences(input)).toBe('{"key": "value"}')
  })

  it('should handle content with only opening fence (no close)', () => {
    // Only opening fence with recognized language, no closing
    const input = '```typescript\nconst x = 1'
    const result = stripMarkdownFences(input)
    expect(result).toContain('const x = 1')
  })

  it('should handle empty content between fences', () => {
    const input = '```\n\n```'
    expect(stripMarkdownFences(input).trim()).toBe('')
  })
})

// ============================================================================
// normalizeLineEndings
// ============================================================================

describe('normalizeLineEndings', () => {
  it('should convert CRLF to LF', () => {
    expect(normalizeLineEndings('a\r\nb\r\nc')).toBe('a\nb\nc')
  })

  it('should convert standalone CR to LF', () => {
    expect(normalizeLineEndings('a\rb\rc')).toBe('a\nb\nc')
  })

  it('should leave LF unchanged', () => {
    expect(normalizeLineEndings('a\nb\nc')).toBe('a\nb\nc')
  })

  it('should handle mixed line endings', () => {
    expect(normalizeLineEndings('a\r\nb\rc\nd')).toBe('a\nb\nc\nd')
  })

  it('should handle content with no line breaks', () => {
    expect(normalizeLineEndings('hello world')).toBe('hello world')
  })

  it('should handle empty string', () => {
    expect(normalizeLineEndings('')).toBe('')
  })
})

// ============================================================================
// trimTrailingWhitespace
// ============================================================================

describe('trimTrailingWhitespace', () => {
  it('should trim trailing spaces from each line', () => {
    expect(trimTrailingWhitespace('a  \nb  ')).toBe('a\nb')
  })

  it('should trim trailing tabs', () => {
    expect(trimTrailingWhitespace('a\t\t\nb\t')).toBe('a\nb')
  })

  it('should preserve leading whitespace', () => {
    expect(trimTrailingWhitespace('  a  \n  b  ')).toBe('  a\n  b')
  })

  it('should handle lines with no trailing whitespace', () => {
    expect(trimTrailingWhitespace('a\nb\nc')).toBe('a\nb\nc')
  })

  it('should handle empty lines', () => {
    expect(trimTrailingWhitespace('a\n\nb')).toBe('a\n\nb')
  })

  it('should handle single line', () => {
    expect(trimTrailingWhitespace('hello   ')).toBe('hello')
  })
})

// ============================================================================
// ensureTrailingNewline
// ============================================================================

describe('ensureTrailingNewline', () => {
  it('should add newline when missing', () => {
    expect(ensureTrailingNewline('abc')).toBe('abc\n')
  })

  it('should keep exactly one newline when already present', () => {
    expect(ensureTrailingNewline('abc\n')).toBe('abc\n')
  })

  it('should collapse multiple trailing newlines to one', () => {
    expect(ensureTrailingNewline('abc\n\n\n')).toBe('abc\n')
  })

  it('should handle empty string', () => {
    expect(ensureTrailingNewline('')).toBe('\n')
  })

  it('should handle string of only newlines', () => {
    expect(ensureTrailingNewline('\n\n\n')).toBe('\n')
  })

  it('should preserve content before trailing newlines', () => {
    expect(ensureTrailingNewline('line1\nline2\n\n')).toBe('line1\nline2\n')
  })
})

// ============================================================================
// sanitizeContent
// ============================================================================

describe('sanitizeContent', () => {
  it('should run full pipeline on fenced code with CRLF', () => {
    const input = '```typescript\r\nconst x = 1\r\n```'
    const result = sanitizeContent(input)
    expect(result).toBe('const x = 1\n')
  })

  it('should apply Gemini fixes (escaped newlines)', () => {
    const input = 'line1\\nline2'
    const result = sanitizeContent(input, { modelId: 'gemini-pro' })
    expect(result).toBe('line1\nline2\n')
  })

  it('should apply DeepSeek fixes (HTML entities)', () => {
    const input = 'a &amp;&amp; b &lt; c'
    const result = sanitizeContent(input, { modelId: 'deepseek-coder' })
    expect(result).toBe('a && b < c\n')
  })

  it('should apply DeepSeek &gt; entity fix', () => {
    const input = 'x &gt; 0'
    const result = sanitizeContent(input, { modelId: 'deepseek-coder' })
    expect(result).toBe('x > 0\n')
  })

  it('should apply DeepSeek &quot; entity fix', () => {
    const input = '&quot;hello&quot;'
    const result = sanitizeContent(input, { modelId: 'deepseek-coder' })
    expect(result).toBe('"hello"\n')
  })

  it('should disable fence stripping with option', () => {
    const input = '```typescript\nconst x = 1\n```'
    const result = sanitizeContent(input, { stripFences: false })
    expect(result).toContain('```')
  })

  it('should disable line ending normalization with option', () => {
    const input = 'a\r\nb'
    const result = sanitizeContent(input, { normalizeLineEndings: false })
    expect(result).toContain('\r\n')
  })

  it('should enable trailing whitespace trimming with option', () => {
    const input = 'a  \nb  '
    const result = sanitizeContent(input, { trimTrailingWhitespace: true })
    expect(result).toBe('a\nb\n')
  })

  it('should disable trailing newline with option', () => {
    const input = 'abc'
    const result = sanitizeContent(input, { ensureTrailingNewline: false })
    expect(result).toBe('abc')
  })

  it('should handle plain content without fences', () => {
    const input = 'const x = 1'
    const result = sanitizeContent(input)
    expect(result).toBe('const x = 1\n')
  })

  it('should handle unknown model gracefully', () => {
    const input = 'const x = 1'
    const result = sanitizeContent(input, { modelId: 'some-random-model' })
    expect(result).toBe('const x = 1\n')
  })

  it('should handle content with no modelId', () => {
    const input = 'hello world'
    const result = sanitizeContent(input)
    expect(result).toBe('hello world\n')
  })
})

// ============================================================================
// hasMarkdownFences
// ============================================================================

describe('hasMarkdownFences', () => {
  it('should return true when both opening and closing fences present', () => {
    expect(hasMarkdownFences('```typescript\ncode\n```')).toBe(true)
  })

  it('should return true for plain fences', () => {
    expect(hasMarkdownFences('```\ncode\n```')).toBe(true)
  })

  it('should return false when no fences present', () => {
    expect(hasMarkdownFences('just plain text')).toBe(false)
  })

  it('should return false when only opening fence present', () => {
    // Only an opening fence on its own line would match FENCE_OPEN_REGEX
    // but no closing fence on its own line, so FENCE_CLOSE_REGEX fails
    expect(hasMarkdownFences('```typescript\ncode continues')).toBe(false)
  })

  it('should return true for multiple fence blocks', () => {
    const input = '```js\nconst a = 1\n```\n\n```python\nx = 1\n```'
    expect(hasMarkdownFences(input)).toBe(true)
  })

  it('should handle fence with whitespace', () => {
    expect(hasMarkdownFences('  ```\ncode\n  ```')).toBe(true)
  })
})

// ============================================================================
// extractFenceLanguage
// ============================================================================

describe('extractFenceLanguage', () => {
  it('should extract typescript from fence', () => {
    expect(extractFenceLanguage('```typescript\nconst x = 1\n```')).toBe('typescript')
  })

  it('should extract js from fence', () => {
    expect(extractFenceLanguage('```js\nconst x = 1\n```')).toBe('js')
  })

  it('should extract python from fence', () => {
    expect(extractFenceLanguage('```python\ndef fn():\n    pass\n```')).toBe('python')
  })

  it('should return null for fence without language', () => {
    expect(extractFenceLanguage('```\ncode\n```')).toBe(null)
  })

  it('should return null when no fence present', () => {
    expect(extractFenceLanguage('just plain text')).toBe(null)
  })

  it('should extract rust from fence', () => {
    expect(extractFenceLanguage('```rust\nfn main() {}\n```')).toBe('rust')
  })

  it('should extract json from fence', () => {
    expect(extractFenceLanguage('```json\n{}\n```')).toBe('json')
  })

  it('should extract html from fence', () => {
    expect(extractFenceLanguage('```html\n<div></div>\n```')).toBe('html')
  })

  it('should extract css from fence', () => {
    expect(extractFenceLanguage('```css\nbody {}\n```')).toBe('css')
  })
})
