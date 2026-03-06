import { describe, expect, it } from 'vitest'
import type { SessionWithStats } from '../../../types'
import { formatDate, formatSessionName, groupSessionsByDate } from './session-utils'

// ============================================================================
// formatSessionName
// ============================================================================

describe('formatSessionName', () => {
  it('returns short names unchanged', () => {
    expect(formatSessionName('Fix bug')).toBe('Fix bug')
  })

  it('truncates names longer than 28 chars', () => {
    const long = 'A'.repeat(30)
    expect(formatSessionName(long)).toBe('A'.repeat(28) + '...')
  })

  it('returns 28-char names unchanged', () => {
    const exactly28 = 'B'.repeat(28)
    expect(formatSessionName(exactly28)).toBe(exactly28)
  })

  it('handles empty string', () => {
    expect(formatSessionName('')).toBe('')
  })
})

// ============================================================================
// formatDate
// ============================================================================

describe('formatDate', () => {
  it('returns "Today" for current timestamp', () => {
    expect(formatDate(Date.now())).toBe('Today')
  })

  it('returns "Yesterday" for 1 day ago', () => {
    const yesterday = Date.now() - 24 * 60 * 60 * 1000
    expect(formatDate(yesterday)).toBe('Yesterday')
  })

  it('returns "Xd ago" for 2-6 days ago', () => {
    const threeDays = Date.now() - 3 * 24 * 60 * 60 * 1000
    expect(formatDate(threeDays)).toBe('3d ago')
  })

  it('returns formatted date for 7+ days ago', () => {
    const twoWeeks = Date.now() - 14 * 24 * 60 * 60 * 1000
    const result = formatDate(twoWeeks)
    // Should be like "Feb 20" — contains a month abbreviation
    expect(result).toMatch(/[A-Z][a-z]{2}\s+\d{1,2}/)
  })
})

// ============================================================================
// groupSessionsByDate
// ============================================================================

function makeSession(overrides: Partial<SessionWithStats> = {}): SessionWithStats {
  return {
    id: Math.random().toString(36).slice(2),
    name: 'Test Session',
    createdAt: Date.now(),
    updatedAt: Date.now(),
    status: 'active',
    messageCount: 0,
    totalTokens: 0,
    ...overrides,
  }
}

describe('groupSessionsByDate', () => {
  it('returns empty array for no sessions', () => {
    expect(groupSessionsByDate([])).toEqual([])
  })

  it('groups sessions into Today', () => {
    const sessions = [makeSession({ updatedAt: Date.now() })]
    const groups = groupSessionsByDate(sessions)
    expect(groups).toHaveLength(1)
    expect(groups[0]!.label).toBe('Today')
    expect(groups[0]!.sessions).toHaveLength(1)
  })

  it('groups into multiple date buckets in order', () => {
    const now = Date.now()
    const sessions = [
      makeSession({ updatedAt: now }),
      makeSession({ updatedAt: now - 24 * 60 * 60 * 1000 }),
      makeSession({ updatedAt: now - 3 * 24 * 60 * 60 * 1000 }),
      makeSession({ updatedAt: now - 14 * 24 * 60 * 60 * 1000 }),
    ]
    const groups = groupSessionsByDate(sessions)
    const labels = groups.map((g) => g.label)
    expect(labels).toEqual(['Today', 'Yesterday', 'This Week', 'Older'])
  })

  it('omits empty groups', () => {
    const now = Date.now()
    const sessions = [
      makeSession({ updatedAt: now }),
      makeSession({ updatedAt: now - 14 * 24 * 60 * 60 * 1000 }),
    ]
    const groups = groupSessionsByDate(sessions)
    const labels = groups.map((g) => g.label)
    expect(labels).toEqual(['Today', 'Older'])
    expect(labels).not.toContain('Yesterday')
    expect(labels).not.toContain('This Week')
  })

  it('preserves group ordering even with reversed input', () => {
    const now = Date.now()
    const sessions = [
      makeSession({ updatedAt: now - 14 * 24 * 60 * 60 * 1000 }),
      makeSession({ updatedAt: now }),
    ]
    const groups = groupSessionsByDate(sessions)
    expect(groups[0]!.label).toBe('Today')
    expect(groups[1]!.label).toBe('Older')
  })
})
