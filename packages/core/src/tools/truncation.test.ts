/**
 * Output Truncation Tests
 * Tests for synchronous truncation utilities
 */

import { describe, expect, it } from 'vitest'
import { TRUNCATION_LIMITS, truncateForMetadata, truncateLine } from './truncation.js'

// ============================================================================
// truncateLine
// ============================================================================

describe('truncateLine', () => {
  it('should return short line unchanged', () => {
    expect(truncateLine('hello world')).toBe('hello world')
  })

  it('should return line at exact max length unchanged', () => {
    const line = 'a'.repeat(2000)
    expect(truncateLine(line)).toBe(line)
  })

  it('should truncate line over default max and add ellipsis', () => {
    const line = 'x'.repeat(2500)
    const result = truncateLine(line)
    expect(result.length).toBe(2000)
    expect(result.endsWith('...')).toBe(true)
    expect(result.slice(0, 1997)).toBe('x'.repeat(1997))
  })

  it('should use custom maxLength', () => {
    const line = 'hello world, this is a long line'
    const result = truncateLine(line, 10)
    expect(result.length).toBe(10)
    expect(result).toBe('hello w...')
  })

  it('should return empty string unchanged', () => {
    expect(truncateLine('')).toBe('')
  })

  it('should handle line exactly one over max', () => {
    const line = 'a'.repeat(2001)
    const result = truncateLine(line)
    expect(result.length).toBe(2000)
    expect(result.endsWith('...')).toBe(true)
  })

  it('should handle very small maxLength', () => {
    const result = truncateLine('abcdefghij', 5)
    expect(result).toBe('ab...')
    expect(result.length).toBe(5)
  })

  it('should handle single character line', () => {
    expect(truncateLine('x')).toBe('x')
  })

  it('should handle custom maxLength where line is under limit', () => {
    expect(truncateLine('short', 100)).toBe('short')
  })
})

// ============================================================================
// truncateForMetadata
// ============================================================================

describe('truncateForMetadata', () => {
  it('should return short text unchanged', () => {
    expect(truncateForMetadata('hello')).toBe('hello')
  })

  it('should return text unchanged when exactly at limit', () => {
    // 30KB of ASCII = 30720 bytes
    const text = 'a'.repeat(30 * 1024)
    expect(truncateForMetadata(text)).toBe(text)
  })

  it('should truncate long text keeping the end (tail)', () => {
    // Create text over 30KB
    const prefix = 'START_'
    const suffix = '_END'
    const padding = 'x'.repeat(40 * 1024)
    const text = `${prefix}${padding}${suffix}`
    const result = truncateForMetadata(text)
    // Should keep the end, so suffix should be present
    expect(result.endsWith(suffix)).toBe(true)
    // Prefix should have been truncated away
    expect(result.includes(prefix)).toBe(false)
  })

  it('should use custom maxBytes', () => {
    const text = 'abcdefghij' // 10 bytes
    const result = truncateForMetadata(text, 5)
    // Keeps from end: "fghij"
    expect(result).toBe('fghij')
  })

  it('should handle empty string', () => {
    expect(truncateForMetadata('')).toBe('')
  })

  it('should return unchanged when under custom limit', () => {
    expect(truncateForMetadata('hello', 100)).toBe('hello')
  })

  it('should handle multibyte characters', () => {
    // Each emoji is 4 bytes in UTF-8
    const text = 'a'.repeat(100)
    const result = truncateForMetadata(text, 50)
    expect(result.length).toBeLessThanOrEqual(50)
  })

  it('should use default MAX_METADATA_BYTES (30KB)', () => {
    // Text under 30KB should pass through
    const text = 'a'.repeat(1000)
    expect(truncateForMetadata(text)).toBe(text)
  })
})

// ============================================================================
// TRUNCATION_LIMITS
// ============================================================================

describe('TRUNCATION_LIMITS', () => {
  it('should have MAX_LINES of 2000', () => {
    expect(TRUNCATION_LIMITS.MAX_LINES).toBe(2000)
  })

  it('should have MAX_BYTES of 50KB', () => {
    expect(TRUNCATION_LIMITS.MAX_BYTES).toBe(50 * 1024)
  })

  it('should have MAX_METADATA_BYTES of 30KB', () => {
    expect(TRUNCATION_LIMITS.MAX_METADATA_BYTES).toBe(30 * 1024)
  })

  it('should have MAX_LINE_LENGTH of 2000', () => {
    expect(TRUNCATION_LIMITS.MAX_LINE_LENGTH).toBe(2000)
  })

  it('should be a readonly object', () => {
    // Verify all expected keys exist
    expect(Object.keys(TRUNCATION_LIMITS)).toEqual(
      expect.arrayContaining(['MAX_LINES', 'MAX_BYTES', 'MAX_METADATA_BYTES', 'MAX_LINE_LENGTH'])
    )
  })
})
