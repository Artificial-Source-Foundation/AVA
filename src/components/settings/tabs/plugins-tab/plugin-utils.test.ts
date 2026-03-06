import { describe, expect, it } from 'vitest'
import {
  categoryLabel,
  formatDownloads,
  formatSyncTime,
  permissionColor,
  sourceLabel,
} from './plugin-utils'

// ============================================================================
// permissionColor
// ============================================================================

describe('permissionColor', () => {
  it('returns error color for high-risk permissions', () => {
    expect(permissionColor('shell')).toBe('var(--error)')
  })

  it('returns warning color for medium-risk permissions', () => {
    expect(permissionColor('fs')).toBe('var(--warning)')
    expect(permissionColor('network')).toBe('var(--warning)')
  })

  it('returns muted color for low-risk permissions', () => {
    expect(permissionColor('clipboard')).toBe('var(--text-muted)')
  })
})

// ============================================================================
// formatDownloads
// ============================================================================

describe('formatDownloads', () => {
  it('returns "0" for undefined', () => {
    expect(formatDownloads(undefined)).toBe('0')
  })

  it('returns "0" for zero', () => {
    expect(formatDownloads(0)).toBe('0')
  })

  it('returns raw number below 1000', () => {
    expect(formatDownloads(42)).toBe('42')
    expect(formatDownloads(999)).toBe('999')
  })

  it('formats thousands with one decimal', () => {
    expect(formatDownloads(1000)).toBe('1.0K')
    expect(formatDownloads(1200)).toBe('1.2K')
    expect(formatDownloads(15_900)).toBe('15.9K')
  })

  it('handles boundary at 1000 exactly', () => {
    expect(formatDownloads(999)).toBe('999')
    expect(formatDownloads(1000)).toBe('1.0K')
  })
})

// ============================================================================
// categoryLabel
// ============================================================================

describe('categoryLabel', () => {
  it('capitalizes first letter', () => {
    expect(categoryLabel('workflow')).toBe('Workflow')
    expect(categoryLabel('quality')).toBe('Quality')
    expect(categoryLabel('integration')).toBe('Integration')
  })

  it('handles single-character strings', () => {
    expect(categoryLabel('a')).toBe('A')
  })

  it('handles empty string without throwing', () => {
    expect(categoryLabel('')).toBe('')
  })
})

// ============================================================================
// formatSyncTime
// ============================================================================

describe('formatSyncTime', () => {
  it('returns "never" for null', () => {
    expect(formatSyncTime(null)).toBe('never')
  })

  it('returns "never" for zero', () => {
    expect(formatSyncTime(0)).toBe('never')
  })

  it('formats a valid timestamp to HH:MM', () => {
    // Use a known timestamp and check format pattern (locale-dependent)
    const ts = new Date('2026-03-06T14:30:00Z').getTime()
    const result = formatSyncTime(ts)
    // Should contain digits and colon (HH:MM pattern regardless of locale)
    expect(result).toMatch(/\d{1,2}:\d{2}/)
  })
})

// ============================================================================
// sourceLabel
// ============================================================================

describe('sourceLabel', () => {
  it('returns "Git" for git source', () => {
    expect(sourceLabel('git')).toBe('Git')
  })

  it('returns "Local" for local-link source', () => {
    expect(sourceLabel('local-link')).toBe('Local')
  })

  it('returns "Catalog" for undefined', () => {
    expect(sourceLabel(undefined)).toBe('Catalog')
  })

  it('returns "Catalog" for unrecognized source', () => {
    expect(sourceLabel('npm')).toBe('Catalog')
    expect(sourceLabel('')).toBe('Catalog')
  })
})
