/**
 * Audit Trail Tests
 */

import { beforeEach, describe, expect, it } from 'vitest'
import { AuditTrail, getAuditTrail, setAuditTrail } from './audit.js'

describe('AuditTrail', () => {
  let trail: AuditTrail

  beforeEach(() => {
    trail = new AuditTrail()
  })

  it('records entries with timestamp', () => {
    const entry = trail.record({
      tool: 'bash',
      params: { command: 'ls' },
      inspector: 'security',
      decision: 'allow',
      confidence: 0.1,
      reason: 'Safe command',
    })

    expect(entry.timestamp).toBeGreaterThan(0)
    expect(entry.tool).toBe('bash')
    expect(trail.count).toBe(1)
  })

  it('query returns all entries without filter', () => {
    trail.record({
      tool: 'bash',
      params: {},
      inspector: 'security',
      decision: 'allow',
      confidence: 0,
      reason: 'ok',
    })
    trail.record({
      tool: 'read',
      params: {},
      inspector: 'security',
      decision: 'block',
      confidence: 0.9,
      reason: 'bad',
    })
    expect(trail.query()).toHaveLength(2)
  })

  it('filters by tool', () => {
    trail.record({
      tool: 'bash',
      params: {},
      inspector: 'security',
      decision: 'allow',
      confidence: 0,
      reason: 'ok',
    })
    trail.record({
      tool: 'read',
      params: {},
      inspector: 'security',
      decision: 'allow',
      confidence: 0,
      reason: 'ok',
    })
    expect(trail.query({ tool: 'bash' })).toHaveLength(1)
  })

  it('filters by inspector', () => {
    trail.record({
      tool: 'bash',
      params: {},
      inspector: 'security',
      decision: 'allow',
      confidence: 0,
      reason: 'ok',
    })
    trail.record({
      tool: 'bash',
      params: {},
      inspector: 'repetition',
      decision: 'allow',
      confidence: 0,
      reason: 'ok',
    })
    expect(trail.query({ inspector: 'repetition' })).toHaveLength(1)
  })

  it('filters by decision', () => {
    trail.record({
      tool: 'bash',
      params: {},
      inspector: 'security',
      decision: 'allow',
      confidence: 0,
      reason: 'ok',
    })
    trail.record({
      tool: 'bash',
      params: {},
      inspector: 'security',
      decision: 'block',
      confidence: 0.9,
      reason: 'bad',
    })
    trail.record({
      tool: 'bash',
      params: {},
      inspector: 'security',
      decision: 'warn',
      confidence: 0.5,
      reason: 'caution',
    })
    expect(trail.query({ decision: 'block' })).toHaveLength(1)
  })

  it('filters by sessionId', () => {
    trail.record({
      tool: 'bash',
      params: {},
      inspector: 'security',
      decision: 'allow',
      confidence: 0,
      reason: 'ok',
      sessionId: 'a',
    })
    trail.record({
      tool: 'bash',
      params: {},
      inspector: 'security',
      decision: 'allow',
      confidence: 0,
      reason: 'ok',
      sessionId: 'b',
    })
    expect(trail.query({ sessionId: 'a' })).toHaveLength(1)
  })

  it('filters by category', () => {
    trail.record({
      tool: 'bash',
      params: {},
      inspector: 'security',
      decision: 'block',
      confidence: 0.9,
      reason: 'bad',
      category: 'command_injection',
    })
    trail.record({
      tool: 'bash',
      params: {},
      inspector: 'security',
      decision: 'block',
      confidence: 0.9,
      reason: 'bad',
      category: 'file_access',
    })
    expect(trail.query({ category: 'command_injection' })).toHaveLength(1)
  })

  it('getBlocked returns only blocked entries', () => {
    trail.record({
      tool: 'a',
      params: {},
      inspector: 's',
      decision: 'allow',
      confidence: 0,
      reason: '',
    })
    trail.record({
      tool: 'b',
      params: {},
      inspector: 's',
      decision: 'block',
      confidence: 0.9,
      reason: '',
    })
    trail.record({
      tool: 'c',
      params: {},
      inspector: 's',
      decision: 'warn',
      confidence: 0.5,
      reason: '',
    })
    expect(trail.getBlocked()).toHaveLength(1)
    expect(trail.getBlocked()[0].tool).toBe('b')
  })

  it('getWarnings returns only warn entries', () => {
    trail.record({
      tool: 'a',
      params: {},
      inspector: 's',
      decision: 'allow',
      confidence: 0,
      reason: '',
    })
    trail.record({
      tool: 'b',
      params: {},
      inspector: 's',
      decision: 'warn',
      confidence: 0.5,
      reason: '',
    })
    expect(trail.getWarnings()).toHaveLength(1)
  })

  it('export returns copy of entries', () => {
    trail.record({
      tool: 'a',
      params: {},
      inspector: 's',
      decision: 'allow',
      confidence: 0,
      reason: '',
    })
    const exported = trail.export()
    expect(exported).toHaveLength(1)
    exported.pop()
    expect(trail.count).toBe(1) // Original unaffected
  })

  it('clear removes all entries', () => {
    trail.record({
      tool: 'a',
      params: {},
      inspector: 's',
      decision: 'allow',
      confidence: 0,
      reason: '',
    })
    trail.record({
      tool: 'b',
      params: {},
      inspector: 's',
      decision: 'allow',
      confidence: 0,
      reason: '',
    })
    trail.clear()
    expect(trail.count).toBe(0)
  })

  it('trims to maxEntries', () => {
    const small = new AuditTrail(3)
    for (let i = 0; i < 5; i++) {
      small.record({
        tool: `t${i}`,
        params: {},
        inspector: 's',
        decision: 'allow',
        confidence: 0,
        reason: '',
      })
    }
    expect(small.count).toBe(3)
    // Should keep the latest entries
    expect(small.export()[0].tool).toBe('t2')
  })
})

// ============================================================================
// Singleton
// ============================================================================

describe('AuditTrail singleton', () => {
  beforeEach(() => setAuditTrail(null))

  it('getAuditTrail returns singleton', () => {
    const a = getAuditTrail()
    const b = getAuditTrail()
    expect(a).toBe(b)
  })

  it('setAuditTrail replaces singleton', () => {
    const custom = new AuditTrail(10)
    setAuditTrail(custom)
    expect(getAuditTrail()).toBe(custom)
  })

  it('setAuditTrail(null) clears singleton', () => {
    const a = getAuditTrail()
    setAuditTrail(null)
    const b = getAuditTrail()
    expect(a).not.toBe(b)
  })
})
