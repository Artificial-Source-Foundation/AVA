import { afterEach, describe, expect, it } from 'vitest'
import {
  check,
  clearSession,
  configure,
  detectGlobalDoomLoop,
  getConfig,
  getGlobalToolCallLog,
  getHistory,
  resetDoomLoop,
  resetGlobalDoomLoop,
  trackGlobalToolCall,
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

describe('Global Doom Loop Detector', () => {
  afterEach(() => resetGlobalDoomLoop())

  it('tracks global tool calls', () => {
    trackGlobalToolCall('agent-1', 'read_file', { path: '/a.ts' })
    trackGlobalToolCall('agent-2', 'write_file', { path: '/b.ts' })
    const log = getGlobalToolCallLog()
    expect(log).toHaveLength(2)
    expect(log[0]!.agentId).toBe('agent-1')
    expect(log[1]!.agentId).toBe('agent-2')
  })

  it('does not detect loop for few calls', () => {
    trackGlobalToolCall('agent-1', 'read_file', { path: '/a.ts' })
    trackGlobalToolCall('agent-2', 'read_file', { path: '/a.ts' })
    const result = detectGlobalDoomLoop()
    expect(result.detected).toBe(false)
  })

  it('detects loop when same call happens 5 times across agents', () => {
    for (let i = 0; i < 5; i++) {
      trackGlobalToolCall(`agent-${i}`, 'read_file', { path: '/a.ts' })
    }
    const result = detectGlobalDoomLoop()
    expect(result.detected).toBe(true)
    expect(result.count).toBe(5)
    expect(result.pattern).toContain('read_file')
  })

  it('detects loop when same agent repeats globally', () => {
    for (let i = 0; i < 5; i++) {
      trackGlobalToolCall('agent-1', 'bash', { command: 'ls' })
    }
    const result = detectGlobalDoomLoop()
    expect(result.detected).toBe(true)
    expect(result.pattern).toContain('bash')
  })

  it('does not detect loop for different tool calls', () => {
    trackGlobalToolCall('a1', 'read_file', { path: '/a.ts' })
    trackGlobalToolCall('a2', 'write_file', { path: '/b.ts' })
    trackGlobalToolCall('a3', 'bash', { command: 'ls' })
    trackGlobalToolCall('a4', 'glob', { pattern: '*.ts' })
    trackGlobalToolCall('a5', 'grep', { pattern: 'foo' })
    const result = detectGlobalDoomLoop()
    expect(result.detected).toBe(false)
  })

  it('respects time window', () => {
    // Add old entries by manipulating timestamps
    for (let i = 0; i < 5; i++) {
      trackGlobalToolCall('agent-1', 'read_file', { path: '/a.ts' })
    }
    // With default 60s window they should be detected
    expect(detectGlobalDoomLoop(60_000).detected).toBe(true)
    // With 0ms window, nothing should be in range
    expect(detectGlobalDoomLoop(0).detected).toBe(false)
  })

  it('trims global log to MAX_GLOBAL_LOG (100)', () => {
    for (let i = 0; i < 110; i++) {
      trackGlobalToolCall(`agent-${i}`, 'echo', { i })
    }
    const log = getGlobalToolCallLog()
    expect(log).toHaveLength(100)
  })

  it('resets global doom loop state', () => {
    for (let i = 0; i < 5; i++) {
      trackGlobalToolCall('agent-1', 'read_file', { path: '/a.ts' })
    }
    expect(detectGlobalDoomLoop().detected).toBe(true)
    resetGlobalDoomLoop()
    expect(getGlobalToolCallLog()).toHaveLength(0)
    expect(detectGlobalDoomLoop().detected).toBe(false)
  })

  it('hash includes tool args in pattern', () => {
    for (let i = 0; i < 5; i++) {
      trackGlobalToolCall('a', 'read_file', { path: '/specific.ts' })
    }
    const result = detectGlobalDoomLoop()
    expect(result.detected).toBe(true)
    expect(result.pattern).toContain('/specific.ts')
  })

  it('same tool with different args does not trigger', () => {
    trackGlobalToolCall('a1', 'read_file', { path: '/a.ts' })
    trackGlobalToolCall('a2', 'read_file', { path: '/b.ts' })
    trackGlobalToolCall('a3', 'read_file', { path: '/c.ts' })
    trackGlobalToolCall('a4', 'read_file', { path: '/d.ts' })
    trackGlobalToolCall('a5', 'read_file', { path: '/e.ts' })
    const result = detectGlobalDoomLoop()
    expect(result.detected).toBe(false)
  })
})
