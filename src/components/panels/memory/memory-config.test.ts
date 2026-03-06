import { describe, expect, it } from 'vitest'
import { formatTokens } from './memory-config'

// ============================================================================
// formatTokens
// ============================================================================

describe('formatTokens', () => {
  it('returns raw number for small values', () => {
    expect(formatTokens(0)).toBe('0')
    expect(formatTokens(42)).toBe('42')
    expect(formatTokens(999)).toBe('999')
  })

  it('formats thousands with one decimal', () => {
    expect(formatTokens(1000)).toBe('1.0K')
    expect(formatTokens(4500)).toBe('4.5K')
    expect(formatTokens(128_000)).toBe('128.0K')
  })

  it('formats millions with one decimal', () => {
    expect(formatTokens(1_000_000)).toBe('1.0M')
    expect(formatTokens(2_500_000)).toBe('2.5M')
  })

  it('handles boundary at 1000', () => {
    expect(formatTokens(999)).toBe('999')
    expect(formatTokens(1000)).toBe('1.0K')
  })

  it('handles boundary at 1M', () => {
    expect(formatTokens(999_999)).toBe('1000.0K')
    expect(formatTokens(1_000_000)).toBe('1.0M')
  })
})
