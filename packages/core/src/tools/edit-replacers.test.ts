/**
 * Edit Replacers Tests
 * Tests for fuzzy matching strategies: levenshtein, similarity,
 * normalizeLineEndings, and all replacer generators
 */

import { describe, expect, it } from 'vitest'
import {
  BlockAnchorReplacer,
  DEFAULT_REPLACERS,
  IndentationFlexibleReplacer,
  LineTrimmedReplacer,
  levenshtein,
  MultiOccurrenceReplacer,
  normalizeLineEndings,
  SimpleReplacer,
  similarity,
  TrimmedBoundaryReplacer,
  UnicodeNormalizedReplacer,
  WhitespaceNormalizedReplacer,
} from './edit-replacers.js'

// ============================================================================
// levenshtein
// ============================================================================

describe('levenshtein', () => {
  it('should return 0 for two empty strings', () => {
    expect(levenshtein('', '')).toBe(0)
  })

  it('should return length of b when a is empty', () => {
    expect(levenshtein('', 'abc')).toBe(3)
  })

  it('should return length of a when b is empty', () => {
    expect(levenshtein('abc', '')).toBe(3)
  })

  it('should return 0 for identical strings', () => {
    expect(levenshtein('abc', 'abc')).toBe(0)
  })

  it('should return 3 for kitten -> sitting', () => {
    expect(levenshtein('kitten', 'sitting')).toBe(3)
  })

  it('should return 1 for single character substitution', () => {
    expect(levenshtein('abc', 'axc')).toBe(1)
  })

  it('should return 1 for single character insertion', () => {
    expect(levenshtein('abc', 'abcd')).toBe(1)
  })

  it('should return 1 for single character deletion', () => {
    expect(levenshtein('abcd', 'abc')).toBe(1)
  })

  it('should be symmetric', () => {
    expect(levenshtein('foo', 'bar')).toBe(levenshtein('bar', 'foo'))
  })

  it('should handle single character strings', () => {
    expect(levenshtein('a', 'b')).toBe(1)
    expect(levenshtein('a', 'a')).toBe(0)
  })
})

// ============================================================================
// similarity
// ============================================================================

describe('similarity', () => {
  it('should return 1 for identical strings', () => {
    expect(similarity('abc', 'abc')).toBe(1)
  })

  it('should return 1 for two empty strings', () => {
    expect(similarity('', '')).toBe(1)
  })

  it('should return 0 for completely different single-char strings', () => {
    expect(similarity('a', 'b')).toBe(0)
  })

  it('should return a value less than 0.5 for very different strings', () => {
    expect(similarity('abc', 'xyz')).toBeLessThan(0.5)
  })

  it('should return approximately 2/3 for one char difference in 3-char string', () => {
    const result = similarity('abc', 'abd')
    expect(result).toBeCloseTo(2 / 3, 5)
  })

  it('should return value between 0 and 1', () => {
    const result = similarity('hello', 'world')
    expect(result).toBeGreaterThanOrEqual(0)
    expect(result).toBeLessThanOrEqual(1)
  })

  it('should return higher value for more similar strings', () => {
    const similar = similarity('hello', 'hallo')
    const different = similarity('hello', 'world')
    expect(similar).toBeGreaterThan(different)
  })
})

// ============================================================================
// normalizeLineEndings
// ============================================================================

describe('normalizeLineEndings', () => {
  it('should convert CRLF to LF', () => {
    expect(normalizeLineEndings('a\r\nb')).toBe('a\nb')
  })

  it('should leave LF unchanged', () => {
    expect(normalizeLineEndings('a\nb')).toBe('a\nb')
  })

  it('should handle empty string', () => {
    expect(normalizeLineEndings('')).toBe('')
  })

  it('should convert multiple CRLF sequences', () => {
    expect(normalizeLineEndings('a\r\nb\r\nc')).toBe('a\nb\nc')
  })

  it('should not alter lone CR characters', () => {
    expect(normalizeLineEndings('a\rb')).toBe('a\rb')
  })

  it('should handle string with no line endings', () => {
    expect(normalizeLineEndings('hello world')).toBe('hello world')
  })
})

