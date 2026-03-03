import { afterEach, describe, expect, it } from 'vitest'
import { getTokenStats, resetTokenStats, trackTokens } from './tracker.js'

describe('Token Tracker', () => {
  afterEach(() => {
    resetTokenStats()
  })

  it('tracks tokens for a session', () => {
    trackTokens('session1', 100, 50)
    const stats = getTokenStats('session1')
    expect(stats).toEqual({
      inputTokens: 100,
      outputTokens: 50,
      totalTokens: 150,
      turnCount: 1,
      cacheReadTokens: 0,
      cacheCreationTokens: 0,
    })
  })

  it('accumulates across turns', () => {
    trackTokens('session1', 100, 50)
    trackTokens('session1', 200, 100)
    const stats = getTokenStats('session1')
    expect(stats).toEqual({
      inputTokens: 300,
      outputTokens: 150,
      totalTokens: 450,
      turnCount: 2,
      cacheReadTokens: 0,
      cacheCreationTokens: 0,
    })
  })

  it('tracks separate sessions independently', () => {
    trackTokens('session1', 100, 50)
    trackTokens('session2', 200, 100)
    expect(getTokenStats('session1')?.totalTokens).toBe(150)
    expect(getTokenStats('session2')?.totalTokens).toBe(300)
  })

  it('returns null for unknown session', () => {
    expect(getTokenStats('unknown')).toBeNull()
  })

  it('resets specific session', () => {
    trackTokens('session1', 100, 50)
    trackTokens('session2', 200, 100)
    resetTokenStats('session1')
    expect(getTokenStats('session1')).toBeNull()
    expect(getTokenStats('session2')).not.toBeNull()
  })

  it('resets all sessions', () => {
    trackTokens('session1', 100, 50)
    trackTokens('session2', 200, 100)
    resetTokenStats()
    expect(getTokenStats('session1')).toBeNull()
    expect(getTokenStats('session2')).toBeNull()
  })
})
