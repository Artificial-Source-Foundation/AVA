/**
 * Unicode Normalization Tests
 * Tests for Sprint 3: Edit Reliability
 */

import { describe, expect, it } from 'vitest'
import { replace } from '../edit.js'
import { UnicodeNormalizedReplacer } from '../edit-replacers.js'
import { getNormalizationDetails, hasNormalizableChars, normalizeUnicode } from './normalize.js'

// ============================================================================
// Basic Normalization Tests
// ============================================================================

describe('normalizeUnicode', () => {
  describe('Quote Normalization', () => {
    it('should normalize left double quote', () => {
      expect(normalizeUnicode('\u201C')).toBe('"')
    })

    it('should normalize right double quote', () => {
      expect(normalizeUnicode('\u201D')).toBe('"')
    })

    it('should normalize smart double quotes', () => {
      expect(normalizeUnicode('"Hello"')).toBe('"Hello"')
    })

    it('should normalize left single quote', () => {
      expect(normalizeUnicode('\u2018')).toBe("'")
    })

    it('should normalize right single quote', () => {
      expect(normalizeUnicode('\u2019')).toBe("'")
    })

    it('should normalize smart single quotes', () => {
      expect(normalizeUnicode("'test'")).toBe("'test'")
    })

    it('should normalize angle quotes', () => {
      expect(normalizeUnicode('«Hello»')).toBe('"Hello"')
    })
  })

  describe('Dash Normalization', () => {
    it('should normalize en dash', () => {
      expect(normalizeUnicode('\u2013')).toBe('-')
    })

    it('should normalize em dash', () => {
      expect(normalizeUnicode('\u2014')).toBe('-')
    })

    it('should normalize minus sign', () => {
      expect(normalizeUnicode('\u2212')).toBe('-')
    })

    it('should normalize fullwidth hyphen', () => {
      expect(normalizeUnicode('\uFF0D')).toBe('-')
    })

    it('should normalize range with en dash', () => {
      expect(normalizeUnicode('1–10')).toBe('1-10')
    })
  })

  describe('Space Normalization', () => {
    it('should normalize non-breaking space', () => {
      expect(normalizeUnicode('\u00A0')).toBe(' ')
    })

    it('should normalize thin space', () => {
      expect(normalizeUnicode('\u2009')).toBe(' ')
    })

    it('should normalize em space', () => {
      expect(normalizeUnicode('\u2003')).toBe(' ')
    })

    it('should remove BOM', () => {
      expect(normalizeUnicode('\uFEFF')).toBe('')
    })

    it('should normalize ideographic space', () => {
      expect(normalizeUnicode('\u3000')).toBe(' ')
    })
  })

  describe('Other Normalizations', () => {
    it('should normalize ellipsis', () => {
      expect(normalizeUnicode('…')).toBe('...')
    })

    it('should normalize right arrow', () => {
      expect(normalizeUnicode('→')).toBe('->')
    })

    it('should normalize double right arrow', () => {
      expect(normalizeUnicode('⇒')).toBe('=>')
    })

    it('should normalize multiplication sign', () => {
      expect(normalizeUnicode('×')).toBe('*')
    })

    it('should normalize bullet', () => {
      expect(normalizeUnicode('•')).toBe('*')
    })
  })

  describe('Mixed Content', () => {
    it('should normalize code snippet with smart quotes', () => {
      const input = 'const message = "Hello, world!"'
      const expected = 'const message = "Hello, world!"'
      expect(normalizeUnicode(input)).toBe(expected)
    })

    it('should normalize arrow function', () => {
      const input = 'const fn = (x) ⇒ x × 2'
      const expected = 'const fn = (x) => x * 2'
      expect(normalizeUnicode(input)).toBe(expected)
    })

    it('should handle mixed quotes and dashes', () => {
      const input = '\u201Clong\u2014dash\u201D and \u2018short\u2013dash\u2019'
      const expected = '"long-dash" and \'short-dash\''
      expect(normalizeUnicode(input)).toBe(expected)
    })

    it('should preserve ASCII characters', () => {
      const input = 'function foo() { return "bar"; }'
      expect(normalizeUnicode(input)).toBe(input)
    })
  })
})

