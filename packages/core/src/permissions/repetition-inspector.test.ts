/**
 * Repetition Inspector Tests
 */

import { describe, expect, it, vi } from 'vitest'
import { RepetitionInspector } from './repetition-inspector.js'

describe('RepetitionInspector', () => {
  it('allows first call', () => {
    const inspector = new RepetitionInspector()
    const result = inspector.check('bash', { command: 'ls' })
    expect(result.detected).toBe(false)
    expect(result.count).toBe(1)
  })

  it('allows calls below threshold', () => {
    const inspector = new RepetitionInspector({ threshold: 3 })
    inspector.check('bash', { command: 'ls' })
    const result = inspector.check('bash', { command: 'ls' })
    expect(result.detected).toBe(false)
    expect(result.count).toBe(2)
  })

  it('detects repetition at threshold', () => {
    const inspector = new RepetitionInspector({ threshold: 3 })
    inspector.check('bash', { command: 'ls' })
    inspector.check('bash', { command: 'ls' })
    const result = inspector.check('bash', { command: 'ls' })
    expect(result.detected).toBe(true)
    expect(result.count).toBe(3)
    expect(result.reason).toContain('bash')
    expect(result.reason).toContain('3 times')
  })

  it('distinguishes different tools', () => {
    const inspector = new RepetitionInspector({ threshold: 2 })
    inspector.check('bash', { command: 'ls' })
    const result = inspector.check('read_file', { path: '/etc/hosts' })
    expect(result.detected).toBe(false)
    expect(result.count).toBe(1)
  })

  it('distinguishes different params for same tool', () => {
    const inspector = new RepetitionInspector({ threshold: 2 })
    inspector.check('bash', { command: 'ls' })
    const result = inspector.check('bash', { command: 'pwd' })
    expect(result.detected).toBe(false)
    expect(result.count).toBe(1)
  })

  it('considers params order-insensitive', () => {
    const inspector = new RepetitionInspector({ threshold: 2 })
    inspector.check('edit', { path: 'a.ts', content: 'x' })
    const result = inspector.check('edit', { content: 'x', path: 'a.ts' })
    expect(result.detected).toBe(true)
    expect(result.count).toBe(2)
  })

  it('respects time window', () => {
    const inspector = new RepetitionInspector({ threshold: 2, windowMs: 100 })

    // First call at t=0
    vi.spyOn(Date, 'now').mockReturnValue(1000)
    inspector.check('bash', { command: 'ls' })

    // Second call at t=200ms (outside window)
    vi.spyOn(Date, 'now').mockReturnValue(1200)
    const result = inspector.check('bash', { command: 'ls' })

    expect(result.detected).toBe(false)
    expect(result.count).toBe(1) // First call was trimmed

    vi.restoreAllMocks()
  })

  it('clears history', () => {
    const inspector = new RepetitionInspector({ threshold: 2 })
    inspector.check('bash', { command: 'ls' })
    inspector.clear()
    const result = inspector.check('bash', { command: 'ls' })
    expect(result.detected).toBe(false)
    expect(result.count).toBe(1)
  })

  it('reports history length', () => {
    const inspector = new RepetitionInspector()
    expect(inspector.historyLength).toBe(0)
    inspector.check('bash', { command: 'ls' })
    inspector.check('bash', { command: 'pwd' })
    expect(inspector.historyLength).toBe(2)
  })

  it('trims history at maxHistory', () => {
    const inspector = new RepetitionInspector({ maxHistory: 3 })
    inspector.check('a', { x: 1 })
    inspector.check('b', { x: 2 })
    inspector.check('c', { x: 3 })
    inspector.check('d', { x: 4 })
    expect(inspector.historyLength).toBeLessThanOrEqual(3)
  })

  it('configure updates settings', () => {
    const inspector = new RepetitionInspector()
    expect(inspector.getConfig().threshold).toBe(3)
    inspector.configure({ threshold: 5 })
    expect(inspector.getConfig().threshold).toBe(5)
  })

  it('getConfig returns copy', () => {
    const inspector = new RepetitionInspector()
    const config = inspector.getConfig()
    config.threshold = 999
    expect(inspector.getConfig().threshold).toBe(3)
  })

  it('handles non-serializable params gracefully', () => {
    const inspector = new RepetitionInspector({ threshold: 2 })
    const circular: Record<string, unknown> = { a: 1 }
    circular.self = circular

    // Should not throw
    expect(() => inspector.check('bash', circular)).not.toThrow()
  })

  it('custom threshold of 1 detects immediately', () => {
    const inspector = new RepetitionInspector({ threshold: 1 })
    const result = inspector.check('bash', { command: 'ls' })
    expect(result.detected).toBe(true)
    expect(result.count).toBe(1)
  })
})
