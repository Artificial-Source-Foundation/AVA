import { describe, expect, it } from 'vitest'
import { detectMode, getModeConfig, resolveModeFromSlash } from './mode-selector.js'

describe('mode selector', () => {
  it('detects full mode for sprint implementation', () => {
    expect(detectMode('Implement Sprint 15')).toBe('full')
  })

  it('detects light mode for bug fix', () => {
    expect(detectMode('Fix the bug in auth.ts')).toBe('light')
  })

  it('detects solo mode for explanations', () => {
    expect(detectMode('Explain how the router works')).toBe('solo')
  })

  it('allows slash override', () => {
    expect(resolveModeFromSlash('full', 'Explain how the router works')).toBe('full')
  })

  it('returns mode configuration', () => {
    expect(getModeConfig('full').chain).toEqual(['director', 'tech-lead', 'engineer', 'reviewer'])
  })
})
