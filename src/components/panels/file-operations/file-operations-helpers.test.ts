import { describe, expect, it } from 'vitest'
import { formatTimestamp, getDirectory, getFileName } from './file-operations-helpers'

describe('getFileName', () => {
  it('returns the last path segment', () => {
    expect(getFileName('/home/user/project/README.md')).toBe('README.md')
  })

  it('returns the whole string when there is no separator', () => {
    expect(getFileName('file.txt')).toBe('file.txt')
  })

  it('handles empty string', () => {
    expect(getFileName('')).toBe('')
  })

  it('handles trailing slash gracefully', () => {
    // trailing slash → pop() returns "" → fallback to full path
    expect(getFileName('foo/bar/')).toBe('foo/bar/')
  })
})

describe('getDirectory', () => {
  it('returns parent directory path', () => {
    expect(getDirectory('/home/user/project/README.md')).toBe('/home/user/project')
  })

  it('returns empty string for a bare filename', () => {
    expect(getDirectory('file.txt')).toBe('')
  })

  it('handles deeply nested paths', () => {
    expect(getDirectory('a/b/c/d/e.ts')).toBe('a/b/c/d')
  })
})

describe('formatTimestamp', () => {
  it('returns "Just now" for timestamps less than 60 s ago', () => {
    expect(formatTimestamp(Date.now() - 30_000)).toBe('Just now')
  })

  it('returns minutes ago for timestamps < 1 hour', () => {
    const fiveMinAgo = Date.now() - 5 * 60_000
    expect(formatTimestamp(fiveMinAgo)).toBe('5m ago')
  })

  it('returns hours ago for timestamps < 1 day', () => {
    const threeHoursAgo = Date.now() - 3 * 3_600_000
    expect(formatTimestamp(threeHoursAgo)).toBe('3h ago')
  })

  it('returns a locale date string for timestamps >= 1 day', () => {
    const twoDaysAgo = Date.now() - 2 * 86_400_000
    // Just check it contains a date-like string (locale-dependent)
    const result = formatTimestamp(twoDaysAgo)
    expect(result).not.toContain('ago')
    expect(result).not.toBe('Just now')
    expect(result.length).toBeGreaterThan(0)
  })

  it('floors minute/hour values (no rounding)', () => {
    const ninetySeconds = Date.now() - 90_000 // 1.5 minutes → "1m ago"
    expect(formatTimestamp(ninetySeconds)).toBe('1m ago')
  })
})
