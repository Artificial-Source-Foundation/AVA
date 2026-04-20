import { describe, expect, it } from 'vitest'
import { segmentedBtnClass as segmentedBtn } from '../../../ui/SegmentedControl'

// ============================================================================
// segmentedBtn
// ============================================================================

describe('segmentedBtn', () => {
  it('returns active classes when true', () => {
    const result = segmentedBtn(true)
    expect(result).toContain('bg-[#0A84FF]')
    expect(result).toContain('text-white')
  })

  it('returns inactive classes when false', () => {
    const result = segmentedBtn(false)
    expect(result).toContain('text-[#48484A]')
    expect(result).toContain('hover:text-[#C8C8CC]')
  })

  it('always includes shared base classes', () => {
    expect(segmentedBtn(true)).toContain('px-4')
    expect(segmentedBtn(false)).toContain('px-4')
    expect(segmentedBtn(true)).toContain('transition-colors')
    expect(segmentedBtn(false)).toContain('transition-colors')
  })

  it('active and inactive produce different strings', () => {
    expect(segmentedBtn(true)).not.toBe(segmentedBtn(false))
  })
})