// ============================================================================
// Detection Tests
// ============================================================================

describe('hasNormalizableChars', () => {
  it('should return true for smart quotes', () => {
    expect(hasNormalizableChars('\u201Ctest\u201D')).toBe(true)
  })

  it('should return true for en dash', () => {
    expect(hasNormalizableChars('a\u2013b')).toBe(true)
  })

  it('should return true for ellipsis', () => {
    expect(hasNormalizableChars('test\u2026')).toBe(true) // ELLIPSIS
  })

  it('should return false for ASCII only', () => {
    expect(hasNormalizableChars('const x = "hello"')).toBe(false)
  })

  it('should return false for empty string', () => {
    expect(hasNormalizableChars('')).toBe(false)
  })
})

describe('getNormalizationDetails', () => {
  it('should return empty array for ASCII', () => {
    expect(getNormalizationDetails('hello world')).toHaveLength(0)
  })

  it('should return details for smart quotes', () => {
    const details = getNormalizationDetails('\u201Ctest\u201D')
    expect(details).toHaveLength(2)
    expect(details[0].char).toBe('\u201C')
    expect(details[0].replacement).toBe('"')
    expect(details[0].position).toBe(0)
    expect(details[1].char).toBe('\u201D')
    expect(details[1].replacement).toBe('"')
    expect(details[1].position).toBe(5)
  })

  it('should return details with names', () => {
    const details = getNormalizationDetails('a\u2013b')
    expect(details).toHaveLength(1)
    expect(details[0].name).toBe('en dash')
  })
})

// ============================================================================
// Integration Tests with Replace
// ============================================================================

describe('UnicodeNormalizedReplacer Integration', () => {
  it('should match smart quotes against straight quotes', () => {
    const content = 'const x = "hello"' // straight quotes in file
    const search = 'const x = \u201Chello\u201D' // smart quotes from LLM

    const matches = [...UnicodeNormalizedReplacer(content, search)]
    expect(matches.length).toBeGreaterThan(0)
    expect(matches[0]).toBe('const x = "hello"')
  })

  it('should match em dash against hyphen', () => {
    const content = 'a-b-c' // hyphens in file
    const search = 'a\u2014b\u2014c' // em dashes from LLM

    const matches = [...UnicodeNormalizedReplacer(content, search)]
    expect(matches.length).toBeGreaterThan(0)
  })

  it('should handle multi-line with smart quotes', () => {
    const content = `function test() {
  return "hello";
}`
    const search = `function test() {
  return \u201Chello\u201D;
}` // Smart quotes

    const matches = [...UnicodeNormalizedReplacer(content, search)]
    expect(matches.length).toBeGreaterThan(0)
  })
})

describe('replace() with Unicode normalization', () => {
  it('should replace content with smart quotes in search', () => {
    const content = 'const greeting = "Hello, world!"' // File has straight quotes
    const oldString = 'const greeting = \u201CHello, world!\u201D' // LLM uses smart quotes
    const newString = 'const greeting = "Hi there!"'

    // This should work due to UnicodeNormalizedReplacer
    const result = replace(content, oldString, newString)
    expect(result).toBe('const greeting = "Hi there!"')
  })

  it('should replace content with em dashes in search', () => {
    const content = 'x - y - z' // File has hyphens
    const oldString = 'x \u2014 y \u2014 z' // LLM uses em dashes
    const newString = 'a + b + c'

    const result = replace(content, oldString, newString)
    expect(result).toBe('a + b + c')
  })

  it('should handle ellipsis normalization', () => {
    const content = 'Loading...'
    const oldString = 'Loading\u2026' // Unicode ellipsis
    const newString = 'Done!'

    const result = replace(content, oldString, newString)
    expect(result).toBe('Done!')
  })

  it('should still prefer exact matches', () => {
    const content = 'const x = "test"' // Straight quotes
    const oldString = 'const x = "test"' // Straight quotes (exact match)
    const newString = 'const y = "test"'

    const result = replace(content, oldString, newString)
    expect(result).toBe('const y = "test"')
  })
})
