import { afterEach, describe, expect, it } from 'vitest'
import {
  check,
  clearSession,
  configure,
  getConfig,
  getHistory,
  resetDoomLoop,
} from './doom-loop.js'

describe('Doom Loop Detector', () => {
  afterEach(() => resetDoomLoop())

  it('does not detect loop for single call', () => {
    const result = check('s1', 'read_file', { path: '/tmp/a.ts' })
    expect(result.detected).toBe(false)
    expect(result.consecutiveCount).toBe(1)
  })

  it('does not detect loop for different calls', () => {
    check('s1', 'read_file', { path: '/tmp/a.ts' })
    check('s1', 'write_file', { path: '/tmp/b.ts' })
    const result = check('s1', 'read_file', { path: '/tmp/c.ts' })
    expect(result.detected).toBe(false)
  })

  it('detects loop at threshold (3 identical calls)', () => {
    check('s1', 'read_file', { path: '/tmp/a.ts' })
    check('s1', 'read_file', { path: '/tmp/a.ts' })
    const result = check('s1', 'read_file', { path: '/tmp/a.ts' })
    expect(result.detected).toBe(true)
    expect(result.consecutiveCount).toBe(3)
    expect(result.suggestion).toContain('read_file')
  })

  it('does not detect loop for same tool but different args', () => {
    check('s1', 'read_file', { path: '/tmp/a.ts' })
    check('s1', 'read_file', { path: '/tmp/b.ts' })
    const result = check('s1', 'read_file', { path: '/tmp/c.ts' })
    expect(result.detected).toBe(false)
  })

  it('tracks separate sessions', () => {
    check('s1', 'read_file', { path: '/tmp/a.ts' })
    check('s1', 'read_file', { path: '/tmp/a.ts' })
    check('s2', 'read_file', { path: '/tmp/a.ts' })
    const r1 = check('s1', 'read_file', { path: '/tmp/a.ts' })
    const r2 = check('s2', 'read_file', { path: '/tmp/a.ts' })
    expect(r1.detected).toBe(true)
    expect(r2.detected).toBe(false) // only 2 for s2
  })

  it('respects custom threshold', () => {
    configure({ threshold: 2 })
    check('s1', 'bash', { command: 'ls' })
    const result = check('s1', 'bash', { command: 'ls' })
    expect(result.detected).toBe(true)
    expect(result.consecutiveCount).toBe(2)
  })

  it('clears session history', () => {
    check('s1', 'read_file', { path: '/tmp/a.ts' })
    check('s1', 'read_file', { path: '/tmp/a.ts' })
    clearSession('s1')
    const result = check('s1', 'read_file', { path: '/tmp/a.ts' })
    expect(result.detected).toBe(false)
    expect(result.consecutiveCount).toBe(1)
  })

  it('returns history', () => {
    check('s1', 'read_file', { path: '/a' })
    check('s1', 'write_file', { path: '/b' })
    const history = getHistory('s1')
    expect(history).toHaveLength(2)
    expect(history[0]?.tool).toBe('read_file')
    expect(history[1]?.tool).toBe('write_file')
  })

  it('trims history to configured size', () => {
    configure({ historySize: 3 })
    for (let i = 0; i < 10; i++) {
      check('s1', 'echo', { i })
    }
    expect(getHistory('s1')).toHaveLength(3)
  })

  it('returns default config', () => {
    expect(getConfig().threshold).toBe(3)
    expect(getConfig().historySize).toBe(10)
  })
})
