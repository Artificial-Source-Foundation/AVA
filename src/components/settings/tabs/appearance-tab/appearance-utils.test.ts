import { describe, expect, it } from 'vitest'
import { segmentedBtn } from './appearance-utils'

// ============================================================================
// segmentedBtn
// ============================================================================

describe('segmentedBtn', () => {
  it('returns active classes when true', () => {
    const result = segmentedBtn(true)
    expect(result).toContain('bg-[var(--accent)]')
    expect(result).toContain('text-white')
  })

  it('returns inactive classes when false', () => {
    const result = segmentedBtn(false)
    expect(result).toContain('bg-[var(--surface-raised)]')
    expect(result).toContain('text-[var(--text-secondary)]')
    expect(result).toContain('hover:bg-')
  })

  it('always includes shared base classes', () => {
    expect(segmentedBtn(true)).toContain('px-2.5')
    expect(segmentedBtn(false)).toContain('px-2.5')
    expect(segmentedBtn(true)).toContain('transition-colors')
    expect(segmentedBtn(false)).toContain('transition-colors')
  })

  it('active and inactive produce different strings', () => {
    expect(segmentedBtn(true)).not.toBe(segmentedBtn(false))
  })
})