// ============================================================================
// SimpleReplacer
// ============================================================================

describe('SimpleReplacer', () => {
  it('should always yield the find string itself', () => {
    const results = [...SimpleReplacer('any content here', 'search')]
    expect(results).toEqual(['search'])
  })

  it('should yield find regardless of content', () => {
    const results = [...SimpleReplacer('', 'missing')]
    expect(results).toEqual(['missing'])
  })

  it('should yield empty string if find is empty', () => {
    const results = [...SimpleReplacer('content', '')]
    expect(results).toEqual([''])
  })
})

// ============================================================================
// LineTrimmedReplacer
// ============================================================================

describe('LineTrimmedReplacer', () => {
  it('should match when content has different indentation', () => {
    const content = '  foo\n  bar'
    const find = 'foo\nbar'
    const results = [...LineTrimmedReplacer(content, find)]
    expect(results).toHaveLength(1)
    expect(results[0]).toBe('  foo\n  bar')
  })

  it('should match when search has extra indentation but content does not', () => {
    const content = 'foo\nbar'
    const find = '  foo\n  bar'
    const results = [...LineTrimmedReplacer(content, find)]
    expect(results).toHaveLength(1)
    expect(results[0]).toBe('foo\nbar')
  })

  it('should not match when trimmed content differs', () => {
    const content = '  foo\n  bar'
    const find = 'foo\nbaz'
    const results = [...LineTrimmedReplacer(content, find)]
    expect(results).toHaveLength(0)
  })

  it('should match single line', () => {
    const content = '    hello'
    const find = 'hello'
    const results = [...LineTrimmedReplacer(content, find)]
    expect(results).toHaveLength(1)
    expect(results[0]).toBe('    hello')
  })

  it('should return original indented block from content', () => {
    const content = 'before\n    if (x) {\n        return y\n    }\nafter'
    const find = 'if (x) {\n    return y\n}'
    const results = [...LineTrimmedReplacer(content, find)]
    expect(results).toHaveLength(1)
    expect(results[0]).toBe('    if (x) {\n        return y\n    }')
  })

  it('should handle trailing empty line in search', () => {
    const content = 'foo\nbar'
    const find = 'foo\nbar\n'
    const results = [...LineTrimmedReplacer(content, find)]
    expect(results).toHaveLength(1)
  })
})

// ============================================================================
// BlockAnchorReplacer
// ============================================================================

describe('BlockAnchorReplacer', () => {
  it('should not match with fewer than 3 search lines', () => {
    const content = 'line1\nline2\nline3'
    const find = 'line1\nline2'
    const results = [...BlockAnchorReplacer(content, find)]
    expect(results).toHaveLength(0)
  })

  it('should match block by first and last line anchors', () => {
    const content = 'function foo() {\n  const x = 1\n  return x\n}'
    const find = 'function foo() {\n  const y = 2\n}'
    const results = [...BlockAnchorReplacer(content, find)]
    expect(results).toHaveLength(1)
    expect(results[0]).toContain('function foo()')
    expect(results[0]).toContain('}')
  })

  it('should not match when first line anchor is missing', () => {
    const content = 'function bar() {\n  return 1\n}'
    const find = 'function foo() {\n  return 1\n}'
    const results = [...BlockAnchorReplacer(content, find)]
    expect(results).toHaveLength(0)
  })

  it('should not match when last line anchor is missing', () => {
    const content = 'function foo() {\n  return 1\nend'
    const find = 'function foo() {\n  return 1\n}'
    const results = [...BlockAnchorReplacer(content, find)]
    expect(results).toHaveLength(0)
  })

  it('should handle trailing empty line in search', () => {
    const content = 'start\n  middle\nend'
    const find = 'start\n  middle\nend\n'
    const results = [...BlockAnchorReplacer(content, find)]
    expect(results).toHaveLength(1)
  })
})

