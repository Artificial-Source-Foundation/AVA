import { describe, expect, it } from 'vitest'
import type { Agent } from '../../../types'
import { formatDuration, getAgentDuration, getProgress, mapStatus } from './activity-config'

// ============================================================================
// mapStatus
// ============================================================================

describe('mapStatus', () => {
  it('maps idle → pending', () => {
    expect(mapStatus('idle')).toBe('pending')
  })

  it('maps waiting → pending', () => {
    expect(mapStatus('waiting')).toBe('pending')
  })

  it('maps thinking → running', () => {
    expect(mapStatus('thinking')).toBe('running')
  })

  it('maps executing → running', () => {
    expect(mapStatus('executing')).toBe('running')
  })

  it('maps completed → completed', () => {
    expect(mapStatus('completed')).toBe('completed')
  })

  it('maps error → error', () => {
    expect(mapStatus('error')).toBe('error')
  })

  it('defaults to pending for unknown status', () => {
    expect(mapStatus('unknown' as Agent['status'])).toBe('pending')
  })
})

// ============================================================================
// formatDuration
// ============================================================================

describe('formatDuration', () => {
  it('formats sub-minute durations as seconds', () => {
    expect(formatDuration(5000)).toBe('5s')
    expect(formatDuration(59_000)).toBe('59s')
  })

  it('formats multi-minute durations', () => {
    expect(formatDuration(90_000)).toBe('1m 30s')
    expect(formatDuration(120_000)).toBe('2m 0s')
  })

  it('handles zero', () => {
    expect(formatDuration(0)).toBe('0s')
  })

  it('handles sub-second durations as 0s', () => {
    expect(formatDuration(500)).toBe('0s')
  })
})

// ============================================================================
// getAgentDuration
// ============================================================================

function makeAgent(overrides: Partial<Agent> = {}): Agent {
  return {
    id: 'a-1',
    sessionId: 's-1',
    type: 'operator',
    status: 'idle',
    model: 'test-model',
    createdAt: Date.now() - 10_000,
    ...overrides,
  }
}

describe('getAgentDuration', () => {
  it('uses completedAt - createdAt for completed agents', () => {
    const agent = makeAgent({ createdAt: 1000, completedAt: 6000 })
    expect(getAgentDuration(agent)).toBe(5000)
  })

  it('uses Date.now() - createdAt for running agents', () => {
    const start = Date.now() - 3000
    const agent = makeAgent({ createdAt: start, completedAt: undefined })
    const duration = getAgentDuration(agent)
    // Should be approximately 3000ms (allow some tolerance for test execution)
    expect(duration).toBeGreaterThanOrEqual(2900)
    expect(duration).toBeLessThan(4000)
  })
})

// ============================================================================
// getProgress
// ============================================================================

describe('getProgress', () => {
  it('returns 0 for idle', () => {
    expect(getProgress(makeAgent({ status: 'idle' }))).toBe(0)
  })

  it('returns 10 for waiting', () => {
    expect(getProgress(makeAgent({ status: 'waiting' }))).toBe(10)
  })

  it('returns 40 for thinking', () => {
    expect(getProgress(makeAgent({ status: 'thinking' }))).toBe(40)
  })

  it('returns 70 for executing', () => {
    expect(getProgress(makeAgent({ status: 'executing' }))).toBe(70)
  })

  it('returns 100 for completed', () => {
    expect(getProgress(makeAgent({ status: 'completed' }))).toBe(100)
  })

  it('returns 50 for error with result', () => {
    const agent = makeAgent({
      status: 'error',
      result: { success: false, summary: 'fail', filesModified: [], tokensUsed: 0 },
    })
    expect(getProgress(agent)).toBe(50)
  })

  it('returns 20 for error without result', () => {
    expect(getProgress(makeAgent({ status: 'error' }))).toBe(20)
  })

  it('returns 0 for unknown status', () => {
    expect(getProgress(makeAgent({ status: 'unknown' as Agent['status'] }))).toBe(0)
  })
})