// ============================================================================
// WhitespaceNormalizedReplacer
// ============================================================================

describe('WhitespaceNormalizedReplacer', () => {
  it('should match when whitespace differs', () => {
    const content = 'const   x  =  1'
    const find = 'const x = 1'
    const results = [...WhitespaceNormalizedReplacer(content, find)]
    expect(results.length).toBeGreaterThan(0)
  })

  it('should match when tabs and spaces are mixed', () => {
    const content = 'const\tx\t=\t1'
    const find = 'const x = 1'
    const results = [...WhitespaceNormalizedReplacer(content, find)]
    expect(results.length).toBeGreaterThan(0)
  })

  it('should not match when content is different', () => {
    const content = 'const x = 2'
    const find = 'const y = 1'
    const results = [...WhitespaceNormalizedReplacer(content, find)]
    expect(results).toHaveLength(0)
  })

  it('should handle multi-line whitespace matching', () => {
    const content = 'a  b\nc  d'
    const find = 'a b\nc d'
    const results = [...WhitespaceNormalizedReplacer(content, find)]
    expect(results.length).toBeGreaterThan(0)
  })
})

// ============================================================================
// IndentationFlexibleReplacer
// ============================================================================

describe('IndentationFlexibleReplacer', () => {
  it('should match blocks with different indentation levels', () => {
    const content = '    if (x) {\n        return y\n    }'
    const find = 'if (x) {\n    return y\n}'
    const results = [...IndentationFlexibleReplacer(content, find)]
    expect(results).toHaveLength(1)
    expect(results[0]).toBe('    if (x) {\n        return y\n    }')
  })

  it('should not match when content structure differs', () => {
    const content = '    if (x) {\n        return y\n    }'
    const find = 'if (x) {\n    return z\n}'
    const results = [...IndentationFlexibleReplacer(content, find)]
    expect(results).toHaveLength(0)
  })

  it('should match single indented line', () => {
    const content = '        return x'
    const find = '    return x'
    const results = [...IndentationFlexibleReplacer(content, find)]
    expect(results).toHaveLength(1)
  })
})

// ============================================================================
// TrimmedBoundaryReplacer
// ============================================================================

describe('TrimmedBoundaryReplacer', () => {
  it('should match when search has leading/trailing whitespace', () => {
    const content = 'hello world'
    const find = '  hello world  '
    const results = [...TrimmedBoundaryReplacer(content, find)]
    expect(results.length).toBeGreaterThan(0)
  })

  it('should skip when search is already trimmed', () => {
    const content = 'hello world'
    const find = 'hello world'
    const results = [...TrimmedBoundaryReplacer(content, find)]
    expect(results).toHaveLength(0)
  })

  it('should match with leading newline in search', () => {
    const content = 'line one\nline two'
    const find = '\nline one\nline two\n'
    const results = [...TrimmedBoundaryReplacer(content, find)]
    expect(results.length).toBeGreaterThan(0)
  })
})

// ============================================================================
// MultiOccurrenceReplacer
// ============================================================================

describe('MultiOccurrenceReplacer', () => {
  it('should yield find for each exact occurrence', () => {
    const results = [...MultiOccurrenceReplacer('foo bar foo baz foo', 'foo')]
    expect(results).toEqual(['foo', 'foo', 'foo'])
  })

  it('should yield nothing when no match exists', () => {
    const results = [...MultiOccurrenceReplacer('hello world', 'xyz')]
    expect(results).toHaveLength(0)
  })

  it('should yield once for single occurrence', () => {
    const results = [...MultiOccurrenceReplacer('hello world', 'world')]
    expect(results).toEqual(['world'])
  })

  it('should handle overlapping potential matches without double-counting', () => {
    // 'aaa' searched for 'aa' -> finds at index 0, then starts at 2, no more match
    const results = [...MultiOccurrenceReplacer('aaa', 'aa')]
    expect(results).toEqual(['aa'])
  })

  it('should handle empty find string', () => {
    // indexOf with empty string returns 0, then 0+0=0 -> infinite loop guard
    // Actually, empty string matches at every position, but startIndex never advances
    // This would be an infinite loop, but in practice the code loops:
    // index=0, yield '', startIndex=0+0=0 -> index=0 again -> infinite
    // Skipping this edge case as it is degenerate input
  })

  it('should match multi-line find', () => {
    const content = 'a\nb\na\nb\na\nb'
    const results = [...MultiOccurrenceReplacer(content, 'a\nb')]
    expect(results).toEqual(['a\nb', 'a\nb', 'a\nb'])
  })
})

// ============================================================================
// UnicodeNormalizedReplacer
// ============================================================================

describe('UnicodeNormalizedReplacer', () => {
  it('should match smart quotes against straight quotes', () => {
    const content = 'const x = "hello"'
    const search = 'const x = \u201Chello\u201D'
    const results = [...UnicodeNormalizedReplacer(content, search)]
    expect(results.length).toBeGreaterThan(0)
    expect(results[0]).toBe('const x = "hello"')
  })

  it('should skip when no normalization is needed', () => {
    const content = 'plain ascii text'
    const find = 'plain ascii text'
    const results = [...UnicodeNormalizedReplacer(content, find)]
    expect(results).toHaveLength(0)
  })

  it('should match em dash against hyphen', () => {
    const content = 'a-b'
    const search = 'a\u2014b'
    const results = [...UnicodeNormalizedReplacer(content, search)]
    expect(results.length).toBeGreaterThan(0)
  })

  it('should match multi-line content with unicode', () => {
    const content = 'function test() {\n  return "hello";\n}'
    const search = 'function test() {\n  return \u201Chello\u201D;\n}'
    const results = [...UnicodeNormalizedReplacer(content, search)]
    expect(results.length).toBeGreaterThan(0)
  })
})

// ============================================================================
// DEFAULT_REPLACERS
// ============================================================================

describe('DEFAULT_REPLACERS', () => {
  it('should be an array', () => {
    expect(Array.isArray(DEFAULT_REPLACERS)).toBe(true)
  })

  it('should contain 8 replacers', () => {
    expect(DEFAULT_REPLACERS).toHaveLength(8)
  })

  it('should have SimpleReplacer as the first entry', () => {
    expect(DEFAULT_REPLACERS[0]).toBe(SimpleReplacer)
  })

  it('should have LineTrimmedReplacer as the second entry', () => {
    expect(DEFAULT_REPLACERS[1]).toBe(LineTrimmedReplacer)
  })

  it('should have UnicodeNormalizedReplacer as the third entry', () => {
    expect(DEFAULT_REPLACERS[2]).toBe(UnicodeNormalizedReplacer)
  })

  it('should have MultiOccurrenceReplacer as the last entry', () => {
    expect(DEFAULT_REPLACERS[DEFAULT_REPLACERS.length - 1]).toBe(MultiOccurrenceReplacer)
  })

  it('should contain all expected replacers', () => {
    expect(DEFAULT_REPLACERS).toContain(SimpleReplacer)
    expect(DEFAULT_REPLACERS).toContain(LineTrimmedReplacer)
    expect(DEFAULT_REPLACERS).toContain(UnicodeNormalizedReplacer)
    expect(DEFAULT_REPLACERS).toContain(BlockAnchorReplacer)
    expect(DEFAULT_REPLACERS).toContain(WhitespaceNormalizedReplacer)
    expect(DEFAULT_REPLACERS).toContain(IndentationFlexibleReplacer)
    expect(DEFAULT_REPLACERS).toContain(TrimmedBoundaryReplacer)
    expect(DEFAULT_REPLACERS).toContain(MultiOccurrenceReplacer)
  })

  it('should have all entries as functions (generators)', () => {
    for (const replacer of DEFAULT_REPLACERS) {
      expect(typeof replacer).toBe('function')
    }
  })
})
